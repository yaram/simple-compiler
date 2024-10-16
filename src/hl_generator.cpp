#include "hl_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "hlir.h"
#include "profiler.h"
#include "list.h"
#include "util.h"
#include "string.h"
#include "constant.h"
#include "types.h"
#include "jobs.h"

struct AnyRuntimeValue;

struct RegisterValue {
    inline RegisterValue() = default;
    explicit inline RegisterValue(IRType type, size_t register_index) : type(type), register_index(register_index) {}

    IRType type;

    size_t register_index;
};

struct AddressedValue {
    inline AddressedValue() = default;
    explicit inline AddressedValue(IRType pointed_to_type, size_t pointer_register) : pointed_to_type(pointed_to_type), pointer_register(pointer_register) {}

    IRType pointed_to_type;

    size_t pointer_register;
};

struct UndeterminedStructValue {
    inline UndeterminedStructValue() = default;
    explicit inline UndeterminedStructValue(Array<AnyRuntimeValue> members) : members(members) {}

    Array<AnyRuntimeValue> members;
};

enum struct RuntimeValueKind {
    ConstantValue,
    RegisterValue,
    AddressedValue,
    UndeterminedStructValue
};

struct AnyRuntimeValue {
    RuntimeValueKind kind;

    union {
        AnyConstantValue constant;
        RegisterValue register_;
        AddressedValue addressed;
        UndeterminedStructValue undetermined_struct;
    };

    inline AnyRuntimeValue() = default;
    explicit inline AnyRuntimeValue(AnyConstantValue constant) : kind(RuntimeValueKind::ConstantValue), constant(constant) {}
    explicit inline AnyRuntimeValue(RegisterValue register_) : kind(RuntimeValueKind::RegisterValue), register_(register_) {}
    explicit inline AnyRuntimeValue(AddressedValue addressed) : kind(RuntimeValueKind::AddressedValue), addressed(addressed) {}
    explicit inline AnyRuntimeValue(UndeterminedStructValue undetermined_struct) : kind(RuntimeValueKind::UndeterminedStructValue), undetermined_struct(undetermined_struct) {}

    inline AnyConstantValue unwrap_constant_value() {
        assert(kind == RuntimeValueKind::ConstantValue);

        return constant;
    }

    inline RegisterValue unwrap_register_value() {
        assert(kind == RuntimeValueKind::RegisterValue);

        return register_;
    }

    inline AddressedValue unwrap_addressed_value() {
        assert(kind == RuntimeValueKind::AddressedValue);

        return addressed;
    }

    inline UndeterminedStructValue unwrap_undetermined_struct_value() {
        assert(kind == RuntimeValueKind::UndeterminedStructValue);

        return undetermined_struct;
    }
};

struct Variable {
    Identifier name;

    AnyType type;

    AddressedValue value;
};

struct VariableScope {
    ConstantScope* constant_scope;

    List<Variable> variables;

    size_t debug_scope_index;
};

struct GenerationContext {
    Array<AnyType> return_types;

    Array<ConstantScope*> child_scopes;
    size_t next_child_scope_index;

    bool in_breakable_scope;
    Block* break_end_block;

    List<VariableScope> variable_scope_stack;

    List<DebugScope> debug_scopes;

    List<Block*> blocks;
    Block* current_block;
    List<Instruction*> instructions;

    size_t next_register;

    List<StaticConstant*> static_constants;
};

static Result<void> add_new_variable(GenerationContext* context, Identifier name, AnyType type, AddressedValue value) {
    assert(context->variable_scope_stack.length != 0);
    auto variable_scope = &(context->variable_scope_stack[context->variable_scope_stack.length - 1]);

    for(auto variable : variable_scope->variables) {
        if(variable.name.text == name.text) {
            error(variable_scope->constant_scope, name.range, "Duplicate variable name %.*s", STRING_PRINTF_ARGUMENTS(name.text));
            error(variable_scope->constant_scope, variable.name.range, "Original declared here");

            return err();
        }
    }

    Variable variable {};
    variable.name = name;
    variable.type = type;
    variable.value = value;

    variable_scope->variables.append(variable);

    return ok();
}

struct TypedRuntimeValue {
    inline TypedRuntimeValue() = default;
    explicit inline TypedRuntimeValue(AnyType type, AnyRuntimeValue value) : type(type), value(value) {}

    AnyType type;

    AnyRuntimeValue value;
};

inline size_t allocate_register(GenerationContext* context) {
    auto index = context->next_register;

    context->next_register += 1;

    return index;
}

static size_t append_integer_arithmetic_operation(
    GenerationContext* context,
    FileRange range,
    IntegerArithmeticOperation::Operation operation,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_arithmetic_operation = new IntegerArithmeticOperation;
    integer_arithmetic_operation->range = range;
    integer_arithmetic_operation->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_arithmetic_operation->operation = operation;
    integer_arithmetic_operation->source_register_a = source_register_a;
    integer_arithmetic_operation->source_register_b = source_register_b;
    integer_arithmetic_operation->destination_register = destination_register;

    context->instructions.append(integer_arithmetic_operation);

    return destination_register;
}

static size_t append_integer_comparison_operation(
    GenerationContext* context,
    FileRange range,
    IntegerComparisonOperation::Operation operation,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_comparison_operation = new IntegerComparisonOperation;
    integer_comparison_operation->range = range;
    integer_comparison_operation->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_comparison_operation->operation = operation;
    integer_comparison_operation->source_register_a = source_register_a;
    integer_comparison_operation->source_register_b = source_register_b;
    integer_comparison_operation->destination_register = destination_register;

    context->instructions.append(integer_comparison_operation);

    return destination_register;
}

static size_t append_integer_extension(
    GenerationContext* context,
    FileRange range,
    bool is_signed,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_extension = new IntegerExtension;
    integer_extension->range = range;
    integer_extension->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_extension->is_signed = is_signed;
    integer_extension->source_register = source_register;
    integer_extension->destination_size = destination_size;
    integer_extension->destination_register = destination_register;

    context->instructions.append(integer_extension);

    return destination_register;
}

static size_t append_integer_truncation(
    GenerationContext* context,
    FileRange range,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_truncation = new IntegerTruncation;
    integer_truncation->range = range;
    integer_truncation->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_truncation->source_register = source_register;
    integer_truncation->destination_size = destination_size;
    integer_truncation->destination_register = destination_register;

    context->instructions.append(integer_truncation);

    return destination_register;
}

static size_t append_float_arithmetic_operation(
    GenerationContext* context,
    FileRange range,
    FloatArithmeticOperation::Operation operation,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto float_arithmetic_operation = new FloatArithmeticOperation;
    float_arithmetic_operation->range = range;
    float_arithmetic_operation->debug_scope_index = current_variable_scope.debug_scope_index;
    float_arithmetic_operation->operation = operation;
    float_arithmetic_operation->source_register_a = source_register_a;
    float_arithmetic_operation->source_register_b = source_register_b;
    float_arithmetic_operation->destination_register = destination_register;

    context->instructions.append(float_arithmetic_operation);

    return destination_register;
}

static size_t append_float_comparison_operation(
    GenerationContext* context,
    FileRange range,
    FloatComparisonOperation::Operation operation,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto float_comparison_operation = new FloatComparisonOperation;
    float_comparison_operation->range = range;
    float_comparison_operation->debug_scope_index = current_variable_scope.debug_scope_index;
    float_comparison_operation->operation = operation;
    float_comparison_operation->source_register_a = source_register_a;
    float_comparison_operation->source_register_b = source_register_b;
    float_comparison_operation->destination_register = destination_register;

    context->instructions.append(float_comparison_operation);

    return destination_register;
}

static size_t append_float_conversion(
    GenerationContext* context,
    FileRange range,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto float_conversion = new FloatConversion;
    float_conversion->range = range;
    float_conversion->debug_scope_index = current_variable_scope.debug_scope_index;
    float_conversion->source_register = source_register;
    float_conversion->destination_size = destination_size;
    float_conversion->destination_register = destination_register;

    context->instructions.append(float_conversion);

    return destination_register;
}

static size_t append_float_from_integer(
    GenerationContext* context,
    FileRange range,
    bool is_signed,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto float_from_integer = new FloatFromInteger;
    float_from_integer->range = range;
    float_from_integer->debug_scope_index = current_variable_scope.debug_scope_index;
    float_from_integer->is_signed = is_signed;
    float_from_integer->source_register = source_register;
    float_from_integer->destination_size = destination_size;
    float_from_integer->destination_register = destination_register;

    context->instructions.append(float_from_integer);

    return destination_register;
}

static size_t append_integer_from_float(
    GenerationContext* context,
    FileRange range,
    bool is_signed,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_from_float = new IntegerFromFloat;
    integer_from_float->range = range;
    integer_from_float->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_from_float->is_signed = is_signed;
    integer_from_float->source_register = source_register;
    integer_from_float->destination_size = destination_size;
    integer_from_float->destination_register = destination_register;

    context->instructions.append(integer_from_float);

    return destination_register;
}

static size_t append_pointer_equality(
    GenerationContext* context,
    FileRange range,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto pointer_equality = new PointerEquality;
    pointer_equality->range = range;
    pointer_equality->debug_scope_index = current_variable_scope.debug_scope_index;
    pointer_equality->source_register_a = source_register_a;
    pointer_equality->source_register_b = source_register_b;
    pointer_equality->destination_register = destination_register;

    context->instructions.append(pointer_equality);

    return destination_register;
}

static size_t append_pointer_from_integer(
    GenerationContext* context,
    FileRange range,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto pointer_from_integer = new PointerFromInteger;
    pointer_from_integer->range = range;
    pointer_from_integer->debug_scope_index = current_variable_scope.debug_scope_index;
    pointer_from_integer->source_register = source_register;
    pointer_from_integer->destination_register = destination_register;

    context->instructions.append(pointer_from_integer);

    return destination_register;
}

static size_t append_integer_from_pointer(
    GenerationContext* context,
    FileRange range,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto integer_from_pointer = new IntegerFromPointer;
    integer_from_pointer->range = range;
    integer_from_pointer->debug_scope_index = current_variable_scope.debug_scope_index;
    integer_from_pointer->source_register = source_register;
    integer_from_pointer->destination_size = destination_size;
    integer_from_pointer->destination_register = destination_register;

    context->instructions.append(integer_from_pointer);

    return destination_register;
}

static size_t append_boolean_arithmetic_operation(
    GenerationContext* context,
    FileRange range,
    BooleanArithmeticOperation::Operation operation,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto boolean_arithmetic_operation = new BooleanArithmeticOperation;
    boolean_arithmetic_operation->range = range;
    boolean_arithmetic_operation->debug_scope_index = current_variable_scope.debug_scope_index;
    boolean_arithmetic_operation->operation = operation;
    boolean_arithmetic_operation->source_register_a = source_register_a;
    boolean_arithmetic_operation->source_register_b = source_register_b;
    boolean_arithmetic_operation->destination_register = destination_register;

    context->instructions.append(boolean_arithmetic_operation);

    return destination_register;
}

static size_t append_boolean_equality(
    GenerationContext* context,
    FileRange range,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto boolean_equality = new BooleanEquality;
    boolean_equality->range = range;
    boolean_equality->debug_scope_index = current_variable_scope.debug_scope_index;
    boolean_equality->source_register_a = source_register_a;
    boolean_equality->source_register_b = source_register_b;
    boolean_equality->destination_register = destination_register;

    context->instructions.append(boolean_equality);

    return destination_register;
}

static size_t append_boolean_inversion(
    GenerationContext* context,
    FileRange range,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto boolean_inversion = new BooleanInversion;
    boolean_inversion->range = range;
    boolean_inversion->debug_scope_index = current_variable_scope.debug_scope_index;
    boolean_inversion->source_register = source_register;
    boolean_inversion->destination_register = destination_register;

    context->instructions.append(boolean_inversion);

    return destination_register;
}

static size_t append_assemble_static_array(
    GenerationContext* context,
    FileRange range,
    Array<size_t> element_registers
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto assembly_static_array = new AssembleStaticArray;
    assembly_static_array->range = range;
    assembly_static_array->debug_scope_index = current_variable_scope.debug_scope_index;
    assembly_static_array->element_registers = element_registers;
    assembly_static_array->destination_register = destination_register;

    context->instructions.append(assembly_static_array);

    return destination_register;
}

static size_t append_read_static_array_element(
    GenerationContext* context,
    FileRange range,
    size_t element_index,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto read_static_array_element = new ReadStaticArrayElement;
    read_static_array_element->range = range;
    read_static_array_element->debug_scope_index = current_variable_scope.debug_scope_index;
    read_static_array_element->element_index = element_index;
    read_static_array_element->source_register = source_register;
    read_static_array_element->destination_register = destination_register;

    context->instructions.append(read_static_array_element);

    return destination_register;
}

static size_t append_assemble_struct(
    GenerationContext* context,
    FileRange range,
    Array<size_t> member_registers
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto assemble_struct = new AssembleStruct;
    assemble_struct->range = range;
    assemble_struct->debug_scope_index = current_variable_scope.debug_scope_index;
    assemble_struct->member_registers = member_registers;
    assemble_struct->destination_register = destination_register;

    context->instructions.append(assemble_struct);

    return destination_register;
}

static size_t append_read_struct_member(
    GenerationContext* context,
    FileRange range,
    size_t member_index,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto read_read_struct_member = new ReadStructMember;
    read_read_struct_member->range = range;
    read_read_struct_member->debug_scope_index = current_variable_scope.debug_scope_index;
    read_read_struct_member->member_index = member_index;
    read_read_struct_member->source_register = source_register;
    read_read_struct_member->destination_register = destination_register;

    context->instructions.append(read_read_struct_member);

    return destination_register;
}

static size_t append_literal(GenerationContext* context, FileRange range, IRType type, IRConstantValue value) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto literal = new Literal;
    literal->range = range;
    literal->debug_scope_index = current_variable_scope.debug_scope_index;
    literal->destination_register = destination_register;
    literal->type = type;
    literal->value = value;

    context->instructions.append(literal);

    return destination_register;
}

static void append_jump(GenerationContext* context, FileRange range, Block* destination_block) {
    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto jump = new Jump;
    jump->range = range;
    jump->debug_scope_index = current_variable_scope.debug_scope_index;
    jump->destination_block = destination_block;

    context->instructions.append(jump);
}

static void append_branch(
    GenerationContext* context,
    FileRange range,
    size_t condition_register,
    Block* true_destination_block,
    Block* false_destination_block
) {
    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto branch = new Branch;
    branch->range = range;
    branch->debug_scope_index = current_variable_scope.debug_scope_index;
    branch->condition_register = condition_register;
    branch->true_destination_block = true_destination_block;
    branch->false_destination_block = false_destination_block;

    context->instructions.append(branch);
}

static size_t append_allocate_local(
    GenerationContext* context,
    FileRange range,
    IRType type
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto allocate_local = new AllocateLocal;
    allocate_local->range = range;
    allocate_local->debug_scope_index = current_variable_scope.debug_scope_index;
    allocate_local->type = type;
    allocate_local->destination_register = destination_register;
    allocate_local->has_debug_info = false;

    context->instructions.append(allocate_local);

    return destination_register;
}

static size_t append_allocate_local(
    GenerationContext* context,
    FileRange range,
    IRType type,
    String debug_name,
    AnyType debug_type
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto allocate_local = new AllocateLocal;
    allocate_local->range = range;
    allocate_local->debug_scope_index = current_variable_scope.debug_scope_index;
    allocate_local->type = type;
    allocate_local->destination_register = destination_register;
    allocate_local->has_debug_info = true;
    allocate_local->debug_name = debug_name;
    allocate_local->debug_type = debug_type;

    context->instructions.append(allocate_local);

    return destination_register;
}

static size_t append_load(
    GenerationContext* context,
    FileRange range,
    size_t pointer_register,
    IRType destination_type
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto load = new Load;
    load->range = range;
    load->debug_scope_index = current_variable_scope.debug_scope_index;
    load->pointer_register = pointer_register;
    load->destination_type = destination_type;
    load->destination_register = destination_register;

    context->instructions.append(load);

    return destination_register;
}

static void append_store(
    GenerationContext* context,
    FileRange range,
    size_t source_register,
    size_t pointer_register
) {
    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto store = new Store;
    store->range = range;
    store->debug_scope_index = current_variable_scope.debug_scope_index;
    store->source_register = source_register;
    store->pointer_register = pointer_register;

    context->instructions.append(store);
}

static size_t append_struct_member_pointer(
    GenerationContext* context,
    FileRange range,
    Array<IRType> members,
    size_t member_index,
    size_t pointer_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto struct_member_pointer = new StructMemberPointer;
    struct_member_pointer->range = range;
    struct_member_pointer->debug_scope_index = current_variable_scope.debug_scope_index;
    struct_member_pointer->members = members;
    struct_member_pointer->member_index = member_index;
    struct_member_pointer->pointer_register = pointer_register;
    struct_member_pointer->destination_register = destination_register;

    context->instructions.append(struct_member_pointer);

    return destination_register;
}

static size_t append_pointer_index(
    GenerationContext* context,
    FileRange range,
    size_t index_register,
    IRType pointed_to_type,
    size_t pointer_register
) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto pointer_index = new PointerIndex;
    pointer_index->range = range;
    pointer_index->debug_scope_index = current_variable_scope.debug_scope_index;
    pointer_index->index_register = index_register;
    pointer_index->pointed_to_type = pointed_to_type;
    pointer_index->pointer_register = pointer_register;
    pointer_index->destination_register = destination_register;

    context->instructions.append(pointer_index);

    return destination_register;
}

static size_t append_reference_static(GenerationContext* context, FileRange range, RuntimeStatic* runtime_static) {
    auto destination_register = allocate_register(context);

    assert(context->variable_scope_stack.length != 0);
    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

    auto reference_static = new ReferenceStatic;
    reference_static->range = range;
    reference_static->debug_scope_index = current_variable_scope.debug_scope_index;
    reference_static->runtime_static = runtime_static;
    reference_static->destination_register = destination_register;

    context->instructions.append(reference_static);

    return destination_register;
}

inline IRType get_array_ir_type(ArchitectureSizes architecture_sizes, ArrayTypeType array) {
    auto members = allocate<IRType>(2);

    members[0].kind = IRTypeKind::Integer;
    members[0].integer.size = architecture_sizes.address_size;

    members[1].kind = IRTypeKind::Pointer;

    return IRType::create_struct(Array(2, members));
}

static IRType get_ir_type(ArchitectureSizes architecture_sizes, AnyType type);

inline IRType get_static_array_ir_type(ArchitectureSizes architecture_sizes, StaticArray static_array) {
    return IRType::create_static_array(
        static_array.length,
        heapify(get_ir_type(architecture_sizes, *static_array.element_type))
    );
}

inline IRType get_struct_ir_type(ArchitectureSizes architecture_sizes, StructType struct_) {
    auto members = allocate<IRType>(struct_.members.length);

    for(size_t i = 0; i < struct_.members.length; i += 1) {
        members[i] = get_ir_type(architecture_sizes, struct_.members[i].type);
    }

    return IRType::create_struct(Array(struct_.members.length, members));
}

inline IRType get_union_ir_type(ArchitectureSizes architecture_sizes, UnionType union_) {
    return IRType::create_static_array(union_.get_size(architecture_sizes), heapify(IRType::create_integer(RegisterSize::Size8)));
}

static IRType get_ir_type(ArchitectureSizes architecture_sizes, AnyType type) {
    if(type.kind == TypeKind::Integer) {
        return IRType::create_integer(type.integer.size);
    } else if(type.kind == TypeKind::Boolean) {
        return IRType::create_boolean();
    } else if(type.kind == TypeKind::FloatType) {
        return IRType::create_float(type.float_.size);
    } else if(type.kind == TypeKind::Pointer) {
        return IRType::create_pointer();
    } else if(type.kind == TypeKind::ArrayTypeType) {
        return get_array_ir_type(architecture_sizes, type.array);
    } else if(type.kind == TypeKind::StaticArray) {
        return get_static_array_ir_type(architecture_sizes, type.static_array);
    } else if(type.kind == TypeKind::StructType) {
        return get_struct_ir_type(architecture_sizes, type.struct_);
    } else if(type.kind == TypeKind::UnionType) {
        return get_union_ir_type(architecture_sizes, type.union_);
    } else if(type.kind == TypeKind::Enum) {
        return IRType::create_integer(type.enum_.backing_type->size);
    } else {
        abort();
    }
}

static IRConstantValue get_runtime_ir_constant_value(AnyConstantValue value);

inline IRConstantValue get_array_ir_constant_value(ArrayConstant array) {
    auto members = allocate<IRConstantValue>(2);

    members[0] = get_runtime_ir_constant_value(*array.length);

    members[1] = get_runtime_ir_constant_value(*array.pointer);

    return IRConstantValue::create_struct(Array(2, members));
}

inline IRConstantValue get_static_array_ir_constant_value(StaticArrayConstant static_array) {
    auto elements = allocate<IRConstantValue>(static_array.elements.length);

    for(size_t i = 0; i < static_array.elements.length; i += 1) {
        elements[i] = get_runtime_ir_constant_value(static_array.elements[i]);
    }

    return IRConstantValue::create_static_array(Array(static_array.elements.length, elements));
}

inline IRConstantValue get_struct_ir_constant_value(StructConstant struct_) {
    auto members = allocate<IRConstantValue>(struct_.members.length);

    for(size_t i = 0; i < struct_.members.length; i += 1) {
        members[i] = get_runtime_ir_constant_value(struct_.members[i]);
    }

    return IRConstantValue::create_struct(Array(struct_.members.length, members));
}

static IRConstantValue get_runtime_ir_constant_value(AnyConstantValue value) {
    if(value.kind == ConstantValueKind::IntegerConstant) {
        return IRConstantValue::create_integer(value.integer);
    } else if(value.kind == ConstantValueKind::FloatConstant) {
        return IRConstantValue::create_float(value.float_);
    } else if(value.kind == ConstantValueKind::BooleanConstant) {
        return IRConstantValue::create_boolean(value.boolean);
    } else if(value.kind == ConstantValueKind::ArrayConstant) {
        return get_array_ir_constant_value(value.array);
    } else if(value.kind == ConstantValueKind::StaticArrayConstant) {
        return get_static_array_ir_constant_value(value.static_array);
    } else if(value.kind == ConstantValueKind::StructConstant) {
        return get_struct_ir_constant_value(value.struct_);
    } else if(value.kind == ConstantValueKind::UndefConstant) {
        return IRConstantValue::create_undef();
    } else {
        abort();
    }
}

static StaticConstant* register_static_constant(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyConstantValue value
) {
    auto ir_type = get_ir_type(info.architecture_sizes, type);
    auto ir_value = get_runtime_ir_constant_value(value);

    auto constant = new StaticConstant;
    constant->name = u8"static_constant"_S;
    constant->is_no_mangle = false;
    constant->path = get_scope_file_path(*scope);
    constant->range = range;
    constant->debug_type = type;
    constant->type = ir_type;
    constant->value = ir_value;

    context->static_constants.append(constant);

    return constant;
}

static size_t generate_in_register_value(
    GenerationContext* context,
    FileRange range,
    IRType type,
    AnyRuntimeValue value
) {
    if(value.kind == RuntimeValueKind::ConstantValue) {
        auto constant_value = value.constant;

        auto ir_constant_value = get_runtime_ir_constant_value(constant_value);

        return append_literal(context, range, type, ir_constant_value);
    } else if(value.kind == RuntimeValueKind::RegisterValue) {
        auto register_value = value.register_;

        assert(register_value.type == type);

        return register_value.register_index;
    } else if(value.kind == RuntimeValueKind::AddressedValue) {
        auto addressed_value = value.addressed;

        assert(addressed_value.pointed_to_type == type);

        return append_load(context, range, addressed_value.pointer_register, type);
    } else {
        abort();
    }
}

static Result<RegisterValue> coerce_to_integer_register_value(
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Integer target_type,
    bool probing
) {
    auto ir_type = IRType::create_integer(target_type.size);

    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        if(integer.size == target_type.size && integer.is_signed == target_type.is_signed) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = value.unwrap_constant_value().unwrap_integer();

        expect_void(check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value, probing));

        auto register_index = append_literal(context, range, ir_type, IRConstantValue::create_integer(integer_value));

        return ok(RegisterValue(ir_type, register_index));
    } else if(type.kind == TypeKind::Enum) {
        auto enum_ = type.enum_;

        if(enum_.backing_type->is_signed == target_type.is_signed && enum_.backing_type->size == target_type.size) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(type.kind == TypeKind::Undef) {
        auto register_index = generate_in_register_value(context, range, ir_type, value);

        return ok(RegisterValue(ir_type, register_index));
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description()));
    }

    return err();
}

static Result<RegisterValue> coerce_to_float_register_value(
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    FloatType target_type,
    bool probing
) {
    auto ir_type = IRType::create_float(target_type.size);

    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = value.unwrap_constant_value().unwrap_integer();

        auto register_index = append_literal(context, range, ir_type, IRConstantValue::create_float((double)integer_value));

        return ok(RegisterValue(ir_type, register_index));
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        if(target_type.size == float_type.size) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        auto float_value = value.unwrap_constant_value().unwrap_float();

        auto register_index = append_literal(context, range, ir_type, IRConstantValue::create_float(float_value));

        return ok(RegisterValue(ir_type, register_index));
    } else if(type.kind == TypeKind::Undef) {
        auto register_index = generate_in_register_value(context, range, ir_type, value);

        return ok(RegisterValue(ir_type, register_index));
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description()));
    }

    return err();
}

static Result<RegisterValue> coerce_to_pointer_register_value(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Pointer target_type,
    bool probing
) {
    auto ir_type = IRType::create_pointer();

    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = value.unwrap_constant_value().unwrap_integer();

        auto register_index = append_literal(context, range, ir_type, IRConstantValue::create_integer(integer_value));

        return ok(RegisterValue(ir_type, register_index));
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        if(*pointer.pointed_to_type == *target_type.pointed_to_type) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(type.kind == TypeKind::Undef) {
        auto register_index = generate_in_register_value(context, range, ir_type, value);

        return ok(RegisterValue(ir_type, register_index));
    }

    if (!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description()));
    }

    return err();
}


static Result<RegisterValue> coerce_to_type_register(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type,
    bool probing
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer = target_type.integer;

        expect(register_value, coerce_to_integer_register_value(
            scope,
            context,
            range,
            type,
            value,
            integer,
            probing
        ));

        return ok(register_value);
    } else if(target_type.kind == TypeKind::Boolean) {
        auto ir_type = IRType::create_boolean();

        if(type.kind == TypeKind::Boolean) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(target_type.kind == TypeKind::FloatType) {
        auto float_type = target_type.float_;

        expect(register_index, coerce_to_float_register_value(
            scope,
            context,
            range,
            type,
            value,
            float_type,
            probing
        ));

        return ok(register_index);
    } else if(target_type.kind == TypeKind::Pointer) {
        auto pointer = target_type.pointer;

        expect(register_index, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            range,
            type,
            value,
            pointer,
            probing
        ));

        return ok(register_index);
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        auto ir_type = get_array_ir_type(info.architecture_sizes, target_type.array);

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;
            if(*target_array.element_type == *array_type.element_type) {
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    if(value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = value.constant.array;

                        auto ir_value = get_array_ir_constant_value(array_value);

                        auto register_index = append_literal(context, range, ir_type, ir_value);

                        return ok(RegisterValue(ir_type, register_index));
                    }
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    auto register_index = register_value.register_index;

                    return ok(RegisterValue(ir_type, register_index));
                } else if(value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = value.addressed;

                    auto register_index = append_load(context, range, addressed_value.pointer_register, ir_type);

                    return ok(RegisterValue(ir_type, register_index));
                } else {
                    abort();
                }
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_array.element_type == *static_array.element_type) {
                if(value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = value.addressed;

                    auto length_register = append_literal(
                        context,
                        range,
                        IRType::create_integer(info.architecture_sizes.address_size),
                        IRConstantValue::create_integer(static_array.length)
                    );

                    auto element_ir_type = get_ir_type(info.architecture_sizes, *target_array.element_type);

                    auto member_registers = allocate<size_t>(2);

                    member_registers[0] = length_register;
                    member_registers[1] = addressed_value.pointer_register;

                    auto register_index = append_assemble_struct(context, range, Array(2, member_registers));

                    return ok(RegisterValue(ir_type, register_index));
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == u8"length"_S &&
                undetermined_struct.members[1].name == u8"pointer"_S
            ) {
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = value.constant;

                    auto undetermined_struct_value = constant_value.unwrap_struct();

                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        AnyRuntimeValue(undetermined_struct_value.members[0]),
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer_register_value(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            AnyRuntimeValue(undetermined_struct_value.members[1]),
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            auto member_registers = allocate<size_t>(2);

                            member_registers[0] = length_result.value.register_index;
                            member_registers[1] = pointer_result.value.register_index;

                            auto register_index = append_assemble_struct(context, range, Array(2, member_registers));

                            return ok(RegisterValue(ir_type, register_index));
                        }
                    }
                } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                    auto undetermined_struct_value = value.undetermined_struct;

                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        undetermined_struct_value.members[0],
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer_register_value(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            undetermined_struct_value.members[1],
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            auto member_registers = allocate<size_t>(2);

                            member_registers[0] = length_result.value.register_index;
                            member_registers[1] = pointer_result.value.register_index;

                            auto register_index = append_assemble_struct(context, range, Array(2, member_registers));

                            return ok(RegisterValue(ir_type, register_index));
                        }
                    }
                } else {
                    abort();
                }
            }
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        auto ir_type = get_static_array_ir_type(info.architecture_sizes, target_static_array);

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_static_array.element_type == *static_array.element_type && target_static_array.length == static_array.length) {
                size_t register_index;
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = value.constant;

                    auto ir_constant_value = get_runtime_ir_constant_value(constant_value);

                    register_index = append_literal(
                        context,
                        range,
                        ir_type,
                        ir_constant_value
                    );
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    register_index = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = value.addressed;

                    register_index = append_load(context, range, addressed_value.pointer_register, ir_type);
                } else {
                    abort();
                }

                return ok(RegisterValue(ir_type, register_index));
            }
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(target_type.kind == TypeKind::StructType) {
        auto target_struct_type = target_type.struct_;

        auto ir_type = get_struct_ir_type(info.architecture_sizes, target_struct_type);

        if(type.kind == TypeKind::StructType) {
            auto struct_type = type.struct_;

            if(target_struct_type.definition == struct_type.definition && target_struct_type.members.length == struct_type.members.length) {
                auto same_members = true;
                for(size_t i = 0; i < struct_type.members.length; i += 1) {
                    if(
                        target_struct_type.members[i].name != struct_type.members[i].name ||
                        target_struct_type.members[i].type != struct_type.members[i].type
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    size_t register_index;
                    if(value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = value.register_;

                        register_index = register_value.register_index;
                    } else if(value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = value.addressed;

                        register_index = append_load(context, range, addressed_value.pointer_register, ir_type);
                    } else {
                        abort();
                    }

                    return ok(RegisterValue(ir_type, register_index));
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto member_registers = allocate<size_t>(undetermined_struct.members.length);

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                AnyRuntimeValue(undetermined_struct_value.members[i]),
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }

                            member_registers[i] = result.value.register_index;
                        }

                        if(success) {
                            auto register_index = append_assemble_struct(
                                context,
                                range,
                                Array(undetermined_struct.members.length, member_registers)
                            );

                            return ok(RegisterValue(ir_type, register_index));
                        }
                    }
                }
            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = value.undetermined_struct;

                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto member_registers = allocate<size_t>(undetermined_struct.members.length);

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                undetermined_struct_value.members[i],
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }

                            member_registers[i] = result.value.register_index;
                        }

                        if(success) {
                            auto register_index = append_assemble_struct(
                                context,
                                range,
                                Array(undetermined_struct.members.length, member_registers)
                            );

                            return ok(RegisterValue(ir_type, register_index));
                        }
                    }
                }
            } else {
                abort();
            }
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(target_type.kind == TypeKind::UnionType) {
        auto target_union_type = target_type.union_;

        auto ir_type = get_union_ir_type(info.architecture_sizes, target_union_type);

        if(type.kind == TypeKind::UnionType) {
            auto union_type = type.union_;

            if(target_union_type.definition == union_type.definition && target_union_type.members.length == union_type.members.length) {
                auto same_members = true;
                for(size_t i = 0; i < union_type.members.length; i += 1) {
                    if(
                        target_union_type.members[i].name != union_type.members[i].name ||
                        target_union_type.members[i].type != union_type.members[i].type
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    size_t register_index;
                    if(value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = value.register_;

                        register_index = register_value.register_index;
                    } else if(value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = value.addressed;

                        register_index = append_load(context, range, addressed_value.pointer_register, ir_type);
                    } else {
                        abort();
                    }

                    return ok(RegisterValue(ir_type, register_index));
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto pointer_register = append_allocate_local(
                                context,
                                range,
                                ir_type
                            );

                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                AnyRuntimeValue(undetermined_struct_value.members[0]),
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                append_store(context, range, result.value.register_index, pointer_register);

                                auto register_index = append_load(context, range, pointer_register, ir_type);

                                return ok(RegisterValue(ir_type, register_index));
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = value.undetermined_struct;

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto pointer_register = append_allocate_local(
                                context,
                                range,
                                ir_type
                            );

                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                undetermined_struct_value.members[0],
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                append_store(context, range, result.value.register_index, pointer_register);

                                auto register_index = append_load(context, range, pointer_register, ir_type);

                                return ok(RegisterValue(ir_type, register_index));
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                abort();
            }
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else if(target_type.kind == TypeKind::Enum) {
        auto target_enum = target_type.enum_;

        auto ir_type = IRType::create_integer(target_enum.backing_type->size);

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            if(integer.size == target_enum.backing_type->size && integer.is_signed == target_enum.backing_type->is_signed) {
                auto register_index = generate_in_register_value(context, range, ir_type, value);

                return ok(RegisterValue(ir_type, register_index));
            }
        } else if(type.kind == TypeKind::UndeterminedInteger) {
            auto integer_value = value.unwrap_constant_value().unwrap_integer();

            expect_void(check_undetermined_integer_to_integer_coercion(scope, range, *target_enum.backing_type, (int64_t)integer_value, probing));

            auto register_index = append_literal(context, range, ir_type, IRConstantValue::create_integer(integer_value));

            return ok(RegisterValue(ir_type, register_index));
        } else if(type.kind == TypeKind::Enum) {
            auto enum_ = type.enum_;

            if(target_enum.definition == enum_.definition) {
                auto register_index = generate_in_register_value(context, range, ir_type, value);

                return ok(RegisterValue(ir_type, register_index));
            }
        } else if(type.kind == TypeKind::Undef) {
            auto register_index = generate_in_register_value(context, range, ir_type, value);

            return ok(RegisterValue(ir_type, register_index));
        }
    } else {
        abort();
    }

    if(!probing) {
        if(value.kind == RuntimeValueKind::ConstantValue) {
            error(scope, range, "Cannot implicitly convert constant '%.*s' (%.*s) to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(value.constant.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
        } else if(value.kind == RuntimeValueKind::RegisterValue) {
            error(scope, range, "Cannot implicitly convert anonymous '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
        } else {
            error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
        }
    }

    return err();
}

static DelayedResult<TypedRuntimeValue> generate_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    Expression* expression
);

static DelayedResult<AnyType> evaluate_type_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    Expression* expression
) {
    expect_delayed(expression_value, generate_expression(info, jobs, scope, context, expression));

    if(expression_value.type.kind == TypeKind::Type) {
        auto constant_value = expression_value.value.unwrap_constant_value();

        return ok(constant_value.unwrap_type());
    } else {
        error(scope, expression->range, "Expected a type, got %.*s", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

        return err();
    }
}

static DelayedResult<TypedRuntimeValue> generate_binary_operation(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    Expression* left_expression,
    Expression* right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect_delayed(left, generate_expression(info, jobs, scope, context, left_expression));

    expect_delayed(right, generate_expression(info, jobs, scope, context, right_expression));

    if(left.value.kind == RuntimeValueKind::ConstantValue && right.value.kind == RuntimeValueKind::ConstantValue) {
        expect(constant, evaluate_constant_binary_operation(
            info,
            scope,
            range,
            binary_operator,
            left_expression->range,
            left.type,
            left.value.constant,
            right_expression->range,
            right.type,
            right.value.constant
        ));

        return ok(TypedRuntimeValue(
            constant.type,
            AnyRuntimeValue(constant.value)
        ));
    }

    expect(type, determine_binary_operation_type(scope, range, left.type, right.type));

    expect(determined_type, coerce_to_default_type(info, scope, range, type));

    if(determined_type.kind == TypeKind::Integer) {
        auto integer = determined_type.integer;

        expect(left_register, coerce_to_integer_register_value(
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            integer,
            false
        ));

        expect(right_register, coerce_to_integer_register_value(
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            integer,
            false
        ));

        auto is_arithmetic = true;
        IntegerArithmeticOperation::Operation arithmetic_operation;
        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Add;
            } break;

            case BinaryOperation::Operator::Subtraction: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Subtract;
            } break;

            case BinaryOperation::Operator::Multiplication: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Multiply;
            } break;

            case BinaryOperation::Operator::Division: {
                if(integer.is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::SignedDivide;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedDivide;
                }
            } break;

            case BinaryOperation::Operator::Modulo: {
                if(integer.is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::SignedModulus;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedModulus;
                }
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseAnd;
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseOr;
            } break;

            case BinaryOperation::Operator::LeftShift: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::LeftShift;
            } break;

            case BinaryOperation::Operator::RightShift: {
                if(integer.is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::RightArithmeticShift;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::RightShift;
                }
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        AnyType result_type;
        if(is_arithmetic) {
            result_register = append_integer_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register.register_index,
                right_register.register_index
            );

            result_type = AnyType(integer);
        } else {
            IntegerComparisonOperation::Operation comparison_operation;
            auto invert = false;
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                } break;

                case BinaryOperation::Operator::NotEqual: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                    invert = true;
                } break;

                case BinaryOperation::Operator::LessThan: {
                    if(integer.is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedLessThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedLessThan;
                    }
                } break;

                case BinaryOperation::Operator::GreaterThan: {
                    if(integer.is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
                    }
                } break;

                default: {
                    error(scope, range, "Cannot perform that operation on integers");

                    return err();
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                range,
                comparison_operation,
                left_register.register_index,
                right_register.register_index
            );

            if(invert) {
                result_register = append_boolean_inversion(context, range, result_register);
            }

            result_type = AnyType::create_boolean();
        }

        auto result_ir_type = get_ir_type(info.architecture_sizes, result_type);

        return ok(TypedRuntimeValue(
            result_type,
            AnyRuntimeValue(RegisterValue(result_ir_type, result_register))
        ));
    } else if(determined_type.kind == TypeKind::Boolean) {
        if(left.type.kind != TypeKind::Boolean) {
            error(scope, left_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(left.type.get_description()));

            return err();
        }

        auto ir_type = IRType::create_boolean();

        auto left_register = generate_in_register_value(context, left_expression->range, ir_type, left.value);

        if(right.type.kind != TypeKind::Boolean) {
            error(scope, right_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(right.type.get_description()));

            return err();
        }

        auto right_register = generate_in_register_value(context, right_expression->range, ir_type, right.value);

        auto is_arithmetic = true;
        BooleanArithmeticOperation::Operation arithmetic_operation;
        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                arithmetic_operation = BooleanArithmeticOperation::Operation::BooleanAnd;
            } break;

            case BinaryOperation::Operator::BooleanOr: {
                arithmetic_operation = BooleanArithmeticOperation::Operation::BooleanOr;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        if(is_arithmetic) {
            result_register = append_boolean_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register,
                right_register
            );
        } else {
            auto invert = false;
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal: {} break;

                case BinaryOperation::Operator::NotEqual: {
                    invert = true;
                } break;

                default: {
                    error(scope, range, "Cannot perform that operation on 'bool'");

                    return err();
                } break;
            }

            result_register = append_boolean_equality(
                context,
                range,
                left_register,
                right_register
            );

            if(invert) {
                result_register = append_boolean_inversion(context, range, result_register);
            }
        }

        return ok(TypedRuntimeValue(
            AnyType::create_boolean(),
            AnyRuntimeValue(RegisterValue(ir_type, result_register))
        ));
    } else if(determined_type.kind == TypeKind::FloatType) {
        auto float_type = determined_type.float_;

        expect(left_register, coerce_to_float_register_value(
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            float_type,
            false
        ));

        expect(right_register, coerce_to_float_register_value(
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            float_type,
            false
        ));

        auto is_arithmetic = true;
        FloatArithmeticOperation::Operation arithmetic_operation;
        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Add;
            } break;

            case BinaryOperation::Operator::Subtraction: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Subtract;
            } break;

            case BinaryOperation::Operator::Multiplication: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Multiply;
            } break;

            case BinaryOperation::Operator::Division: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Divide;
            } break;

            case BinaryOperation::Operator::Modulo: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Modulus;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        AnyType result_type;
        if(is_arithmetic) {
            result_register = append_float_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register.register_index,
                right_register.register_index
            );

            result_type = AnyType(float_type);
        } else {
            FloatComparisonOperation::Operation comparison_operation;
            auto invert = false;
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal: {
                    comparison_operation = FloatComparisonOperation::Operation::Equal;
                } break;

                case BinaryOperation::Operator::NotEqual: {
                    comparison_operation = FloatComparisonOperation::Operation::Equal;
                    invert = true;
                } break;

                case BinaryOperation::Operator::LessThan: {
                    comparison_operation = FloatComparisonOperation::Operation::LessThan;
                } break;

                case BinaryOperation::Operator::GreaterThan: {
                    comparison_operation = FloatComparisonOperation::Operation::GreaterThan;
                } break;

                default: {
                    error(scope, range, "Cannot perform that operation on floats");

                    return err();
                } break;
            }

            result_register = append_float_comparison_operation(
                context,
                range,
                comparison_operation,
                left_register.register_index,
                right_register.register_index
            );

            if(invert) {
                result_register = append_boolean_inversion(context, range, result_register);
            }

            result_type = AnyType::create_boolean();
        }

        auto result_ir_type = get_ir_type(info.architecture_sizes, result_type);

        return ok(TypedRuntimeValue(
            result_type,
            AnyRuntimeValue(RegisterValue(result_ir_type, result_register))
        ));
    } else if(determined_type.kind == TypeKind::Pointer) {
        auto pointer = determined_type.pointer;

        expect(left_register, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            pointer,
            false
        ));

        expect(right_register, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            pointer,
            false
        ));

        auto invert = false;
        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {} break;

            case BinaryOperation::Operator::NotEqual: {
                invert = true;
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(AnyType(pointer).get_description()));

                return err();
            } break;
        }

        auto result_register = append_pointer_equality(
            context,
            range,
            left_register.register_index,
            right_register.register_index
        );

        if(invert) {
            result_register = append_boolean_inversion(context, range, result_register);
        }

        return ok(TypedRuntimeValue(
            AnyType::create_boolean(),
            AnyRuntimeValue(RegisterValue(IRType::create_boolean(), result_register))
        ));
    } else if(determined_type.kind == TypeKind::Enum) {
        auto pointer = determined_type.pointer;

        expect(left_register, coerce_to_type_register(
            info,
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            determined_type,
            false
        ));

        expect(right_register, coerce_to_type_register(
            info,
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            determined_type,
            false
        ));

        auto invert = false;
        IntegerComparisonOperation::Operation operation;
        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {
                operation = IntegerComparisonOperation::Operation::Equal;
            } break;

            case BinaryOperation::Operator::NotEqual: {
                operation = IntegerComparisonOperation::Operation::Equal;
                invert = true;
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            } break;
        }

        auto result_register = append_integer_comparison_operation(
            context,
            range,
            operation,
            left_register.register_index,
            right_register.register_index
        );

        if(invert) {
            result_register = append_boolean_inversion(context, range, result_register);
        }

        return ok(TypedRuntimeValue(
            AnyType::create_boolean(),
            AnyRuntimeValue(RegisterValue(IRType::create_boolean(), result_register))
        ));
    } else {
        abort();
    }
}

struct RuntimeNameSearchResult {
    bool found;

    AnyType type;
    AnyRuntimeValue value;
};

static_profiled_function(DelayedResult<RuntimeNameSearchResult>, search_for_name, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    String name,
    uint32_t name_hash,
    ConstantScope* name_scope,
    FileRange name_range,
    Array<Statement*> statements,
    DeclarationHashTable declarations,
    bool external
), (
    info,
    jobs,
    scope,
    context,
    name,
    name_hash,
    name_scope,
    name_range,
    statements,
    declarations,
    external
)) {
    auto declaration = search_in_declaration_hash_table(declarations, name_hash, name);

    if(declaration != nullptr) {
        if(external && !is_declaration_public(declaration)) {
            RuntimeNameSearchResult result {};
            result.found = false;

            return ok(result);
        }

        expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, declaration));

        RuntimeNameSearchResult result {};
        result.found = true;
        result.type = value.type;
        result.value = AnyRuntimeValue(value.value);

        return ok(result);
    }

    for(auto statement : statements) {
        if(statement->kind == StatementKind::UsingStatement) {
            auto using_statement = (UsingStatement*)statement;

            if(!external || using_statement->export_) {
                expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, nullptr, using_statement->value));

                if(expression_value.type.kind == TypeKind::FileModule) {
                    auto file_module = expression_value.value.unwrap_file_module();

                    expect_delayed(search_value, search_for_name(
                        info,
                        jobs,
                        file_module.scope,
                        context,
                        name,
                        name_hash,
                        name_scope,
                        name_range,
                        file_module.scope->statements,
                        file_module.scope->declarations,
                        true
                    ));

                    if(search_value.found) {
                        RuntimeNameSearchResult result {};
                        result.found = true;
                        result.type = search_value.type;
                        result.value = search_value.value;

                        return ok(result);
                    }
                } else if(expression_value.type.kind == TypeKind::Type) {
                    auto type = expression_value.value.unwrap_type();

                    if(type.kind == TypeKind::Enum) {
                        auto enum_ = type.enum_;

                        for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                            if(enum_.definition->variants[i].name.text == name) {
                                RuntimeNameSearchResult result {};
                                result.found = true;
                                result.type = AnyType(*enum_.backing_type);
                                result.value = AnyRuntimeValue(AnyConstantValue(enum_.variant_values[i]));

                                return ok(result);
                            }
                        }
                    } else {
                        error(scope, using_statement->range, "Cannot apply 'using' with type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                        return err();
                    }
                } else {
                    error(scope, using_statement->range, "Cannot apply 'using' with type '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }
            }
        } else if(statement->kind == StatementKind::StaticIf) {
            auto static_if = (StaticIf*)statement;

            auto found = false;
            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolveStaticIf) {
                    auto resolve_static_if = job.resolve_static_if;

                    if(
                        resolve_static_if.static_if == static_if &&
                        resolve_static_if.scope == scope
                    ) {
                        found = true;

                        if(job.state == JobState::Done) {
                            if(resolve_static_if.condition) {
                                expect_delayed(search_value, search_for_name(
                                    info,
                                    jobs,
                                    scope,
                                    context,
                                    name,
                                    name_hash,
                                    name_scope,
                                    name_range,
                                    static_if->statements,
                                    resolve_static_if.declarations,
                                    false
                                ));

                                if(search_value.found) {
                                    RuntimeNameSearchResult result {};
                                    result.found = true;
                                    result.type = search_value.type;
                                    result.value = search_value.value;

                                    return ok(result);
                                }
                            }
                        } else {
                            bool could_have_declaration;
                            if(external) {
                                could_have_declaration = does_or_could_have_public_name(static_if, name);
                            } else {
                                could_have_declaration = does_or_could_have_name(static_if, name);
                            }

                            if(could_have_declaration) {
                                return wait(i);
                            }
                        }
                    }
                }
            }

            assert(found);
        } else if(statement->kind == StatementKind::VariableDeclaration) {
            if(scope->is_top_level) {
                auto variable_declaration = (VariableDeclaration*)statement;

                if(variable_declaration->name.text == name) {
                    for(size_t i = 0; i < jobs->length; i += 1) {
                        auto job = (*jobs)[i];

                        if(job.kind == JobKind::GenerateStaticVariable) {
                            auto generate_static_variable = job.generate_static_variable;

                            if(generate_static_variable.declaration == variable_declaration) {
                                if(job.state == JobState::Done) {
                                    auto pointer_register = append_reference_static(
                                        context,
                                        name_range,
                                        generate_static_variable.static_variable
                                    );

                                    auto ir_type = get_ir_type(info.architecture_sizes, generate_static_variable.type);

                                    RuntimeNameSearchResult result {};
                                    result.found = true;
                                    result.type = generate_static_variable.type;
                                    result.value = AnyRuntimeValue(AddressedValue(ir_type, pointer_register));

                                    return ok(result);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }

                    abort();
                }
            }
        }
    }

    for(auto scope_constant : scope->scope_constants) {
        if(scope_constant.name == name) {
            RuntimeNameSearchResult result {};
            result.found = true;
            result.type = scope_constant.type;
            result.value = AnyRuntimeValue(scope_constant.value);

            return ok(result);
        }
    }

    RuntimeNameSearchResult result {};
    result.found = false;

    return ok(result);
}

static_profiled_function(DelayedResult<TypedRuntimeValue>, generate_expression, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    Expression* expression
), (
    info,
    jobs,
    scope,
    context,
    expression
)) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        auto name_hash = calculate_string_hash(named_reference->name.text);

        assert(context->variable_scope_stack.length > 0);

        for(size_t i = 0; i < context->variable_scope_stack.length; i += 1) {
            auto current_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1 - i];

            for(auto variable : current_scope.variables) {
                if(variable.name.text == named_reference->name.text) {
                    return ok(TypedRuntimeValue(
                        variable.type,
                        AnyRuntimeValue(variable.value)
                    ));
                }
            }

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_scope.constant_scope,
                context,
                named_reference->name.text,
                name_hash,
                scope,
                named_reference->name.range,
                current_scope.constant_scope->statements,
                current_scope.constant_scope->declarations,
                false
            ));

            if(search_value.found) {
                return ok(TypedRuntimeValue(
                    search_value.type,
                    search_value.value
                ));
            }
        }

        assert(!context->variable_scope_stack[0].constant_scope->is_top_level);

        auto current_scope = context->variable_scope_stack[0].constant_scope->parent;
        while(true) {
            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_scope,
                context,
                named_reference->name.text,
                name_hash,
                scope,
                named_reference->name.range,
                current_scope->statements,
                current_scope->declarations,
                false
            ));

            if(search_value.found) {
                return ok(TypedRuntimeValue(
                    search_value.type,
                    search_value.value
                ));
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(named_reference->name.text == global_constant.name) {
                return ok(TypedRuntimeValue(
                    global_constant.type,
                    AnyRuntimeValue(global_constant.value)
                ));
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %.*s", STRING_PRINTF_ARGUMENTS(named_reference->name.text));

        return err();
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, index_reference->expression));

        expect_delayed(index, generate_expression(info, jobs, scope, context, index_reference->index));

        if(expression_value.value.kind == RuntimeValueKind::ConstantValue && index.value.kind == RuntimeValueKind::ConstantValue) {
             expect(constant, evaluate_constant_index(
                info,
                scope,
                expression_value.type,
                expression_value.value.constant,
                index_reference->expression->range,
                index.type,
                index.value.constant,
                index_reference->index->range
            ));

            return ok(TypedRuntimeValue(
                constant.type,
                AnyRuntimeValue(constant.value)
            ));
        }

        expect(index_register, coerce_to_integer_register_value(
            scope,
            context,
            index_reference->index->range,
            index.type,
            index.value,
            Integer(
                info.architecture_sizes.address_size,
                false
            ),
            false
        ));

        AnyType element_type;
        IRType element_ir_type;
        size_t base_pointer_register;
        if(expression_value.type.kind == TypeKind::ArrayTypeType) {
            auto array_type = expression_value.type.array;
            element_type = *array_type.element_type;

            auto ir_type = get_array_ir_type(info.architecture_sizes, array_type);
            element_ir_type = get_ir_type(info.architecture_sizes, element_type);

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                    auto array_value = expression_value.value.constant.array;

                    base_pointer_register = append_literal(
                        context,
                        index_reference->expression->range,
                        IRType::create_pointer(),
                        get_runtime_ir_constant_value(*array_value.pointer)
                    );
                } else {
                    error(scope, index_reference->expression->range, "Cannot index array constant at runtime");

                    return err();
                }
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.value.register_;

                base_pointer_register = append_read_struct_member(
                    context,
                    index_reference->expression->range,
                    1,
                    register_value.register_index
                );
            } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                auto addressed_value = expression_value.value.addressed;

                auto member_pointer = append_struct_member_pointer(
                    context,
                    index_reference->expression->range,
                    ir_type.struct_.members,
                    1,
                    addressed_value.pointer_register
                );

                base_pointer_register = append_load(
                    context,
                    index_reference->expression->range,
                    member_pointer,
                    IRType::create_pointer()
                );
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::StaticArray) {
            auto static_array = expression_value.type.static_array;
            element_type = *static_array.element_type;

            auto ir_type = get_static_array_ir_type(info.architecture_sizes, static_array);
            element_ir_type = get_ir_type(info.architecture_sizes, element_type);

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                error(scope, index_reference->expression->range, "Cannot index static array constant at runtime");

                return err();
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                error(scope, index_reference->expression->range, "Cannot index anonymous static array");

                return err();
            } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                auto addressed_value = expression_value.value.addressed;

                base_pointer_register = addressed_value.pointer_register;
            } else {
                abort();
            }
        } else {
            error(scope, index_reference->expression->range, "Cannot index '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
        }

        auto pointer_register = append_pointer_index(
            context,
            index_reference->range,
            index_register.register_index,
            element_ir_type,
            base_pointer_register
        );

        return ok(TypedRuntimeValue(
            element_type,
            AnyRuntimeValue(AddressedValue(element_ir_type, pointer_register))
        ));
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, member_reference->expression));

        AnyType actual_type;
        AnyRuntimeValue actual_value;
        if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;
            actual_type = *pointer.pointed_to_type;

            if(!actual_type.is_runtime_type()) {
                error(scope, member_reference->expression->range, "Cannot access members of '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description()));

                return err();
            }

            auto actual_ir_type = get_ir_type(info.architecture_sizes, actual_type);

            size_t pointer_register;
            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                pointer_register = append_literal(
                    context,
                    member_reference->expression->range,
                    IRType::create_pointer(),
                    get_runtime_ir_constant_value(expression_value.value.constant)
                );
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.value.register_;

                pointer_register = register_value.register_index;
            } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                auto addressed_value = expression_value.value.addressed;

                pointer_register = append_load(
                    context,
                    member_reference->expression->range,
                    addressed_value.pointer_register,
                    IRType::create_pointer()
                );
            } else {
                abort();
            }

            actual_value = AnyRuntimeValue(AddressedValue(actual_ir_type, pointer_register));
        } else {
            actual_type = expression_value.type;
            actual_value = expression_value.value;
        }

        if(actual_type.kind == TypeKind::ArrayTypeType) {
            auto array_type = actual_type.array;

            auto array_ir_type = get_array_ir_type(info.architecture_sizes, array_type);

            if(member_reference->name.text == u8"length"_S) {
                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = expression_value.value.constant.unwrap_array();

                        value = AnyRuntimeValue(AnyConstantValue(array_value.length));
                    } else if(expression_value.value.constant.kind == ConstantValueKind::StaticArrayConstant) {
                        auto static_array_value = expression_value.value.constant.unwrap_static_array();

                        value = AnyRuntimeValue(AnyConstantValue(static_array_value.elements.length));
                    } else {
                        assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                        error(scope, member_reference->range, "Cannot get length of undefined array constant");

                        return err();
                    }
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto length_register = append_read_struct_member(
                        context,
                        member_reference->range,
                        0,
                        register_value.register_index
                    );

                    value = AnyRuntimeValue(RegisterValue(
                        IRType::create_integer(info.architecture_sizes.address_size),
                        length_register
                    ));
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = actual_value.addressed;

                    auto pointer_register = append_struct_member_pointer(
                        context,
                        member_reference->range,
                        array_ir_type.struct_.members,
                        0,
                        addressed_value.pointer_register
                    );

                    value = AnyRuntimeValue(AddressedValue(
                        IRType::create_integer(info.architecture_sizes.address_size),
                        pointer_register
                    ));
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    AnyType(Integer(
                        info.architecture_sizes.address_size,
                        false
                    )),
                    value
                ));
            } else if(member_reference->name.text == u8"pointer"_S) {
                auto element_ir_type = get_ir_type(info.architecture_sizes, *array_type.element_type);

                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = expression_value.value.constant.unwrap_array();

                        value = AnyRuntimeValue(AnyConstantValue(array_value.pointer));
                    } else {
                        error(scope, member_reference->range, "Cannot take pointer to contents of constant array");

                        return err();
                    }
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto pointer_register = append_read_struct_member(
                        context,
                        member_reference->range,
                        1,
                        register_value.register_index
                    );

                    value = AnyRuntimeValue(RegisterValue(
                        IRType::create_pointer(),
                        pointer_register
                    ));
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = actual_value.addressed;

                    auto pointer_register = append_struct_member_pointer(
                        context,
                        member_reference->range,
                        array_ir_type.struct_.members,
                        1,
                        addressed_value.pointer_register
                    );

                    value = AnyRuntimeValue(AddressedValue(
                        element_ir_type,
                        pointer_register
                    ));
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    AnyType(Pointer(array_type.element_type)),
                    value
                ));
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StaticArray) {
            auto static_array = actual_type.static_array;

            auto element_ir_type = get_ir_type(info.architecture_sizes, *static_array.element_type);

            if(member_reference->name.text == u8"length"_S) {
                return ok(TypedRuntimeValue(
                    AnyType(Integer(
                        info.architecture_sizes.address_size,
                        false
                    )),
                    AnyRuntimeValue(AnyConstantValue(static_array.length))
                ));
            } else if(member_reference->name.text == u8"pointer"_S) {
                size_t pointer_register;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of constant static array");

                    return err();
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of r-value static array");

                    return err();
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = actual_value.addressed;

                    pointer_register = addressed_value.pointer_register;
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    AnyType(Pointer(static_array.element_type)),
                    AnyRuntimeValue(RegisterValue(
                        IRType::create_pointer(),
                        pointer_register
                    ))
                ));
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StructType) {
            auto struct_type = actual_type.struct_;

            auto struct_ir_type = get_struct_ir_type(info.architecture_sizes, struct_type);

            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(struct_type.members[i].name == member_reference->name.text) {
                    auto member_type = struct_type.members[i].type;
                    auto member_ir_type = get_ir_type(info.architecture_sizes, member_type);

                    if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::StructConstant) {
                            auto struct_value = expression_value.value.constant.unwrap_struct();

                            return ok(TypedRuntimeValue(
                                member_type,
                                AnyRuntimeValue(struct_value.members[i])
                            ));
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, member_reference->range, "Cannot access members of undefined array constant");

                            return err();
                        }
                    } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = actual_value.register_;

                        auto register_index = append_read_struct_member(
                            context,
                            member_reference->range,
                            i,
                            register_value.register_index
                        );

                        return ok(TypedRuntimeValue(
                            member_type,
                            AnyRuntimeValue(RegisterValue(member_ir_type, register_index))
                        ));
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = actual_value.addressed;

                        auto pointer_register = append_struct_member_pointer(
                            context,
                            member_reference->range,
                            struct_ir_type.struct_.members,
                            i,
                            addressed_value.pointer_register
                        );

                        return ok(TypedRuntimeValue(
                            member_type,
                            AnyRuntimeValue(AddressedValue(member_ir_type, pointer_register))
                        ));
                    } else {
                        abort();
                    }
                }
            }

            error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(actual_type.kind == TypeKind::UnionType) {
            auto union_type = actual_type.union_;

            for(size_t i = 0; i < union_type.members.length; i += 1) {
                if(union_type.members[i].name == member_reference->name.text) {
                    auto member_type = union_type.members[i].type;
                    auto member_ir_type = get_ir_type(info.architecture_sizes, member_type);

                    if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = actual_value.register_;

                        auto union_ir_type = get_union_ir_type(info.architecture_sizes, union_type);

                        auto pointer_register = append_allocate_local(
                            context,
                            member_reference->range,
                            union_ir_type
                        );

                        append_store(
                            context,
                            member_reference->range,
                            register_value.register_index,
                            pointer_register
                        );

                        auto register_index = append_load(
                            context,
                            member_reference->range,
                            pointer_register,
                            member_ir_type
                        );

                        return ok(TypedRuntimeValue(
                            member_type,
                            AnyRuntimeValue(RegisterValue(member_ir_type, register_index))
                        ));
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = actual_value.addressed;

                        auto pointer_register = addressed_value.pointer_register;

                        return ok(TypedRuntimeValue(
                            member_type,
                            AnyRuntimeValue(AddressedValue(member_ir_type, pointer_register))
                        ));
                    } else {
                        abort();
                    }
                }
            }

            error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(actual_type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = actual_type.undetermined_struct;

            if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = actual_value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                    if(undetermined_struct.members[i].name == member_reference->name.text) {
                        return ok(TypedRuntimeValue(
                            undetermined_struct.members[i].type,
                            AnyRuntimeValue(undetermined_struct_value.members[i])
                        ));
                    }
                }

                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            } else if(actual_value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = actual_value.undetermined_struct;

                for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                    if(undetermined_struct.members[i].name == member_reference->name.text) {
                        return ok(TypedRuntimeValue(
                            undetermined_struct.members[i].type,
                            undetermined_struct_value.members[i]
                        ));
                    }
                }

                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            } else {
                abort();
            }
        } else if(actual_type.kind == TypeKind::FileModule) {
            auto file_module_value = expression_value.value.constant.unwrap_file_module();

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                file_module_value.scope,
                context,
                member_reference->name.text,
                calculate_string_hash(member_reference->name.text),
                scope,
                member_reference->name.range,
                file_module_value.scope->statements,
                file_module_value.scope->declarations,
                true
            ));

            if(search_value.found) {
                return ok(TypedRuntimeValue(
                    search_value.type,
                    search_value.value
                ));
            }

            error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(expression_value.type.kind == TypeKind::Type) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto type = constant_value.type;

            if(type.kind == TypeKind::Enum) {
                auto enum_ = type.enum_;

                for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                    if(enum_.definition->variants[i].name.text == member_reference->name.text) {
                        return ok(TypedRuntimeValue(
                            type,
                            AnyRuntimeValue(AnyConstantValue(enum_.variant_values[i]))
                        ));
                    }
                }

                error(
                    scope,
                    member_reference->name.range,
                    "Enum '%.*s' has no variant with name '%.*s'",
                    STRING_PRINTF_ARGUMENTS(enum_.definition->name.text),
                    STRING_PRINTF_ARGUMENTS(member_reference->name.text)
                );

                return err();
            } else {
                error(
                    scope,
                    member_reference->expression->range,
                    "Type '%.*s' has no members",
                    STRING_PRINTF_ARGUMENTS(type.get_description())
                );

                return err();
            }
        } else {
            error(scope, member_reference->expression->range, "Type %.*s has no members", STRING_PRINTF_ARGUMENTS(actual_type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return ok(TypedRuntimeValue(
            AnyType::create_undetermined_integer(),
            AnyRuntimeValue(AnyConstantValue(integer_literal->value))
        ));
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return ok(TypedRuntimeValue(
            AnyType::create_undetermined_float(),
            AnyRuntimeValue(AnyConstantValue(float_literal->value))
        ));
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.length;

        auto characters = allocate<AnyConstantValue>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = AnyConstantValue((uint64_t)string_literal->characters[i]);
        }

        return ok(TypedRuntimeValue(
            AnyType(StaticArray(
                character_count,
                heapify(AnyType(Integer(
                    RegisterSize::Size8,
                    false
                )))
            )),
            AnyRuntimeValue(AnyConstantValue(StaticArrayConstant(
                Array(character_count, characters)
            )))
        ));
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.length;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err();
        }

        expect_delayed(first_element, generate_expression(info, jobs, scope, context, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!determined_element_type.is_runtime_type()) {
            error(scope, array_literal->range, "Arrays cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(determined_element_type.get_description()));

            return err();
        }

        auto elements = allocate<TypedRuntimeValue>(element_count);
        elements[0] = first_element;

        auto all_constant = first_element.value.kind == RuntimeValueKind::ConstantValue;
        for(size_t i = 1; i < element_count; i += 1) {
            expect_delayed(element, generate_expression(info, jobs, scope, context, array_literal->elements[i]));

            elements[i] = element;

            if(element.value.kind != RuntimeValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyRuntimeValue value;
        if(all_constant) {
            auto element_values = allocate<AnyConstantValue>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                expect(coerced_constant_value, coerce_constant_to_type(
                    info,
                    scope,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    elements[i].value.constant,
                    determined_element_type,
                    false
                ));

                element_values[i] = coerced_constant_value;
            }

            value = AnyRuntimeValue(AnyConstantValue(StaticArrayConstant(
                Array(element_count, element_values)
            )));
        } else {
            auto element_ir_type = get_ir_type(info.architecture_sizes, determined_element_type);

            auto element_registers = allocate<size_t>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                expect(register_value, coerce_to_type_register(
                    info,
                    scope,
                    context,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    elements[i].value,
                    determined_element_type,
                    false
                ));

                element_registers[i] = register_value.register_index;
            }

            auto register_index = append_assemble_static_array(
                context,
                array_literal->range,
                Array(element_count, element_registers)
            );

            value = AnyRuntimeValue(RegisterValue(
                IRType::create_static_array(element_count, heapify(element_ir_type)),
                register_index
            ));
        }

        return ok(TypedRuntimeValue(
            AnyType(StaticArray(
                element_count,
                heapify(determined_element_type)
            )),
            value
        ));
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        if(struct_literal->members.length == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err();
        }

        auto member_count = struct_literal->members.length;

        auto type_members = allocate<StructTypeMember>(member_count);
        auto member_values = allocate<AnyRuntimeValue>(member_count);
        auto all_constant = true;

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(struct_literal->members[i].name.text == type_members[j].name) {
                    error(scope, struct_literal->members[i].name.range, "Duplicate struct member %.*s", STRING_PRINTF_ARGUMENTS(struct_literal->members[i].name.text));

                    return err();
                }
            }

            expect_delayed(member, generate_expression(info, jobs, scope, context, struct_literal->members[i].value));

            type_members[i] = {
                struct_literal->members[i].name.text,
                member.type
            };

            member_values[i] = member.value;

            if(member.value.kind != RuntimeValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyRuntimeValue value;
        if(all_constant) {
            auto constant_member_values = allocate<AnyConstantValue>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                constant_member_values[i] = member_values[i].constant;
            }

            value = AnyRuntimeValue(AnyConstantValue(StructConstant(
                Array(member_count, constant_member_values)
            )));
        } else {
            value = AnyRuntimeValue(UndeterminedStructValue(
                Array(member_count, member_values)
            ));
        }

        return ok(TypedRuntimeValue(
            AnyType(UndeterminedStruct(
                Array(member_count, type_members)
            )),
            value
        ));
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, function_call->expression));

        if(expression_value.type.kind == TypeKind::FunctionTypeType || expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto call_parameter_count = function_call->parameters.length;

            auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

                call_parameters[i] = parameter_value;
            }

            FunctionTypeType function_type;
            FunctionConstant function_value;
            if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
                auto constant_value = expression_value.value.unwrap_constant_value();

                auto polymorphic_function_value = constant_value.unwrap_polymorphic_function();

                auto declaration_parameters = polymorphic_function_value.declaration->parameters;
                auto declaration_parameter_count = declaration_parameters.length;

                if(call_parameter_count != declaration_parameter_count) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        declaration_parameter_count,
                        call_parameter_count
                    );

                    return err();
                }

                auto polymorphic_parameters = allocate<TypedConstantValue>(declaration_parameter_count);

                for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                    auto declaration_parameter = declaration_parameters[i];

                    if(declaration_parameter.is_polymorphic_determiner) {
                        polymorphic_parameters[i].type = call_parameters[i].type;
                    }

                    if(declaration_parameter.is_constant) {
                        if(call_parameters[i].value.kind != RuntimeValueKind::ConstantValue) {
                            error(
                                scope,
                                function_call->parameters[i]->range,
                                "Non-constant value provided for constant parameter '%.*s'",
                                STRING_PRINTF_ARGUMENTS(declaration_parameter.name.text)
                            );

                            return err();
                        }

                        polymorphic_parameters[i] = TypedConstantValue(
                            call_parameters[i].type,
                            call_parameters[i].value.constant
                        );
                    }
                }

                auto found = false;
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicFunction) {
                        auto resolve_polymorphic_function = job.resolve_polymorphic_function;

                        if(
                            resolve_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                            resolve_polymorphic_function.scope == polymorphic_function_value.scope
                        ) {
                            auto matching_polymorphic_parameters = true;
                            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                                auto declaration_parameter = declaration_parameters[i];
                                auto call_parameter = polymorphic_parameters[i];
                                auto job_parameter = resolve_polymorphic_function.parameters[i];

                                if(
                                    (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                    job_parameter.type != call_parameter.type
                                ) {
                                    matching_polymorphic_parameters = false;
                                    break;
                                }

                                if(
                                    declaration_parameter.is_constant &&
                                    !constant_values_equal(call_parameter.value, job_parameter.value)
                                ) {
                                    matching_polymorphic_parameters = false;
                                    break;
                                }
                            }

                            if(!matching_polymorphic_parameters) {
                                continue;
                            }

                            if(job.state == JobState::Done) {
                                found = true;

                                function_type = resolve_polymorphic_function.type;
                                function_value = resolve_polymorphic_function.value;

                                break;
                            } else {
                                return wait(i);
                            }  
                        }
                    }
                }

                if(!found) {
                    auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

                    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                        call_parameter_ranges[i] = function_call->parameters[i]->range;
                    }

                    AnyJob job;
                    job.kind = JobKind::ResolvePolymorphicFunction;
                    job.state = JobState::Working;
                    job.resolve_polymorphic_function.declaration = polymorphic_function_value.declaration;
                    job.resolve_polymorphic_function.parameters = polymorphic_parameters;
                    job.resolve_polymorphic_function.scope = polymorphic_function_value.scope;
                    job.resolve_polymorphic_function.call_scope = scope;
                    job.resolve_polymorphic_function.call_parameter_ranges = call_parameter_ranges;

                    auto job_index = jobs->append(job);

                    return wait(job_index);
                }
            } else {
                function_type = expression_value.type.function;

                auto constant_value = expression_value.value.unwrap_constant_value();

                function_value = constant_value.unwrap_function();

                if(call_parameter_count != function_type.parameters.length) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        function_type.parameters.length,
                        call_parameter_count
                    );

                    return err();
                }
            }

            auto found = false;
            Function* runtime_function;
            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::GenerateFunction) {
                    auto generate_function = job.generate_function;

                    if(
                        AnyType(generate_function.type) == AnyType(function_type) &&
                        generate_function.value.declaration == function_value.declaration &&
                        generate_function.value.body_scope == function_value.body_scope
                    ) {
                        found = true;

                        runtime_function = generate_function.function;

                        break;
                    }
                }
            }

            if(!found) {
                runtime_function = new Function;

                AnyJob job;
                job.kind = JobKind::GenerateFunction;
                job.state = JobState::Working;
                job.generate_function.type = function_type;
                job.generate_function.value = function_value;
                job.generate_function.function = runtime_function;

                jobs->append(job);
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(function_type.parameters.length);

            size_t runtime_parameter_index = 0;
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                if(!function_value.declaration->parameters[i].is_constant) {
                    expect(parameter_register, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        function_call->parameters[i]->range,
                        call_parameters[i].type,
                        call_parameters[i].value,
                        function_type.parameters[i],
                        false
                    ));

                    auto ir_type = get_ir_type(info.architecture_sizes, function_type.parameters[i]);

                    instruction_parameters[i] = {
                        ir_type,
                        parameter_register.register_index
                    };

                    runtime_parameter_index += 1;
                }
            }

            assert(runtime_parameter_index == function_type.parameters.length);

            AnyType return_type;
            bool has_ir_return;
            IRType return_ir_type;
            if(function_type.return_types.length == 0) {
                return_type = AnyType::create_void();
                has_ir_return = false;
            } else if(function_type.return_types.length == 1) {
                return_type = function_type.return_types[0];
                has_ir_return = true;
                return_ir_type = get_ir_type(info.architecture_sizes, return_type);
            } else {
                return_type = AnyType(MultiReturn(function_type.return_types));

                auto member_ir_types = allocate<IRType>(function_type.return_types.length);

                for(size_t i = 0; i < function_type.return_types.length; i += 1) {
                    member_ir_types[i] = get_ir_type(info.architecture_sizes, function_type.return_types[i]);
                }

                has_ir_return = true;
                return_ir_type = IRType::create_struct(Array(function_type.return_types.length, member_ir_types));
            }

            auto pointer_register = append_reference_static(context, function_call->range, runtime_function);

            assert(context->variable_scope_stack.length != 0);
            auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->range = function_call->range;
            function_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
            function_call_instruction->pointer_register = pointer_register;
            function_call_instruction->parameters = Array(function_type.parameters.length, instruction_parameters);
            function_call_instruction->has_return = has_ir_return;
            function_call_instruction->return_type = return_ir_type;
            function_call_instruction->calling_convention = function_type.calling_convention;

            AnyRuntimeValue value;
            if(return_type.kind != TypeKind::Void) {
                auto return_register = allocate_register(context);

                function_call_instruction->return_register = return_register;

                value = AnyRuntimeValue(RegisterValue(return_ir_type, return_register));
            } else {
                value = AnyRuntimeValue(AnyConstantValue::create_void());
            }

            context->instructions.append(function_call_instruction);

            return ok(TypedRuntimeValue(
                return_type,
                value
            ));
        } else if(expression_value.type.kind == TypeKind::BuiltinFunction) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto builtin_function_value = constant_value.unwrap_builtin_function();

            if(builtin_function_value.name == u8"size_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                AnyType type;
                if(parameter_value.type.kind == TypeKind::Type) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    type = constant_value.unwrap_type();
                } else {
                    type = parameter_value.type;
                }

                if(!type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "'%.*s'' has no size", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description()));

                    return err();
                }

                auto size = type.get_size(info.architecture_sizes);

                return ok(TypedRuntimeValue(
                    AnyType(Integer(
                        info.architecture_sizes.address_size,
                        false
                    )),
                    AnyRuntimeValue(AnyConstantValue(size))
                ));
            } else if(builtin_function_value.name == u8"type_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                return ok(TypedRuntimeValue(
                    AnyType::AnyType::create_type_type(),
                    AnyRuntimeValue(AnyConstantValue(parameter_value.type))
                ));
            } else if(builtin_function_value.name == u8"globalify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                expect(determined_type, coerce_to_default_type(info, scope, function_call->parameters[0]->range, parameter_value.type));

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description()));

                    return err();
                }

                if(parameter_value.value.kind != RuntimeValueKind::ConstantValue) {
                    error(scope, function_call->parameters[0]->range, "Cannot globalify a non-constant value");

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_constant_to_type(
                    info,
                    scope,
                    function_call->parameters[0]->range,
                    parameter_value.type,
                    constant_value,
                    determined_type,
                    false
                ));

                auto static_constant = register_static_constant(
                    info,
                    scope,
                    context,
                    function_call->range,
                    determined_type,
                    coerced_value
                );

                auto pointer_register = append_reference_static(
                    context,
                    function_call->range,
                    static_constant
                );

                return ok(TypedRuntimeValue(
                    determined_type,
                    AnyRuntimeValue(AddressedValue(static_constant->type, pointer_register))
                ));
            } else if(builtin_function_value.name == u8"stackify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                expect(determined_type, coerce_to_default_type(info, scope, function_call->parameters[0]->range, parameter_value.type));

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description()));

                    return err();
                }

                if(parameter_value.value.kind != RuntimeValueKind::ConstantValue) {
                    error(scope, function_call->parameters[0]->range, "Cannot stackify a non-constant value");

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_constant_to_type(
                    info,
                    scope,
                    function_call->parameters[0]->range,
                    parameter_value.type,
                    constant_value,
                    determined_type,
                    false
                ));

                auto ir_constant_value = get_runtime_ir_constant_value(coerced_value);

                auto ir_type = get_ir_type(info.architecture_sizes, determined_type);

                auto pointer_register = append_allocate_local(
                    context,
                    function_call->range,
                    ir_type
                );

                auto literal_register = append_literal(
                    context,
                    function_call->range,
                    ir_type,
                    ir_constant_value
                );

                append_store(
                    context,
                    function_call->range,
                    literal_register,
                    pointer_register
                );

                return ok(TypedRuntimeValue(
                    determined_type,
                    AnyRuntimeValue(AddressedValue(ir_type, pointer_register))
                ));
            } else if(builtin_function_value.name == u8"sqrt"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                if(parameter_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    RegisterSize result_size;
                    double value;
                    if(parameter_value.type.kind == TypeKind::UndeterminedInteger) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        auto integer_value = constant_value.unwrap_integer();

                        result_size = info.architecture_sizes.default_float_size;
                        value = (double)integer_value;
                    } else if(parameter_value.type.kind == TypeKind::UndeterminedFloat) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = info.architecture_sizes.default_float_size;
                        value = constant_value.unwrap_float();
                    } else if(parameter_value.type.kind == TypeKind::FloatType) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = parameter_value.type.float_.size;
                        value = constant_value.unwrap_float();
                    } else {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description()));

                        return err();
                    }

                    auto result = sqrt(value);

                    return ok(TypedRuntimeValue(
                        AnyType(FloatType(result_size)),
                        AnyRuntimeValue(AnyConstantValue(result))
                    ));
                } else {
                    if(parameter_value.type.kind != TypeKind::FloatType) {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description()));

                        return err();
                    }

                    auto ir_type = IRType::create_float(parameter_value.type.float_.size);

                    size_t register_index;
                    if(parameter_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = parameter_value.value.register_;

                        register_index = register_value.register_index;
                    } else {
                        auto addressed_value = parameter_value.value.unwrap_addressed_value();

                        register_index = append_load(
                            context,
                            function_call->parameters[0]->range,
                            addressed_value.pointer_register,
                            ir_type
                        );
                    }

                    auto return_register = allocate_register(context);

                    IntrinsicCallInstruction::Parameter ir_parameter;
                    ir_parameter.type = ir_type;
                    ir_parameter.register_index = register_index;

                    assert(context->variable_scope_stack.length != 0);
                    auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                    auto intrinsic_call_instruction = new IntrinsicCallInstruction;
                    intrinsic_call_instruction->range = function_call->range;
                    intrinsic_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
                    intrinsic_call_instruction->intrinsic = IntrinsicCallInstruction::Intrinsic::Sqrt;
                    intrinsic_call_instruction->parameters = Array(1, heapify(ir_parameter));
                    intrinsic_call_instruction->has_return = true;
                    intrinsic_call_instruction->return_type = ir_type;
                    intrinsic_call_instruction->return_register = return_register;

                    context->instructions.append(intrinsic_call_instruction);

                    return ok(TypedRuntimeValue(
                        parameter_value.type,
                        AnyRuntimeValue(RegisterValue(ir_type, return_register))
                    ));
                }
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;

            if(pointer.pointed_to_type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                return err();
            }

            auto function_type = pointer.pointed_to_type->function;

            auto pointer_register = generate_in_register_value(
                context,
                function_call->expression->range,
                IRType::create_pointer(),
                expression_value.value
            );

            auto parameter_count = function_type.parameters.length;

            if(function_call->parameters.length != parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    parameter_count,
                    function_call->parameters.length
                );

                return err();
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

                expect(parameter_register, coerce_to_type_register(
                    info,
                    scope,
                    context,
                    function_call->parameters[i]->range,
                    parameter_value.type,
                    parameter_value.value,
                    function_type.parameters[i],
                    false
                ));

                auto parameter_ir_type = get_ir_type(info.architecture_sizes, function_type.parameters[i]);

                instruction_parameters[i] = {
                    parameter_ir_type,
                    parameter_register.register_index
                };
            }

            AnyType return_type;
            bool has_ir_return;
            IRType return_ir_type;
            if(function_type.return_types.length == 0) {
                return_type = AnyType::create_void();
                has_ir_return = false;
            } else if(function_type.return_types.length == 1) {
                return_type = function_type.return_types[0];
                has_ir_return = true;
                return_ir_type = get_ir_type(info.architecture_sizes, return_type);
            } else {
                return_type = AnyType(MultiReturn(function_type.return_types));

                auto member_ir_types = allocate<IRType>(function_type.return_types.length);

                for(size_t i = 0; i < function_type.return_types.length; i += 1) {
                    member_ir_types[i] = get_ir_type(info.architecture_sizes, function_type.return_types[i]);
                }

                has_ir_return = true;
                return_ir_type = IRType::create_struct(Array(function_type.return_types.length, member_ir_types));
            }

            assert(context->variable_scope_stack.length != 0);
            auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->range = function_call->range;
            function_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
            function_call_instruction->pointer_register = pointer_register;
            function_call_instruction->parameters = Array(parameter_count, instruction_parameters);
            function_call_instruction->has_return = has_ir_return;
            function_call_instruction->return_type = return_ir_type;
            function_call_instruction->calling_convention = function_type.calling_convention;

            AnyRuntimeValue value;
            if(return_type.kind != TypeKind::Void) {
                auto return_register = allocate_register(context);

                function_call_instruction->return_register = return_register;

                value = AnyRuntimeValue(RegisterValue(return_ir_type, return_register));
            } else {
                value = AnyRuntimeValue(AnyConstantValue::create_void());
            }

            context->instructions.append(function_call_instruction);

            return ok(TypedRuntimeValue(
                return_type,
                value
            ));
        } else if(expression_value.type.kind == TypeKind::Type) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto type = constant_value.unwrap_type();

            if(type.kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = type.polymorphic_struct;
                auto definition = polymorphic_struct.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto parameters = allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.type,
                        parameter.value,
                        polymorphic_struct.parameter_types[i],
                        false
                    ));

                    parameters[i] = {
                        parameter_value
                    };
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicStruct) {
                        auto resolve_polymorphic_struct = job.resolve_polymorphic_struct;

                        if(resolve_polymorphic_struct.definition == definition && resolve_polymorphic_struct.parameters != nullptr) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameters[i], resolve_polymorphic_struct.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    return ok(TypedRuntimeValue(
                                        AnyType::AnyType::create_type_type(),
                                        AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_struct.type))
                                    ));
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job;
                job.kind = JobKind::ResolvePolymorphicStruct;
                job.state = JobState::Working;
                job.resolve_polymorphic_struct.definition = definition;
                job.resolve_polymorphic_struct.parameters = parameters;
                job.resolve_polymorphic_struct.scope = polymorphic_struct.parent;

                auto job_index = jobs->append(job);

                return wait(job_index);
            } else if(type.kind == TypeKind::PolymorphicUnion) {
                auto polymorphic_union = type.polymorphic_union;
                auto definition = polymorphic_union.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect union parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto parameters = allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.type,
                        parameter.value,
                        polymorphic_union.parameter_types[i],
                        false
                    ));

                    parameters[i] = {
                        parameter_value
                    };
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicUnion) {
                        auto resolve_polymorphic_union = job.resolve_polymorphic_union;

                        if(resolve_polymorphic_union.definition == definition && resolve_polymorphic_union.parameters != nullptr) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameters[i], resolve_polymorphic_union.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    return ok(TypedRuntimeValue(
                                        AnyType::AnyType::create_type_type(),
                                        AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_union.type))
                                    ));
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job;
                job.kind = JobKind::ResolvePolymorphicUnion;
                job.state = JobState::Working;
                job.resolve_polymorphic_union.definition = definition;
                job.resolve_polymorphic_union.parameters = parameters;
                job.resolve_polymorphic_union.scope = polymorphic_union.parent;

                auto job_index = jobs->append(job);

                return wait(job_index);
            } else {
                error(scope, function_call->expression->range, "Type '%.*s' is not polymorphic", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        expect_delayed(result_value, generate_binary_operation(
            info,
            jobs,
            scope,
            context,
            binary_operation->range,
            binary_operation->left,
            binary_operation->right,
            binary_operation->binary_operator
        ));

        return ok(result_value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                size_t pointer_register;
                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = expression_value.value.constant;

                    if(expression_value.type.kind == TypeKind::FunctionTypeType) {
                        auto function = expression_value.type.function;

                        auto function_value = constant_value.unwrap_function();

                        auto found = false;
                        Function* runtime_function;
                        for(size_t i = 0; i < jobs->length; i += 1) {
                            auto job = (*jobs)[i];

                            if(job.kind == JobKind::GenerateFunction) {
                                auto generate_function = job.generate_function;

                                if(
                                    AnyType(generate_function.type) == AnyType(function) &&
                                    generate_function.value.declaration == function_value.declaration &&
                                    generate_function.value.body_scope == function_value.body_scope
                                ) {
                                    found = true;

                                    runtime_function = generate_function.function;

                                    break;
                                }
                            }
                        }

                        if(!found) {
                            runtime_function = new Function;

                            AnyJob job;
                            job.kind = JobKind::GenerateFunction;
                            job.state = JobState::Working;
                            job.generate_function.type = function;
                            job.generate_function.value = function_value;
                            job.generate_function.function = runtime_function;

                            jobs->append(job);
                        }

                        pointer_register = append_reference_static(
                            context,
                            unary_operation->range,
                            runtime_function
                        );
                    } else if(expression_value.type.kind == TypeKind::Type) {
                        auto type = constant_value.unwrap_type();

                        if(!type.is_pointable_type()) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                            return err();
                        }

                        return ok(TypedRuntimeValue(
                            AnyType::AnyType::create_type_type(),
                            AnyRuntimeValue(AnyConstantValue(AnyType(Pointer(heapify(type)))))
                        ));
                    } else {
                        error(scope, unary_operation->expression->range, "Cannot take pointers to constants of type '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                        return err();
                    }
                } else if(
                    expression_value.value.kind == RuntimeValueKind::RegisterValue ||
                    expression_value.value.kind == RuntimeValueKind::UndeterminedStructValue
                ) {
                    error(scope, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return err();
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    pointer_register = addressed_value.pointer_register;
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    AnyType(Pointer(heapify(expression_value.type))),
                    AnyRuntimeValue(RegisterValue(IRType::create_pointer(), pointer_register))
                ));
            } break;

            case UnaryOperation::Operator::PointerDereference: {
                if(expression_value.type.kind != TypeKind::Pointer) {
                    error(scope, unary_operation->expression->range, "Expected a pointer, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }

                auto pointed_to_type = *expression_value.type.pointer.pointed_to_type;

                if(!pointed_to_type.is_runtime_type()) {
                    error(scope, unary_operation->expression->range, "Cannot dereference pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(pointed_to_type.get_description()));

                    return err();
                }

                auto pointed_to_ir_type = get_ir_type(info.architecture_sizes, pointed_to_type);

                auto pointer_register = generate_in_register_value(
                    context,
                    unary_operation->expression->range,
                    IRType::create_pointer(),
                    expression_value.value
                );

                return ok(TypedRuntimeValue(
                    pointed_to_type,
                    AnyRuntimeValue(AddressedValue(pointed_to_ir_type, pointer_register))
                ));
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type.kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }

                size_t register_index;
                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::BooleanConstant) {
                        auto boolean_value = expression_value.value.constant.unwrap_boolean();

                        return ok(TypedRuntimeValue(
                            AnyType::create_boolean(),
                            AnyRuntimeValue(AnyConstantValue(!boolean_value))
                        ));
                    } else {
                        assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                        error(scope, unary_operation->expression->range, "Cannot invert an undefined boolean constant");

                        return err();
                    }
                } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    register_index = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    register_index = append_load(
                        context,
                        unary_operation->expression->range,
                        addressed_value.pointer_register,
                        IRType::create_boolean()
                    );
                }

                auto result_register = append_boolean_inversion(context, unary_operation->expression->range, register_index);

                return ok(TypedRuntimeValue(
                    AnyType::create_boolean(),
                    AnyRuntimeValue(RegisterValue(IRType::create_boolean(), result_register))
                ));
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto integer_value = constant_value.unwrap_integer();

                    return ok(TypedRuntimeValue(
                        AnyType::create_undetermined_integer(),
                        AnyRuntimeValue(AnyConstantValue((uint64_t)-(int64_t)integer_value))
                    ));
                } else if(expression_value.type.kind == TypeKind::Integer) {
                    auto integer = expression_value.type.integer;

                    auto ir_type = IRType::create_integer(integer.size);

                    size_t register_index;
                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::IntegerConstant) {
                            auto integer_value = expression_value.value.constant.unwrap_integer();

                            return ok(TypedRuntimeValue(
                                AnyType::create_undetermined_integer(),
                                AnyRuntimeValue(AnyConstantValue((uint64_t)-(int64_t)integer_value))
                            ));
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined integer constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.value.addressed;

                        register_index = append_load(
                            context,
                            unary_operation->expression->range,
                            addressed_value.pointer_register,
                            ir_type
                        );
                    }

                    auto zero_register = append_literal(
                        context,
                        unary_operation->range,
                        ir_type,
                        IRConstantValue::create_integer(0)
                    );

                    auto result_register = append_integer_arithmetic_operation(
                        context,
                        unary_operation->range,
                        IntegerArithmeticOperation::Operation::Subtract,
                        zero_register,
                        register_index
                    );

                    return ok(TypedRuntimeValue(
                        AnyType(integer),
                        AnyRuntimeValue(RegisterValue(ir_type, result_register))
                    ));
                } else if(expression_value.type.kind == TypeKind::FloatType) {
                    auto float_type = expression_value.type.float_;

                    auto ir_type = IRType::create_float(float_type.size);

                    size_t register_index;
                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::FloatConstant) {
                            auto float_value = expression_value.value.constant.unwrap_float();

                            return ok(TypedRuntimeValue(
                                AnyType(float_type),
                                AnyRuntimeValue(AnyConstantValue(-float_value))
                            ));
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined float constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.value.addressed;

                        register_index = append_load(
                            context,
                            unary_operation->expression->range,
                            addressed_value.pointer_register,
                            ir_type
                        );
                    }

                    auto zero_register = append_literal(
                        context,
                        unary_operation->range,
                        ir_type,
                        IRConstantValue::create_float(0.0)
                    );

                    auto result_register = append_float_arithmetic_operation(
                        context,
                        unary_operation->range,
                        FloatArithmeticOperation::Operation::Subtract,
                        zero_register,
                        register_index
                    );

                    return ok(TypedRuntimeValue(
                        AnyType(float_type),
                        AnyRuntimeValue(RegisterValue(ir_type, result_register))
                    ));
                } else if(expression_value.type.kind == TypeKind::UndeterminedFloat) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto float_value = constant_value.unwrap_float();

                    return ok(TypedRuntimeValue(
                        AnyType::create_undetermined_float(),
                        AnyRuntimeValue(AnyConstantValue(-float_value))
                    ));
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, cast->expression));

        expect_delayed(target_type, evaluate_type_expression(info, jobs, scope, context, cast->type));

        if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
            auto constant_cast_result = evaluate_constant_cast(
                info,
                scope,
                expression_value.type,
                expression_value.value.constant,
                cast->expression->range,
                target_type,
                cast->type->range,
                true
            );

            if(constant_cast_result.status) {
                return ok(TypedRuntimeValue(
                    target_type,
                    AnyRuntimeValue(constant_cast_result.value)
                ));
            }
        }

        auto coercion_result = coerce_to_type_register(
            info,
            scope,
            context,
            cast->range,
            expression_value.type,
            expression_value.value,
            target_type,
            true
        );

        auto has_cast = false;
        size_t register_index;
        if(coercion_result.status) {
            has_cast = true;
            register_index = coercion_result.value.register_index;
        } else if(target_type.kind == TypeKind::Integer) {
            auto target_integer = target_type.integer;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                has_cast = true;

                if(target_integer.size > integer.size) {
                    register_index = append_integer_extension(
                        context,
                        cast->range,
                        integer.is_signed,
                        target_integer.size,
                        value_register
                    );
                } else if(target_integer.size < integer.size) {
                    register_index = append_integer_truncation(
                        context,
                        cast->range,
                        target_integer.size,
                        value_register
                    );
                } else {
                    register_index = value_register;
                }
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_integer_from_float(
                    context,
                    cast->range,
                    target_integer.is_signed,
                    target_integer.size,
                    value_register
                );
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;
                if(target_integer.size == info.architecture_sizes.address_size && !target_integer.is_signed) {
                    has_cast = true;

                    size_t value_register;
                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        value_register = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.value.addressed;

                        value_register = append_load(
                            context,
                            cast->expression->range,
                            addressed_value.pointer_register,
                            addressed_value.pointed_to_type
                        );
                    } else {
                        abort();
                    }

                    register_index = append_integer_from_pointer(
                        context,
                        cast->range,
                        target_integer.size,
                        value_register
                    );
                }
            }
        } else if(target_type.kind == TypeKind::FloatType) {
            auto target_float_type = target_type.float_;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_from_integer(
                    context,
                    cast->range,
                    integer.is_signed,
                    target_float_type.size,
                    value_register
                );
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_conversion(
                    context,
                    cast->range,
                    target_float_type.size,
                    value_register
                );
            }
        } else if(target_type.kind == TypeKind::Pointer) {
            auto target_pointer = target_type.pointer;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                if(integer.size == info.architecture_sizes.address_size && !integer.is_signed) {
                    has_cast = true;

                    size_t value_register;
                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        value_register = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.value.addressed;

                        value_register = append_load(
                            context,
                            cast->expression->range,
                            addressed_value.pointer_register,
                            addressed_value.pointed_to_type
                        );
                    } else {
                        abort();
                    }

                    register_index = append_pointer_from_integer(
                        context,
                        cast->range,
                        value_register
                    );
                }
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;
                has_cast = true;

                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                register_index = value_register;
            }
        } else if(target_type.kind == TypeKind::Enum) {
            auto target_enum = target_type.enum_;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.value.addressed;

                    value_register = append_load(
                        context,
                        cast->expression->range,
                        addressed_value.pointer_register,
                        addressed_value.pointed_to_type
                    );
                } else {
                    abort();
                }

                has_cast = true;

                if(target_enum.backing_type->size > integer.size) {
                    register_index = append_integer_extension(
                        context,
                        cast->range,
                        integer.is_signed,
                        target_enum.backing_type->size,
                        value_register
                    );
                } else if(target_enum.backing_type->size < integer.size) {
                    register_index = append_integer_truncation(
                        context,
                        cast->range,
                        target_enum.backing_type->size,
                        value_register
                    );
                } else {
                    register_index = value_register;
                }
            }
        } else {
            abort();
        }

        if(has_cast) {
            auto ir_type = get_ir_type(info.architecture_sizes, target_type);

            return ok(TypedRuntimeValue(
                target_type,
                AnyRuntimeValue(RegisterValue(ir_type, register_index))
            ));
        } else {
            error(scope, cast->range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, function_call->expression));

        auto call_parameter_count = function_call->parameters.length;

        auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

            call_parameters[i] = parameter_value;
        }

        if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto polymorphic_function_value = constant_value.unwrap_polymorphic_function();

            auto declaration_parameters = polymorphic_function_value.declaration->parameters;
            auto declaration_parameter_count = declaration_parameters.length;

            if(call_parameter_count != declaration_parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    declaration_parameter_count,
                    call_parameter_count
                );

                return err();
            }

            auto polymorphic_parameters = allocate<TypedConstantValue>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                auto declaration_parameter = declaration_parameters[i];

                if(declaration_parameter.is_polymorphic_determiner) {
                    polymorphic_parameters[i].type = call_parameters[i].type;
                }

                if(declaration_parameter.is_constant) {
                    if(call_parameters[i].value.kind != RuntimeValueKind::ConstantValue) {
                        error(
                            scope,
                            function_call->parameters[i]->range,
                            "Non-constant value provided for constant parameter '%.*s'",
                            STRING_PRINTF_ARGUMENTS(declaration_parameter.name.text)
                        );

                        return err();
                    }

                    polymorphic_parameters[i] = TypedConstantValue(
                        call_parameters[i].type,
                        call_parameters[i].value.constant
                    );
                }
            }

            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolvePolymorphicFunction) {
                    auto resolve_polymorphic_function = job.resolve_polymorphic_function;

                    if(
                        resolve_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                        resolve_polymorphic_function.scope == polymorphic_function_value.scope
                    ) {
                        auto matching_polymorphic_parameters = true;
                        for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                            auto declaration_parameter = declaration_parameters[i];
                            auto call_parameter = polymorphic_parameters[i];
                            auto job_parameter = resolve_polymorphic_function.parameters[i];

                            if(
                                (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                job_parameter.type != call_parameter.type
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }

                            if(
                                declaration_parameter.is_constant &&
                                !constant_values_equal( call_parameter.value, job_parameter.value)
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }
                        }

                        if(!matching_polymorphic_parameters) {
                            continue;
                        }

                        if(job.state == JobState::Done) {
                            return ok(TypedRuntimeValue(
                                AnyType(resolve_polymorphic_function.type),
                                AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_function.value))
                            ));
                        } else {
                            return wait(i);
                        }  
                    }
                }
            }

            auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                call_parameter_ranges[i] = function_call->parameters[i]->range;
            }

            AnyJob job;
            job.kind = JobKind::ResolvePolymorphicFunction;
            job.state = JobState::Working;
            job.resolve_polymorphic_function.declaration = polymorphic_function_value.declaration;
            job.resolve_polymorphic_function.parameters = polymorphic_parameters;
            job.resolve_polymorphic_function.scope = polymorphic_function_value.scope;
            job.resolve_polymorphic_function.call_scope = scope;
            job.resolve_polymorphic_function.call_parameter_ranges = call_parameter_ranges;

            auto job_index = jobs->append(job);

            return wait(job_index);
        } else if(expression_value.type.kind == TypeKind::FunctionTypeType) {
            auto function_type = expression_value.type.function;

            auto constant_value = expression_value.value.unwrap_constant_value();

            auto function_value = constant_value.unwrap_function();

            if(call_parameter_count != function_type.parameters.length) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    function_type.parameters.length,
                    call_parameter_count
                );

                return err();
            }

            return ok(TypedRuntimeValue(
                AnyType(function_type),
                AnyRuntimeValue(AnyConstantValue(function_value))
            ));
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, context, array_type->expression));

        if(!type.is_runtime_type()) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        if(array_type->length != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, nullptr, array_type->length));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->length->range,
                index_value.type,
                index_value.value,
                Integer(
                    info.architecture_sizes.address_size,
                    false
                ),
                false
            ));

            if(length.kind == ConstantValueKind::UndefConstant) {
                error(scope, array_type->length->range, "Length cannot be undefined");

                return err();
            }

            auto length_integer = length.unwrap_integer();

            return ok(TypedRuntimeValue(
                AnyType::create_type_type(),
                AnyRuntimeValue(AnyConstantValue(AnyType(StaticArray(
                    length_integer,
                    heapify(type)
                ))))
            ));
        } else {
            return ok(TypedRuntimeValue(
                AnyType::create_type_type(),
                AnyRuntimeValue(AnyConstantValue(AnyType(ArrayTypeType(
                    heapify(type)
                ))))
            ));
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.length;

        auto parameters = allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err();
            }

            expect_delayed(type, evaluate_type_expression(info, jobs, scope, context, parameter.type));

            if(!type.is_runtime_type()) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            parameters[i] = type;
        }

        auto return_type_count = function_type->return_types.length;

        auto return_types = allocate<AnyType>(return_type_count);

        for(size_t i = 0; i < return_type_count; i += 1) {
            auto expression = function_type->return_types[i];

            expect_delayed(type, evaluate_type_expression(info, jobs, scope, (Statement*)nullptr, expression));

            if(!type.is_runtime_type()) {
                error(scope, expression->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            return_types[i] = type;
        }

        auto is_calling_convention_specified = false;
        auto calling_convention = CallingConvention::Default;
        for(auto tag : function_type->tags) {
            if(tag.name.text == u8"extern"_S) {
                error(scope, tag.range, "Function types cannot be external");

                return err();
            } else if(tag.name.text == u8"no_mangle"_S) {
                error(scope, tag.range, "Function types cannot be no_mangle");

                return err();
            } else if(tag.name.text == u8"call_conv"_S) {
                if(is_calling_convention_specified) {
                    error(scope, tag.range, "Duplicate 'call_conv' tag");

                    return err();
                }

                if(tag.parameters.length != 1) {
                    error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                    return err();
                }

                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[0]));

                expect(calling_convention_name, array_to_string(scope, tag.parameters[0]->range, parameter.type, parameter.value));

                if(calling_convention_name == u8"default"_S) {
                    calling_convention = CallingConvention::Default;
                } else if(calling_convention_name == u8"stdcall"_S) {
                    calling_convention = CallingConvention::StdCall;
                }

                is_calling_convention_specified = true;
            } else {
                error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                return err();
            }
        }

        return ok(TypedRuntimeValue(
            AnyType::create_type_type(),
            AnyRuntimeValue(AnyConstantValue(AnyType(FunctionTypeType(
                Array(parameter_count, parameters),
                Array(return_type_count, return_types),
                calling_convention
            ))))
        ));
    } else {
        abort();
    }
}

static bool does_current_block_need_finisher(GenerationContext* context) {
    if(context->instructions.length == 0) {
        return true;
    }

    auto last_instruction = context->instructions[context->instructions.length - 1];

    return
        last_instruction->kind != InstructionKind::ReturnInstruction &&
        last_instruction->kind != InstructionKind::Branch &&
        last_instruction->kind != InstructionKind::Jump
    ;
}

static void enter_new_block(GenerationContext* context, FileRange range) {
    if(context->instructions.length == 0) {
        // New block is not required
        return;
    }

    auto new_block = new Block;

    auto last_instruction = context->instructions[context->instructions.length - 1];

    if(does_current_block_need_finisher(context)) {
        append_jump(context, range, new_block);
    }

    context->current_block->instructions = context->instructions;
    context->blocks.append(context->current_block);

    context->current_block = new_block;
    context->instructions = {};
}

static void change_block(GenerationContext* context, FileRange range, Block* block) {
    assert(!does_current_block_need_finisher(context));

    context->current_block->instructions = context->instructions;
    context->blocks.append(context->current_block);

    context->current_block = block;
    context->instructions = {};
}

static bool is_runtime_statement(Statement* statement) {
    return !(
        statement->kind == StatementKind::FunctionDeclaration ||
        statement->kind == StatementKind::ConstantDefinition ||
        statement->kind == StatementKind::StructDefinition ||
        statement->kind == StatementKind::UnionDefinition ||
        statement->kind == StatementKind::EnumDefinition ||
        statement->kind == StatementKind::StaticIf
    );
}

static_profiled_function(DelayedResult<void>, generate_runtime_statements, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    Array<Statement*> statements
), (
    info,
    jobs,
    scope,
    context,
    statements
)) {
    auto unreachable = false;
    for(auto statement : statements) {
        if(is_runtime_statement(statement)) {
            if(unreachable) {
                error(scope, statement->range, "Unreachable code");

                return err();
            }

            assert(does_current_block_need_finisher(context));

            if(statement->kind == StatementKind::ExpressionStatement) {
                auto expression_statement = (ExpressionStatement*)statement;

                expect_delayed(value, generate_expression(info, jobs, scope, context, expression_statement->expression));
            } else if(statement->kind == StatementKind::VariableDeclaration) {
                auto variable_declaration = (VariableDeclaration*)statement;

                for(auto tag : variable_declaration->tags) {
                    if(tag.name.text == u8"extern"_S) {
                        error(scope, variable_declaration->range, "Local variables cannot be external");

                        return err();
                    } else if(tag.name.text == u8"no_mangle"_S) {
                        error(scope, variable_declaration->range, "Local variables cannot be no_mangle");

                        return err();
                    } else {
                        error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                        return err();
                    }
                }

                if(variable_declaration->initializer == nullptr) {
                    error(scope, variable_declaration->range, "Variable must be initialized");

                    return err();
                }

                AnyType type;
                AddressedValue addressed_value;
                if(variable_declaration->type != nullptr) {
                    expect_delayed(type_value, evaluate_type_expression(info, jobs, scope, context, variable_declaration->type));

                    if(!type_value.is_runtime_type()) {
                        error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.get_description()));

                        return err();
                    }

                    type = type_value;

                    expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                    auto ir_type = get_ir_type(info.architecture_sizes, type);

                    auto pointer_register = append_allocate_local(
                        context,
                        variable_declaration->range,
                        ir_type,
                        variable_declaration->name.text,
                        type
                    );

                    expect(register_value, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        variable_declaration->range,
                        initializer_value.type,
                        initializer_value.value,
                        type,
                        false
                    ));

                    append_store(
                        context,
                        variable_declaration->range,
                        register_value.register_index,
                        pointer_register
                    );

                    addressed_value = AddressedValue(ir_type, pointer_register);
                } else {
                    expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                    expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type));
                    
                    if(!actual_type.is_runtime_type()) {
                        error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description()));

                        return err();
                    }

                    type = actual_type;

                    auto ir_type = get_ir_type(info.architecture_sizes, type);

                    auto pointer_register = append_allocate_local(
                        context,
                        variable_declaration->range,
                        ir_type,
                        variable_declaration->name.text,
                        type
                    );

                    expect(register_value, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        variable_declaration->range,
                        initializer_value.type,
                        initializer_value.value,
                        type,
                        false
                    ));

                    append_store(
                        context,
                        variable_declaration->range,
                        register_value.register_index,
                        pointer_register
                    );

                    addressed_value = AddressedValue(ir_type, pointer_register);
                }

                if(
                    !add_new_variable(
                        context,
                        variable_declaration->name,
                        type,
                        addressed_value
                    ).status
                ) {
                    return err();
                }
            } else if(statement->kind == StatementKind::MultiReturnVariableDeclaration) {
                auto variable_declaration = (MultiReturnVariableDeclaration*)statement;

                assert(variable_declaration->names.length > 1);

                expect_delayed(initializer, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                if(initializer.type.kind != TypeKind::MultiReturn) {
                    error(scope, variable_declaration->initializer->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(initializer.type.get_description()));

                    return err();
                }

                auto return_types = initializer.type.multi_return.types;

                if(return_types.length != variable_declaration->names.length) {
                    error(
                        scope,
                        variable_declaration->initializer->range,
                        "Incorrect number of return values. Expected %zu, got %zu",
                        variable_declaration->names.length,
                        return_types.length
                    );

                    return err();
                }

                auto register_value = initializer.value.unwrap_register_value();

                auto return_struct_member_ir_types = allocate<IRType>(return_types.length);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    return_struct_member_ir_types[i] = get_ir_type(info.architecture_sizes, return_types[i]);
                }

                for(size_t i = 0; i < return_types.length; i += 1) {
                    auto return_struct_register = append_read_struct_member(
                        context,
                        variable_declaration->names[i].range,
                        i,
                        register_value.register_index
                    );

                    auto pointer_register = append_allocate_local(
                        context,
                        variable_declaration->names[i].range,
                        return_struct_member_ir_types[i],
                        variable_declaration->names[i].text,
                        return_types[i]
                    );

                    append_store(
                        context,
                        variable_declaration->names[i].range,
                        return_struct_register,
                        pointer_register
                    );

                    if(
                        !add_new_variable(
                            context,
                            variable_declaration->names[i],
                            return_types[i],
                            AddressedValue(return_struct_member_ir_types[i], pointer_register)
                        ).status
                    ) {
                        return err();
                    }
                }
            } else if(statement->kind == StatementKind::Assignment) {
                auto assignment = (Assignment*)statement;

                expect_delayed(target, generate_expression(info, jobs, scope, context, assignment->target));

                size_t pointer_register;
                if(target.value.kind == RuntimeValueKind::AddressedValue){
                    auto addressed_value = target.value.addressed;

                    pointer_register = addressed_value.pointer_register;
                } else {
                    error(scope, assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(value, generate_expression(info, jobs, scope, context, assignment->value));

                expect(register_value, coerce_to_type_register(
                    info,
                    scope,
                    context,
                    assignment->range,
                    value.type,
                    value.value,
                    target.type,
                    false
                ));

                append_store(
                    context,
                    assignment->range,
                    register_value.register_index,
                    pointer_register
                );
            } else if(statement->kind == StatementKind::MultiReturnAssignment) {
                auto assignment = (MultiReturnAssignment*)statement;

                assert(assignment->targets.length > 1);

                expect_delayed(value, generate_expression(info, jobs, scope, context, assignment->value));

                if(value.type.kind != TypeKind::MultiReturn) {
                    error(scope, assignment->value->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(value.type.get_description()));

                    return err();
                }

                auto return_types = value.type.multi_return.types;

                if(return_types.length != assignment->targets.length) {
                    error(
                        scope,
                        assignment->value->range,
                        "Incorrect number of return values. Expected %zu, got %zu",
                        assignment->targets.length,
                        return_types.length
                    );

                    return err();
                }

                auto register_value = value.value.unwrap_register_value();

                auto return_struct_member_ir_types = allocate<IRType>(return_types.length);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    return_struct_member_ir_types[i] = get_ir_type(info.architecture_sizes, return_types[i]);
                }

                for(size_t i = 0; i < return_types.length; i += 1) {
                    expect_delayed(target, generate_expression(info, jobs, scope, context, assignment->targets[i]));

                    size_t pointer_register;
                    if(target.value.kind == RuntimeValueKind::AddressedValue){
                        auto addressed_value = target.value.addressed;

                        pointer_register = addressed_value.pointer_register;
                    } else {
                        error(scope, assignment->targets[i]->range, "Value is not assignable");

                        return err();
                    }

                    auto return_struct_register = append_read_struct_member(
                        context,
                        assignment->targets[i]->range,
                        i,
                        register_value.register_index
                    );

                    expect(register_value, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        assignment->range,
                        return_types[i],
                        AnyRuntimeValue(RegisterValue(return_struct_member_ir_types[i], return_struct_register)),
                        target.type,
                        false
                    ));

                    append_store(
                        context,
                        assignment->range,
                        return_struct_register,
                        pointer_register
                    );
                }
            } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
                auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

                expect_delayed(target, generate_expression(info, jobs, scope, context, binary_operation_assignment->target));

                size_t pointer_register;
                if(target.value.kind == RuntimeValueKind::AddressedValue){
                    auto addressed_value = target.value.addressed;

                    pointer_register = addressed_value.pointer_register;
                } else {
                    error(scope, binary_operation_assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(value, generate_binary_operation(
                    info,
                    jobs,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    binary_operation_assignment->target,
                    binary_operation_assignment->value,
                    binary_operation_assignment->binary_operator
                ));

                expect(register_value, coerce_to_type_register(
                    info,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    value.type,
                    value.value,
                    target.type,
                    false
                ));

                append_store(
                    context,
                    binary_operation_assignment->range,
                    register_value.register_index,
                    pointer_register
                );
            } else if(statement->kind == StatementKind::IfStatement) {
                auto if_statement = (IfStatement*)statement;

                auto end_block = new Block;

                Block* next_block;
                if(if_statement->else_ifs.length == 0 && if_statement->else_statements.length == 0) {
                    next_block = end_block;
                } else {
                    next_block = new Block;
                }

                auto body_block = new Block;

                expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

                    return err();
                }

                auto condition_register = generate_in_register_value(
                    context,
                    if_statement->condition->range,
                    IRType::create_boolean(),
                    condition.value
                );

                append_branch(
                    context,
                    if_statement->condition->range,
                    condition_register,
                    body_block,
                    next_block
                );

                change_block(context, if_statement->range, body_block);

                auto if_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                DebugScope debug_scope {};
                debug_scope.has_parent = true;
                debug_scope.parent_scope_index = parent_variable_scope.debug_scope_index;
                debug_scope.range = if_statement->range;

                auto debug_scope_index = context->debug_scopes.append(debug_scope);

                VariableScope if_variable_scope {};
                if_variable_scope.constant_scope = if_scope;
                if_variable_scope.debug_scope_index = debug_scope_index;

                context->variable_scope_stack.append(if_variable_scope);

                expect_delayed_void(generate_runtime_statements(info, jobs, if_scope, context, if_statement->statements));

                context->variable_scope_stack.length -= 1;

                if(does_current_block_need_finisher(context)) {
                    append_jump(context, if_statement->range, end_block);
                }

                for(size_t i = 0; i < if_statement->else_ifs.length; i += 1) {
                    change_block(context, if_statement->range, next_block);

                    if(i == if_statement->else_ifs.length - 1 && if_statement->else_statements.length == 0) {
                        next_block = end_block;
                    } else {
                        next_block = new Block;
                    }

                    auto body_block = new Block;

                    expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->else_ifs[i].condition));

                    if(condition.type.kind != TypeKind::Boolean) {
                        error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

                        return err();
                    }

                    auto condition_register = generate_in_register_value(
                        context,
                        if_statement->else_ifs[i].condition->range,
                        IRType::create_boolean(),
                        condition.value
                    );

                    append_branch(
                        context,
                        if_statement->else_ifs[i].condition->range,
                        condition_register,
                        body_block,
                        next_block
                    );

                    change_block(context, if_statement->range, body_block);

                    auto else_if_scope = context->child_scopes[context->next_child_scope_index];
                    context->next_child_scope_index += 1;
                    assert(context->next_child_scope_index <= context->child_scopes.length);

                    DebugScope debug_scope {};
                    debug_scope.has_parent = true;
                    debug_scope.parent_scope_index = parent_variable_scope.debug_scope_index;
                    debug_scope.range = if_statement->range;

                    auto debug_scope_index = context->debug_scopes.append(debug_scope);

                    VariableScope else_if_variable_scope {};
                    else_if_variable_scope.constant_scope = else_if_scope;
                    else_if_variable_scope.debug_scope_index = debug_scope_index;

                    context->variable_scope_stack.append(else_if_variable_scope);

                    expect_delayed_void(generate_runtime_statements(info, jobs, if_scope, context, if_statement->else_ifs[i].statements));

                    context->variable_scope_stack.length -= 1;

                    if(does_current_block_need_finisher(context)) {
                        append_jump(context, if_statement->range, end_block);
                    }
                }

                if(if_statement->else_statements.length != 0) {
                    change_block(context, if_statement->range, next_block);

                    auto else_scope = context->child_scopes[context->next_child_scope_index];
                    context->next_child_scope_index += 1;
                    assert(context->next_child_scope_index <= context->child_scopes.length);

                    DebugScope debug_scope {};
                    debug_scope.has_parent = true;
                    debug_scope.parent_scope_index = parent_variable_scope.debug_scope_index;
                    debug_scope.range = if_statement->range;

                    auto debug_scope_index = context->debug_scopes.append(debug_scope);

                    VariableScope else_variable_scope {};
                    else_variable_scope.constant_scope = else_scope;
                    else_variable_scope.debug_scope_index = debug_scope_index;

                    context->variable_scope_stack.append(else_variable_scope);

                    expect_delayed_void(generate_runtime_statements(info, jobs, else_scope, context, if_statement->else_statements));

                    context->variable_scope_stack.length -= 1;

                    if(does_current_block_need_finisher(context)) {
                        append_jump(context, if_statement->range, end_block);
                    }
                }

                change_block(context, if_statement->range, end_block);
            } else if(statement->kind == StatementKind::WhileLoop) {
                auto while_loop = (WhileLoop*)statement;

                auto end_block = new Block;

                auto body_block = new Block;

                enter_new_block(context, while_loop->condition->range);

                auto condition_block = context->current_block;

                expect_delayed(condition, generate_expression(info, jobs, scope, context, while_loop->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

                    return err();
                }

                auto condition_register = generate_in_register_value(
                    context,
                    while_loop->condition->range,
                    IRType::create_boolean(),
                    condition.value
                );

                append_branch(
                    context,
                    while_loop->condition->range,
                    condition_register,
                    body_block,
                    end_block
                );

                change_block(context, while_loop->range, body_block);

                auto while_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                DebugScope debug_scope {};
                debug_scope.has_parent = true;
                debug_scope.parent_scope_index = parent_variable_scope.debug_scope_index;
                debug_scope.range = while_loop->range;

                auto debug_scope_index = context->debug_scopes.append(debug_scope);

                VariableScope while_variable_scope {};
                while_variable_scope.constant_scope = while_scope;
                while_variable_scope.debug_scope_index = debug_scope_index;

                context->variable_scope_stack.append(while_variable_scope);

                auto old_in_breakable_scope = context->in_breakable_scope;
                auto old_break_end_block = context->break_end_block;

                context->in_breakable_scope = true;
                context->break_end_block = end_block;

                expect_delayed_void(generate_runtime_statements(info, jobs, while_scope, context, while_loop->statements));

                context->in_breakable_scope = old_in_breakable_scope;
                context->break_end_block = old_break_end_block;

                context->variable_scope_stack.length -= 1;

                if(does_current_block_need_finisher(context)) {
                    append_jump(context, while_loop->range, condition_block);
                }

                change_block(context, while_loop->range, end_block);
            } else if(statement->kind == StatementKind::ForLoop) {
                auto for_loop = (ForLoop*)statement;

                Identifier index_name {};
                if(for_loop->has_index_name) {
                    index_name = for_loop->index_name;
                } else {
                    index_name.text = u8"it"_S;
                    index_name.range = for_loop->range;
                }

                expect_delayed(from_value, generate_expression(info, jobs, scope, context, for_loop->from));

                expect_delayed(to_value, generate_expression(info, jobs, scope, context, for_loop->to));

                Integer determined_index_type;
                if(from_value.type.kind == TypeKind::UndeterminedInteger && to_value.type.kind == TypeKind::UndeterminedInteger) {
                    determined_index_type = Integer(
                        info.architecture_sizes.default_integer_size,
                        true
                    );
                } else if(from_value.type.kind == TypeKind::Integer) {
                    determined_index_type = from_value.type.integer;
                } else if(to_value.type.kind == TypeKind::Integer) {
                    determined_index_type = to_value.type.integer;
                } else {
                    error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(from_value.type.get_description()));

                    return err();
                }

                expect(from_register_value, coerce_to_integer_register_value(
                    scope,
                    context,
                    for_loop->from->range,
                    from_value.type,
                    from_value.value,
                    determined_index_type,
                    false
                ));

                expect(to_register_value, coerce_to_integer_register_value(
                    scope,
                    context,
                    for_loop->from->range,
                    to_value.type,
                    to_value.value,
                    determined_index_type,
                    false
                ));

                auto determined_index_ir_type = IRType::create_integer(determined_index_type.size);

                auto index_pointer_register = append_allocate_local(
                    context,
                    for_loop->range,
                    determined_index_ir_type,
                    index_name.text,
                    AnyType(determined_index_type)
                );

                append_store(
                    context,
                    for_loop->range,
                    from_register_value.register_index,
                    index_pointer_register
                );

                auto end_block = new Block;

                auto body_block = new Block;

                enter_new_block(context, for_loop->range);

                auto condition_block = context->current_block;

                auto current_index_register = append_load(
                    context,
                    for_loop->range,
                    index_pointer_register,
                    determined_index_ir_type
                );

                IntegerComparisonOperation::Operation operation;
                if(determined_index_type.is_signed) {
                    operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
                } else {
                    operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
                }

                auto condition_register = append_integer_comparison_operation(
                    context,
                    for_loop->range,
                    operation,
                    current_index_register,
                    to_register_value.register_index
                );

                append_branch(
                    context,
                    for_loop->range,
                    condition_register,
                    end_block,
                    body_block
                );

                change_block(context, for_loop->range, body_block);

                auto for_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                DebugScope debug_scope {};
                debug_scope.has_parent = true;
                debug_scope.parent_scope_index = parent_variable_scope.debug_scope_index;
                debug_scope.range = for_loop->range;

                auto debug_scope_index = context->debug_scopes.append(debug_scope);

                VariableScope for_variable_scope {};
                for_variable_scope.constant_scope = for_scope;
                for_variable_scope.debug_scope_index = debug_scope_index;

                context->variable_scope_stack.append(for_variable_scope);

                auto old_in_breakable_scope = context->in_breakable_scope;
                auto old_break_end_block = context->break_end_block;

                context->in_breakable_scope = true;
                context->break_end_block = end_block;

                expect_void(add_new_variable(
                    context,
                    index_name,
                    AnyType(determined_index_type),
                    AddressedValue(determined_index_ir_type, index_pointer_register)
                ));

                expect_delayed_void(generate_runtime_statements(info, jobs, for_scope, context, for_loop->statements));

                context->in_breakable_scope = old_in_breakable_scope;
                context->break_end_block = old_break_end_block;

                context->variable_scope_stack.length -= 1;

                auto one_register = append_literal(
                    context,
                    for_loop->range,
                    determined_index_ir_type,
                    IRConstantValue::create_integer(1)
                );

                auto next_index_register = append_integer_arithmetic_operation(
                    context,
                    for_loop->range,
                    IntegerArithmeticOperation::Operation::Add,
                    current_index_register,
                    one_register
                );

                append_store(context, for_loop->range, next_index_register, index_pointer_register);

                if(does_current_block_need_finisher(context)) {
                    append_jump(context, for_loop->range, condition_block);
                }

                change_block(context, for_loop->range, end_block);
            } else if(statement->kind == StatementKind::ReturnStatement) {
                auto return_statement = (ReturnStatement*)statement;

                unreachable = true;

                assert(context->variable_scope_stack.length != 0);
                auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                auto return_instruction = new ReturnInstruction;
                return_instruction->range = return_statement->range;
                return_instruction->debug_scope_index = current_variable_scope.debug_scope_index;

                if(return_statement->values.length != context->return_types.length) {
                    error(
                        scope,
                        return_statement->range,
                        "Incorrect number of returns, expected %zu, got %zu",
                        context->return_types.length,
                        return_statement->values.length
                    );

                    return err();
                }

                auto return_type_count = context->return_types.length;

                if(return_type_count == 1) {
                    expect_delayed(value, generate_expression(info, jobs, scope, context, return_statement->values[0]));

                    expect(register_value, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        return_statement->values[0]->range,
                        value.type,
                        value.value,
                        context->return_types[0],
                        false
                    ));

                    return_instruction->value_register = register_value.register_index;
                } else if(return_type_count > 1) {
                    auto return_struct_members = allocate<size_t>(return_type_count);

                    for(size_t i = 0; i < return_type_count; i += 1) {
                        expect_delayed(value, generate_expression(info, jobs, scope, context, return_statement->values[i]));

                        expect(register_value, coerce_to_type_register(
                            info,
                            scope,
                            context,
                            return_statement->values[i]->range,
                            value.type,
                            value.value,
                            context->return_types[i],
                            false
                        ));

                        return_struct_members[i] = register_value.register_index;
                    }

                    return_instruction->value_register = append_assemble_struct(
                        context,
                        return_statement->range,
                        Array(return_type_count, return_struct_members)
                    );
                }

                context->instructions.append(return_instruction);
            } else if(statement->kind == StatementKind::BreakStatement) {
                auto break_statement = (BreakStatement*)statement;

                unreachable = true;

                if(!context->in_breakable_scope) {
                    error(scope, break_statement->range, "Not in a break-able scope");

                    return err();
                }

                append_jump(context, break_statement->range, context->break_end_block);
            } else if(statement->kind == StatementKind::InlineAssembly) {
                auto inline_assembly = (InlineAssembly*)statement;

                auto bindings = allocate<AssemblyInstruction::Binding>(inline_assembly->bindings.length);

                for(size_t i = 0; i < inline_assembly->bindings.length; i += 1) {
                    auto binding = inline_assembly->bindings[i];

                    if(binding.constraint.length < 1) {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    }

                    expect(value, generate_expression(
                        info,
                        jobs,
                        scope,
                        context,
                        binding.value
                    ));

                    if(binding.constraint[0] == '=') {
                        if(binding.constraint.length < 2) {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(binding.constraint[1] == '*') {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(value.value.kind != RuntimeValueKind::AddressedValue) {
                            error(scope, binding.value->range, "Output binding value must be assignable");

                            return err();
                        }

                        auto pointer_register = value.value.addressed.pointer_register;

                        AssemblyInstruction::Binding instruction_binding {};
                        instruction_binding.constraint = binding.constraint;
                        instruction_binding.pointed_to_type = value.value.addressed.pointed_to_type;
                        instruction_binding.register_index = pointer_register;

                        bindings[i] = instruction_binding;
                    } else if(binding.constraint[0] == '*') {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    } else {
                        expect(determined_value_type, coerce_to_default_type(info, scope, binding.value->range, value.type));

                        if(!determined_value_type.is_runtime_type()) {
                            error(scope, binding.value->range, "Value of type '%.*s' cannot be used as a binding", STRING_PRINTF_ARGUMENTS(determined_value_type.get_description()));

                            return err();
                        }

                        expect(value_register, coerce_to_type_register(
                            info,
                            scope,
                            context,
                            binding.value->range,
                            value.type,
                            value.value,
                            determined_value_type,
                            false
                        ));

                        AssemblyInstruction::Binding instruction_binding {};
                        instruction_binding.constraint = binding.constraint;
                        instruction_binding.register_index = value_register.register_index;

                        bindings[i] = instruction_binding;
                    }
                }

                assert(context->variable_scope_stack.length != 0);
                auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                auto assembly_instruction = new AssemblyInstruction;
                assembly_instruction->range = inline_assembly->range;
                assembly_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
                assembly_instruction->assembly = inline_assembly->assembly;
                assembly_instruction->bindings = Array(inline_assembly->bindings.length, bindings);

                context->instructions.append(assembly_instruction);
            } else {
                abort();
            }
        }
    }

    return ok();
}

profiled_function(DelayedResult<Array<StaticConstant*>>, do_generate_function, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionTypeType type,
    FunctionConstant value,
    Function* function
), (
    info,
    jobs,
    type,
    value,
    function
)) {
    auto declaration = value.declaration;

    auto declaration_parameter_count = declaration->parameters.length;

    auto file_path = get_scope_file_path(*value.body_scope);

    auto runtime_parameter_count = type.parameters.length;

    auto ir_parameters = allocate<IRType>(runtime_parameter_count);

    size_t runtime_parameter_index = 0;
    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
        if(!declaration->parameters[i].is_constant) {
            auto argument_type = type.parameters[runtime_parameter_index];

            ir_parameters[runtime_parameter_index] = get_ir_type(info.architecture_sizes, argument_type);

            runtime_parameter_index += 1;
        }
    }

    assert(runtime_parameter_index == runtime_parameter_count);

    bool has_ir_return;
    IRType return_ir_type;
    if(type.return_types.length == 0) {
        has_ir_return = false;
    } else if(type.return_types.length == 1) {
        has_ir_return = true;
        return_ir_type = get_ir_type(info.architecture_sizes, type.return_types[0]);
    } else {
        auto return_struct_members = allocate<IRType>(type.return_types.length);

        for(size_t i = 0; i < type.return_types.length; i += 1) {
            return_struct_members[i] = get_ir_type(info.architecture_sizes, type.return_types[i]);
        }

        has_ir_return = true;
        return_ir_type = IRType::create_struct(Array(type.return_types.length, return_struct_members));
    }

    function->name = declaration->name.text;
    function->range = declaration->range;
    function->path = get_scope_file_path(*value.body_scope);
    function->parameters = Array(runtime_parameter_count, ir_parameters);
    function->has_return = has_ir_return;
    function->return_type = return_ir_type;
    function->calling_convention = type.calling_convention;
    function->debug_type = AnyType(type);

    if(value.is_external) {
        function->is_external = true;
        function->is_no_mangle = true;
        function->libraries = value.external_libraries;

        return ok(Array<StaticConstant*>::empty());
    } else {
        function->is_external = false;
        function->is_no_mangle = value.is_no_mangle;

        GenerationContext context {};

        context.return_types = type.return_types;

        context.next_register = runtime_parameter_count;

        DebugScope debug_scope {};
        debug_scope.range = declaration->range;

        auto debug_scope_index = context.debug_scopes.append(debug_scope);

        VariableScope body_variable_scope {};
        body_variable_scope.constant_scope = value.body_scope;
        body_variable_scope.debug_scope_index = debug_scope_index;

        context.variable_scope_stack.append(body_variable_scope);

        context.child_scopes = value.child_scopes;

        context.current_block = new Block;

        List<Instruction*> first_block_instructions {};

        size_t runtime_parameter_index = 0;
        for(size_t i = 0; i < declaration->parameters.length; i += 1) {
            if(!declaration->parameters[i].is_constant) {
                auto parameter_type = type.parameters[i];

                auto pointer_register = append_allocate_local(
                    &context,
                    declaration->parameters[i].name.range,
                    ir_parameters[runtime_parameter_index],
                    declaration->parameters[i].name.text,
                    parameter_type
                );

                append_store(
                    &context,
                    declaration->parameters[i].name.range,
                    runtime_parameter_index,
                    pointer_register
                );

                add_new_variable(
                    &context,
                    declaration->parameters[i].name,
                    parameter_type,
                    AddressedValue(ir_parameters[runtime_parameter_index], pointer_register)
                );

                runtime_parameter_index += 1;
            }
        }

        assert(runtime_parameter_index == runtime_parameter_count);

        expect_delayed_void(generate_runtime_statements(
            info,
            jobs,
            value.body_scope,
            &context,
            declaration->statements
        ));

        assert(context.next_child_scope_index == value.child_scopes.length);

        bool has_return_at_end;
        if(declaration->statements.length > 0) {
            auto last_statement = declaration->statements[declaration->statements.length - 1];

            has_return_at_end = last_statement->kind == StatementKind::ReturnStatement;
        } else {
            has_return_at_end = false;
        }

        if(!has_return_at_end) {
            if(type.return_types.length > 0) {
                error(value.body_scope, declaration->range, "Function '%.*s' must end with a return", STRING_PRINTF_ARGUMENTS(declaration->name.text));

                return err();
            } else {
                auto return_instruction = new ReturnInstruction;
                return_instruction->range = declaration->range;
                return_instruction->debug_scope_index = debug_scope_index;

                context.instructions.append(return_instruction);
            }
        }

        function->debug_scopes = context.debug_scopes;

        context.current_block->instructions = context.instructions;
        context.blocks.append(context.current_block);

        function->blocks = context.blocks;

        return ok((Array<StaticConstant*>)context.static_constants);
    }
}

profiled_function(DelayedResult<StaticVariableResult>, do_generate_static_variable, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    VariableDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    declaration,
    scope
)) {
    auto is_external = false;
    Array<String> external_libraries;
    auto is_no_mangle = false;
    for(auto tag : declaration->tags) {
        if(tag.name.text == u8"extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            List<String> libraries {};

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[i]));

                if(parameter.type.kind == TypeKind::ArrayTypeType) {
                    auto array = parameter.type.array;

                    if(
                        array.element_type->kind == TypeKind::ArrayTypeType ||
                        array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::ArrayConstant) {
                            error(scope, tag.parameters[i]->range, "Cannot use an array with non-constant elements in a constant context");

                            return err();
                        } else {
                            auto static_array_value = parameter.value.unwrap_static_array();

                            for(auto element : static_array_value.elements) {
                                expect(library_path, array_to_string(scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        }
                    } else {
                        expect(library_path, array_to_string(scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else if(parameter.type.kind == TypeKind::StaticArray) {
                    auto static_array = parameter.type.static_array;

                    if(
                        static_array.element_type->kind == TypeKind::ArrayTypeType ||
                        static_array.element_type->kind == TypeKind::StaticArray
                    ) {
                        auto static_array_value = parameter.value.unwrap_static_array();

                        assert(static_array.length == static_array_value.elements.length);

                        for(auto element : static_array_value.elements) {
                            expect(library_path, array_to_string(scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.type.get_description()));

                    return err();
                }
            }

            is_external = true;
            external_libraries = libraries;
        } else if(tag.name.text == u8"no_mangle"_S) {
            if(is_no_mangle) {
                error(scope, tag.range, "Duplicate 'no_mangle' tag");

                return err();
            }

            is_no_mangle = true;
        } else {
            error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

            return err();
        }
    }

    if(is_external && is_no_mangle) {
        error(scope, declaration->range, "External variables cannot be no_mangle");

        return err();
    }

    if(is_external) {
        if(declaration->initializer != nullptr) {
            error(scope, declaration->range, "External variables cannot have initializers");

            return err();
        }

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, (Statement*)nullptr, declaration->type));

        if(!type.is_runtime_type()) {
            error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        auto static_variable = new StaticVariable;
        static_variable->name = declaration->name.text;
        static_variable->is_no_mangle = true;
        static_variable->path = get_scope_file_path(*scope);
        static_variable->range = declaration->range;
        static_variable->type = get_ir_type(info.architecture_sizes, type);
        static_variable->is_external = true;
        static_variable->libraries = external_libraries;
        static_variable->debug_type = type;

        StaticVariableResult result {};
        result.static_variable = static_variable;
        result.type = type;

        return ok(result);
    } else {
        if(declaration->initializer == nullptr) {
            error(scope, declaration->range, "Variable must be initialized");

            return err();
        }

        if(declaration->type != nullptr) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, (Statement*)nullptr, declaration->type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            expect_delayed(initial_value, evaluate_constant_expression(info, jobs, scope, nullptr, declaration->initializer));

            expect(coerced_initial_value, coerce_constant_to_type(
                info,
                scope,
                declaration->initializer->range,
                initial_value.type,
                initial_value.value,
                type,
                false
            ));

            auto ir_initial_value = get_runtime_ir_constant_value(coerced_initial_value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->type = get_ir_type(info.architecture_sizes, type);
            static_variable->is_external = false;
            static_variable->has_initial_value = true;
            static_variable->initial_value = ir_initial_value;
            static_variable->debug_type = type;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        } else {
            expect_delayed(initial_value, evaluate_constant_expression(info, jobs, scope, nullptr, declaration->initializer));

            expect(type, coerce_to_default_type(info, scope, declaration->initializer->range, initial_value.type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            auto ir_initial_value = get_runtime_ir_constant_value(initial_value.value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->type = get_ir_type(info.architecture_sizes, type);
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->is_external = false;
            static_variable->has_initial_value = true;
            static_variable->initial_value = ir_initial_value;
            static_variable->debug_type = type;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        }
    }
}