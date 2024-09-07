#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "timing.h"
#include "profiler.h"
#include "list.h"
#include "util.h"
#include "string.h"
#include "path.h"
#include "lexer.h"
#include "parser.h"
#include "constant.h"
#include "types.h"
#include "jobs.h"

struct Variable {
    Identifier name;

    AnyType type;

    size_t address_register;
};

struct VariableScope {
    ConstantScope* constant_scope;

    List<Variable> variables;
};

struct GenerationContext {
    AnyType return_type;
    size_t return_parameter_register;

    Array<ConstantScope*> child_scopes;
    size_t next_child_scope_index;

    bool in_breakable_scope;
    List<Jump*> break_jumps;

    List<VariableScope> variable_scope_stack;

    size_t next_register;

    List<StaticConstant*> static_constants;
};

struct RegisterRepresentation {
    bool is_in_register;

    RegisterSize value_size;
    bool is_float;
};

static RegisterRepresentation get_type_representation(GlobalInfo info, AnyType type) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        return {
            true,
            integer.size,
            false
        };
    } else if(type.kind == TypeKind::Boolean) {
        return {
            true,
            info.architecture_sizes.boolean_size,
            false
        };
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        return {
            true,
            float_type.size,
            true
        };
    } else if(type.kind == TypeKind::Pointer) {
        return {
            true,
            info.architecture_sizes.address_size,
            false
        };
    } else if(
        type.kind == TypeKind::ArrayTypeType ||
        type.kind == TypeKind::StaticArray ||
        type.kind == TypeKind::StructType
    ) {
        return {
            false
        };
    } else {
        abort();
    }
}

static void write_value(GlobalInfo info, uint8_t* data, size_t offset, AnyType type, AnyConstantValue value);

static Result<void> add_new_variable(GenerationContext* context, Identifier name, size_t address_register, AnyType type) {
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
    variable.address_register = address_register;

    variable_scope->variables.append(variable);

    return ok();
}

struct AnyRuntimeValue;

struct RegisterValue {
    size_t register_index;
};

struct AddressValue {
    size_t address_register;
};

struct UndeterminedStructValue {
    AnyRuntimeValue* members;
};

enum struct RuntimeValueKind {
    ConstantValue,
    RegisterValue,
    AddressValue,
    UndeterminedStructValue
};

struct AnyRuntimeValue {
    RuntimeValueKind kind;

    union {
        AnyConstantValue constant;
        RegisterValue register_;
        AddressValue address;
        UndeterminedStructValue undetermined_struct;
    };
};

inline AnyRuntimeValue wrap_constant_value(AnyConstantValue value) {
    AnyRuntimeValue result;
    result.kind = RuntimeValueKind::ConstantValue;
    result.constant = value;

    return result;
}

inline AnyConstantValue unwrap_constant_value(AnyRuntimeValue value) {
    assert(value.kind == RuntimeValueKind::ConstantValue);

    return value.constant;
}

inline AnyRuntimeValue wrap_register_value(RegisterValue value) {
    AnyRuntimeValue result;
    result.kind = RuntimeValueKind::RegisterValue;
    result.register_ = value;

    return result;
}

inline RegisterValue unwrap_register_value(AnyRuntimeValue value) {
    assert(value.kind == RuntimeValueKind::RegisterValue);

    return value.register_;
}

inline AnyRuntimeValue wrap_address_value(AddressValue value) {
    AnyRuntimeValue result;
    result.kind = RuntimeValueKind::AddressValue;
    result.address = value;

    return result;
}

inline AddressValue unwrap_address_value(AnyRuntimeValue value) {
    assert(value.kind == RuntimeValueKind::AddressValue);

    return value.address;
}

inline AnyRuntimeValue wrap_undetermined_struct_value(UndeterminedStructValue value) {
    AnyRuntimeValue result;
    result.kind = RuntimeValueKind::UndeterminedStructValue;
    result.undetermined_struct = value;

    return result;
}

inline UndeterminedStructValue unwrap_undetermined_struct_value(AnyRuntimeValue value) {
    assert(value.kind == RuntimeValueKind::UndeterminedStructValue);

    return value.undetermined_struct;
}

struct TypedRuntimeValue {
    inline TypedRuntimeValue() {}
    inline TypedRuntimeValue(AnyType type, AnyRuntimeValue value) : type(type), value(value) {}

    AnyType type;

    AnyRuntimeValue value;
};

static size_t allocate_register(GenerationContext* context) {
    auto index = context->next_register;

    context->next_register += 1;

    return index;
}

static void write_integer(uint8_t* buffer, size_t offset, RegisterSize size, uint64_t value) {
    buffer[offset] = (uint8_t)value;

    if(size >= RegisterSize::Size16) {
        buffer[offset + 1] = (uint8_t)(value >> 8);
    } else {
        return;
    }

    if(size >= RegisterSize::Size32) {
        buffer[offset + 2] = (uint8_t)(value >> 16);
        buffer[offset + 3] = (uint8_t)(value >> 24);
    } else {
        return;
    }

    if(size == RegisterSize::Size64) {
        buffer[offset + 4] = (uint8_t)(value >> 32);
        buffer[offset + 5] = (uint8_t)(value >> 40);
        buffer[offset + 6] = (uint8_t)(value >> 48);
        buffer[offset + 7] = (uint8_t)(value >> 56);
    } else {
        abort();
    }
}

static void write_struct(GlobalInfo info, uint8_t* data, size_t offset, StructType struct_type, AnyConstantValue* member_values) {
    for(size_t i = 0; i < struct_type.members.length; i += 1) {
        write_value(
            info,
            data,
            offset + struct_type.get_member_offset(info.architecture_sizes, i),
            struct_type.members[i].type,
            member_values[i]
        );
    }
}

static void write_static_array(GlobalInfo info, uint8_t* data, size_t offset, AnyType element_type, Array<AnyConstantValue> elements) {
    auto element_size = element_type.get_size(info.architecture_sizes);

    for(size_t i = 0; i < elements.length; i += 1) {
        write_value(
            info,
            data,
            offset + i * element_size,
            element_type,
            elements[i]
        );
    }
}

static void write_value(GlobalInfo info, uint8_t* data, size_t offset, AnyType type, AnyConstantValue value) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        auto integer_value = unwrap_integer_constant(value);

        write_integer(data, offset, integer.size, integer_value);
    } else if(type.kind == TypeKind::Boolean) {
        auto boolean_value = unwrap_boolean_constant(value);

        write_integer(data, offset, info.architecture_sizes.boolean_size, boolean_value);
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        auto float_value = unwrap_float_constant(value);

        uint64_t integer_value;
        switch(float_type.size) {
            case RegisterSize::Size32: {
                auto value = (float)float_value;

                integer_value = (uint64_t)*(uint32_t*)&value;
            } break;

            case RegisterSize::Size64: {
                integer_value = (uint64_t)*(uint64_t*)&float_value;
            } break;

            default: {
                abort();
            } break;
        }

        write_integer(data, offset, float_type.size, integer_value);
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer_value = unwrap_pointer_constant(value);

        write_integer(data, offset, info.architecture_sizes.address_size, pointer_value);
    } else if(type.kind == TypeKind::ArrayTypeType) {
        auto array_value = unwrap_array_constant(value);

        write_integer(
            data,
            offset,
            info.architecture_sizes.address_size,
            array_value.pointer
        );

        write_integer(
            data,
            offset + register_size_to_byte_size(info.architecture_sizes.address_size),
            info.architecture_sizes.address_size,
            array_value.length
        );
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        auto static_array_value = unwrap_static_array_constant(value);

        write_static_array(
            info,
            data,
            offset,
            *static_array.element_type,
            {
                static_array.length,
                static_array_value.elements
            }
        );
    } else if(type.kind == TypeKind::StructType) {
        auto struct_type = type.struct_;

        auto struct_value = unwrap_struct_constant(value);

        write_struct(info, data, offset, struct_type, struct_value.members);
    } else {
        abort();
    }
}

static StaticConstant* register_static_array_constant(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    AnyType element_type,
    Array<AnyConstantValue> elements
) {
    auto data_length = element_type.get_size(info.architecture_sizes) * elements.length;
    auto data = allocate<uint8_t>(data_length);

    write_static_array(info, data, 0, element_type, elements);

    auto constant = new StaticConstant;
    constant->name = "array_constant"_S;
    constant->is_no_mangle = false;
    constant->path = get_scope_file_path(*scope);
    constant->range = range;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = element_type.get_alignment(info.architecture_sizes);

    context->static_constants.append(constant);

    return constant;
}

static StaticConstant* register_struct_constant(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    FileRange range,
    StructType struct_type,
    AnyConstantValue* members
) {
    auto data_length = struct_type.get_size(info.architecture_sizes);
    auto data = allocate<uint8_t>(data_length);

    write_struct(info, data, 0, struct_type, members);

    auto constant = new StaticConstant;
    constant->name = "struct_constant"_S;
    constant->is_no_mangle = false;
    constant->path = get_scope_file_path(*scope);
    constant->range = range;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = struct_type.get_alignment(info.architecture_sizes);

    context->static_constants.append(constant);

    return constant;
}

static size_t append_integer_arithmetic_operation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    IntegerArithmeticOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto integer_arithmetic_operation = new IntegerArithmeticOperation;
    integer_arithmetic_operation->range = range;
    integer_arithmetic_operation->operation = operation;
    integer_arithmetic_operation->size = size;
    integer_arithmetic_operation->source_register_a = source_register_a;
    integer_arithmetic_operation->source_register_b = source_register_b;
    integer_arithmetic_operation->destination_register = destination_register;

    instructions->append(integer_arithmetic_operation);

    return destination_register;
}

static size_t append_integer_comparison_operation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    IntegerComparisonOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto integer_comparison_operation = new IntegerComparisonOperation;
    integer_comparison_operation->range = range;
    integer_comparison_operation->operation = operation;
    integer_comparison_operation->size = size;
    integer_comparison_operation->source_register_a = source_register_a;
    integer_comparison_operation->source_register_b = source_register_b;
    integer_comparison_operation->destination_register = destination_register;

    instructions->append(integer_comparison_operation);

    return destination_register;
}

static size_t append_integer_extension(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto integer_extension = new IntegerExtension;
    integer_extension->range = range;
    integer_extension->is_signed = is_signed;
    integer_extension->source_size = source_size;
    integer_extension->source_register = source_register;
    integer_extension->destination_size = destination_size;
    integer_extension->destination_register = destination_register;

    instructions->append(integer_extension);

    return destination_register;
}

static size_t append_integer_truncation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto integer_truncation = new IntegerTruncation;
    integer_truncation->range = range;
    integer_truncation->source_size = source_size;
    integer_truncation->source_register = source_register;
    integer_truncation->destination_size = destination_size;
    integer_truncation->destination_register = destination_register;

    instructions->append(integer_truncation);

    return destination_register;
}

static size_t append_integer_constant(GenerationContext* context, List<Instruction*>* instructions, FileRange range, RegisterSize size, uint64_t value) {
    auto destination_register = allocate_register(context);

    auto constant = new IntegerConstantInstruction;
    constant->range = range;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    instructions->append(constant);

    return destination_register;
}

static size_t append_float_arithmetic_operation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    FloatArithmeticOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto float_arithmetic_operation = new FloatArithmeticOperation;
    float_arithmetic_operation->range = range;
    float_arithmetic_operation->operation = operation;
    float_arithmetic_operation->size = size;
    float_arithmetic_operation->source_register_a = source_register_a;
    float_arithmetic_operation->source_register_b = source_register_b;
    float_arithmetic_operation->destination_register = destination_register;

    instructions->append(float_arithmetic_operation);

    return destination_register;
}

static size_t append_float_comparison_operation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    FloatComparisonOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto float_comparison_operation = new FloatComparisonOperation;
    float_comparison_operation->range = range;
    float_comparison_operation->operation = operation;
    float_comparison_operation->size = size;
    float_comparison_operation->source_register_a = source_register_a;
    float_comparison_operation->source_register_b = source_register_b;
    float_comparison_operation->destination_register = destination_register;

    instructions->append(float_comparison_operation);

    return destination_register;
}

static size_t append_float_conversion(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_conversion = new FloatConversion;
    float_conversion->range = range;
    float_conversion->source_size = source_size;
    float_conversion->source_register = source_register;
    float_conversion->destination_size = destination_size;
    float_conversion->destination_register = destination_register;

    instructions->append(float_conversion);

    return destination_register;
}

static size_t append_float_truncation(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_truncation = new FloatTruncation;
    float_truncation->range = range;
    float_truncation->source_size = source_size;
    float_truncation->source_register = source_register;
    float_truncation->destination_size = destination_size;
    float_truncation->destination_register = destination_register;

    instructions->append(float_truncation);

    return destination_register;
}

static size_t append_float_from_integer(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_from_integer = new FloatFromInteger;
    float_from_integer->range = range;
    float_from_integer->is_signed = is_signed;
    float_from_integer->source_size = source_size;
    float_from_integer->source_register = source_register;
    float_from_integer->destination_size = destination_size;
    float_from_integer->destination_register = destination_register;

    instructions->append(float_from_integer);

    return destination_register;
}

static size_t append_float_constant(GenerationContext* context, List<Instruction*>* instructions, FileRange range, RegisterSize size, double value) {
    auto destination_register = allocate_register(context);

    auto constant = new FloatConstantInstruction;
    constant->range = range;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    instructions->append(constant);

    return destination_register;
}

static size_t append_reference_static(GenerationContext* context, List<Instruction*>* instructions, FileRange range, RuntimeStatic* runtime_static) {
    auto destination_register = allocate_register(context);

    auto reference_static = new ReferenceStatic;
    reference_static->range = range;
    reference_static->runtime_static = runtime_static;
    reference_static->destination_register = destination_register;

    instructions->append(reference_static);

    return destination_register;
}

static size_t append_allocate_local(GenerationContext* context, List<Instruction*>* instructions, FileRange range, size_t size, size_t alignment) {
    auto destination_register = allocate_register(context);

    auto allocate_local = new AllocateLocal;
    allocate_local->range = range;
    allocate_local->size = size;
    allocate_local->alignment = alignment;
    allocate_local->destination_register = destination_register;

    instructions->append(allocate_local);

    return destination_register;
}

static void append_branch(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    size_t condition_register,
    size_t destination_instruction
) {
    auto branch = new Branch;
    branch->range = range;
    branch->condition_register = condition_register;
    branch->destination_instruction = destination_instruction;

    instructions->append(branch);
}

static void append_jump(GenerationContext* context, List<Instruction*>* instructions, FileRange range, size_t destination_instruction) {
    auto jump = new Jump;
    jump->range = range;
    jump->destination_instruction = destination_instruction;

    instructions->append(jump);
}

static void append_copy_memory(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    size_t length,
    size_t source_address_register,
    size_t destination_address_register,
    size_t alignment
) {
    auto copy_memory = new CopyMemory;
    copy_memory->range = range;
    copy_memory->length = length;
    copy_memory->source_address_register = source_address_register;
    copy_memory->destination_address_register = destination_address_register;
    copy_memory->alignment = alignment;

    instructions->append(copy_memory);
}

static size_t append_load_integer(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    auto load_integer = new LoadInteger;
    load_integer->range = range;
    load_integer->size = size;
    load_integer->address_register = address_register;
    load_integer->destination_register = destination_register;

    instructions->append(load_integer);

    return destination_register;
}

static void append_store_integer(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    auto store_integer = new StoreInteger;
    store_integer->range = range;
    store_integer->size = size;
    store_integer->source_register = source_register;
    store_integer->address_register = address_register;

    instructions->append(store_integer);
}

static size_t append_load_float(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    auto load_integer = new LoadFloat;
    load_integer->range = range;
    load_integer->size = size;
    load_integer->address_register = address_register;
    load_integer->destination_register = destination_register;

    instructions->append(load_integer);

    return destination_register;
}

static void append_store_float(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    auto store_integer = new StoreFloat;
    store_integer->range = range;
    store_integer->size = size;
    store_integer->source_register = source_register;
    store_integer->address_register = address_register;

    instructions->append(store_integer);
}

static size_t generate_address_offset(
    GlobalInfo info,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    size_t address_register,
    size_t offset
) {
    auto offset_register = append_integer_constant(
        context,
        instructions,
        range,
        info.architecture_sizes.address_size,
        offset
    );

    auto final_address_register = append_integer_arithmetic_operation(
        context,
        instructions,
        range,
        IntegerArithmeticOperation::Operation::Add,
        info.architecture_sizes.address_size,
        address_register,
        offset_register
    );

    return final_address_register;
}

static size_t generate_boolean_invert(GlobalInfo info, GenerationContext* context, List<Instruction*>* instructions, FileRange range, size_t value_register) {
    auto local_register = append_allocate_local(
        context,
        instructions,
        range,
        register_size_to_byte_size(info.architecture_sizes.boolean_size),
        register_size_to_byte_size(info.architecture_sizes.boolean_size)
    );

    append_branch(context, instructions, range, value_register, instructions->length + 4);

    auto true_register = append_integer_constant(context, instructions, range, info.architecture_sizes.boolean_size, 1);

    append_store_integer(context, instructions, range, info.architecture_sizes.boolean_size, true_register, local_register);

    append_jump(context, instructions, range, instructions->length + 3);

    auto false_register = append_integer_constant(context, instructions, range, info.architecture_sizes.boolean_size, 0);

    append_store_integer(context, instructions, range, info.architecture_sizes.boolean_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range, info.architecture_sizes.boolean_size, local_register);

    return result_register;
}

static size_t generate_in_register_integer_value(
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    Integer type,
    AnyRuntimeValue value
) {
    if(value.kind == RuntimeValueKind::ConstantValue) {
        auto integer_value = unwrap_integer_constant(value.constant);

        return append_integer_constant(context, instructions, range, type.size, integer_value);
    } else if(value.kind == RuntimeValueKind::RegisterValue) {
        auto register_value = value.register_;

        return register_value.register_index;
    } else if(value.kind == RuntimeValueKind::AddressValue) {
        auto address_value = value.address;

        return append_load_integer(context, instructions, range, type.size, address_value.address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_boolean_value(GlobalInfo info, GenerationContext* context, List<Instruction*>* instructions, FileRange range, AnyRuntimeValue value) {
    if(value.kind == RuntimeValueKind::ConstantValue) {
        auto boolean_value = unwrap_boolean_constant(value.constant);

        return append_integer_constant(context, instructions, range, info.architecture_sizes.boolean_size, boolean_value);
    } else if(value.kind == RuntimeValueKind::RegisterValue) {
        auto register_value = value.register_;

        return register_value.register_index;
    } else if(value.kind == RuntimeValueKind::AddressValue) {
        auto address_value = value.address;

        return append_load_integer(context, instructions, range, info.architecture_sizes.boolean_size, address_value.address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_pointer_value(GlobalInfo info, GenerationContext* context, List<Instruction*>* instructions, FileRange range, AnyRuntimeValue value) {
    if(value.kind == RuntimeValueKind::ConstantValue) {
        auto pointer_value = unwrap_pointer_constant(value.constant);

        return append_integer_constant(context, instructions, range, info.architecture_sizes.address_size, pointer_value);
    } else if(value.kind == RuntimeValueKind::RegisterValue) {
        auto register_value = value.register_;

        return register_value.register_index;
    } else if(value.kind == RuntimeValueKind::AddressValue) {
        auto address_value = value.address;

        return append_load_integer(context, instructions, range, info.architecture_sizes.address_size, address_value.address_register);
    } else {
        abort();
    }
}

static Result<size_t> coerce_to_integer_register_value(
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Integer target_type,
    bool probing
) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        if(integer.size == target_type.size && integer.is_signed == target_type.is_signed) {
            auto register_index = generate_in_register_integer_value(context, instructions, range, target_type, value);

            return ok(register_index);
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = unwrap_integer_constant(value.constant);

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value, probing)) {
            return err();
        }

        auto regsiter_index = append_integer_constant(context, instructions, range, target_type.size, integer_value);

        return ok(regsiter_index);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_type).get_description()));
    }

    return err();
}

static Result<size_t> coerce_to_float_register_value(
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    FloatType target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = unwrap_integer_constant(value.constant);

        auto register_index = append_float_constant(context, instructions, range, target_type.size, (double)integer_value);

        return ok(register_index);
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        if(target_type.size == float_type.size) {
            size_t register_index;
            if(value.kind == RuntimeValueKind::ConstantValue) {
                auto float_value = unwrap_float_constant(value.constant);

                register_index = append_float_constant(context, instructions, range, float_type.size, float_value);
            } else if(value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = value.register_;

                register_index = register_value.register_index;
            } else if(value.kind == RuntimeValueKind::AddressValue) {
                auto address_value = value.address;

                register_index = append_load_float(context, instructions, range, float_type.size, address_value.address_register);
            } else {
                abort();
            }

            return ok(register_index);
        }
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        auto float_value = unwrap_float_constant(value.constant);

        auto register_index = append_float_constant(context, instructions, range, target_type.size, float_value);

        return ok(register_index);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_float_type(target_type).get_description()));
    }

    return err();
}

static Result<size_t> coerce_to_pointer_register_value(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Pointer target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = unwrap_integer_constant(value.constant);

        auto register_index = append_integer_constant(
            context,
            instructions,
            range,
            info.architecture_sizes.address_size,
            integer_value
        );

        return ok(register_index);
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        if(*pointer.type == *target_type.type) {
            auto register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

            return ok(register_index);
        }
    }

    if (!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_pointer_type(target_type).get_description()));
    }

    return err();
}

static Result<void> coerce_to_type_write(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type,
    size_t address_register
);

static Result<size_t> coerce_to_type_register(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type,
    bool probing
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer = target_type.integer;

        expect(register_index, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            range,
            type,
            value,
            integer,
            probing
        ));

        return ok(register_index);
    } else if(target_type.kind == TypeKind::Boolean) {
        if(type.kind == TypeKind::Boolean) {
            auto register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

            return ok(register_index);
        }
    } else if(target_type.kind == TypeKind::FloatType) {
        auto float_type = target_type.float_;

        expect(register_index, coerce_to_float_register_value(
            scope,
            context,
            instructions,
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
            instructions,
            range,
            type,
            value,
            pointer,
            probing
        ));

        return ok(register_index);
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;
            if(*target_array.element_type == *array_type.element_type) {
                size_t register_index;
                if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    register_index = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    register_index = address_value.address_register;
                } else {
                    abort();
                }

                return ok(register_index);
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_array.element_type == *static_array.element_type) {
                size_t pointer_register;
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto static_array_value = unwrap_static_array_constant(value.constant);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        *static_array.element_type,
                        { static_array.length, static_array_value.elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    pointer_register = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    pointer_register = address_value.address_register;
                } else {
                    abort();
                }

                auto address_register = append_allocate_local(
                    context,
                    instructions,
                    range,
                    2 * register_size_to_byte_size(info.architecture_sizes.address_size),
                    register_size_to_byte_size(info.architecture_sizes.address_size)
                );

                append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.architecture_sizes.address_size)
                );

                auto length_register = append_integer_constant(context, instructions, range, info.architecture_sizes.address_size, static_array.length);

                append_store_integer(context, instructions, range, info.architecture_sizes.address_size, length_register, length_address_register);

                return ok(address_register);
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == "pointer"_S &&
                undetermined_struct.members[1].name == "length"_S
            ) {
                auto undetermined_struct_value = unwrap_undetermined_struct_value(value);

                auto pointer_result = coerce_to_pointer_register_value(
                    info,
                    scope,
                    context,
                    instructions,
                    range,
                    undetermined_struct.members[0].type,
                    undetermined_struct_value.members[0],
                    {
                        target_array.element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        instructions,
                        range,
                        undetermined_struct.members[1].type,
                        undetermined_struct_value.members[1],
                        {
                            info.architecture_sizes.address_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range,
                            2 * register_size_to_byte_size(info.architecture_sizes.address_size),
                            register_size_to_byte_size(info.architecture_sizes.address_size)
                        );

                        append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(info.architecture_sizes.address_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range,
                            info.architecture_sizes.address_size,
                            length_result.value,
                            length_address_register
                        );

                        return ok(address_register);
                    }
                }
            }
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_static_array.element_type == *static_array.element_type && target_static_array.length == static_array.length) {
                size_t register_index;
                if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    register_index = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    register_index = address_value.address_register;
                } else {
                    abort();
                }

                return ok(register_index);
            }
        }
    } else if(target_type.kind == TypeKind::StructType) {
        auto target_struct_type = target_type.struct_;

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
                    } else if(value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = value.address;

                        register_index = address_value.address_register;
                    } else {
                        abort();
                    }

                    return ok(register_index);
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            auto undetermined_struct_value = unwrap_undetermined_struct_value(value);

            if(target_struct_type.definition->is_union) {
                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_struct_type.members.length; i += 1) {
                        if(target_struct_type.members[i].name == undetermined_struct.members[0].name) {
                            auto address_register = append_allocate_local(
                                context,
                                instructions,
                                range,
                                target_struct_type.get_size(info.architecture_sizes),
                                target_struct_type.get_alignment(info.architecture_sizes)
                            );

                            if(
                                coerce_to_type_write(
                                    info,
                                    scope,
                                    context,
                                    instructions,
                                    range,
                                    undetermined_struct.members[0].type,
                                    undetermined_struct_value.members[0],
                                    target_struct_type.members[i].type,
                                    address_register
                                ).status
                            ) {
                                return ok(address_register);
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range,
                            target_struct_type.get_size(info.architecture_sizes),
                            target_struct_type.get_alignment(info.architecture_sizes)
                        );

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto member_address_register = generate_address_offset(
                                info,
                                context,
                                instructions,
                                range,
                                address_register,
                                target_struct_type.get_member_offset(info.architecture_sizes, i)
                            );

                            if(
                                !coerce_to_type_write(
                                    info,
                                    scope,
                                    context,
                                    instructions,
                                    range,
                                    undetermined_struct.members[i].type,
                                    undetermined_struct_value.members[i],
                                    target_struct_type.members[i].type,
                                    member_address_register
                                ).status
                            ) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok(address_register);
                        }
                    }
                }
            }
        }
    } else {
        abort();
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
    }

    return err();
}

static Result<void> coerce_to_type_write(
    GlobalInfo info,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type,
    size_t address_register
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer_type = target_type.integer;

        expect(register_index, coerce_to_integer_register_value(scope, context, instructions, range, type, value, integer_type, false));

        append_store_integer(context, instructions, range, integer_type.size, register_index, address_register);

        return ok();
    } else if(target_type.kind == TypeKind::Boolean && type.kind == TypeKind::Boolean) {
        size_t register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

        append_store_integer(context, instructions, range, info.architecture_sizes.boolean_size, register_index, address_register);

        return ok();
    } else if(target_type.kind == TypeKind::FloatType) {
        auto float_type = target_type.float_;

        expect(register_index, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            range,
            type,
            value,
            float_type,
            false
        ));

        append_store_float(context, instructions, range, float_type.size, register_index, address_register);

        return ok();
    } else if(target_type.kind == TypeKind::Pointer) {
        auto target_pointer = target_type.pointer;

        if(type.kind == TypeKind::UndeterminedInteger) {
            auto integer_value = unwrap_integer_constant(value.constant);

            auto register_index = append_integer_constant(
                context,
                instructions,
                range,
                info.architecture_sizes.address_size,
                integer_value
            );

            append_store_integer(
                context,
                instructions,
                range,
                info.architecture_sizes.address_size,
                register_index,
                address_register
            );

            return ok();
        } else if(type.kind == TypeKind::Pointer) {
            auto pointer = type.pointer;

            if(*target_pointer.type == *pointer.type) {
                size_t register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

                append_store_integer(context, instructions, range, info.architecture_sizes.address_size, register_index, address_register);

                return ok();
            }
        }
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;

            if(*target_array.element_type == *array_type.element_type) {
                size_t source_address_register;
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto array_value = unwrap_array_constant(value.constant);

                    auto pointer_register = append_integer_constant(
                        context,
                        instructions,
                        range,
                        info.architecture_sizes.address_size,
                        array_value.pointer
                    );

                    append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_register, address_register);

                    auto length_register = append_integer_constant(
                        context,
                        instructions,
                        range,
                        info.architecture_sizes.address_size,
                        array_value.length
                    );

                    auto length_address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        range,
                        address_register,
                        register_size_to_byte_size(info.architecture_sizes.address_size)
                    );

                    append_store_integer(context, instructions, range, info.architecture_sizes.address_size, length_register, length_address_register);

                    return ok();
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    source_address_register = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    source_address_register = address_value.address_register;
                } else {
                    abort();
                }

                append_copy_memory(
                    context,
                    instructions,
                    range,
                    2 * register_size_to_byte_size(info.architecture_sizes.address_size),
                    source_address_register,
                    address_register,
                    register_size_to_byte_size(info.architecture_sizes.address_size)
                );

                return ok();
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;
            if(*target_array.element_type == *static_array.element_type) {
                size_t pointer_register;
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto static_array_value = unwrap_static_array_constant(value.constant);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        *static_array.element_type,
                        { static_array.length, static_array_value.elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    pointer_register = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    pointer_register = address_value.address_register;
                } else {
                    abort();
                }

                append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.architecture_sizes.address_size)
                );

                auto length_register = append_integer_constant(context, instructions, range, info.architecture_sizes.address_size, static_array.length);

                append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_register, length_address_register);

                return ok();
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == "pointer"_S &&
                undetermined_struct.members[1].name == "length"_S
            ) {
                auto undetermined_struct_value = unwrap_undetermined_struct_value(value);

                auto pointer_result = coerce_to_pointer_register_value(
                    info,
                    scope,
                    context,
                    instructions,
                    range,
                    undetermined_struct.members[0].type,
                    undetermined_struct_value.members[0],
                    {
                        target_array.element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        instructions,
                        range,
                        undetermined_struct.members[1].type,
                        undetermined_struct_value.members[1],
                        {
                            info.architecture_sizes.address_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        append_store_integer(context, instructions, range, info.architecture_sizes.address_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(info.architecture_sizes.address_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range,
                            info.architecture_sizes.address_size,
                            length_result.value,
                            length_address_register
                        );

                        return ok();
                    }
                }
            }
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_static_array.element_type == *static_array.element_type && target_static_array.length == static_array.length) {
                size_t source_address_register;
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto static_array_value = unwrap_static_array_constant(value.constant);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        *static_array.element_type,
                        { static_array.length, static_array_value.elements }
                    );

                    source_address_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = value.register_;

                    source_address_register = register_value.register_index;
                } else if(value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = value.address;

                    source_address_register = address_value.address_register;
                } else {
                    abort();
                }

                append_copy_memory(
                    context,
                    instructions,
                    range,
                    static_array.length * static_array.element_type->get_size(info.architecture_sizes),
                    source_address_register,
                    address_register,
                    static_array.element_type->get_size(info.architecture_sizes)
                );

                return ok();
            }
        }
    } else if(target_type.kind == TypeKind::StructType) {
        auto target_struct_type = target_type.struct_;

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
                    size_t source_address_register;
                    if(value.kind == RuntimeValueKind::ConstantValue) {
                        auto struct_value = unwrap_struct_constant(value.constant);

                        auto static_constant = register_struct_constant(
                            info,
                            scope,
                            context,
                            range,
                            struct_type,
                            struct_value.members
                        );

                        source_address_register = append_reference_static(context, instructions, range, static_constant);
                    } else if(value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = value.register_;

                        source_address_register = register_value.register_index;
                    } else if(value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = value.address;

                        source_address_register = address_value.address_register;
                    } else {
                        abort();
                    }

                    append_copy_memory(
                        context,
                        instructions,
                        range,
                        struct_type.get_size(info.architecture_sizes),
                        source_address_register,
                        address_register,
                        struct_type.get_alignment(info.architecture_sizes)
                    );

                    return ok();
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(target_struct_type.definition->is_union) {
                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_struct_type.members.length; i += 1) {
                        if(target_struct_type.members[i].name == undetermined_struct.members[0].name) {
                            AnyRuntimeValue variant_value;
                            if(value.kind == RuntimeValueKind::ConstantValue) {
                                auto struct_value = unwrap_struct_constant(value.constant);

                                variant_value = wrap_constant_value(struct_value.members[0]);
                            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                                auto undetermined_struct_value = value.undetermined_struct;

                                variant_value = undetermined_struct_value.members[0];
                            } else {
                                abort();
                            }

                            if(
                                coerce_to_type_write(
                                    info,
                                    scope,
                                    context,
                                    instructions,
                                    range,
                                    undetermined_struct.members[0].type,
                                    variant_value,
                                    target_struct_type.members[i].type,
                                    address_register
                                ).status
                            ) {
                                return ok();
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            AnyRuntimeValue member_value;
                            if(value.kind == RuntimeValueKind::ConstantValue) {
                                auto struct_value = unwrap_struct_constant(value.constant);

                                member_value = wrap_constant_value(struct_value.members[i]);
                            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                                auto undetermined_struct_value = value.undetermined_struct;

                                member_value = undetermined_struct_value.members[i];
                            } else {
                                abort();
                            }

                            auto member_address_register = generate_address_offset(
                                info,
                                context,
                                instructions,
                                range,
                                address_register,
                                target_struct_type.get_member_offset(info.architecture_sizes, i)
                            );

                            if(
                                !coerce_to_type_write(
                                    info,
                                    scope,
                                    context,
                                    instructions,
                                    range,
                                    undetermined_struct.members[i].type,
                                    member_value,
                                    target_struct_type.members[i].type,
                                    member_address_register
                                ).status
                            ) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok();
                        }
                    }
                }
            }
        }
    } else {
        abort();
    }

    error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));

    return err();
}

static DelayedResult<TypedRuntimeValue> generate_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    Expression* expression
);

static DelayedResult<AnyType> evaluate_type_expression_runtime(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    Expression* expression
) {
    expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, expression));

    if(expression_value.type.kind == TypeKind::Type) {
        auto constant_value = unwrap_constant_value(expression_value.value);

        return ok(unwrap_type_constant(constant_value));
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
    List<Instruction*>* instructions,
    FileRange range,
    Expression* left_expression,
    Expression* right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect_delayed(left, generate_expression(info, jobs, scope, context, instructions, left_expression));

    expect_delayed(right, generate_expression(info, jobs, scope, context, instructions, right_expression));

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
            wrap_constant_value(constant.value)
        ));
    }

    expect(type, determine_binary_operation_type(scope, range, left.type, right.type));

    expect(determined_type, coerce_to_default_type(info, scope, range, type));

    if(determined_type.kind == TypeKind::Integer) {
        auto integer = determined_type.integer;

        expect(left_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            left_expression->range,
            left.type,
            left.value,
            integer,
            false
        ));

        expect(right_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
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

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        AnyType result_type;
        if(is_arithmetic) {
            result_register = append_integer_arithmetic_operation(
                context,
                instructions,
                range,
                arithmetic_operation,
                integer.size,
                left_register,
                right_register
            );

            result_type = wrap_integer_type(integer);
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
                instructions,
                range,
                comparison_operation,
                integer.size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }

            result_type = create_boolean_type();
        }

        return ok(TypedRuntimeValue(
            result_type,
            wrap_register_value({ result_register })
        ));
    } else if(determined_type.kind == TypeKind::Boolean) {
        if(left.type.kind != TypeKind::Boolean) {
            error(scope, left_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(left.type.get_description()));

            return err();
        }

        auto left_register = generate_in_register_boolean_value(info, context, instructions, left_expression->range, left.value);

        if(right.type.kind != TypeKind::Boolean) {
            error(scope, right_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(right.type.get_description()));

            return err();
        }

        auto right_register = generate_in_register_boolean_value(info, context, instructions, right_expression->range, right.value);

        auto is_arithmetic = true;
        IntegerArithmeticOperation::Operation arithmetic_operation;
        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseAnd;
            } break;

            case BinaryOperation::Operator::BooleanOr: {
                arithmetic_operation = IntegerArithmeticOperation::Operation::BitwiseOr;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        if(is_arithmetic) {
            result_register = append_integer_arithmetic_operation(
                context,
                instructions,
                range,
                arithmetic_operation,
                info.architecture_sizes.boolean_size,
                left_register,
                right_register
            );
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

                default: {
                    error(scope, range, "Cannot perform that operation on 'bool'");

                    return err();
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                instructions,
                range,
                comparison_operation,
                info.architecture_sizes.boolean_size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }
        }

        return ok(TypedRuntimeValue(
            create_boolean_type(),
            wrap_register_value({ result_register })
        ));
    } else if(determined_type.kind == TypeKind::FloatType) {
        auto float_type = determined_type.float_;

        expect(left_register, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            left_expression->range,
            left.type,
            left.value,
            float_type,
            false
        ));

        expect(right_register, coerce_to_float_register_value(
            scope,
            context,
            instructions,
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

            default: {
                is_arithmetic = false;
            } break;
        }

        size_t result_register;
        AnyType result_type;
        if(is_arithmetic) {
            result_register = append_float_arithmetic_operation(
                context,
                instructions,
                range,
                arithmetic_operation,
                float_type.size,
                left_register,
                right_register
            );

            result_type = wrap_float_type(float_type);
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
                instructions,
                range,
                comparison_operation,
                float_type.size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }

            result_type = create_boolean_type();
        }

        return ok(TypedRuntimeValue(
            result_type,
            wrap_register_value({ result_register })
        ));
    } else if(determined_type.kind == TypeKind::Pointer) {
        auto pointer = determined_type.pointer;

        expect(left_register, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            instructions,
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
            instructions,
            right_expression->range,
            right.type,
            right.value,
            pointer,
            false
        ));

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

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(wrap_pointer_type(pointer).get_description()));

                return err();
            } break;
        }

        auto result_register = append_integer_comparison_operation(
            context,
            instructions,
            range,
            comparison_operation,
            info.architecture_sizes.address_size,
            left_register,
            right_register
        );

        if(invert) {
            result_register = generate_boolean_invert(info, context, instructions, range, result_register);
        }

        return ok(TypedRuntimeValue(
            create_boolean_type(),
            wrap_register_value({ result_register })
        ));
    } else {
        abort();
    }
}

struct RuntimeDeclarationSearchResult {
    bool found;

    AnyType type;
    AnyRuntimeValue value;
};

static_profiled_function(DelayedResult<RuntimeDeclarationSearchResult>, search_for_declaration, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
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
    instructions,
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
            RuntimeDeclarationSearchResult result {};
            result.found = false;

            return ok(result);
        }

        expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, declaration));

        RuntimeDeclarationSearchResult result {};
        result.found = true;
        result.type = value.type;
        result.value = wrap_constant_value(value.value);

        return ok(result);
    }

    for(auto statement : statements) {
        if(statement->kind == StatementKind::UsingStatement) {
            if(!external) {
                auto using_statement = (UsingStatement*)statement;

                expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, nullptr, using_statement->module));

                if(expression_value.type.kind != TypeKind::FileModule) {
                    error(scope, using_statement->range, "Expected a module, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }

                auto file_module = unwrap_file_module_constant(expression_value.value);

                expect_delayed(search_value, search_for_declaration(
                    info,
                    jobs,
                    file_module.scope,
                    context,
                    instructions,
                    name,
                    name_hash,
                    name_scope,
                    name_range,
                    file_module.scope->statements,
                    file_module.scope->declarations,
                    true
                ));

                if(search_value.found) {
                    RuntimeDeclarationSearchResult result {};
                    result.found = true;
                    result.type = search_value.type;
                    result.value = search_value.value;

                    return ok(result);
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
                                expect_delayed(search_value, search_for_declaration(
                                    info,
                                    jobs,
                                    scope,
                                    context,
                                    instructions,
                                    name,
                                    name_hash,
                                    name_scope,
                                    name_range,
                                    static_if->statements,
                                    resolve_static_if.declarations,
                                    false
                                ));

                                if(search_value.found) {
                                    RuntimeDeclarationSearchResult result {};
                                    result.found = true;
                                    result.type = search_value.type;
                                    result.value = search_value.value;

                                    return ok(result);
                                }
                            }
                        } else {
                            auto have_to_wait = false;
                            for(auto statement : static_if->statements) {
                                bool matching;
                                if(external) {
                                    matching = match_public_declaration(statement, name);
                                } else {
                                    matching = match_declaration(statement, name);
                                }

                                if(matching) {
                                    have_to_wait = true;
                                } else if(statement->kind == StatementKind::UsingStatement) {
                                    if(!external) {
                                        have_to_wait = true;
                                    }
                                } else if(statement->kind == StatementKind::StaticIf) {
                                    have_to_wait = true;
                                }
                            }

                            if(have_to_wait) {
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
                                    auto address_register = append_reference_static(
                                        context,
                                        instructions,
                                        name_range,
                                        generate_static_variable.static_variable
                                    );

                                    RuntimeDeclarationSearchResult result {};
                                    result.found = true;
                                    result.type = generate_static_variable.type;
                                    result.value = wrap_address_value({ address_register });

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
            RuntimeDeclarationSearchResult result {};
            result.found = true;
            result.type = scope_constant.type;
            result.value = wrap_constant_value(scope_constant.value);

            return ok(result);
        }
    }

    RuntimeDeclarationSearchResult result {};
    result.found = false;

    return ok(result);
}

static_profiled_function(DelayedResult<TypedRuntimeValue>, generate_expression, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    Expression* expression
), (
    info,
    jobs,
    scope,
    context,
    instructions,
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
                        wrap_address_value({ variable.address_register })
                    ));
                }
            }

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                current_scope.constant_scope,
                context,
                instructions,
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
            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                current_scope,
                context,
                instructions,
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
                    wrap_constant_value(global_constant.value)
                ));
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %.*s", STRING_PRINTF_ARGUMENTS(named_reference->name.text));

        return err();
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, index_reference->expression));

        expect_delayed(index, generate_expression(info, jobs, scope, context, instructions, index_reference->index));

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
                wrap_constant_value(constant.value)
            ));
        }

        expect(index_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            index_reference->index->range,
            index.type,
            index.value,
            {
                info.architecture_sizes.address_size,
                false
            },
            false
        ));

        size_t base_address_register;
        AnyType element_type;
        if(expression_value.type.kind == TypeKind::ArrayTypeType) {
            auto array_type = expression_value.type.array;
            element_type =* array_type.element_type;

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                auto pointer_value = unwrap_pointer_constant(expression_value.value.constant);

                base_address_register = append_integer_constant(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.architecture_sizes.address_size,
                    pointer_value
                );
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.value.register_;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.architecture_sizes.address_size,
                    register_value.register_index
                );
            } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                auto address_value = expression_value.value.address;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.architecture_sizes.address_size,
                    address_value.address_register
                );
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::StaticArray) {
            auto static_array = expression_value.type.static_array;
            element_type =* static_array.element_type;

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                auto static_array_value = unwrap_static_array_constant(expression_value.value.constant);

                auto static_constant = register_static_array_constant(
                    info,
                    scope,
                    context,
                    index_reference->expression->range,
                    *static_array.element_type,
                    { static_array.length, static_array_value.elements }
                );

                base_address_register = append_reference_static(
                    context,
                    instructions,
                    index_reference->expression->range,
                    static_constant
                );
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.value.register_;

                base_address_register = register_value.register_index;
            } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                auto address_value = expression_value.value.address;

                base_address_register = address_value.address_register;
            } else {
                abort();
            }
        }

        auto element_size_register = append_integer_constant(
            context,
            instructions,
            index_reference->range,
            info.architecture_sizes.address_size,
            element_type.get_size(info.architecture_sizes)
        );

        auto offset = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range,
            IntegerArithmeticOperation::Operation::Multiply,
            info.architecture_sizes.address_size,
            element_size_register,
            index_register
        );

        auto address_register = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range,
            IntegerArithmeticOperation::Operation::Add,
            info.architecture_sizes.address_size,
            base_address_register,
            offset
        );

        return ok(TypedRuntimeValue(
            element_type,
            wrap_address_value({ address_register })
        ));
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, member_reference->expression));

        AnyType actual_type;
        AnyRuntimeValue actual_value;
        if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;
            actual_type =* pointer.type;

            size_t address_register;
            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                auto integer_value = unwrap_integer_constant(expression_value.value.constant);

                address_register = append_integer_constant(
                    context,
                    instructions,
                    member_reference->expression->range,
                    info.architecture_sizes.address_size,
                    integer_value
                );
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                auto register_value = expression_value.value.register_;

                address_register = register_value.register_index;
            } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                auto address_value = expression_value.value.address;

                address_register = append_load_integer(
                    context,
                    instructions,
                    member_reference->expression->range,
                    info.architecture_sizes.address_size,
                    address_value.address_register
                );
            } else {
                abort();
            }

            actual_value = wrap_address_value({ address_register });
        } else {
            actual_type = expression_value.type;
            actual_value = expression_value.value;
        }

        if(actual_type.kind == TypeKind::ArrayTypeType) {
            auto array_type = actual_type.array;

            if(member_reference->name.text == "length"_S) {
                auto type = new Integer {
                    info.architecture_sizes.address_size,
                    false
                };

                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    auto array_value = unwrap_array_constant(expression_value.value.constant);

                    value = wrap_constant_value(wrap_integer_constant(array_value.length));
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        member_reference->range,
                        register_value.register_index,
                        register_size_to_byte_size(info.architecture_sizes.address_size)
                    );

                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range,
                        info.architecture_sizes.address_size,
                        address_register
                    );

                    value = wrap_register_value({ length_register });
                } else if(actual_value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = actual_value.address;

                    auto address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        member_reference->range,
                        address_value.address_register,
                        register_size_to_byte_size(info.architecture_sizes.address_size)
                    );

                    value = wrap_address_value({ address_register });
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    wrap_integer_type({
                        info.architecture_sizes.address_size,
                        false
                    }),
                    value
                ));
            } else if(member_reference->name.text == "pointer"_S) {
                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    auto array_value = unwrap_array_constant(expression_value.value.constant);

                    value = wrap_constant_value(wrap_pointer_constant(array_value.pointer));
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range,
                        info.architecture_sizes.address_size,
                        register_value.register_index
                    );

                    value = wrap_register_value({ length_register });
                } else if(actual_value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = actual_value.address;

                    value = wrap_address_value({ address_value.address_register });
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    wrap_pointer_type({
                        array_type.element_type
                    }),
                    value
                ));
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StaticArray) {
            auto static_array = actual_type.static_array;

            if(member_reference->name.text == "length"_S) {
                return ok(TypedRuntimeValue(
                    wrap_integer_type({
                        info.architecture_sizes.address_size,
                        false
                    }),
                    wrap_constant_value(wrap_integer_constant(static_array.length))
                ));
            } else if(member_reference->name.text == "pointer"_S) {
                size_t address_regsiter;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    auto static_array_value = unwrap_static_array_constant(expression_value.value.constant);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        member_reference->expression->range,
                        *static_array.element_type,
                        { static_array.length, static_array_value.elements }
                    );

                    address_regsiter = append_reference_static(context, instructions, member_reference->range, static_constant);
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = actual_value.register_;

                    address_regsiter = register_value.register_index;
                } else if(actual_value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = actual_value.address;

                    address_regsiter = address_value.address_register;
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    wrap_pointer_type({
                        static_array.element_type
                    }),
                    wrap_register_value({ address_regsiter })
                ));
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StructType) {
            auto struct_type = actual_type.struct_;

            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(struct_type.members[i].name == member_reference->name.text) {
                    auto member_type = struct_type.members[i].type;

                    if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                        auto struct_value = unwrap_struct_constant(expression_value.value.constant);

                        assert(!struct_type.definition->is_union);

                        return ok(TypedRuntimeValue(
                            member_type,
                            wrap_constant_value(struct_value.members[i])
                        ));
                    } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = actual_value.register_;

                        auto address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            member_reference->range,
                            register_value.register_index,
                            struct_type.get_member_offset(info.architecture_sizes, i)
                        );

                        auto member_representation = get_type_representation(info, member_type);

                        size_t register_index;
                        if(member_representation.is_in_register) {
                            if(member_representation.is_float) {
                                register_index = append_load_float(
                                    context,
                                    instructions,
                                    member_reference->range,
                                    member_representation.value_size,
                                    address_register
                                );
                            } else {
                                register_index = append_load_integer(
                                    context,
                                    instructions,
                                    member_reference->range,
                                    member_representation.value_size,
                                    address_register
                                );
                            }
                        } else {
                            register_index = address_register;
                        }

                        return ok(TypedRuntimeValue(
                            member_type,
                            wrap_register_value({ register_index })
                        ));
                    } else if(actual_value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = actual_value.address;

                        auto address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            member_reference->range,
                            address_value.address_register,
                            struct_type.get_member_offset(info.architecture_sizes, i)
                        );

                        return ok(TypedRuntimeValue(
                            member_type,
                            wrap_address_value({ address_register })
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
        } else if(actual_type.kind == TypeKind::FileModule) {
            auto file_module_value = unwrap_file_module_constant(expression_value.value.constant);

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                file_module_value.scope,
                context,
                instructions,
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
        } else {
            error(scope, member_reference->expression->range, "Type %.*s has no members", STRING_PRINTF_ARGUMENTS(actual_type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return ok(TypedRuntimeValue(
            create_undetermined_integer_type(),
            wrap_constant_value(wrap_integer_constant(integer_literal->value))
        ));
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return ok(TypedRuntimeValue(
            create_undetermined_float_type(),
            wrap_constant_value(wrap_float_constant(float_literal->value))
        ));
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.length;

        auto characters = allocate<AnyConstantValue>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = wrap_integer_constant((uint64_t)string_literal->characters[i]);
        }

        return ok(TypedRuntimeValue(
            wrap_static_array_type({
                character_count,
                heapify(wrap_integer_type({
                    RegisterSize::Size8,
                    false
                }))
            }),
            wrap_constant_value(wrap_static_array_constant({
                characters
            }))
        ));
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.length;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err();
        }

        expect_delayed(first_element, generate_expression(info, jobs, scope, context, instructions, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!determined_element_type.is_runtime_type()) {
            error(scope, array_literal->range, "Arrays cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(determined_element_type.get_description()));

            return err();
        }

        auto elements = allocate<TypedRuntimeValue>(element_count);
        elements[0] = first_element;

        auto all_constant = true;
        for(size_t i = 1; i < element_count; i += 1) {
            expect_delayed(element, generate_expression(info, jobs, scope, context, instructions, array_literal->elements[i]));

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

            value = wrap_constant_value(wrap_static_array_constant({
                element_values
            }));
        } else {
            auto element_size = determined_element_type.get_size(info.architecture_sizes);

            auto address_register = append_allocate_local(
                context,
                instructions,
                array_literal->range,
                array_literal->elements.length * element_size,
                determined_element_type.get_alignment(info.architecture_sizes)
            );

            auto element_size_register = append_integer_constant(
                context,
                instructions,
                array_literal->range,
                info.architecture_sizes.address_size,
                element_size
            );

            auto element_address_register = address_register;
            for(size_t i = 0; i < element_count; i += 1) {
                if(
                    !coerce_to_type_write(
                        info,
                        scope,
                        context,
                        instructions,
                        array_literal->elements[i]->range,
                        elements[i].type,
                        elements[i].value,
                        determined_element_type,
                        element_address_register
                    ).status
                ) {
                    return err();
                }

                if(i != element_count - 1) {
                    element_address_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        array_literal->elements[i]->range,
                        IntegerArithmeticOperation::Operation::Add,
                        info.architecture_sizes.address_size,
                        element_address_register,
                        element_size_register
                    );
                }
            }

            value = wrap_register_value({ address_register });
        }

        return ok(TypedRuntimeValue(
            wrap_static_array_type({
                element_count,
                heapify(determined_element_type)
            }),
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

            expect_delayed(member, generate_expression(info, jobs, scope, context, instructions, struct_literal->members[i].value));

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

            value = wrap_constant_value(wrap_struct_constant({
                constant_member_values
            }));
        } else {
            value = wrap_undetermined_struct_value({
                member_values
            });
        }

        return ok(TypedRuntimeValue(
            wrap_undetermined_struct_type({
                {
                    member_count,
                    type_members
                }
            }),
            value
        ));
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, function_call->expression));

        if(expression_value.type.kind == TypeKind::FunctionTypeType || expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto call_parameter_count = function_call->parameters.length;

            auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[i]));

                call_parameters[i] = parameter_value;
            }

            FunctionTypeType function_type;
            FunctionConstant function_value;
            if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
                auto constant_value = unwrap_constant_value(expression_value.value);

                auto polymorphic_function_value = unwrap_polymorphic_function_constant(constant_value);

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

                        polymorphic_parameters[i] = {
                            call_parameters[i].type,
                            call_parameters[i].value.constant
                        };
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
                                    !constant_values_equal( call_parameter.type, call_parameter.value, job_parameter.value)
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

                auto constant_value = unwrap_constant_value(expression_value.value);

                function_value = unwrap_function_constant(constant_value);

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
                        wrap_function_type(generate_function.type) == wrap_function_type(function_type) &&
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

            auto has_return = function_type.return_type->kind != TypeKind::Void;

            RegisterRepresentation return_type_representation;
            if(has_return) {
                return_type_representation = get_type_representation(info,* function_type.return_type);
            }

            auto instruction_parameter_count = function_type.parameters.length;
            if(has_return && !return_type_representation.is_in_register) {
                instruction_parameter_count += 1;
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(instruction_parameter_count);

            size_t runtime_parameter_index = 0;
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                if(!function_value.declaration->parameters[i].is_constant) {
                    expect(parameter_register, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        instructions,
                        function_call->parameters[i]->range,
                        call_parameters[i].type,
                        call_parameters[i].value,
                        function_type.parameters[i],
                        false
                    ));

                    auto representation = get_type_representation(info, function_type.parameters[i]);

                    RegisterSize size;
                    if(representation.is_in_register) {
                        size = representation.value_size;
                    } else {
                        size = info.architecture_sizes.address_size;
                    }

                    instruction_parameters[i] = {
                        size,
                        representation.is_in_register && representation.is_float,
                        parameter_register
                    };

                    runtime_parameter_index += 1;
                }
            }

            assert(runtime_parameter_index == function_type.parameters.length);

            if(has_return && !return_type_representation.is_in_register) {
                auto parameter_register = append_allocate_local(
                    context,
                    instructions,
                    function_call->range,
                    function_type.return_type->get_size(info.architecture_sizes),
                    function_type.return_type->get_alignment(info.architecture_sizes)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.architecture_sizes.address_size,
                    false,
                    parameter_register
                };
            }

            auto address_register = append_reference_static(context, instructions, function_call->range, runtime_function);

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->range = function_call->range;
            function_call_instruction->address_register = address_register;
            function_call_instruction->parameters = { instruction_parameter_count, instruction_parameters };
            function_call_instruction->has_return = has_return && return_type_representation.is_in_register;
            function_call_instruction->calling_convention = function_type.calling_convention;

            AnyRuntimeValue value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_size = return_type_representation.value_size;
                    function_call_instruction->is_return_float = return_type_representation.is_float;
                    function_call_instruction->return_register = return_register;

                    value = wrap_register_value({ return_register });
                } else {
                    value = wrap_register_value({ instruction_parameters[instruction_parameter_count - 1].register_index });
                }
            } else {
                value = wrap_constant_value(create_void_constant());
            }

            instructions->append(function_call_instruction);

            return ok(TypedRuntimeValue(
                *function_type.return_type,
                value
            ));
        } else if(expression_value.type.kind == TypeKind::BuiltinFunction) {
            auto constant_value = unwrap_constant_value(expression_value.value);

            auto builtin_function_value = unwrap_builtin_function_constant(constant_value);

            if(builtin_function_value.name == "size_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                AnyType type;
                if(parameter_value.type.kind == TypeKind::Type) {
                    auto constant_value = unwrap_constant_value(parameter_value.value);

                    type = unwrap_type_constant(constant_value);
                } else {
                    type = parameter_value.type;
                }

                if(!type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "'%.*s'' has no size", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description()));

                    return err();
                }

                auto size = type.get_size(info.architecture_sizes);

                return ok(TypedRuntimeValue(
                    wrap_integer_type({
                        info.architecture_sizes.address_size,
                        false
                    }),
                    wrap_constant_value(wrap_integer_constant(size))
                ));
            } else if(builtin_function_value.name == "type_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                return ok(TypedRuntimeValue(
                    create_type_type(),
                    wrap_constant_value(wrap_type_constant(parameter_value.type))
                ));
            } else if(builtin_function_value.name == "memcpy"_S) {
                if(function_call->parameters.length != 3) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 3 got %zu", function_call->parameters.length);

                    return err();
                }

                auto u8_type = wrap_integer_type({ RegisterSize::Size8, false });

                auto u8_pointer_type = wrap_pointer_type({ &u8_type });

                expect_delayed(destination_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                if(destination_value.type != u8_pointer_type) {
                    error(
                        scope,
                        function_call->parameters[0]->range,
                        "Incorrect type for parameter 0. Expected '%.*s', got '%.*s'",
                        STRING_PRINTF_ARGUMENTS(u8_pointer_type.get_description()),
                        STRING_PRINTF_ARGUMENTS(destination_value.type.get_description())
                    );

                    return err();
                }

                expect_delayed(source_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[1]));

                if(source_value.type != u8_pointer_type) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 1. Expected '%.*s', got '%.*s'",
                        STRING_PRINTF_ARGUMENTS(u8_pointer_type.get_description()),
                        STRING_PRINTF_ARGUMENTS(source_value.type.get_description())
                    );

                    return err();
                }

                auto usize_type = wrap_integer_type({ info.architecture_sizes.address_size, false });

                expect_delayed(size, evaluate_constant_expression(info, jobs, scope, nullptr, function_call->parameters[2]));

                if(size.type != usize_type) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 2. Expected '%.*s', got '%.*s'",
                        STRING_PRINTF_ARGUMENTS(usize_type.get_description()),
                        STRING_PRINTF_ARGUMENTS(size.type.get_description())
                    );

                    return err();
                }

                auto size_value = unwrap_integer_constant(size.value);

                auto destination_address_register = generate_in_register_pointer_value(
                    info,
                    context,
                    instructions,
                    function_call->parameters[0]->range,
                    destination_value.value
                );

                auto source_address_register = generate_in_register_pointer_value(
                    info,
                    context,
                    instructions,
                    function_call->parameters[1]->range,
                    source_value.value
                );

                append_copy_memory(
                    context,
                    instructions,
                    function_call->range,
                    size_value,
                    source_address_register,
                    destination_address_register,
                    1
                );

                return ok(TypedRuntimeValue(
                    create_void_type(),
                    wrap_constant_value(create_void_constant())
                ));
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;

            if(pointer.type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                return err();
            }

            auto function = pointer.type->function;

            auto address_register = generate_in_register_pointer_value(info, context, instructions, function_call->expression->range, expression_value.value);

            auto parameter_count = function.parameters.length;

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

            auto has_return = function.return_type->kind != TypeKind::Void;

            RegisterRepresentation return_type_representation;
            if(has_return) {
                return_type_representation = get_type_representation(info,* function.return_type);
            }

            auto instruction_parameter_count = parameter_count;
            if(has_return && !return_type_representation.is_in_register) {
                instruction_parameter_count += 1;
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(instruction_parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[i]));

                expect(parameter_register, coerce_to_type_register(
                    info,
                    scope,
                    context,
                    instructions,
                    function_call->parameters[i]->range,
                    parameter_value.type,
                    parameter_value.value,
                    function.parameters[i],
                    false
                ));

                auto representation = get_type_representation(info, function.parameters[i]);

                RegisterSize size;
                if(representation.is_in_register) {
                    size = representation.value_size;
                } else {
                    size = info.architecture_sizes.address_size;
                }

                instruction_parameters[i] = {
                    size,
                    representation.is_in_register && representation.is_float,
                    parameter_register
                };
            }

            if(has_return && !return_type_representation.is_in_register) {
                auto parameter_register = append_allocate_local(
                    context,
                    instructions,
                    function_call->range,
                    function.return_type->get_size(info.architecture_sizes),
                    function.return_type->get_alignment(info.architecture_sizes)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.architecture_sizes.address_size,
                    false,
                    parameter_register
                };
            }

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->range = function_call->range;
            function_call_instruction->address_register = address_register;
            function_call_instruction->parameters = { parameter_count, instruction_parameters };
            function_call_instruction->has_return = has_return && return_type_representation.is_in_register;
            function_call_instruction->calling_convention = function.calling_convention;

            AnyRuntimeValue value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_size = return_type_representation.value_size;
                    function_call_instruction->is_return_float = return_type_representation.is_float;
                    function_call_instruction->return_register = return_register;

                    value = wrap_register_value({ return_register });
                } else {
                    value = wrap_register_value({ instruction_parameters[instruction_parameter_count - 1].register_index });
                }
            } else {
                value = wrap_constant_value(create_void_constant());
            }

            instructions->append(function_call_instruction);

            return ok(TypedRuntimeValue(
                *function.return_type,
                value
            ));
        } else if(expression_value.type.kind == TypeKind::Type) {
            auto constant_value = unwrap_constant_value(expression_value.value);

            auto type = unwrap_type_constant(constant_value);

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
                                if(!constant_values_equal(polymorphic_struct.parameter_types[i], parameters[i], resolve_polymorphic_struct.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    return ok(TypedRuntimeValue(
                                        create_type_type(),
                                        wrap_constant_value(wrap_type_constant(resolve_polymorphic_struct.type))
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
            instructions,
            binary_operation->range,
            binary_operation->left,
            binary_operation->right,
            binary_operation->binary_operator
        ));

        return ok(result_value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                size_t address_register;
                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = expression_value.value.constant;

                    if(expression_value.type.kind == TypeKind::FunctionTypeType) {
                        auto function = expression_value.type.function;

                        auto function_value = unwrap_function_constant(constant_value);

                        auto found = false;
                        Function* runtime_function;
                        for(size_t i = 0; i < jobs->length; i += 1) {
                            auto job = (*jobs)[i];

                            if(job.kind == JobKind::GenerateFunction) {
                                auto generate_function = job.generate_function;

                                if(
                                    wrap_function_type(generate_function.type) == wrap_function_type(function) &&
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

                        address_register = append_reference_static(
                            context,
                            instructions,
                            unary_operation->range,
                            runtime_function
                        );
                    } else if(expression_value.type.kind == TypeKind::Type) {
                        auto type = unwrap_type_constant(constant_value);

                        if(
                            !type.is_runtime_type() &&
                            type.kind != TypeKind::Void &&
                            type.kind != TypeKind::FunctionTypeType
                        ) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                            return err();
                        }

                        return ok(TypedRuntimeValue(
                            create_type_type(),
                            wrap_constant_value(wrap_type_constant(wrap_pointer_type({
                                heapify(type)
                            })))
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
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    address_register = address_value.address_register;
                } else {
                    abort();
                }

                return ok(TypedRuntimeValue(
                    wrap_pointer_type({
                        heapify(expression_value.type)
                    }),
                    wrap_register_value({ address_register })
                ));
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type.kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }

                size_t register_index;
                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto boolean_value = unwrap_boolean_constant(expression_value.value.constant);

                    return ok(TypedRuntimeValue(
                        create_boolean_type(),
                        wrap_constant_value(wrap_boolean_constant(!boolean_value))
                    ));
                } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    register_index = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    register_index = append_load_integer(
                        context,
                        instructions,
                        unary_operation->expression->range,
                        info.architecture_sizes.boolean_size,
                        address_value.address_register
                    );
                }

                auto result_register = generate_boolean_invert(info, context, instructions, unary_operation->expression->range, register_index);

                return ok(TypedRuntimeValue(
                    create_boolean_type(),
                    wrap_register_value({ result_register })
                ));
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                    auto constant_value = unwrap_constant_value(expression_value.value);

                    auto integer_value = unwrap_integer_constant(constant_value);

                    return ok(TypedRuntimeValue(
                        create_undetermined_integer_type(),
                        wrap_constant_value(wrap_integer_constant((uint64_t)-(int64_t)integer_value))
                    ));
                } else if(expression_value.type.kind == TypeKind::Integer) {
                    auto integer = expression_value.type.integer;

                    size_t register_index;
                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        auto integer_value = unwrap_integer_constant(expression_value.value.constant);

                        return ok(TypedRuntimeValue(
                            create_undetermined_integer_type(),
                            wrap_constant_value(wrap_integer_constant((uint64_t)-(int64_t)integer_value))
                        ));
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = expression_value.value.address;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            unary_operation->expression->range,
                            integer.size,
                            address_value.address_register
                        );
                    }

                    auto zero_register = append_integer_constant(context, instructions, unary_operation->range, integer.size, 0);

                    auto result_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range,
                        IntegerArithmeticOperation::Operation::Subtract,
                        integer.size,
                        zero_register,
                        register_index
                    );

                    return ok(TypedRuntimeValue(
                        wrap_integer_type(integer),
                        wrap_register_value({ result_register })
                    ));
                } else if(expression_value.type.kind == TypeKind::FloatType) {
                    auto float_type = expression_value.type.float_;

                    size_t register_index;
                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        auto float_value = unwrap_float_constant(expression_value.value.constant);

                        return ok(TypedRuntimeValue(
                            wrap_float_type(float_type),
                            wrap_constant_value(wrap_float_constant(-float_value))
                        ));
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = expression_value.value.address;

                        register_index = append_load_float(
                            context,
                            instructions,
                            unary_operation->expression->range,
                            float_type.size,
                            address_value.address_register
                        );
                    }

                    auto zero_register = append_float_constant(context, instructions, unary_operation->range, float_type.size, 0.0);

                    auto result_register = append_float_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range,
                        FloatArithmeticOperation::Operation::Subtract,
                        float_type.size,
                        zero_register,
                        register_index
                    );

                    return ok(TypedRuntimeValue(
                        wrap_float_type(float_type),
                        wrap_register_value({ result_register })
                    ));
                } else if(expression_value.type.kind == TypeKind::UndeterminedFloat) {
                    auto constant_value = unwrap_constant_value(expression_value.value);

                    auto float_value = unwrap_float_constant(constant_value);

                    return ok(TypedRuntimeValue(
                        create_undetermined_float_type(),
                        wrap_constant_value(wrap_float_constant(-float_value))
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

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, cast->expression));

        expect_delayed(target_type, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, cast->type));

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
                    wrap_constant_value(constant_cast_result.value)
                ));
            }
        }

        auto coercion_result = coerce_to_type_register(
            info,
            scope,
            context,
            instructions,
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
            register_index = coercion_result.value;
        } else if(target_type.kind == TypeKind::Integer) {
            auto target_integer = target_type.integer;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    value_register = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        integer.size,
                        address_value.address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;

                if(target_integer.size > integer.size) {
                    register_index = append_integer_extension(
                        context,
                        instructions,
                        cast->range,
                        integer.is_signed,
                        integer.size,
                        target_integer.size,
                        value_register
                    );
                } else {
                    register_index = append_integer_truncation(
                        context,
                        instructions,
                        cast->range,
                        integer.size,
                        target_integer.size,
                        value_register
                    );
                }
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    value_register = append_load_float(
                        context,
                        instructions,
                        cast->expression->range,
                        float_type.size,
                        address_value.address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_truncation(
                    context,
                    instructions,
                    cast->range,
                    float_type.size,
                    target_integer.size,
                    value_register
                );
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;
                if(target_integer.size == info.architecture_sizes.address_size && !target_integer.is_signed) {
                    has_cast = true;

                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = expression_value.value.address;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range,
                            info.architecture_sizes.address_size,
                            address_value.address_register
                        );
                    } else {
                        abort();
                    }
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
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    value_register = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        integer.size,
                        address_value.address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_from_integer(
                    context,
                    instructions,
                    cast->range,
                    integer.is_signed,
                    integer.size,
                    target_float_type.size,
                    value_register
                );
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;
                size_t value_register;
                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    value_register = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    value_register = append_load_float(
                        context,
                        instructions,
                        cast->expression->range,
                        float_type.size,
                        address_value.address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_conversion(
                    context,
                    instructions,
                    cast->range,
                    float_type.size,
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

                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = expression_value.value.register_;

                        register_index = register_value.register_index;
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                        auto address_value = expression_value.value.address;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range,
                            info.architecture_sizes.address_size,
                            address_value.address_register
                        );
                    } else {
                        abort();
                    }
                }
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;
                has_cast = true;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = expression_value.value.register_;

                    register_index = register_value.register_index;
                } else if(expression_value.value.kind == RuntimeValueKind::AddressValue) {
                    auto address_value = expression_value.value.address;

                    register_index = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        info.architecture_sizes.address_size,
                        address_value.address_register
                    );
                } else {
                    abort();
                }
            }
        } else {
            abort();
        }

        if(has_cast) {
            return ok(TypedRuntimeValue(
                target_type,
                wrap_register_value({ register_index })
            ));
        } else {
            error(scope, cast->range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, function_call->expression));

        auto call_parameter_count = function_call->parameters.length;

        auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[i]));

            call_parameters[i] = parameter_value;
        }

        if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto constant_value = unwrap_constant_value(expression_value.value);

            auto polymorphic_function_value = unwrap_polymorphic_function_constant(constant_value);

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

                    polymorphic_parameters[i] = {
                        call_parameters[i].type,
                        call_parameters[i].value.constant
                    };
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
                                !constant_values_equal( call_parameter.type, call_parameter.value, job_parameter.value)
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
                                wrap_function_type(resolve_polymorphic_function.type),
                                wrap_constant_value(wrap_function_constant(resolve_polymorphic_function.value))
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

            auto constant_value = unwrap_constant_value(expression_value.value);

            auto function_value = unwrap_function_constant(constant_value);

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
                wrap_function_type(function_type),
                wrap_constant_value(wrap_function_constant(function_value))
            ));
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, array_type->expression));

        if(!type.is_runtime_type()) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        if(array_type->index != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, nullptr, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    info.architecture_sizes.address_size,
                    false
                },
                false
            ));

            return ok(TypedRuntimeValue(
                create_type_type(),
                wrap_constant_value(wrap_type_constant(wrap_static_array_type({
                    length,
                    heapify(type)
                })))
            ));
        } else {
            return ok(TypedRuntimeValue(
                create_type_type(),
                wrap_constant_value(wrap_type_constant(wrap_array_type({
                    heapify(type)
                })))
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

            expect_delayed(type, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, parameter.type));

            if(!type.is_runtime_type()) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            parameters[i] = type;
        }

        auto is_calling_convention_specified = false;
        auto calling_convention = CallingConvention::Default;
        for(auto tag : function_type->tags) {
            if(tag.name.text == "extern"_S) {
                error(scope, tag.range, "Function types cannot be external");

                return err();
            } else if(tag.name.text == "no_mangle"_S) {
                error(scope, tag.range, "Function types cannot be no_mangle");

                return err();
            } else if(tag.name.text == "call_conv"_S) {
                if(is_calling_convention_specified) {
                    error(scope, tag.range, "Duplicate 'call_conv' tag");

                    return err();
                }

                if(tag.parameters.length != 1) {
                    error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                    return err();
                }

                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[0]));

                expect(calling_convention_name, static_array_to_string(scope, tag.parameters[0]->range, parameter.type, parameter.value));

                if(calling_convention_name == "default"_S) {
                    calling_convention = CallingConvention::Default;
                } else if(calling_convention_name == "stdcall"_S) {
                    calling_convention = CallingConvention::StdCall;
                }

                is_calling_convention_specified = true;
            } else {
                error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                return err();
            }
        }

        AnyType return_type;
        if(function_type->return_type == nullptr) {
            return_type = create_void_type();
        } else {
            expect_delayed(return_type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, function_type->return_type));

            if(!return_type_value.is_runtime_type()) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(return_type_value.get_description()));

                return err();
            }

            return_type = return_type_value;
        }

        return ok(TypedRuntimeValue(
            create_type_type(),
            wrap_constant_value(wrap_type_constant(wrap_function_type({
                { parameter_count, parameters },
                heapify(return_type),
                calling_convention
            })))
        ));
    } else {
        abort();
    }
}

static bool is_not_runtime_statement(Statement* statement) {
    return
        statement->kind == StatementKind::FunctionDeclaration ||
        statement->kind == StatementKind::ConstantDefinition ||
        statement->kind == StatementKind::StructDefinition ||
        statement->kind == StatementKind::StaticIf;
}

static_profiled_function(DelayedResult<void>, generate_statement, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    GenerationContext* context,
    List<Instruction*>* instructions,
    Statement* statement
), (
    info,
    jobs,
    scope,
    context,
    instructions,
    statement
)) {
    if(statement->kind == StatementKind::ExpressionStatement) {
        auto expression_statement = (ExpressionStatement*)statement;

        expect_delayed(value, generate_expression(info, jobs, scope, context, instructions, expression_statement->expression));

        return ok();
    } else if(statement->kind == StatementKind::VariableDeclaration) {
        auto variable_declaration = (VariableDeclaration*)statement;

        AnyType type;
        size_t address_register;

        for(auto tag : variable_declaration->tags) {
            if(tag.name.text == "extern"_S) {
                error(scope, variable_declaration->range, "Local variables cannot be external");

                return err();
            } else if(tag.name.text == "no_mangle"_S) {
                error(scope, variable_declaration->range, "Local variables cannot be no_mangle");

                return err();
            } else {
                error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                return err();
            }
        }

        if(variable_declaration->type != nullptr && variable_declaration->initializer != nullptr) {
            expect_delayed(type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, variable_declaration->type));
            
            if(!type_value.is_runtime_type()) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.get_description()));

                return err();
            }

            type = type_value;

            expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, instructions, variable_declaration->initializer));

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                type.get_size(info.architecture_sizes),
                type.get_alignment(info.architecture_sizes)
            );

            if(
                !coerce_to_type_write(
                    info,
                    scope,
                    context,
                    instructions,
                    variable_declaration->range,
                    initializer_value.type,
                    initializer_value.value,
                    type,
                    address_register
                ).status
            ) {
                return err();
            }
        } else if(variable_declaration->type != nullptr) {
            expect_delayed(type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, variable_declaration->type));

            if(!type_value.is_runtime_type()) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.get_description()));

                return err();
            }

            type = type_value;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                type.get_size(info.architecture_sizes),
                type.get_alignment(info.architecture_sizes)
            );
        } else if(variable_declaration->initializer != nullptr) {
            expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, instructions, variable_declaration->initializer));

            expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type));
            
            if(!actual_type.is_runtime_type()) {
                error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description()));

                return err();
            }

            type = actual_type;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                type.get_size(info.architecture_sizes),
                type.get_alignment(info.architecture_sizes)
            );

            if(
                !coerce_to_type_write(
                    info,
                    scope,
                    context,
                    instructions,
                    variable_declaration->range,
                    initializer_value.type,
                    initializer_value.value,
                    type,
                    address_register
                ).status
            ) {
                return err();
            }
        } else {
            abort();
        }

        if(
            !add_new_variable(
                context,
                variable_declaration->name,
                address_register,
                type
            ).status
        ) {
            return err();
        }

        return ok();
    } else if(statement->kind == StatementKind::Assignment) {
        auto assignment = (Assignment*)statement;

        expect_delayed(target, generate_expression(info, jobs, scope, context, instructions, assignment->target));

        size_t address_register;
        if(target.value.kind == RuntimeValueKind::AddressValue){
            auto address_value = target.value.address;

            address_register = address_value.address_register;
        } else {
            error(scope, assignment->target->range, "Value is not assignable");

            return err();
        }

        expect_delayed(value, generate_expression(info, jobs, scope, context, instructions, assignment->value));

        if(
            !coerce_to_type_write(
                info,
                scope,
                context,
                instructions,
                assignment->range,
                value.type,
                value.value,
                target.type,
                address_register
            ).status
        ) {
            return err();
        }

        return ok();
    } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
        auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

        expect_delayed(target, generate_expression(info, jobs, scope, context, instructions, binary_operation_assignment->target));

        size_t address_register;
        if(target.value.kind == RuntimeValueKind::AddressValue){
            auto address_value = target.value.address;

            address_register = address_value.address_register;
        } else {
            error(scope, binary_operation_assignment->target->range, "Value is not assignable");

            return err();
        }

        expect_delayed(value, generate_binary_operation(
            info,
            jobs,
            scope,
            context,
            instructions,
            binary_operation_assignment->range,
            binary_operation_assignment->target,
            binary_operation_assignment->value,
            binary_operation_assignment->binary_operator
        ));

        if(
            !coerce_to_type_write(
                info,
                scope,
                context,
                instructions,
                binary_operation_assignment->range,
                value.type,
                value.value,
                target.type,
                address_register
            ).status
        ) {
            return err();
        }

        return ok();
    } else if(statement->kind == StatementKind::IfStatement) {
        auto if_statement = (IfStatement*)statement;

        List<Jump*> end_jumps {};

        expect_delayed(condition, generate_expression(info, jobs, scope, context, instructions, if_statement->condition));

        if(condition.type.kind != TypeKind::Boolean) {
            error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

            return err();
        }

        auto condition_register = generate_in_register_boolean_value(info, context, instructions, if_statement->condition->range, condition.value);

        append_branch(context, instructions, if_statement->condition->range, condition_register, instructions->length + 2);

        auto first_jump = new Jump;
        first_jump->range = if_statement->range;

        instructions->append(first_jump);

        auto if_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.length);

        VariableScope if_variable_scope{};
        if_variable_scope.constant_scope = if_scope;

        context->variable_scope_stack.append(if_variable_scope);

        for(auto child_statement : if_statement->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void(generate_statement(info, jobs, if_scope, context, instructions, child_statement));
            }
        }

        context->variable_scope_stack.length -= 1;

        if((*instructions)[instructions->length - 1]->kind != InstructionKind::ReturnInstruction) {
            auto first_end_jump = new Jump;
            first_end_jump->range = if_statement->range;

            instructions->append(first_end_jump);

            end_jumps.append(first_end_jump);
        }

        first_jump->destination_instruction = instructions->length;

        for(size_t i = 0; i < if_statement->else_ifs.length; i += 1) {
            expect_delayed(condition, generate_expression(info, jobs, scope, context, instructions, if_statement->else_ifs[i].condition));

            if(condition.type.kind != TypeKind::Boolean) {
                error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

                return err();
            }

            auto condition_register = generate_in_register_boolean_value(
                info,
                context,
                instructions,
                if_statement->else_ifs[i].condition->range,
                condition.value
            );

            append_branch(
                context,
                instructions,
                if_statement->else_ifs[i].condition->range,
                condition_register,
                instructions->length + 2
            );

            auto jump = new Jump;
            jump->range = if_statement->else_ifs[i].condition->range;

            instructions->append(jump);

            auto else_if_scope = context->child_scopes[context->next_child_scope_index];
            context->next_child_scope_index += 1;
            assert(context->next_child_scope_index <= context->child_scopes.length);

            VariableScope else_if_variable_scope {};
            else_if_variable_scope.constant_scope = else_if_scope;

            context->variable_scope_stack.append(else_if_variable_scope);

            for(auto child_statement : if_statement->else_ifs[i].statements) {
                if(!is_not_runtime_statement(child_statement)) {
                    expect_delayed_void(generate_statement(info, jobs, else_if_scope, context, instructions, child_statement));
                }
            }

            context->variable_scope_stack.length -= 1;

            if((*instructions)[instructions->length - 1]->kind != InstructionKind::ReturnInstruction) {
                auto end_jump = new Jump;
                end_jump->range = if_statement->range;

                instructions->append(end_jump);

                end_jumps.append(end_jump);
            }

            jump->destination_instruction = instructions->length;
        }

        if(if_statement->else_statements.length != 0) {
            auto else_scope = context->child_scopes[context->next_child_scope_index];
            context->next_child_scope_index += 1;
            assert(context->next_child_scope_index <= context->child_scopes.length);

            VariableScope else_variable_scope {};
            else_variable_scope.constant_scope = else_scope;

            context->variable_scope_stack.append(else_variable_scope);

            for(auto child_statement : if_statement->else_statements) {
                if(!is_not_runtime_statement(child_statement)) {
                    expect_delayed_void(generate_statement(info, jobs, else_scope, context, instructions, child_statement));
                }
            }

            context->variable_scope_stack.length -= 1;
        }

        for(auto end_jump : end_jumps) {
            end_jump->destination_instruction = instructions->length;
        }

        return ok();
    } else if(statement->kind == StatementKind::WhileLoop) {
        auto while_loop = (WhileLoop*)statement;

        auto condition_index = instructions->length;

        expect_delayed(condition, generate_expression(info, jobs, scope, context, instructions, while_loop->condition));

        if(condition.type.kind != TypeKind::Boolean) {
            error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

            return err();
        }

        auto condition_register = generate_in_register_boolean_value(
            info,
            context,
            instructions,
            while_loop->condition->range,
            condition.value
        );

        append_branch(
            context,
            instructions,
            while_loop->condition->range,
            condition_register,
            instructions->length + 2
        );

        auto jump_out = new Jump;
        jump_out->range = while_loop->condition->range;

        instructions->append(jump_out);

        auto while_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.length);

        VariableScope while_variable_scope {};
        while_variable_scope.constant_scope = while_scope;

        context->variable_scope_stack.append(while_variable_scope);

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        for(auto child_statement : while_loop->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void(generate_statement(info, jobs, while_scope, context, instructions, child_statement));
            }
        }

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.length -= 1;

        if((*instructions)[instructions->length - 1]->kind != InstructionKind::ReturnInstruction) {
            append_jump(
                context,
                instructions,
                while_loop->range,
                condition_index
            );
        }

        jump_out->destination_instruction = instructions->length;

        for(auto jump : context->break_jumps) {
            jump->destination_instruction = instructions->length;
        }

        return ok();
    } else if(statement->kind == StatementKind::ForLoop) {
        auto for_loop = (ForLoop*)statement;

        Identifier index_name {};
        if(for_loop->has_index_name) {
            index_name = for_loop->index_name;
        } else {
            index_name.text = "it"_S;
            index_name.range = for_loop->range;
        }

        expect_delayed(from_value, generate_expression(info, jobs, scope, context, instructions, for_loop->from));

        auto index_address_register = allocate_register(context);

        auto allocate_local = new AllocateLocal;
        allocate_local->range = for_loop->range;
        allocate_local->destination_register = index_address_register;

        instructions->append(allocate_local);

        size_t condition_index;
        size_t to_register;
        Integer index_type;
        if(from_value.type.kind == TypeKind::UndeterminedInteger) {
            auto constant_value = unwrap_constant_value(from_value.value);

            auto from_integer_constant = unwrap_integer_constant(constant_value);

            auto from_regsiter = allocate_register(context);

            auto integer_constant = new IntegerConstantInstruction;
            integer_constant->range = for_loop->range;
            integer_constant->destination_register = from_regsiter;
            integer_constant->value = from_integer_constant;

            instructions->append(integer_constant);

            auto store_integer = new StoreInteger;
            store_integer->range = for_loop->range;
            store_integer->source_register = from_regsiter;
            store_integer->address_register = index_address_register;

            instructions->append(store_integer);

            condition_index = instructions->length;

            expect_delayed(to_value, generate_expression(info, jobs, scope, context, instructions, for_loop->to));

            expect(determined_index_type, coerce_to_default_type(info, scope, for_loop->range, to_value.type));

            if(determined_index_type.kind == TypeKind::Integer) {
                auto integer = determined_index_type.integer;

                allocate_local->size = register_size_to_byte_size(integer.size);
                allocate_local->alignment = register_size_to_byte_size(integer.size);

                integer_constant->size = integer.size;

                store_integer->size = integer.size;

                if(!check_undetermined_integer_to_integer_coercion(scope, for_loop->range, integer, (int64_t)from_integer_constant, false)) {
                    return err();
                }

                expect(to_register_index, coerce_to_integer_register_value(
                    scope,
                    context,
                    instructions,
                    for_loop->to->range,
                    to_value.type,
                    to_value.value,
                    integer,
                    false
                ));

                to_register = to_register_index;
                index_type = integer;
            } else {
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(determined_index_type.get_description()));

                return err();
            }
        } else {
            expect(determined_index_type, coerce_to_default_type(info, scope, for_loop->range, from_value.type));

            if(determined_index_type.kind == TypeKind::Integer) {
                auto integer = determined_index_type.integer;

                allocate_local->size = register_size_to_byte_size(integer.size);
                allocate_local->alignment = register_size_to_byte_size(integer.size);

                expect(from_register, coerce_to_integer_register_value(
                    scope,
                    context,
                    instructions,
                    for_loop->from->range,
                    from_value.type,
                    from_value.value,
                    integer,
                    false
                ));

                append_store_integer(context, instructions, for_loop->range, integer.size, from_register, index_address_register);

                condition_index = instructions->length;

                expect_delayed(to_value, generate_expression(info, jobs, scope, context, instructions, for_loop->to));

                expect(to_register_index, coerce_to_integer_register_value(
                    scope,
                    context,
                    instructions,
                    for_loop->to->range,
                    to_value.type,
                    to_value.value,
                    integer,
                    false
                ));

                to_register = to_register_index;
                index_type = integer;
            } else {
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(determined_index_type.get_description()));

                return err();
            }
        }

        auto current_index_regsiter = append_load_integer(
            context,
            instructions,
            for_loop->range,
            index_type.size,
            index_address_register
        );

        IntegerComparisonOperation::Operation operation;
        if(index_type.is_signed) {
            operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
        } else {
            operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
        }

        auto condition_register = append_integer_comparison_operation(
            context,
            instructions,
            for_loop->range,
            operation,
            index_type.size,
            current_index_regsiter,
            to_register
        );

        auto branch = new Branch;
        branch->range = for_loop->range;
        branch->condition_register = condition_register;

        instructions->append(branch);

        auto for_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.length);

        VariableScope for_variable_scope {};
        for_variable_scope.constant_scope = for_scope;

        context->variable_scope_stack.append(for_variable_scope);

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        if(!add_new_variable(context, index_name, index_address_register, wrap_integer_type(index_type)).status) {
            return err();
        }

        for(auto child_statement : for_loop->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void(generate_statement(info, jobs, for_scope, context, instructions, child_statement));
            }
        }

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.length -= 1;

        auto one_register = append_integer_constant(context, instructions, for_loop->range, index_type.size, 1);

        auto next_index_register = append_integer_arithmetic_operation(
            context,
            instructions,
            for_loop->range,
            IntegerArithmeticOperation::Operation::Add,
            index_type.size,
            current_index_regsiter,
            one_register
        );

        append_store_integer(context, instructions, for_loop->range, index_type.size, next_index_register, index_address_register);

        append_jump(context, instructions, for_loop->range, condition_index);

        for(auto jump : context->break_jumps) {
            jump->destination_instruction = instructions->length;
        }

        branch->destination_instruction = instructions->length;

        return ok();
    } else if(statement->kind == StatementKind::ReturnStatement) {
        auto return_statement = (ReturnStatement*)statement;

        auto return_instruction = new ReturnInstruction;
        return_instruction->range = return_statement->range;

        if(return_statement->value != nullptr) {
            if(context->return_type.kind == TypeKind::Void) {
                error(scope, return_statement->range, "Erroneous return value");

                return err();
            } else {
                expect_delayed(value, generate_expression(info, jobs, scope, context, instructions, return_statement->value));

                auto representation = get_type_representation(info, context->return_type);

                if(representation.is_in_register) {
                    expect(register_index, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        instructions,
                        return_statement->value->range,
                        value.type,
                        value.value,
                        context->return_type,
                        false
                    ));

                    return_instruction->value_register = register_index;
                } else {
                    if(
                        !coerce_to_type_write(
                            info,
                            scope,
                            context,
                            instructions,
                            return_statement->value->range,
                            value.type,
                            value.value,
                            context->return_type,
                            context->return_parameter_register
                        ).status
                    ) {
                        return err();
                    }
                }
            }
        } else if(context->return_type.kind != TypeKind::Void) {
            error(scope, return_statement->range, "Missing return value");

            return err();
        }

        instructions->append(return_instruction);

        return ok();
    } else if(statement->kind == StatementKind::BreakStatement) {
        auto break_statement = (BreakStatement*)statement;

        if(!context->in_breakable_scope) {
            error(scope, break_statement->range, "Not in a break-able scope");

            return err();
        }

        auto jump = new Jump;
        jump->range = break_statement->range;

        instructions->append(jump);

        context->break_jumps.append(jump);

        return ok();
    } else {
        abort();
    }
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

    GenerationContext context {};

    auto file_path = get_scope_file_path(*value.body_scope);

    auto parameter_count = type.parameters.length;
    auto ir_parameter_count = parameter_count;

    RegisterRepresentation return_representation;
    if(type.return_type->kind != TypeKind::Void) {
        return_representation = get_type_representation(info,* type.return_type);

        if(!return_representation.is_in_register) {
            ir_parameter_count += 1;
        }
    }

    auto ir_parameters = allocate<Function::Parameter>(ir_parameter_count);

    size_t parameter_index = 0;
    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
        if(!declaration->parameters[i].is_constant)  {
            auto representation = get_type_representation(info, type.parameters[parameter_index]);

            if(representation.is_in_register) {
                ir_parameters[parameter_index] = {
                    representation.value_size,
                    representation.is_float
                };
            } else {
                ir_parameters[parameter_index] = {
                    info.architecture_sizes.address_size,
                    false
                };
            }

            parameter_index += 1;
        }
    }

    assert(parameter_index == parameter_count);

    if(type.return_type->kind != TypeKind::Void && !return_representation.is_in_register) {
        ir_parameters[ir_parameter_count - 1] = {
            info.architecture_sizes.address_size,
            false
        };
    }

    function->name = declaration->name.text;
    function->range = declaration->range;
    function->path = get_scope_file_path(*value.body_scope);
    function->parameters = { ir_parameter_count, ir_parameters };
    function->has_return = type.return_type->kind != TypeKind::Void && return_representation.is_in_register;
    function->calling_convention = type.calling_convention;

    if(type.return_type->kind != TypeKind::Void && return_representation.is_in_register) {
        function->return_size = return_representation.value_size;
        function->is_return_float = return_representation.is_float;
    }

    Array<StaticConstant*> static_constants;
    if(value.is_external) {
        static_constants = {};

        function->is_external = true;
        function->is_no_mangle = true;
        function->libraries = value.external_libraries;
    } else {
        function->is_external = false;
        function->is_no_mangle = value.is_external;

        GenerationContext context {};

        context.return_type =* type.return_type;
        if(type.return_type->kind != TypeKind::Void && !return_representation.is_in_register) {
            context.return_parameter_register = ir_parameter_count - 1;
        }

        context.next_register = ir_parameter_count;

        VariableScope body_variable_scope {};
        body_variable_scope.constant_scope = value.body_scope;

        context.variable_scope_stack.append(body_variable_scope);

        context.child_scopes = value.child_scopes;

        List<Instruction*> instructions {};

        size_t parameter_index = 0;
        for(size_t i = 0; i < declaration->parameters.length; i += 1) {
            if(!declaration->parameters[i].is_constant) {
                auto parameter_type = type.parameters[i];

                auto size = parameter_type.get_size(info.architecture_sizes);
                auto alignment = parameter_type.get_alignment(info.architecture_sizes);

                auto address_register = append_allocate_local(
                    &context,
                    &instructions,
                    declaration->parameters[i].name.range,
                    size,
                    alignment
                );

                auto representation = get_type_representation(info, parameter_type);

                if(representation.is_in_register) {
                    if(representation.is_float) {
                        append_store_float(
                            &context,
                            &instructions,
                            declaration->parameters[i].name.range,
                            representation.value_size,
                            i,
                            address_register
                        );
                    } else {
                        append_store_integer(
                            &context,
                            &instructions,
                            declaration->parameters[i].name.range,
                            representation.value_size,
                            i,
                            address_register
                        );
                    }
                } else {
                    append_copy_memory(
                        &context,
                        &instructions,
                        declaration->parameters[i].name.range,
                        size,
                        i,
                        address_register,
                        alignment
                    );
                }

                add_new_variable(
                    &context,
                    declaration->parameters[i].name,
                    address_register,
                    parameter_type
                );

                parameter_index += 1;
            }
        }

        assert(parameter_index == parameter_count);

        for(auto statement : declaration->statements) {
            if(!is_not_runtime_statement(statement)) {
                expect_delayed_void(generate_statement(info, jobs, value.body_scope, &context, &instructions, statement));
            }
        }

        assert(context.next_child_scope_index == value.child_scopes.length);

        bool has_return_at_end;
        if(declaration->statements.length > 0) {
            auto last_statement = declaration->statements[declaration->statements.length - 1];

            has_return_at_end = last_statement->kind == StatementKind::ReturnStatement;
        } else {
            has_return_at_end = false;
        }

        if(!has_return_at_end) {
            if(type.return_type->kind != TypeKind::Void) {
                error(value.body_scope, declaration->range, "Function '%.*s' must end with a return", STRING_PRINTF_ARGUMENTS(declaration->name.text));

                return err();
            } else {
                auto return_instruction = new ReturnInstruction;
                return_instruction->range = declaration->range;

                instructions.append(return_instruction);
            }
        }

        function->instructions = instructions;

        static_constants = context.static_constants;
    }

    return ok(static_constants);
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
        if(tag.name.text == "extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            auto libraries = allocate<String>(tag.parameters.length);

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[i]));

                expect(library_path, static_array_to_string(scope, tag.parameters[i]->range, parameter.type, parameter.value));

                libraries[i] = library_path;
            }

            is_external = true;
            external_libraries = {
                tag.parameters.length,
                libraries
            };
        } else if(tag.name.text == "no_mangle"_S) {
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

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

        if(!type.is_runtime_type()) {
            error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        auto size = type.get_size(info.architecture_sizes);
        auto alignment = type.get_alignment(info.architecture_sizes);

        auto static_variable = new StaticVariable;
        static_variable->name = declaration->name.text;
        static_variable->is_no_mangle = true;
        static_variable->path = get_scope_file_path(*scope);
        static_variable->range = declaration->range;
        static_variable->size = size;
        static_variable->alignment = alignment;
        static_variable->is_external = true;
        static_variable->libraries = external_libraries;

        StaticVariableResult result {};
        result.static_variable = static_variable;
        result.type = type;

        return ok(result);
    } else {
        if(declaration->type != nullptr && declaration->initializer != nullptr) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

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

            auto size = type.get_size(info.architecture_sizes);
            auto alignment = type.get_alignment(info.architecture_sizes);

            auto data = allocate<uint8_t>(size);

            write_value(info, data, 0, type, coerced_initial_value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_external = false;
            static_variable->has_initial_data = true;
            static_variable->initial_data = data;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        } else if(declaration->type != nullptr) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            auto size = type.get_size(info.architecture_sizes);
            auto alignment = type.get_alignment(info.architecture_sizes);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->is_external = false;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        } else if(declaration->initializer != nullptr) {
            expect_delayed(initial_value, evaluate_constant_expression(info, jobs, scope, nullptr, declaration->initializer));

            expect(type, coerce_to_default_type(info, scope, declaration->initializer->range, initial_value.type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            auto size = type.get_size(info.architecture_sizes);
            auto alignment = type.get_alignment(info.architecture_sizes);

            auto data = allocate<uint8_t>(size);

            write_value(info, data, 0, type, initial_value.value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->path = get_scope_file_path(*scope);
            static_variable->range = declaration->range;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_no_mangle = is_no_mangle;
            static_variable->is_external = false;
            static_variable->has_initial_data = true;
            static_variable->initial_data = data;

            StaticVariableResult result {};
            result.static_variable = static_variable;
            result.type = type;

            return ok(result);
        } else {
            abort();
        }
    }
}