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

struct RuntimeUndeterminedAggregateValue {
    inline RuntimeUndeterminedAggregateValue() = default;
    explicit inline RuntimeUndeterminedAggregateValue(Array<RegisterValue> values) : values(values) {}

    Array<RegisterValue> values;
};

enum struct RuntimeValueKind {
    RegisterValue,
    AddressedValue,
    RuntimeUndeterminedAggregateValue
};

struct AnyRuntimeValue {
    RuntimeValueKind kind;

    union {
        RegisterValue register_;
        AddressedValue addressed;
        RuntimeUndeterminedAggregateValue undetermined_aggregate;
    };

    inline AnyRuntimeValue() = default;
    explicit inline AnyRuntimeValue(RegisterValue register_) : kind(RuntimeValueKind::RegisterValue), register_(register_) {}
    explicit inline AnyRuntimeValue(AddressedValue addressed) : kind(RuntimeValueKind::AddressedValue), addressed(addressed) {}
    explicit inline AnyRuntimeValue(RuntimeUndeterminedAggregateValue undetermined_aggregate) : kind(RuntimeValueKind::RuntimeUndeterminedAggregateValue), undetermined_aggregate(undetermined_aggregate) {}

    inline RegisterValue unwrap_register_value() {
        assert(kind == RuntimeValueKind::RegisterValue);

        return register_;
    }

    inline AddressedValue unwrap_addressed_value() {
        assert(kind == RuntimeValueKind::AddressedValue);

        return addressed;
    }

    inline RuntimeUndeterminedAggregateValue unwrap_undetermined_aggregate_value() {
        assert(kind == RuntimeValueKind::RuntimeUndeterminedAggregateValue);

        return undetermined_aggregate;
    }
};

struct RuntimeVariable {
    Identifier name;

    AnyType type;

    AddressedValue value;
};

struct RuntimeVariableScope {
    Array<RuntimeVariable> variables;

    size_t debug_scope_index;
};

struct GenerationContext {
    Arena* arena;

    Array<AnyType> return_types;

    Array<ConstantScope*> child_scopes;
    size_t next_child_scope_index;

    Block* break_end_block;

    List<RuntimeVariableScope> variable_scope_stack;
    VariableScope* current_variable_scope;

    List<DebugScope> debug_scopes;

    List<Block*> blocks;
    Block* current_block;
    List<Instruction*> instructions;

    size_t next_register;

    List<StaticConstant*> static_constants;

    Array<TypedFunction> functions;
    Array<TypedStaticVariable> static_variables;
};

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

    auto integer_arithmetic_operation = context->arena->allocate_and_construct<IntegerArithmeticOperation>();
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

    auto integer_comparison_operation = context->arena->allocate_and_construct<IntegerComparisonOperation>();
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

    auto integer_extension = context->arena->allocate_and_construct<IntegerExtension>();
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

    auto integer_truncation = context->arena->allocate_and_construct<IntegerTruncation>();
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

    auto float_arithmetic_operation = context->arena->allocate_and_construct<FloatArithmeticOperation>();
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

    auto float_comparison_operation = context->arena->allocate_and_construct<FloatComparisonOperation>();
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

    auto float_conversion = context->arena->allocate_and_construct<FloatConversion>();
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

    auto float_from_integer = context->arena->allocate_and_construct<FloatFromInteger>();
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

    auto integer_from_float = context->arena->allocate_and_construct<IntegerFromFloat>();
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

    auto pointer_equality = context->arena->allocate_and_construct<PointerEquality>();
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

    auto pointer_from_integer = context->arena->allocate_and_construct<PointerFromInteger>();
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

    auto integer_from_pointer = context->arena->allocate_and_construct<IntegerFromPointer>();
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

    auto boolean_arithmetic_operation = context->arena->allocate_and_construct<BooleanArithmeticOperation>();
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

    auto boolean_equality = context->arena->allocate_and_construct<BooleanEquality>();
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

    auto boolean_inversion = context->arena->allocate_and_construct<BooleanInversion>();
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

    auto assembly_static_array = context->arena->allocate_and_construct<AssembleStaticArray>();
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

    auto read_static_array_element = context->arena->allocate_and_construct<ReadStaticArrayElement>();
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

    auto assemble_struct = context->arena->allocate_and_construct<AssembleStruct>();
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

    auto read_read_struct_member = context->arena->allocate_and_construct<ReadStructMember>();
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

    auto literal = context->arena->allocate_and_construct<Literal>();
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

    auto jump = context->arena->allocate_and_construct<Jump>();
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

    auto branch = context->arena->allocate_and_construct<Branch>();
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

    auto allocate_local = context->arena->allocate_and_construct<AllocateLocal>();
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

    auto allocate_local = context->arena->allocate_and_construct<AllocateLocal>();
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

    auto load = context->arena->allocate_and_construct<Load>();
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

    auto store = context->arena->allocate_and_construct<Store>();
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

    auto struct_member_pointer = context->arena->allocate_and_construct<StructMemberPointer>();
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

    auto pointer_index = context->arena->allocate_and_construct<PointerIndex>();
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

    auto reference_static = context->arena->allocate_and_construct<ReferenceStatic>();
    reference_static->range = range;
    reference_static->debug_scope_index = current_variable_scope.debug_scope_index;
    reference_static->runtime_static = runtime_static;
    reference_static->destination_register = destination_register;

    context->instructions.append(reference_static);

    return destination_register;
}

inline IRType get_array_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, ArrayTypeType array) {
    auto members = arena->allocate<IRType>(2);

    members[0].kind = IRTypeKind::Integer;
    members[0].integer.size = architecture_sizes.address_size;

    members[1].kind = IRTypeKind::Pointer;

    return IRType::create_struct(Array(2, members));
}

static IRType get_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, AnyType type);

inline IRType get_static_array_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, StaticArray static_array) {
    return IRType::create_static_array(
        static_array.length,
        arena->heapify(get_ir_type(arena, architecture_sizes, *static_array.element_type))
    );
}

inline IRType get_struct_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, StructType struct_) {
    auto members = arena->allocate<IRType>(struct_.members.length);

    for(size_t i = 0; i < struct_.members.length; i += 1) {
        members[i] = get_ir_type(arena, architecture_sizes, struct_.members[i].type);
    }

    return IRType::create_struct(Array(struct_.members.length, members));
}

inline IRType get_union_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, UnionType union_) {
    return IRType::create_static_array(union_.get_size(architecture_sizes), arena->heapify(IRType::create_integer(RegisterSize::Size8)));
}

static IRType get_ir_type(Arena* arena, ArchitectureSizes architecture_sizes, AnyType type) {
    if(type.kind == TypeKind::Integer) {
        return IRType::create_integer(type.integer.size);
    } else if(type.kind == TypeKind::Boolean) {
        return IRType::create_boolean();
    } else if(type.kind == TypeKind::FloatType) {
        return IRType::create_float(type.float_.size);
    } else if(type.kind == TypeKind::Pointer) {
        return IRType::create_pointer();
    } else if(type.kind == TypeKind::ArrayTypeType) {
        return get_array_ir_type(arena, architecture_sizes, type.array);
    } else if(type.kind == TypeKind::StaticArray) {
        return get_static_array_ir_type(arena, architecture_sizes, type.static_array);
    } else if(type.kind == TypeKind::StructType) {
        return get_struct_ir_type(arena, architecture_sizes, type.struct_);
    } else if(type.kind == TypeKind::UnionType) {
        return get_union_ir_type(arena, architecture_sizes, type.union_);
    } else if(type.kind == TypeKind::Enum) {
        return IRType::create_integer(type.enum_.backing_type->size);
    } else {
        abort();
    }
}

static IRConstantValue get_runtime_ir_constant_value(Arena* arena, AnyConstantValue value);

inline IRConstantValue get_array_ir_constant_value(Arena* arena, ArrayConstant array) {
    auto members = arena->allocate<IRConstantValue>(2);

    members[0] = get_runtime_ir_constant_value(arena, *array.length);

    members[1] = get_runtime_ir_constant_value(arena, *array.pointer);

    return IRConstantValue::create_aggregate(Array(2, members));
}

inline IRConstantValue get_aggregate_ir_constant_value(Arena* arena, AggregateConstant aggregate) {
    auto values = arena->allocate<IRConstantValue>(aggregate.values.length);

    for(size_t i = 0; i < aggregate.values.length; i += 1) {
        values[i] = get_runtime_ir_constant_value(arena, aggregate.values[i]);
    }

    return IRConstantValue::create_aggregate(Array(aggregate.values.length, values));
}

static IRConstantValue get_runtime_ir_constant_value(Arena* arena, AnyConstantValue value) {
    if(value.kind == ConstantValueKind::IntegerConstant) {
        return IRConstantValue::create_integer(value.integer);
    } else if(value.kind == ConstantValueKind::FloatConstant) {
        return IRConstantValue::create_float(value.float_);
    } else if(value.kind == ConstantValueKind::BooleanConstant) {
        return IRConstantValue::create_boolean(value.boolean);
    } else if(value.kind == ConstantValueKind::ArrayConstant) {
        return get_array_ir_constant_value(arena, value.array);
    } else if(value.kind == ConstantValueKind::AggregateConstant) {
        return get_aggregate_ir_constant_value(arena, value.aggregate);
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
    auto ir_type = get_ir_type(context->arena, info.architecture_sizes, type);
    auto ir_value = get_runtime_ir_constant_value(context->arena, value);

    auto constant = context->arena->allocate_and_construct<StaticConstant>();
    constant->name = u8"static_constant"_S;
    constant->is_no_mangle = false;
    constant->path = scope->get_file_path();
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
    if(value.kind == RuntimeValueKind::RegisterValue) {
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

static RegisterValue convert_to_type_register(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type
) {
    IRType target_ir_type;
    size_t register_index;
    if(target_type.kind == TypeKind::Integer) {
        auto target_integer = target_type.integer;

        target_ir_type = IRType::create_integer(target_integer.size);

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto ir_type = IRType::create_integer(integer.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            if(target_integer.size > integer.size) {
                register_index = append_integer_extension(
                    context,
                    range,
                    integer.is_signed,
                    target_integer.size,
                    value_register
                );
            } else if(target_integer.size < integer.size) {
                register_index = append_integer_truncation(
                    context,
                    range,
                    target_integer.size,
                    value_register
                );
            } else {
                register_index = value_register;
            }
        } else if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;

            auto ir_type = IRType::create_float(float_type.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = append_integer_from_float(
                context,
                range,
                target_integer.is_signed,
                target_integer.size,
                value_register
            );
        } else if(type.kind == TypeKind::Pointer) {
            auto pointer = type.pointer;

            auto ir_type = IRType::create_pointer();

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = append_integer_from_pointer(
                context,
                range,
                target_integer.size,
                value_register
            );
        } else if(type.kind == TypeKind::Enum) {
            auto enum_ = type.enum_;

            auto ir_type = IRType::create_integer(enum_.backing_type->size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            if(target_integer.size > enum_.backing_type->size) {
                register_index = append_integer_extension(
                    context,
                    range,
                    enum_.backing_type->is_signed,
                    target_integer.size,
                    value_register
                );
            } else if(target_integer.size < enum_.backing_type->size) {
                register_index = append_integer_truncation(
                    context,
                    range,
                    target_integer.size,
                    value_register
                );
            } else {
                register_index = value_register;
            }
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::Boolean) {
        target_ir_type = IRType::create_boolean();

        if(type.kind == TypeKind::Boolean) {
            auto ir_type = IRType::create_boolean();

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::FloatType) {
        auto target_float_type = target_type.float_;

        target_ir_type = IRType::create_float(target_float_type.size);

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto ir_type = IRType::create_integer(integer.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = append_float_from_integer(
                context,
                range,
                integer.is_signed,
                target_float_type.size,
                value_register
            );
        } else if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;

            auto ir_type = IRType::create_float(float_type.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            if(target_float_type.size != float_type.size) {
                register_index = append_float_conversion(
                    context,
                    range,
                    target_float_type.size,
                    value_register
                );
            } else {
                register_index = value_register;
            }
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::Pointer) {
        auto target_pointer = target_type.pointer;

        target_ir_type = IRType::create_pointer();

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto ir_type = IRType::create_integer(integer.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = append_pointer_from_integer(
                context,
                range,
                value_register
            );
        } else if(type.kind == TypeKind::Pointer) {
            auto pointer = type.pointer;

            auto ir_type = IRType::create_pointer();

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        }
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        target_ir_type = get_array_ir_type(context->arena, info.architecture_sizes, target_array);

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;

            assert(*target_array.element_type == *array_type.element_type);

            auto ir_type = get_array_ir_type(context->arena, info.architecture_sizes, array_type);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            assert(*target_array.element_type == *static_array.element_type);
            assert(value.kind == RuntimeValueKind::AddressedValue);

            auto addressed_value = value.addressed;

            auto length_register = append_literal(
                context,
                range,
                IRType::create_integer(info.architecture_sizes.address_size),
                IRConstantValue::create_integer(static_array.length)
            );

            auto element_ir_type = get_ir_type(context->arena, info.architecture_sizes, *target_array.element_type);

            auto member_registers = context->arena->allocate<size_t>(2);

            member_registers[0] = length_register;
            member_registers[1] = addressed_value.pointer_register;

            register_index = append_assemble_struct(context, range, Array(2, member_registers));
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            assert(undetermined_struct.members.length == 2);

            assert(undetermined_struct.members[0].name == u8"length"_S);
            assert(undetermined_struct.members[0].type.kind == TypeKind::Integer);
            assert(undetermined_struct.members[0].type.integer.size == info.architecture_sizes.address_size);
            assert(undetermined_struct.members[0].type.integer.is_signed == false);

            assert(undetermined_struct.members[1].name == u8"pointer"_S);
            assert(undetermined_struct.members[1].type.kind == TypeKind::Pointer);
            assert(*undetermined_struct.members[1].type.pointer.pointed_to_type == *target_array.element_type);

            assert(value.kind == RuntimeValueKind::RuntimeUndeterminedAggregateValue);

            auto undetermined_aggregate_value = value.undetermined_aggregate;

            auto length_ir_type = IRType::create_integer(info.architecture_sizes.address_size);

            auto pointer_ir_type = IRType::create_pointer();

            auto member_registers = context->arena->allocate<size_t>(2);

            member_registers[0] = undetermined_aggregate_value.values[0].register_index;
            member_registers[1] = undetermined_aggregate_value.values[1].register_index;

            register_index = append_assemble_struct(context, range, Array(2, member_registers));
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        target_ir_type = get_static_array_ir_type(context->arena, info.architecture_sizes, target_static_array);

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            assert(*target_static_array.element_type == *static_array.element_type);
            assert(target_static_array.length == static_array.length);

            auto ir_type = get_static_array_ir_type(context->arena, info.architecture_sizes, static_array);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::StructType) {
        auto target_struct_type = target_type.struct_;

        target_ir_type = get_struct_ir_type(context->arena, info.architecture_sizes, target_struct_type);

        if(type.kind == TypeKind::StructType) {
            auto struct_type = type.struct_;

            assert(target_struct_type.definition == struct_type.definition);
            assert(target_struct_type.members.length == struct_type.members.length);

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

            assert(same_members);

            auto ir_type = get_struct_ir_type(context->arena, info.architecture_sizes, struct_type);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            assert(value.kind == RuntimeValueKind::RuntimeUndeterminedAggregateValue);

            auto undetermined_aggregate_value = value.undetermined_aggregate;

            assert(target_struct_type.members.length == undetermined_struct.members.length);

            auto same_members = true;
            for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                    same_members = false;

                    break;
                }
            }

            assert(same_members);

            auto member_registers = context->arena->allocate<size_t>(undetermined_struct.members.length);

            for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                member_registers[i] = undetermined_aggregate_value.values[i].register_index;
            }

            register_index = append_assemble_struct(
                context,
                range,
                Array(undetermined_struct.members.length, member_registers)
            );
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::UnionType) {
        auto target_union_type = target_type.union_;

        target_ir_type = get_union_ir_type(context->arena, info.architecture_sizes, target_union_type);

        if(type.kind == TypeKind::UnionType) {
            auto union_type = type.union_;

            assert(target_union_type.definition == union_type.definition);
            assert(target_union_type.members.length == union_type.members.length);

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

            assert(same_members);

            auto ir_type = get_union_ir_type(context->arena, info.architecture_sizes, union_type);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            assert(value.kind == RuntimeValueKind::RuntimeUndeterminedAggregateValue);
            auto undetermined_aggregate_value = value.undetermined_aggregate;

            assert(undetermined_struct.members.length == 1);

            auto found = false;
            for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                    assert(target_union_type.members[i].type == undetermined_struct.members[0].type);

                    auto pointer_register = append_allocate_local(
                        context,
                        range,
                        target_ir_type
                    );

                    auto member_ir_type = get_ir_type(context->arena, info.architecture_sizes, target_union_type.members[i].type);

                    auto value_register = generate_in_register_value(context, range, member_ir_type, value);

                    append_store(context, range, value_register, pointer_register);

                    register_index = append_load(context, range, pointer_register, target_ir_type);

                    found = true;
                    break;
                }
            }

            assert(found);
        } else {
            abort();
        }
    } else if(target_type.kind == TypeKind::Enum) {
        auto target_enum = target_type.enum_;

        target_ir_type = IRType::create_integer(target_enum.backing_type->size);

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto ir_type = IRType::create_integer(integer.size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            if(target_enum.backing_type->size > integer.size) {
                register_index = append_integer_extension(
                    context,
                    range,
                    integer.is_signed,
                    target_enum.backing_type->size,
                    value_register
                );
            } else if(target_enum.backing_type->size < integer.size) {
                register_index = append_integer_truncation(
                    context,
                    range,
                    target_enum.backing_type->size,
                    value_register
                );
            } else {
                register_index = value_register;
            }
        } if(type.kind == TypeKind::Enum) {
            auto enum_ = type.enum_;

            assert(target_enum.definition == enum_.definition);

            auto ir_type = IRType::create_integer(enum_.backing_type->size);

            auto value_register = generate_in_register_value(context, range, ir_type, value);

            register_index = value_register;
        } else {
            abort();
        }
    } else {
        abort();
    }

    return RegisterValue(target_ir_type, register_index);
}

static AnyRuntimeValue generate_expression(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    TypedExpression expression
);

static RegisterValue generate_binary_operation(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    BinaryOperationKind kind,
    TypedExpression left,
    TypedExpression right
) {
    auto left_value = generate_expression(info, jobs, scope, context, left);
    auto right_value = generate_expression(info, jobs, scope, context, right);

    assert(left.type == right.type);

    auto input_type = left.type;

    auto input_ir_type = get_ir_type(context->arena, info.architecture_sizes, input_type);

    auto left_register = generate_in_register_value(context, left.range, input_ir_type, left_value);
    auto right_register = generate_in_register_value(context, left.range, input_ir_type, right_value);

    IRType result_ir_type;
    size_t register_index;
    if(input_type.kind == TypeKind::Integer) {
        auto integer = input_type.integer;

        auto is_arithmetic = true;
        IntegerArithmeticOperation::Operation arithmetic_operation;
        switch(kind) {
            case BinaryOperationKind::Addition: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Add;
            } break;

            case BinaryOperationKind::Subtraction: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Subtract;
            } break;

            case BinaryOperationKind::Multiplication: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::Multiply;
            } break;

            case BinaryOperationKind::Division: {
                if(integer.is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::SignedDivide;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedDivide;
                }
            } break;

            case BinaryOperationKind::Modulus: {
                if(integer.is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::SignedModulus;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedModulus;
                }
            } break;

            case BinaryOperationKind::BitwiseAnd: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseAnd;
            } break;

            case BinaryOperationKind::BitwiseOr: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseOr;
            } break;

            case BinaryOperationKind::LeftShift: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::LeftShift;
            } break;

            case BinaryOperationKind::RightShift: {
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

        if(!is_arithmetic) {
            result_ir_type = IRType::create_integer(integer.size);

            register_index = append_integer_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register,
                right_register
            );
        } else {
            IntegerComparisonOperation::Operation comparison_operation;
            auto invert = false;
            switch(kind) {
                case BinaryOperationKind::Equal: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                } break;

                case BinaryOperationKind::NotEqual: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                    invert = true;
                } break;

                case BinaryOperationKind::LessThan: {
                    if(integer.is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedLessThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedLessThan;
                    }
                } break;

                case BinaryOperationKind::GreaterThan: {
                    if(integer.is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
                    }
                } break;

                default: abort();
            }

            result_ir_type = IRType::create_boolean();

            register_index = append_integer_comparison_operation(
                context,
                range,
                comparison_operation,
                left_register,
                right_register
            );

            if(invert) {
                register_index = append_boolean_inversion(context, range, register_index);
            }
        }
    } else if(input_type.kind == TypeKind::Boolean) {
        result_ir_type = IRType::create_boolean();

        auto is_arithmetic = true;
        BooleanArithmeticOperation::Operation arithmetic_operation;
        switch(kind) {
            case BinaryOperationKind::BooleanAnd: {
                arithmetic_operation = BooleanArithmeticOperation::Operation::BooleanAnd;
            } break;

            case BinaryOperationKind::BooleanOr: {
                arithmetic_operation = BooleanArithmeticOperation::Operation::BooleanOr;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            register_index = append_boolean_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register,
                right_register
            );
        } else {
            auto invert = false;
            switch(kind) {
                case BinaryOperationKind::Equal: {} break;

                case BinaryOperationKind::NotEqual: {
                    invert = true;
                } break;

                default: abort();
            }

            register_index = append_boolean_equality(
                context,
                range,
                left_register,
                right_register
            );

            if(invert) {
                register_index = append_boolean_inversion(context, range, register_index);
            }
        }
    } else if(input_type.kind == TypeKind::FloatType) {
        auto float_type = input_type.float_;

        auto is_arithmetic = true;
        FloatArithmeticOperation::Operation arithmetic_operation;
        switch(kind) {
            case BinaryOperationKind::Addition: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Add;
            } break;

            case BinaryOperationKind::Subtraction: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Subtract;
            } break;

            case BinaryOperationKind::Multiplication: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Multiply;
            } break;

            case BinaryOperationKind::Division: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Divide;
            } break;

            case BinaryOperationKind::Modulus: {
                arithmetic_operation = FloatArithmeticOperation::Operation::Modulus;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            result_ir_type = IRType::create_float(float_type.size);

            register_index = append_float_arithmetic_operation(
                context,
                range,
                arithmetic_operation,
                left_register,
                right_register
            );
        } else {
            FloatComparisonOperation::Operation comparison_operation;
            auto invert = false;
            switch(kind) {
                case BinaryOperationKind::Equal: {
                    comparison_operation = FloatComparisonOperation::Operation::Equal;
                } break;

                case BinaryOperationKind::NotEqual: {
                    comparison_operation = FloatComparisonOperation::Operation::Equal;
                    invert = true;
                } break;

                case BinaryOperationKind::LessThan: {
                    comparison_operation = FloatComparisonOperation::Operation::LessThan;
                } break;

                case BinaryOperationKind::GreaterThan: {
                    comparison_operation = FloatComparisonOperation::Operation::GreaterThan;
                } break;

                default: abort();
            }

            result_ir_type = IRType::create_boolean();

            register_index = append_float_comparison_operation(
                context,
                range,
                comparison_operation,
                left_register,
                right_register
            );

            if(invert) {
                register_index = append_boolean_inversion(context, range, register_index);
            }
        }
    } else if(input_type.kind == TypeKind::Pointer) {
        auto pointer = input_type.pointer;

        auto invert = false;
        switch(kind) {
            case BinaryOperationKind::Equal: {} break;

            case BinaryOperationKind::NotEqual: {
                invert = true;
            } break;

            default: abort();
        }

        result_ir_type = IRType::create_boolean();

        register_index = append_pointer_equality(
            context,
            range,
            left_register,
            right_register
        );

        if(invert) {
            register_index = append_boolean_inversion(context, range, register_index);
        }
    } else if(input_type.kind == TypeKind::Enum) {
        auto pointer = input_type.pointer;

        auto invert = false;
        IntegerComparisonOperation::Operation operation;
        switch(kind) {
            case BinaryOperationKind::Equal: {
                operation = IntegerComparisonOperation::Operation::Equal;
            } break;

            case BinaryOperationKind::NotEqual: {
                operation = IntegerComparisonOperation::Operation::Equal;
                invert = true;
            } break;

            default: abort();
        }

        result_ir_type = IRType::create_boolean();

        register_index = append_integer_comparison_operation(
            context,
            range,
            operation,
            left_register,
            right_register
        );

        if(invert) {
            register_index = append_boolean_inversion(context, range, register_index);
        }
    } else {
        abort();
    }

    return RegisterValue(result_ir_type, register_index);
}

static_profiled_function(AnyRuntimeValue, generate_expression, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    TypedExpression expression
), (
    info,
    jobs,
    scope,
    context,
    expression
)) {
    if(expression.value.kind == ValueKind::ConstantValue) {
        auto ir_type = get_ir_type(context->arena, info.architecture_sizes, expression.type);
        auto ir_constant_value = get_runtime_ir_constant_value(context->arena, expression.value.constant);
        
        auto register_index = append_literal(
            context,
            expression.range,
            ir_type,
            ir_constant_value
        );        

        return AnyRuntimeValue(RegisterValue(ir_type, register_index));
    }

    if(expression.kind == TypedExpressionKind::IndexReference) {
        auto index_reference = expression.index_reference;

        auto expression_value = generate_expression(info, jobs, scope, context, *index_reference.value);

        auto index = generate_expression(info, jobs, scope, context, *index_reference.index);

        assert(index_reference.index->type.kind == TypeKind::Integer);

        auto index_ir_type = IRType::create_integer(index_reference.index->type.integer.size);

        auto index_register = generate_in_register_value(context, index_reference.index->range, index_ir_type, index);

        AnyType element_type;
        IRType element_ir_type;
        size_t base_pointer_register;
        if(index_reference.value->type.kind == TypeKind::ArrayTypeType) {
            auto array_type = index_reference.value->type.array;

            element_type = *array_type.element_type;

            auto ir_type = get_array_ir_type(context->arena, info.architecture_sizes, array_type);
            element_ir_type = get_ir_type(context->arena, info.architecture_sizes, element_type);

            if(expression_value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.register_;

                base_pointer_register = append_read_struct_member(
                    context,
                    index_reference.value->range,
                    1,
                    register_value.register_index
                );
            } else if(expression_value.kind == RuntimeValueKind::AddressedValue) {
                auto addressed_value = expression_value.addressed;

                auto member_pointer = append_struct_member_pointer(
                    context,
                    index_reference.value->range,
                    ir_type.struct_.members,
                    1,
                    addressed_value.pointer_register
                );

                base_pointer_register = append_load(
                    context,
                    index_reference.value->range,
                    member_pointer,
                    IRType::create_pointer()
                );
            } else {
                abort();
            }
        } else if(index_reference.value->type.kind == TypeKind::StaticArray) {
            auto static_array = index_reference.value->type.static_array;

            element_type = *static_array.element_type;

            auto ir_type = get_static_array_ir_type(context->arena, info.architecture_sizes, static_array);
            element_ir_type = get_ir_type(context->arena, info.architecture_sizes, element_type);

            assert(expression_value.kind == RuntimeValueKind::AddressedValue);

            auto addressed_value = expression_value.addressed;

            base_pointer_register = addressed_value.pointer_register;
        } else {
            abort();
        }

        assert(element_type == expression.type);
        assert(expression.value.kind == ValueKind::AssignableValue);

        auto pointer_register = append_pointer_index(
            context,
            expression.range,
            index_register,
            element_ir_type,
            base_pointer_register
        );

        return AnyRuntimeValue(AddressedValue(element_ir_type, pointer_register));
    } else if(expression.kind == TypedExpressionKind::MemberReference) {
        auto member_reference = expression.member_reference;

        auto expression_value = generate_expression(info, jobs, scope, context, *member_reference.value);

        AnyType actual_type;
        AnyRuntimeValue actual_value;
        if(member_reference.value->type.kind == TypeKind::Pointer) {
            auto pointer = member_reference.value->type.pointer;

            actual_type = *pointer.pointed_to_type;

            assert(actual_type.is_runtime_type());

            auto actual_ir_type = get_ir_type(context->arena, info.architecture_sizes, actual_type);

            auto pointer_register = generate_in_register_value(
                context,
                member_reference.value->range,
                IRType::create_pointer(),
                expression_value
            );

            actual_value = AnyRuntimeValue(AddressedValue(actual_ir_type, pointer_register));
        } else {
            actual_type = member_reference.value->type;
            actual_value = expression_value;
        }

        if(actual_type.kind == TypeKind::ArrayTypeType) {
            auto array_type = actual_type.array;

            auto array_ir_type = get_array_ir_type(context->arena, info.architecture_sizes, array_type);

            if(member_reference.name.text == u8"length"_S) {
                AnyRuntimeValue result_value;
                if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto length_register = append_read_struct_member(
                        context,
                        expression.range,
                        0,
                        register_value.register_index
                    );

                    result_value = AnyRuntimeValue(RegisterValue(
                        IRType::create_integer(info.architecture_sizes.address_size),
                        length_register
                    ));
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = actual_value.addressed;

                    auto pointer_register = append_struct_member_pointer(
                        context,
                        expression.range,
                        array_ir_type.struct_.members,
                        0,
                        addressed_value.pointer_register
                    );

                    result_value = AnyRuntimeValue(AddressedValue(
                        IRType::create_integer(info.architecture_sizes.address_size),
                        pointer_register
                    ));
                } else {
                    abort();
                }

                return result_value;
            } else if(member_reference.name.text == u8"pointer"_S) {
                auto element_ir_type = get_ir_type(context->arena, info.architecture_sizes, *array_type.element_type);

                AnyRuntimeValue result_value;
                if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto pointer_register = append_read_struct_member(
                        context,
                        expression.range,
                        1,
                        register_value.register_index
                    );

                    result_value = AnyRuntimeValue(RegisterValue(
                        IRType::create_pointer(),
                        pointer_register
                    ));
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = actual_value.addressed;

                    auto pointer_register = append_struct_member_pointer(
                        context,
                        expression.range,
                        array_ir_type.struct_.members,
                        1,
                        addressed_value.pointer_register
                    );

                    result_value = AnyRuntimeValue(AddressedValue(
                        element_ir_type,
                        pointer_register
                    ));
                } else {
                    abort();
                }

                return result_value;
            } else {
                abort();
            }
        } else if(actual_type.kind == TypeKind::StaticArray) {
            auto static_array = actual_type.static_array;

            auto element_ir_type = get_ir_type(context->arena, info.architecture_sizes, *static_array.element_type);

            if(member_reference.name.text == u8"pointer"_S) {
                assert(actual_value.kind == RuntimeValueKind::AddressedValue);

                auto pointer_register = actual_value.addressed.pointer_register;

                return AnyRuntimeValue(RegisterValue(
                    IRType::create_pointer(),
                    pointer_register
                ));
            } else {
                abort();
            }
        } else if(actual_type.kind == TypeKind::StructType) {
            auto struct_type = actual_type.struct_;

            auto struct_ir_type = get_struct_ir_type(context->arena, info.architecture_sizes, struct_type);

            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(struct_type.members[i].name == member_reference.name.text) {
                    auto member_type = struct_type.members[i].type;
                    auto member_ir_type = get_ir_type(context->arena, info.architecture_sizes, member_type);

                    if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = actual_value.register_;

                        auto register_index = append_read_struct_member(
                            context,
                            expression.range,
                            i,
                            register_value.register_index
                        );

                        return AnyRuntimeValue(RegisterValue(member_ir_type, register_index));
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = actual_value.addressed;

                        auto pointer_register = append_struct_member_pointer(
                            context,
                            expression.range,
                            struct_ir_type.struct_.members,
                            i,
                            addressed_value.pointer_register
                        );

                        return AnyRuntimeValue(AddressedValue(member_ir_type, pointer_register));
                    } else {
                        abort();
                    }
                }
            }

            abort();
        } else if(actual_type.kind == TypeKind::UnionType) {
            auto union_type = actual_type.union_;

            for(size_t i = 0; i < union_type.members.length; i += 1) {
                if(union_type.members[i].name == member_reference.name.text) {
                    auto member_type = union_type.members[i].type;
                    auto member_ir_type = get_ir_type(context->arena, info.architecture_sizes, member_type);

                    if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = actual_value.register_;

                        auto union_ir_type = get_union_ir_type(context->arena, info.architecture_sizes, union_type);

                        auto pointer_register = append_allocate_local(
                            context,
                            expression.range,
                            union_ir_type
                        );

                        append_store(
                            context,
                            expression.range,
                            register_value.register_index,
                            pointer_register
                        );

                        auto register_index = append_load(
                            context,
                            expression.range,
                            pointer_register,
                            member_ir_type
                        );

                        return AnyRuntimeValue(RegisterValue(member_ir_type, register_index));
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = actual_value.addressed;

                        auto pointer_register = addressed_value.pointer_register;

                        return AnyRuntimeValue(AddressedValue(member_ir_type, pointer_register));
                    } else {
                        abort();
                    }
                }
            }

            abort();
        } else if(actual_type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = actual_type.undetermined_struct;

            assert(actual_value.kind == RuntimeValueKind::RuntimeUndeterminedAggregateValue);

            auto undetermined_aggregate_value = actual_value.undetermined_aggregate;

            for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                if(undetermined_struct.members[i].name == member_reference.name.text) {
                    return AnyRuntimeValue(undetermined_aggregate_value.values[i]);
                }
            }

            abort();
        } else {
            abort();
        }
    } else if(expression.kind == TypedExpressionKind::ArrayLiteral) {
        auto array_literal = expression.array_literal;

        auto element_count = array_literal.elements.length;

        assert(element_count != 0);

        auto element_types = context->arena->allocate<AnyType>(element_count);
        auto element_values = context->arena->allocate<RegisterValue>(element_count);

        for(size_t i = 1; i < element_count; i += 1) {
            auto element_value = generate_expression(info, jobs, scope, context, array_literal.elements[i]);

            element_types[i] = array_literal.elements[i].type;

            auto ir_type = get_ir_type(context->arena, info.architecture_sizes, array_literal.elements[i].type);

            auto register_index = generate_in_register_value(context, array_literal.elements[i].range, ir_type, element_value);

            element_values[i] = RegisterValue(ir_type, register_index);
        }

        return AnyRuntimeValue(RuntimeUndeterminedAggregateValue(Array(
            element_count,
            element_values
        )));
    } else if(expression.kind == TypedExpressionKind::StructLiteral) {
        auto struct_literal = expression.struct_literal;

        auto member_count = struct_literal.members.length;

        assert(member_count != 0);

        auto type_members = context->arena->allocate<StructTypeMember>(member_count);
        auto member_values = context->arena->allocate<RegisterValue>(member_count);

        for(size_t i = 1; i < member_count; i += 1) {
            auto member_value = generate_expression(info, jobs, scope, context, struct_literal.members[i].member);

            StructTypeMember type_member {};
            type_member.name = struct_literal.members[i].name.text;
            type_member.type = struct_literal.members[i].member.type;

            type_members[i] = type_member;

            auto ir_type = get_ir_type(context->arena, info.architecture_sizes, struct_literal.members[i].member.type);

            auto register_index = generate_in_register_value(context, struct_literal.members[i].member.range, ir_type, member_value);

            member_values[i] = RegisterValue(ir_type, register_index);
        }

        return AnyRuntimeValue(RuntimeUndeterminedAggregateValue(Array(
            member_count,
            member_values
        )));
    } else if(expression.kind == TypedExpressionKind::FunctionCall) {
        auto function_call = expression.function_call;

        if(function_call.value->type.kind == TypeKind::FunctionTypeType) {
            auto function_type = function_call.value->type.function;

            auto function_value = function_call.value->value.unwrap_constant_value().unwrap_function();

            assert(function_type.parameters.length == function_call.parameters.length);

            auto parameters = context->arena->allocate<FunctionCallInstruction::Parameter>(function_type.parameters.length);
            for(size_t i = 0; i < function_type.parameters.length; i += 1) {
                auto parameter_value = generate_expression(info, jobs, scope, context, function_call.parameters[i]);

                auto ir_type = get_ir_type(context->arena, info.architecture_sizes, function_call.parameters[i].type);

                auto register_index = generate_in_register_value(context, function_call.parameters[i].range, ir_type, parameter_value);

                FunctionCallInstruction::Parameter parameter {};
                parameter.type = ir_type;
                parameter.register_index = register_index;

                parameters[i] = parameter;
            }

            auto found = false;
            Function* runtime_function;
            for(auto typed_function : context->functions) {
                if(
                    AnyType(typed_function.type) == AnyType(function_type) &&
                    typed_function.constant.declaration == function_value.declaration &&
                    typed_function.constant.body_scope == function_value.body_scope
                ) {
                    found = true;
                    runtime_function = typed_function.function;

                    break;
                }
            }

            assert(found);

            bool has_ir_return;
            IRType return_ir_type;
            if(function_type.return_types.length == 0) {
                has_ir_return = false;
            } else if(function_type.return_types.length == 1) {
                has_ir_return = true;
                return_ir_type = get_ir_type(context->arena, info.architecture_sizes, function_type.return_types[0]);
            } else {
                auto member_ir_types = context->arena->allocate<IRType>(function_type.return_types.length);

                for(size_t i = 0; i < function_type.return_types.length; i += 1) {
                    member_ir_types[i] = get_ir_type(context->arena, info.architecture_sizes, function_type.return_types[i]);
                }

                has_ir_return = true;
                return_ir_type = IRType::create_struct(Array(function_type.return_types.length, member_ir_types));
            }

            auto pointer_register = append_reference_static(context, expression.range, runtime_function);

            assert(context->variable_scope_stack.length != 0);
            auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

            auto function_call_instruction = context->arena->allocate_and_construct<FunctionCallInstruction>();
            function_call_instruction->range = expression.range;
            function_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
            function_call_instruction->pointer_register = pointer_register;
            function_call_instruction->parameters = Array(function_type.parameters.length, parameters);
            function_call_instruction->has_return = has_ir_return;
            function_call_instruction->return_type = return_ir_type;
            function_call_instruction->calling_convention = function_type.calling_convention;

            AnyRuntimeValue value;
            if(has_ir_return) {
                auto return_register = allocate_register(context);

                function_call_instruction->return_register = return_register;

                value = AnyRuntimeValue(RegisterValue(return_ir_type, return_register));
            }

            context->instructions.append(function_call_instruction);

            return value;
        } else if(function_call.value->type.kind == TypeKind::BuiltinFunction) {
            auto constant_value = function_call.value->value.unwrap_constant_value();

            auto builtin_function_value = constant_value.unwrap_builtin_function();

            if(builtin_function_value.name == u8"globalify"_S) {
                assert(function_call.parameters.length == 1);

                assert(function_call.parameters[0].type.is_runtime_type());

                assert(function_call.parameters[0].value.kind != ValueKind::ConstantValue);

                auto constant_value = function_call.parameters[0].value.constant;

                auto static_constant = register_static_constant(
                    info,
                    scope,
                    context,
                    expression.range,
                    function_call.parameters[0].type,
                    constant_value
                );

                auto pointer_register = append_reference_static(
                    context,
                    expression.range,
                    static_constant
                );

                return AnyRuntimeValue(AddressedValue(static_constant->type, pointer_register));
            } else if(builtin_function_value.name == u8"stackify"_S) {
                assert(function_call.parameters.length == 1);

                assert(function_call.parameters[0].type.is_runtime_type());

                auto parameter_value = generate_expression(info, jobs, scope, context, function_call.parameters[0]);

                auto ir_type = get_ir_type(context->arena, info.architecture_sizes, function_call.parameters[0].type);

                auto register_index = generate_in_register_value(context, function_call.parameters[0].range, ir_type, parameter_value);

                auto pointer_register = append_allocate_local(
                    context,
                    expression.range,
                    ir_type
                );

                append_store(
                    context,
                    expression.range,
                    register_index,
                    pointer_register
                );

                return AnyRuntimeValue(AddressedValue(ir_type, pointer_register));
            } else if(builtin_function_value.name == u8"sqrt"_S) {
                assert(function_call.parameters.length == 1);

                assert(function_call.parameters[0].type.kind == TypeKind::FloatType);

                auto parameter_value = generate_expression(info, jobs, scope, context, function_call.parameters[0]);

                auto ir_type = IRType::create_float(function_call.parameters[0].type.float_.size);

                auto register_index = generate_in_register_value(context, function_call.parameters[0].range, ir_type, parameter_value);

                auto return_register = allocate_register(context);

                IntrinsicCallInstruction::Parameter ir_parameter;
                ir_parameter.type = ir_type;
                ir_parameter.register_index = register_index;

                assert(context->variable_scope_stack.length != 0);
                auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                auto intrinsic_call_instruction = context->arena->allocate_and_construct<IntrinsicCallInstruction>();
                intrinsic_call_instruction->range = expression.range;
                intrinsic_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
                intrinsic_call_instruction->intrinsic = IntrinsicCallInstruction::Intrinsic::Sqrt;
                intrinsic_call_instruction->parameters = Array(1, context->arena->heapify(ir_parameter));
                intrinsic_call_instruction->has_return = true;
                intrinsic_call_instruction->return_type = ir_type;
                intrinsic_call_instruction->return_register = return_register;

                context->instructions.append(intrinsic_call_instruction);

                return AnyRuntimeValue(RegisterValue(ir_type, return_register));
            } else {
                abort();
            }
        } else if(function_call.value->type.kind == TypeKind::Pointer) {
            auto pointer = function_call.value->type.pointer;

            assert(pointer.pointed_to_type->kind == TypeKind::FunctionTypeType);

            auto expression_value = generate_expression(info, jobs, scope, context, *function_call.value);

            auto function_type = pointer.pointed_to_type->function;

            auto pointer_register = generate_in_register_value(
                context,
                function_call.value->range,
                IRType::create_pointer(),
                expression_value
            );

            auto parameter_count = function_type.parameters.length;

            assert(function_call.parameters.length == parameter_count);

            auto parameters = context->arena->allocate<FunctionCallInstruction::Parameter>(parameter_count);
            for(size_t i = 0; i < parameter_count; i += 1) {
                auto parameter_value = generate_expression(info, jobs, scope, context, function_call.parameters[i]);

                auto ir_type = get_ir_type(context->arena, info.architecture_sizes, function_type.parameters[i]);

                auto register_index = generate_in_register_value(context, function_call.parameters[i].range, ir_type, parameter_value);

                FunctionCallInstruction::Parameter parameter {};
                parameter.type = ir_type;
                parameter.register_index = register_index;

                parameters[i] = parameter;
            }

            bool has_ir_return;
            IRType return_ir_type;
            if(function_type.return_types.length == 0) {
                has_ir_return = false;
            } else if(function_type.return_types.length == 1) {
                has_ir_return = true;
                return_ir_type = get_ir_type(context->arena, info.architecture_sizes, function_type.return_types[0]);
            } else {
                auto member_ir_types = context->arena->allocate<IRType>(function_type.return_types.length);

                for(size_t i = 0; i < function_type.return_types.length; i += 1) {
                    member_ir_types[i] = get_ir_type(context->arena, info.architecture_sizes, function_type.return_types[i]);
                }

                has_ir_return = true;
                return_ir_type = IRType::create_struct(Array(function_type.return_types.length, member_ir_types));
            }

            assert(context->variable_scope_stack.length != 0);
            auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

            auto function_call_instruction = context->arena->allocate_and_construct<FunctionCallInstruction>();
            function_call_instruction->range = expression.range;
            function_call_instruction->debug_scope_index = current_variable_scope.debug_scope_index;
            function_call_instruction->pointer_register = pointer_register;
            function_call_instruction->parameters = Array(parameter_count, parameters);
            function_call_instruction->has_return = has_ir_return;
            function_call_instruction->return_type = return_ir_type;
            function_call_instruction->calling_convention = function_type.calling_convention;

            AnyRuntimeValue value;
            if(has_ir_return) {
                auto return_register = allocate_register(context);

                function_call_instruction->return_register = return_register;

                value = AnyRuntimeValue(RegisterValue(return_ir_type, return_register));
            }

            context->instructions.append(function_call_instruction);

            return value;
        } else {
            abort();
        }
    } else if(expression.kind == TypedExpressionKind::BinaryOperation) {
        auto binary_operation = expression.binary_operation;

        auto result = generate_binary_operation(
            info,
            jobs,
            scope,
            context,
            expression.range,
            binary_operation.kind,
            *binary_operation.left,
            *binary_operation.right
        );

        return AnyRuntimeValue(result);
    } else if(expression.kind == TypedExpressionKind::UnaryOperation) {
        auto unary_operation = expression.unary_operation;

        switch(unary_operation.kind) {
            case UnaryOperationKind::Pointer: {
                size_t pointer_register;
                if(unary_operation.value->value.kind == ValueKind::ConstantValue) {
                    auto constant_value = unary_operation.value->value.constant;

                    assert(unary_operation.value->type.kind == TypeKind::FunctionTypeType);

                    auto function = unary_operation.value->type.function;

                    auto function_value = constant_value.unwrap_function();

                    auto found = false;
                    Function* runtime_function;
                    for(auto typed_function : context->functions) {
                        if(
                            AnyType(typed_function.type) == AnyType(function) &&
                            typed_function.constant.declaration == function_value.declaration &&
                            typed_function.constant.body_scope == function_value.body_scope
                        ) {
                            found = true;
                            runtime_function = typed_function.function;

                            break;
                        }
                    }

                    assert(found);

                    pointer_register = append_reference_static(
                        context,
                        expression.range,
                        runtime_function
                    );
                } else {
                    auto expression_value = generate_expression(info, jobs, scope, context, *unary_operation.value);

                    assert(expression_value.kind == RuntimeValueKind::AddressedValue);

                    auto addressed_value = expression_value.addressed;

                    pointer_register = addressed_value.pointer_register;
                }

                return AnyRuntimeValue(RegisterValue(IRType::create_pointer(), pointer_register));
            } break;

            case UnaryOperationKind::PointerDereference: {
                auto expression_value = generate_expression(info, jobs, scope, context, *unary_operation.value);

                assert(unary_operation.value->type.kind == TypeKind::Pointer);

                auto pointed_to_type = *unary_operation.value->type.pointer.pointed_to_type;

                assert(pointed_to_type.is_runtime_type());

                auto pointed_to_ir_type = get_ir_type(context->arena, info.architecture_sizes, pointed_to_type);

                auto pointer_register = generate_in_register_value(
                    context,
                    unary_operation.value->range,
                    IRType::create_pointer(),
                    expression_value
                );

                return AnyRuntimeValue(AddressedValue(pointed_to_ir_type, pointer_register));
            } break;

            case UnaryOperationKind::BooleanInvert: {
                auto expression_value = generate_expression(info, jobs, scope, context, *unary_operation.value);

                assert(unary_operation.value->type.kind == TypeKind::Boolean);

                size_t register_index;
                if(expression_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.register_;

                    register_index = register_value.register_index;
                } else if(expression_value.kind == RuntimeValueKind::AddressedValue) {
                    auto addressed_value = expression_value.addressed;

                    register_index = append_load(
                        context,
                        unary_operation.value->range,
                        addressed_value.pointer_register,
                        IRType::create_boolean()
                    );
                } else {
                    abort();
                }

                auto result_register = append_boolean_inversion(context, expression.range, register_index);

                return AnyRuntimeValue(RegisterValue(IRType::create_boolean(), result_register));
            } break;

            case UnaryOperationKind::Negation: {
                auto expression_value = generate_expression(info, jobs, scope, context, *unary_operation.value);

                if(unary_operation.value->type.kind == TypeKind::Integer) {
                    auto integer = unary_operation.value->type.integer;

                    auto ir_type = IRType::create_integer(integer.size);

                    size_t register_index;
                    if(expression_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.addressed;

                        register_index = append_load(
                            context,
                            unary_operation.value->range,
                            addressed_value.pointer_register,
                            ir_type
                        );
                    }

                    auto zero_register = append_literal(
                        context,
                        expression.range,
                        ir_type,
                        IRConstantValue::create_integer(0)
                    );

                    auto result_register = append_integer_arithmetic_operation(
                        context,
                        expression.range,
                        IntegerArithmeticOperation::Operation::Subtract,
                        zero_register,
                        register_index
                    );

                    return AnyRuntimeValue(RegisterValue(ir_type, result_register));
                } else if(unary_operation.value->type.kind == TypeKind::FloatType) {
                    auto float_type = unary_operation.value->type.float_;

                    auto ir_type = IRType::create_float(float_type.size);

                    size_t register_index;
                    if(expression_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.kind == RuntimeValueKind::AddressedValue) {
                        auto addressed_value = expression_value.addressed;

                        register_index = append_load(
                            context,
                            unary_operation.value->range,
                            addressed_value.pointer_register,
                            ir_type
                        );
                    }

                    auto zero_register = append_literal(
                        context,
                        expression.range,
                        ir_type,
                        IRConstantValue::create_float(0.0)
                    );

                    auto result_register = append_float_arithmetic_operation(
                        context,
                        expression.range,
                        FloatArithmeticOperation::Operation::Subtract,
                        zero_register,
                        register_index
                    );

                    return AnyRuntimeValue(RegisterValue(ir_type, result_register));
                } else {
                    abort();
                }
            } break;

            default: abort();
        }
    } else if(expression.kind == TypedExpressionKind::Cast) {
        auto cast = expression.cast;

        auto expression_value = generate_expression(info, jobs, scope, context, *cast.value);

        assert(cast.type->type.kind == TypeKind::Type);

        auto target_type = cast.type->value.unwrap_constant_value().unwrap_type();

        auto result_register = convert_to_type_register(
            info,
            scope,
            context,
            expression.range,
            cast.value->type,
            expression_value,
            target_type
        );

        return AnyRuntimeValue(result_register);
    } else if(expression.kind == TypedExpressionKind::Coercion) {
        auto coercion = expression.coercion;

        auto expression_value = generate_expression(info, jobs, scope, context, *coercion.original);

        auto result_register = convert_to_type_register(
            info,
            scope,
            context,
            expression.range,
            coercion.original->type,
            expression_value,
            expression.type
        );

        return AnyRuntimeValue(result_register);
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
        // context->arena->allocate_and_new<block>() is not required
        return;
    }

    auto new_block = context->arena->allocate_and_construct<Block>();

    auto last_instruction = context->instructions[context->instructions.length - 1];

    if(does_current_block_need_finisher(context)) {
        append_jump(context, range, new_block);
    }

    context->current_block->instructions = context->instructions;
    context->blocks.append(context->current_block);

    context->current_block = new_block;
    context->instructions = List<Instruction*>(context->arena);
}

static void change_block(GenerationContext* context, FileRange range, Block* block) {
    assert(!does_current_block_need_finisher(context));

    context->current_block->instructions = context->instructions;
    context->blocks.append(context->current_block);

    context->current_block = block;
    context->instructions = List<Instruction*>(context->arena);
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
    List<AnyJob*>* jobs,
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
                        error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.get_description(context->arena)));

                        return err();
                    }

                    type = type_value;

                    expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                    auto ir_type = get_ir_type(context->arena, info.architecture_sizes, type);

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
                        error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

                        return err();
                    }

                    type = actual_type;

                    auto ir_type = get_ir_type(context->arena, info.architecture_sizes, type);

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
                    error(scope, variable_declaration->initializer->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(initializer.type.get_description(context->arena)));

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

                auto return_struct_member_ir_types = context->arena->allocate<IRType>(return_types.length);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    return_struct_member_ir_types[i] = get_ir_type(context->arena, info.architecture_sizes, return_types[i]);
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
                    error(scope, assignment->value->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(value.type.get_description(context->arena)));

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

                auto return_struct_member_ir_types = context->arena->allocate<IRType>(return_types.length);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    return_struct_member_ir_types[i] = get_ir_type(context->arena, info.architecture_sizes, return_types[i]);
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
                    binary_operation_assignment->kind
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

                auto end_block = context->arena->allocate_and_construct<Block>();

                Block* next_block;
                if(if_statement->else_ifs.length == 0 && if_statement->else_statements.length == 0) {
                    next_block = end_block;
                } else {
                    next_block = context->arena->allocate_and_construct<Block>();
                }

                auto body_block = context->arena->allocate_and_construct<Block>();

                expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

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
                if_variable_scope.variables.arena = context->arena;

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
                        next_block = context->arena->allocate_and_construct<Block>();
                    }

                    auto body_block = context->arena->allocate_and_construct<Block>();

                    expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->else_ifs[i].condition));

                    if(condition.type.kind != TypeKind::Boolean) {
                        error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

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
                    else_if_variable_scope.variables.arena = context->arena;

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
                    else_variable_scope.variables.arena = context->arena;

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

                auto end_block = context->arena->allocate_and_construct<Block>();

                auto body_block = context->arena->allocate_and_construct<Block>();

                enter_new_block(context, while_loop->condition->range);

                auto condition_block = context->current_block;

                expect_delayed(condition, generate_expression(info, jobs, scope, context, while_loop->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

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
                while_variable_scope.variables.arena = context->arena;

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
                    error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(from_value.type.get_description(context->arena)));

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

                auto end_block = context->arena->allocate_and_construct<Block>();

                auto body_block = context->arena->allocate_and_construct<Block>();

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
                for_variable_scope.variables.arena = context->arena;

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

                auto return_instruction = context->arena->allocate_and_construct<ReturnInstruction>();
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
                    auto return_struct_members = context->arena->allocate<size_t>(return_type_count);

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

                auto bindings = context->arena->allocate<AssemblyInstruction::Binding>(inline_assembly->bindings.length);

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
                            error(scope, binding.value->range, "Value of type '%.*s' cannot be used as a binding", STRING_PRINTF_ARGUMENTS(determined_value_type.get_description(context->arena)));

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

                auto assembly_instruction = context->arena->allocate_and_construct<AssemblyInstruction>();
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
    List<AnyJob*>* jobs,
    Arena* arena,
    Array<TypedFunction> functions,
    Array<TypedStaticVariable> static_variables,
    FunctionTypeType type,
    FunctionConstant value,
    Function* function
), (
    info,
    jobs,
    arena,
    type,
    value,
    function
)) {
    auto declaration = value.declaration;

    auto declaration_parameter_count = declaration->parameters.length;

    auto file_path = get_scope_file_path(*value.body_scope);

    auto runtime_parameter_count = type.parameters.length;

    auto ir_parameters = arena->allocate<IRType>(runtime_parameter_count);

    size_t runtime_parameter_index = 0;
    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
        if(!declaration->parameters[i].is_constant) {
            auto argument_type = type.parameters[runtime_parameter_index];

            ir_parameters[runtime_parameter_index] = get_ir_type(arena, info.architecture_sizes, argument_type);

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
        return_ir_type = get_ir_type(arena, info.architecture_sizes, type.return_types[0]);
    } else {
        auto return_struct_members = arena->allocate<IRType>(type.return_types.length);

        for(size_t i = 0; i < type.return_types.length; i += 1) {
            return_struct_members[i] = get_ir_type(arena, info.architecture_sizes, type.return_types[i]);
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
        context.arena = arena;
        context.variable_scope_stack.arena = arena;
        context.debug_scopes.arena = arena;
        context.blocks.arena = arena;
        context.instructions.arena = arena;
        context.static_constants.arena = arena;
        context.scope_search_stack.arena = arena;

        context.functions = functions;
        context.static_variables = static_variables;

        context.return_types = type.return_types;

        context.next_register = runtime_parameter_count;

        DebugScope debug_scope {};
        debug_scope.range = declaration->range;

        auto debug_scope_index = context.debug_scopes.append(debug_scope);

        VariableScope body_variable_scope {};
        body_variable_scope.constant_scope = value.body_scope;
        body_variable_scope.debug_scope_index = debug_scope_index;
        body_variable_scope.variables.arena = context.arena;

        context.variable_scope_stack.append(body_variable_scope);

        context.child_scopes = value.child_scopes;

        context.current_block = arena->allocate_and_construct<Block>();

        List<Instruction*> first_block_instructions(arena);

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
                auto return_instruction = arena->allocate_and_construct<ReturnInstruction>();
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
    List<AnyJob*>* jobs,
    Arena* arena,
    VariableDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    declaration,
    scope
)) {
    List<ConstantScope*> scope_search_stack(arena);

    auto is_external = false;
    Array<String> external_libraries;
    auto is_no_mangle = false;
    for(auto tag : declaration->tags) {
        if(tag.name.text == u8"extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            List<String> libraries(arena);

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, evaluate_constant_expression(
                    arena,
                    info,
                    jobs,
                    scope,
                    nullptr,
                    tag.parameters[i],
                    &scope_search_stack
                ));

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
                                expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.type, parameter.value));

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
                            expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.type.get_description(arena)));

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

        expect_delayed(type, evaluate_type_expression(
            arena,
            info,
            jobs,
            scope,
            nullptr,
            declaration->type,
            &scope_search_stack
        ));

        if(!type.is_runtime_type()) {
            error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

            return err();
        }

        assert(scope_search_stack.length == 0);

        auto static_variable = arena->allocate_and_construct<StaticVariable>();
        static_variable->name = declaration->name.text;
        static_variable->is_no_mangle = true;
        static_variable->path = get_scope_file_path(*scope);
        static_variable->range = declaration->range;
        static_variable->type = get_ir_type(arena, info.architecture_sizes, type);
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
            expect_delayed(type, evaluate_type_expression(
                arena,
                info,
                jobs,
                scope,
                nullptr,
                declaration->type,
                &scope_search_stack
            ));

            if(!type.is_runtime_type()) {
                error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

                return err();
            }

            expect_delayed(initial_value, evaluate_constant_expression(
                arena,
                info,
                jobs,
                scope,
                nullptr,
                declaration->initializer,
                &scope_search_stack
            ));

            expect(coerced_initial_value, coerce_constant_to_type(
                arena,
                info,
                scope,
                declaration->initializer->range,
                initial_value.type,
                initial_value.value,
                type,
                false
            ));

            assert(scope_search_stack.length == 0);

            auto ir_initial_value = get_runtime_ir_constant_value(arena, coerced_initial_value);

            auto static_variable = arena->allocate_and_construct<StaticVariable>();
            static_variable->name = declaration->name.text;
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->type = get_ir_type(arena, info.architecture_sizes, type);
            static_variable->is_external = false;
            static_variable->has_initial_value = true;
            static_variable->initial_value = ir_initial_value;
            static_variable->debug_type = type;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        } else {
            expect_delayed(initial_value, evaluate_constant_expression(
                arena,
                info,
                jobs,
                scope,
                nullptr,
                declaration->initializer,
                &scope_search_stack
            ));

            expect(type, coerce_to_default_type(info, scope, declaration->initializer->range, initial_value.type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

                return err();
            }

            assert(scope_search_stack.length == 0);

            auto ir_initial_value = get_runtime_ir_constant_value(arena, initial_value.value);

            auto static_variable = arena->allocate_and_construct<StaticVariable>();
            static_variable->name = declaration->name.text;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->type = get_ir_type(arena, info.architecture_sizes, type);
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