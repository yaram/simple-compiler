#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "timing.h"
#include "profiler.h"
#include "list.h"
#include "util.h"
#include "path.h"
#include "lexer.h"
#include "parser.h"
#include "constant.h"
#include "types.h"
#include "jobs.h"

struct Variable {
    Identifier name;

    Type *type;

    size_t address_register;
};

struct VariableScope {
    ConstantScope *constant_scope;

    List<Variable> variables;
};

struct GenerationContext {
    Type *return_type;
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

static RegisterRepresentation get_type_representation(GlobalInfo info, Type *type) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        return {
            true,
            integer->size,
            false
        };
    } else if(type->kind == TypeKind::Boolean) {
        return {
            true,
            info.default_integer_size,
            false
        };
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;
        return {
            true,
            float_type->size,
            true
        };
    } else if(type->kind == TypeKind::Pointer) {
        return {
            true,
            info.address_integer_size,
            false
        };
    } else if(
        type->kind == TypeKind::ArrayTypeType ||
        type->kind == TypeKind::StaticArray ||
        type->kind == TypeKind::StructType
    ) {
        return {
            false
        };
    } else {
        abort();
    }
}

static void write_value(GlobalInfo info, uint8_t *data, size_t offset, Type *type, ConstantValue *value);

static bool add_new_variable(GenerationContext *context, Identifier name, size_t address_register, Type *type) {
    auto variable_scope = &(context->variable_scope_stack[context->variable_scope_stack.count - 1]);

    for(auto variable : variable_scope->variables) {
        if(strcmp(variable.name.text, name.text) == 0) {
            error(variable_scope->constant_scope, name.range, "Duplicate variable name %s", name.text);
            error(variable_scope->constant_scope, variable.name.range, "Original declared here");

            return false;
        }
    }

    append(&variable_scope->variables, Variable {
        name,
        type,
        address_register
    });

    return true;
}

enum struct RuntimeValueKind {
    RuntimeConstantValue,
    RegisterValue,
    AddressValue,
    UndeterminedStructValue
};

struct RuntimeValue {
    RuntimeValueKind kind;
};

struct RuntimeConstantValue : RuntimeValue {
    ConstantValue *value;

    RuntimeConstantValue(ConstantValue *value) : RuntimeValue { RuntimeValueKind::RuntimeConstantValue }, value { value } {}
};

template <typename T>
static T *extract_constant_value_internal(RuntimeValue *value, ConstantValueKind kind) {
    assert(value->kind == RuntimeValueKind::RuntimeConstantValue);

    auto runtime_constant_value = (RuntimeConstantValue*)value;

    return extract_constant_value_internal<T>(runtime_constant_value->value, kind);
}

struct RegisterValue : RuntimeValue {
    size_t register_index;

    RegisterValue(size_t register_index) : RuntimeValue { RuntimeValueKind::RegisterValue }, register_index { register_index } {}
};

struct AddressValue : RuntimeValue {
    size_t address_register;

    AddressValue(size_t address_register) : RuntimeValue { RuntimeValueKind::AddressValue }, address_register { address_register } {}
};

struct UndeterminedStructValue : RuntimeValue {
    RuntimeValue **members;

    UndeterminedStructValue(RuntimeValue **members) : RuntimeValue { RuntimeValueKind::UndeterminedStructValue }, members { members } {}
};

struct TypedRuntimeValue {
    Type *type;

    RuntimeValue *value;
};

static size_t allocate_register(GenerationContext *context) {
    auto index = context->next_register;

    context->next_register += 1;

    return index;
}

static void write_integer(uint8_t *buffer, size_t offset, RegisterSize size, uint64_t value) {
    buffer[offset] = value;

    if(size >= RegisterSize::Size16) {
        buffer[offset + 1] = (value >> 8);
    } else {
        return;
    }

    if(size >= RegisterSize::Size32) {
        buffer[offset + 2] = (value >> 16);
        buffer[offset + 3] = (value >> 24);
    } else {
        return;
    }

    if(size == RegisterSize::Size64) {
        buffer[offset + 4] = (value >> 32);
        buffer[offset + 5] = (value >> 40);
        buffer[offset + 6] = (value >> 48);
        buffer[offset + 7] = (value >> 56);
    } else {
        abort();
    }
}

static void write_struct(GlobalInfo info, uint8_t *data, size_t offset, StructType struct_type, ConstantValue **member_values) {
    for(size_t i = 0; i < struct_type.members.count; i += 1) {
        write_value(
            info,
            data,
            offset + get_struct_member_offset(info, struct_type, i),
            struct_type.members[i].type,
            member_values[i]
        );
    }
}

static void write_static_array(GlobalInfo info, uint8_t *data, size_t offset, Type *element_type, Array<ConstantValue*> elements) {
    auto element_size = get_type_size(info, element_type);

    for(size_t i = 0; i < elements.count; i += 1) {
        write_value(
            info,
            data,
            offset + i * element_size,
            element_type,
            elements[i]
        );
    }
}

static void write_value(GlobalInfo info, uint8_t *data, size_t offset, Type *type, ConstantValue *value) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        auto integer_value = (IntegerConstant*)value;

        write_integer(data, offset, integer->size, integer_value->value);
    } else if(type->kind == TypeKind::Boolean) {
        auto boolean_value = (BooleanConstant*)value;

        write_integer(data, offset, info.default_integer_size, boolean_value->value);
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;
        auto float_value = (FloatConstant*)value;

        uint64_t integer_value;
        switch(float_type->size) {
            case RegisterSize::Size32: {
                auto value = (float)float_value->value;

                integer_value = (uint64_t)*(uint32_t*)&value;
            } break;

            case RegisterSize::Size64: {
                integer_value = (uint64_t)*(uint64_t*)&float_value->value;
            } break;

            default: {
                abort();
            } break;
        }

        write_integer(data, offset, float_type->size, integer_value);
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer_value = (PointerConstant*)value;

        write_integer(data, offset, info.address_integer_size, pointer_value->value);
    } else if(type->kind == TypeKind::ArrayTypeType) {
        auto array_value = (ArrayConstant*)value;

        write_integer(
            data,
            offset,
            info.address_integer_size,
            array_value->pointer
        );

        write_integer(
            data,
            offset + register_size_to_byte_size(info.address_integer_size),
            info.address_integer_size,
            array_value->length
        );
    } else if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        auto static_array_value = (StaticArrayConstant*)value;

        write_static_array(
            info,
            data,
            offset,
            static_array->element_type,
            {
                static_array->length,
                static_array_value->elements
            }
        );
    } else if(type->kind == TypeKind::StructType) {
        auto struct_type = (StructType*)type;
        auto struct_value = (StructConstant*)value;

        write_struct(info, data, offset, *struct_type, struct_value->members);
    } else {
        abort();
    }
}

static StaticConstant *register_static_array_constant(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    FileRange range,
    Type* element_type,
    Array<ConstantValue*> elements
) {
    auto data_length = get_type_size(info, element_type) * elements.count;
    auto data = allocate<uint8_t>(data_length);

    write_static_array(info, data, 0, element_type, elements);

    auto constant = new StaticConstant;
    constant->name = "array_constant";
    constant->is_no_mangle = false;
    constant->range = range;
    constant->scope = scope;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_type_alignment(info, element_type);

    append(&context->static_constants, constant);

    return constant;
}

static StaticConstant *register_struct_constant(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    FileRange range,
    StructType struct_type,
    ConstantValue **members
) {
    auto data_length = get_struct_size(info, struct_type);
    auto data = allocate<uint8_t>(data_length);

    write_struct(info, data, 0, struct_type, members);

    auto constant = new StaticConstant;
    constant->name = "struct_constant";
    constant->is_no_mangle = false;
    constant->range = range;
    constant->scope = scope;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_struct_alignment(info, struct_type);

    append(&context->static_constants, constant);

    return constant;
}

static size_t append_integer_arithmetic_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)integer_arithmetic_operation);

    return destination_register;
}

static size_t append_integer_comparison_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)integer_comparison_operation);

    return destination_register;
}

static size_t append_integer_upcast(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto integer_upcast = new IntegerUpcast;
    integer_upcast->range = range;
    integer_upcast->is_signed = is_signed;
    integer_upcast->source_size = source_size;
    integer_upcast->source_register = source_register;
    integer_upcast->destination_size = destination_size;
    integer_upcast->destination_register = destination_register;

    append(instructions, (Instruction*)integer_upcast);

    return destination_register;
}

static size_t append_integer_constant(GenerationContext *context, List<Instruction*> *instructions, FileRange range, RegisterSize size, uint64_t value) {
    auto destination_register = allocate_register(context);

    auto constant = new IntegerConstantInstruction;
    constant->range = range;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    append(instructions, (Instruction*)constant);

    return destination_register;
}

static size_t append_float_arithmetic_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)float_arithmetic_operation);

    return destination_register;
}

static size_t append_float_comparison_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)float_comparison_operation);

    return destination_register;
}

static size_t append_float_conversion(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)float_conversion);

    return destination_register;
}

static size_t append_float_truncation(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)float_truncation);

    return destination_register;
}

static size_t append_float_from_integer(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)float_from_integer);

    return destination_register;
}

static size_t append_float_constant(GenerationContext *context, List<Instruction*> *instructions, FileRange range, RegisterSize size, double value) {
    auto destination_register = allocate_register(context);

    auto constant = new FloatConstantInstruction;
    constant->range = range;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    append(instructions, (Instruction*)constant);

    return destination_register;
}

static size_t append_reference_static(GenerationContext *context, List<Instruction*> *instructions, FileRange range, RuntimeStatic *runtime_static) {
    auto destination_register = allocate_register(context);

    auto reference_static = new ReferenceStatic;
    reference_static->range = range;
    reference_static->runtime_static = runtime_static;
    reference_static->destination_register = destination_register;

    append(instructions, (Instruction*)reference_static);

    return destination_register;
}

static size_t append_allocate_local(GenerationContext *context, List<Instruction*> *instructions, FileRange range, size_t size, size_t alignment) {
    auto destination_register = allocate_register(context);

    auto allocate_local = new AllocateLocal;
    allocate_local->range = range;
    allocate_local->size = size;
    allocate_local->alignment = alignment;
    allocate_local->destination_register = destination_register;

    append(instructions, (Instruction*)allocate_local);

    return destination_register;
}

static void append_branch(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    size_t condition_register,
    size_t destination_instruction
) {
    auto branch = new Branch;
    branch->range = range;
    branch->condition_register = condition_register;
    branch->destination_instruction = destination_instruction;

    append(instructions, (Instruction*)branch);
}

static void append_jump(GenerationContext *context, List<Instruction*> *instructions, FileRange range, size_t destination_instruction) {
    auto jump = new Jump;
    jump->range = range;
    jump->destination_instruction = destination_instruction;

    append(instructions, (Instruction*)jump);
}

static void append_copy_memory(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    size_t length_register,
    size_t source_address_register,
    size_t destination_address_register,
    size_t alignment
) {
    auto copy_memory = new CopyMemory;
    copy_memory->range = range;
    copy_memory->length_register = length_register;
    copy_memory->source_address_register = source_address_register;
    copy_memory->destination_address_register = destination_address_register;
    copy_memory->alignment = alignment;

    append(instructions, (Instruction*)copy_memory);
}

static void generate_constant_size_copy(
    GlobalInfo info,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    size_t length,
    size_t source_address_register,
    size_t destination_address_register,
    size_t alignment
) {
    auto length_register = append_integer_constant(context, instructions, range, info.address_integer_size, length);

    append_copy_memory(context, instructions, range, length_register, source_address_register, destination_address_register, alignment);
}

static size_t append_load_integer(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)load_integer);

    return destination_register;
}

static void append_store_integer(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)store_integer);
}

static size_t append_load_float(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)load_integer);

    return destination_register;
}

static void append_store_float(
    GenerationContext *context,
    List<Instruction*> *instructions,
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

    append(instructions, (Instruction*)store_integer);
}

static size_t generate_address_offset(
    GlobalInfo info,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    size_t address_register,
    size_t offset
) {
    auto offset_register = append_integer_constant(
        context,
        instructions,
        range,
        info.address_integer_size,
        offset
    );

    auto final_address_register = append_integer_arithmetic_operation(
        context,
        instructions,
        range,
        IntegerArithmeticOperation::Operation::Add,
        info.address_integer_size,
        address_register,
        offset_register
    );

    return final_address_register;
}

static size_t generate_boolean_invert(GlobalInfo info, GenerationContext *context, List<Instruction*> *instructions, FileRange range, size_t value_register) {
    auto local_register = append_allocate_local(
        context,
        instructions,
        range,
        register_size_to_byte_size(info.default_integer_size),
        register_size_to_byte_size(info.default_integer_size)
    );

    append_branch(context, instructions, range, value_register, instructions->count + 4);

    auto true_register = append_integer_constant(context, instructions, range, info.default_integer_size, 1);

    append_store_integer(context, instructions, range, info.default_integer_size, true_register, local_register);

    append_jump(context, instructions, range, instructions->count + 3);

    auto false_register = append_integer_constant(context, instructions, range, info.default_integer_size, 0);

    append_store_integer(context, instructions, range, info.default_integer_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range, info.default_integer_size, local_register);

    return result_register;
}

static size_t generate_in_register_integer_value(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Integer type,
    RuntimeValue *value
) {
    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto integer_value = extract_constant_value(IntegerConstant, value);

        return append_integer_constant(context, instructions, range, type.size, integer_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range, type.size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_boolean_value(GlobalInfo info, GenerationContext *context, List<Instruction*> *instructions, FileRange range, RuntimeValue *value) {
    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto boolean_value = extract_constant_value(BooleanConstant, value);

        return append_integer_constant(context, instructions, range, info.default_integer_size, boolean_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range, info.default_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_pointer_value(GlobalInfo info, GenerationContext *context, List<Instruction*> *instructions, FileRange range, RuntimeValue *value) {
    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto pointer_value = extract_constant_value(PointerConstant, value);

        return append_integer_constant(context, instructions, range, info.address_integer_size, pointer_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range, info.address_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static Result<size_t> coerce_to_integer_register_value(
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Integer *target_type,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;

        if(integer->size == target_type->size && integer->is_signed == target_type->is_signed) {
            auto register_index = generate_in_register_integer_value(context, instructions, range, *target_type, value);

            return ok(register_index);
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = extract_constant_value(IntegerConstant, value);

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value->value, probing)) {
            return err;
        }

        auto regsiter_index = append_integer_constant(context, instructions, range, target_type->size, integer_value->value);

        return ok(regsiter_index);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return err;
}

static Result<size_t> coerce_to_float_register_value(
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    FloatType target_type,
    bool probing
) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = extract_constant_value(IntegerConstant, value);

        auto register_index = append_float_constant(context, instructions, range, target_type.size, (double)integer_value->value);

        return ok(register_index);
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;

        if(target_type.size == float_type->size) {
            size_t register_index;
            if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                auto float_value = extract_constant_value(FloatConstant, value);

                register_index = append_float_constant(context, instructions, range, float_type->size, float_value->value);
            } else if(value->kind == RuntimeValueKind::RegisterValue) {
                auto regsiter_value = (RegisterValue*)value;

                register_index = regsiter_value->register_index;
            } else if(value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)value;

                register_index = append_load_float(context, instructions, range, float_type->size, address_value->address_register);
            } else {
                abort();
            }

            return ok(register_index);
        }
    } else if(type->kind == TypeKind::UndeterminedFloat) {
        auto float_value = extract_constant_value(FloatConstant, value);

        auto register_index = append_float_constant(context, instructions, range, target_type.size, float_value->value);

        return ok(register_index);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return err;
}

static Result<size_t> coerce_to_pointer_register_value(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Pointer target_type,
    bool probing
) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = extract_constant_value(IntegerConstant, value);

        auto register_index = append_integer_constant(
            context,
            instructions,
            range,
            info.address_integer_size,
            integer_value->value
        );

        return ok(register_index);
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)type;

        if(types_equal(pointer->type, target_type.type)) {
            auto register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

            return ok(register_index);
        }
    }

    if (!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return err;
}

static bool coerce_to_type_write(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Type *target_type,
    size_t address_register
);

static Result<size_t> coerce_to_type_register(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Type *target_type,
    bool probing
) {
    if(target_type->kind == TypeKind::Integer) {
        auto integer = (Integer*)target_type;

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
    } else if(target_type->kind == TypeKind::Boolean) {
        if(type->kind == TypeKind::Boolean) {
            auto register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

            return ok(register_index);
        }
    } else if(target_type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)target_type;

        expect(register_index, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            range,
            type,
            value,
            *float_type,
            probing
        ));

        return ok(register_index);
    } else if(target_type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)target_type;

        expect(register_index, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            instructions,
            range,
            type,
            value,
            *pointer,
            probing
        ));

        return ok(register_index);
    } else if(target_type->kind == TypeKind::ArrayTypeType) {
        auto target_array = (ArrayTypeType*)target_type;

        if(type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)type;
            if(types_equal(target_array->element_type, array_type->element_type)) {
                size_t register_index;
                if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto regsiter_value = (RegisterValue*)value;

                    register_index = regsiter_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    register_index = address_value->address_register;
                } else {
                    abort();
                }

                return ok(register_index);
            }
        } else if(type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)type;

            if(types_equal(target_array->element_type, static_array->element_type)) {
                size_t pointer_register;
                if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, value);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto regsiter_value = (RegisterValue*)value;

                    pointer_register = regsiter_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    pointer_register = address_value->address_register;
                } else {
                    abort();
                }

                auto address_register = append_allocate_local(
                    context,
                    instructions,
                    range,
                    2 * register_size_to_byte_size(info.address_integer_size),
                    register_size_to_byte_size(info.address_integer_size)
                );

                append_store_integer(context, instructions, range, info.address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range, info.address_integer_size, static_array->length);

                append_store_integer(context, instructions, range, info.address_integer_size, length_register, length_address_register);

                return ok(address_register);
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;

            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = (UndeterminedStructValue*)value;
                assert(value->kind == RuntimeValueKind::UndeterminedStructValue);

                auto pointer_result = coerce_to_pointer_register_value(
                    info,
                    scope,
                    context,
                    instructions,
                    range,
                    undetermined_struct->members[0].type,
                    undetermined_struct_value->members[0],
                    {
                        target_array->element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        instructions,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        heapify<Integer>({
                            info.address_integer_size,
                            false
                        }),
                        true
                    );

                    if(length_result.status) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range,
                            2 * register_size_to_byte_size(info.address_integer_size),
                            register_size_to_byte_size(info.address_integer_size)
                        );

                        append_store_integer(context, instructions, range, info.address_integer_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(info.address_integer_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range,
                            info.address_integer_size,
                            length_result.value,
                            length_address_register
                        );

                        return ok(address_register);
                    }
                }
            }
        }
    } else if(target_type->kind == TypeKind::StaticArray) {
        auto target_static_array = (StaticArray*)target_type;

        if(type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)type;

            if(types_equal(target_static_array->element_type, static_array->element_type) && target_static_array->length == static_array->length) {
                size_t register_index;
                if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)value;

                    register_index = register_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    register_index = address_value->address_register;
                } else {
                    abort();
                }

                return ok(register_index);
            }
        }
    } else if(target_type->kind == TypeKind::StructType) {
        auto target_struct_type = (StructType*)target_type;

        if(type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)type;

            if(target_struct_type->definition == struct_type->definition && target_struct_type->members.count == struct_type->members.count) {
                auto same_members = true;
                for(size_t i = 0; i < struct_type->members.count; i += 1) {
                    if(
                        strcmp(target_struct_type->members[i].name, struct_type->members[i].name) != 0 ||
                        !types_equal(target_struct_type->members[i].type, struct_type->members[i].type)
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    size_t register_index;
                    if(value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)value;

                        register_index = register_value->register_index;
                    } else if(value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)value;

                        register_index = address_value->address_register;
                    } else {
                        abort();
                    }

                    return ok(register_index);
                }
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;

            auto undetermined_struct_value = (UndeterminedStructValue*)value;
            assert(value->kind == RuntimeValueKind::UndeterminedStructValue);

            if(target_struct_type->definition->is_union) {
                if(undetermined_struct->members.count == 1) {
                    for(size_t i = 0; i < target_struct_type->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[0].name) == 0) {
                            auto address_register = append_allocate_local(
                                context,
                                instructions,
                                range,
                                get_struct_size(info, *target_struct_type),
                                get_struct_alignment(info, *target_struct_type)
                            );

                            if(coerce_to_type_write(
                                info,
                                scope,
                                context,
                                instructions,
                                range,
                                undetermined_struct->members[0].type,
                                undetermined_struct_value->members[0],
                                target_struct_type->members[i].type,
                                address_register
                            )) {
                                return ok(address_register);
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                if(target_struct_type->members.count == undetermined_struct->members.count) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[i].name) != 0) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range,
                            get_struct_size(info, *target_struct_type),
                            get_struct_alignment(info, *target_struct_type)
                        );

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                            auto member_address_register = generate_address_offset(
                                info,
                                context,
                                instructions,
                                range,
                                address_register,
                                get_struct_member_offset(info, *target_struct_type, i)
                            );

                            if(!coerce_to_type_write(
                                info,
                                scope,
                                context,
                                instructions,
                                range,
                                undetermined_struct->members[i].type,
                                undetermined_struct_value->members[i],
                                target_struct_type->members[i].type,
                                member_address_register
                            )) {
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
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return err;
}

static bool coerce_to_type_write(
    GlobalInfo info,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Type *target_type,
    size_t address_register
) {
    if(target_type->kind == TypeKind::Integer) {
        auto integer_type = (Integer*)target_type;

        expect(register_index, coerce_to_integer_register_value(scope, context, instructions, range, type, value, integer_type, false));

        append_store_integer(context, instructions, range, integer_type->size, register_index, address_register);

        return true;
    } else if(target_type->kind == TypeKind::Boolean && type->kind == TypeKind::Boolean) {
        size_t register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

        append_store_integer(context, instructions, range, info.default_integer_size, register_index, address_register);

        return true;
    } else if(target_type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)target_type;

        expect(register_index, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            range,
            type,
            value,
            *float_type,
            false
        ));

        append_store_float(context, instructions, range, float_type->size, register_index, address_register);

        return true;
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = extract_constant_value(IntegerConstant, value);

            auto register_index = append_integer_constant(
                context,
                instructions,
                range,
                info.address_integer_size,
                integer_value->value
            );

            append_store_integer(
                context,
                instructions,
                range,
                info.address_integer_size,
                register_index,
                address_register
            );

            return true;
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            if(types_equal(target_pointer->type, pointer->type)) {
                size_t register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

                append_store_integer(context, instructions, range, info.address_integer_size, register_index, address_register);

                return true;
            }
        }
    } else if(target_type->kind == TypeKind::ArrayTypeType) {
        auto target_array = (ArrayTypeType*)target_type;

        if(type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)type;

            if(types_equal(target_array->element_type, array_type->element_type)) {
                size_t source_address_register;
                if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto array_value = extract_constant_value(ArrayConstant, value);

                    auto pointer_register = append_integer_constant(
                        context,
                        instructions,
                        range,
                        info.address_integer_size,
                        array_value->pointer
                    );

                    append_store_integer(context, instructions, range, info.address_integer_size, pointer_register, address_register);

                    auto length_register = append_integer_constant(
                        context,
                        instructions,
                        range,
                        info.address_integer_size,
                        array_value->length
                    );

                    auto length_address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        range,
                        address_register,
                        register_size_to_byte_size(info.address_integer_size)
                    );

                    append_store_integer(context, instructions, range, info.address_integer_size, length_register, length_address_register);

                    return true;
                } else if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)value;

                    source_address_register = register_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    source_address_register = address_value->address_register;
                } else {
                    abort();
                }

                generate_constant_size_copy(
                    info,
                    context,
                    instructions,
                    range,
                    2 * register_size_to_byte_size(info.address_integer_size),
                    source_address_register,
                    address_register,
                    register_size_to_byte_size(info.address_integer_size)
                );

                return true;
            }
        } else if(type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)type;
            if(types_equal(target_array->element_type, static_array->element_type)) {
                size_t pointer_register;
                if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, value);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)value;

                    pointer_register = register_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    pointer_register = address_value->address_register;
                } else {
                    abort();
                }

                append_store_integer(context, instructions, range, info.address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range, info.address_integer_size, static_array->length);

                append_store_integer(context, instructions, range, info.address_integer_size, pointer_register, length_address_register);

                return true;
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;

            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = (UndeterminedStructValue*)value;
                assert(value->kind == RuntimeValueKind::UndeterminedStructValue);

                auto pointer_result = coerce_to_pointer_register_value(
                    info,
                    scope,
                    context,
                    instructions,
                    range,
                    undetermined_struct->members[0].type,
                    undetermined_struct_value->members[0],
                    {
                        target_array->element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        instructions,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        heapify<Integer>({
                            info.address_integer_size,
                            false
                        }),
                        true
                    );

                    if(length_result.status) {
                        append_store_integer(context, instructions, range, info.address_integer_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(info.address_integer_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range,
                            info.address_integer_size,
                            length_result.value,
                            length_address_register
                        );

                        return true;
                    }
                }
            }
        }
    } else if(target_type->kind == TypeKind::StaticArray) {
        auto target_static_array = (StaticArray*)target_type;

        if(type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)type;

            if(types_equal(target_static_array->element_type, static_array->element_type) && target_static_array->length == static_array->length) {
                size_t source_address_register;
                if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, value);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        range,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    source_address_register = append_reference_static(context, instructions, range, static_constant);
                } else if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)value;

                    source_address_register = register_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    source_address_register = address_value->address_register;
                } else {
                    abort();
                }

                generate_constant_size_copy(
                    info,
                    context,
                    instructions,
                    range,
                    static_array->length * get_type_size(info, static_array->element_type),
                    source_address_register,
                    address_register,
                    get_type_size(info, static_array->element_type)
                );

                return true;
            }
        }
    } else if(target_type->kind == TypeKind::StructType) {
        auto target_struct_type = (StructType*)target_type;

        if(type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)type;

            if(target_struct_type->definition == struct_type->definition && target_struct_type->members.count == struct_type->members.count) {
                auto same_members = true;
                for(size_t i = 0; i < struct_type->members.count; i += 1) {
                    if(
                        strcmp(target_struct_type->members[i].name, struct_type->members[i].name) != 0 ||
                        !types_equal(target_struct_type->members[i].type, struct_type->members[i].type)
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    size_t source_address_register;
                    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto struct_value = extract_constant_value(StructConstant, value);

                        auto static_constant = register_struct_constant(
                            info,
                            scope,
                            context,
                            range,
                            *struct_type,
                            struct_value->members
                        );

                        source_address_register = append_reference_static(context, instructions, range, static_constant);
                    } else if(value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)value;

                        source_address_register = register_value->register_index;
                    } else if(value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)value;

                        source_address_register = address_value->address_register;
                    } else {
                        abort();
                    }

                    generate_constant_size_copy(
                        info,
                        context,
                        instructions,
                        range,
                        get_struct_size(info, *struct_type),
                        source_address_register,
                        address_register,
                        get_struct_alignment(info, *struct_type)
                    );

                    return true;
                }
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;

            if(target_struct_type->definition->is_union) {
                if(undetermined_struct->members.count == 1) {
                    for(size_t i = 0; i < target_struct_type->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[0].name) == 0) {
                            RuntimeValue *variant_value;
                            if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                                auto struct_value = extract_constant_value(StructConstant, value);

                                variant_value = new RuntimeConstantValue {
                                    struct_value->members[0]
                                };
                            } else if(value->kind == RuntimeValueKind::UndeterminedStructValue) {
                                auto undetermined_struct_value = (UndeterminedStructValue*)value;

                                variant_value = undetermined_struct_value->members[0];
                            } else {
                                abort();
                            }

                            if(coerce_to_type_write(
                                info,
                                scope,
                                context,
                                instructions,
                                range,
                                undetermined_struct->members[0].type,
                                variant_value,
                                target_struct_type->members[i].type,
                                address_register
                            )) {
                                return true;
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                if(target_struct_type->members.count == undetermined_struct->members.count) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[i].name) != 0) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                            RuntimeValue *member_value;
                            if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                                auto struct_value = extract_constant_value(StructConstant, value);

                                member_value = new RuntimeConstantValue {
                                    struct_value->members[i]
                                };
                            } else if(value->kind == RuntimeValueKind::UndeterminedStructValue) {
                                auto undetermined_struct_value = (UndeterminedStructValue*)value;

                                member_value = undetermined_struct_value->members[i];
                            } else {
                                abort();
                            }

                            auto member_address_register = generate_address_offset(
                                info,
                                context,
                                instructions,
                                range,
                                address_register,
                                get_struct_member_offset(info, *target_struct_type, i)
                            );

                            if(!coerce_to_type_write(
                                info,
                                scope,
                                context,
                                instructions,
                                range,
                                undetermined_struct->members[i].type,
                                member_value,
                                target_struct_type->members[i].type,
                                member_address_register
                            )) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return true;
                        }
                    }
                }
            }
        }
    } else {
        abort();
    }

    error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));

    return err;
}

static DelayedResult<TypedRuntimeValue> generate_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
);

static DelayedResult<Type*> evaluate_type_expression_runtime(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
) {
    expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, expression));

    if(expression_value.type->kind == TypeKind::TypeType) {
        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

        return has(type);
    } else {
        error(scope, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return err;
    }
}

static DelayedResult<TypedRuntimeValue> generate_binary_operation(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Expression *left_expression,
    Expression *right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect_delayed(left, generate_expression(info, jobs, scope, context, instructions, left_expression));

    expect_delayed(right, generate_expression(info, jobs, scope, context, instructions, right_expression));

    if(left.value->kind == RuntimeValueKind::RuntimeConstantValue && right.value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto left_value = ((RuntimeConstantValue*)left.value)->value;

        auto right_value = ((RuntimeConstantValue*)right.value)->value;

        expect(constant, evaluate_constant_binary_operation(
            info,
            scope,
            range,
            binary_operator,
            left_expression->range,
            left.type,
            left_value,
            right_expression->range,
            right.type,
            right_value
        ));

        return has({
            constant.type,
            new RuntimeConstantValue {
                constant.value
            }
        });
    }

    expect(type, determine_binary_operation_type(scope, range, left.type, right.type));

    expect(determined_type, coerce_to_default_type(info, scope, range, type));

    if(determined_type->kind == TypeKind::Integer) {
        auto integer = (Integer*)determined_type;

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
                if(integer->is_signed) {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::SignedDivide;
                } else {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedDivide;
                }
            } break;

            case BinaryOperation::Operator::Modulo: {
                if(integer->is_signed) {
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
        Type *result_type;
        if(is_arithmetic) {
            result_register = append_integer_arithmetic_operation(
                context,
                instructions,
                range,
                arithmetic_operation,
                integer->size,
                left_register,
                right_register
            );

            result_type = integer;
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
                    if(integer->is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedLessThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedLessThan;
                    }
                } break;

                case BinaryOperation::Operator::GreaterThan: {
                    if(integer->is_signed) {
                        comparison_operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
                    } else {
                        comparison_operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
                    }
                } break;

                default: {
                    error(scope, range, "Cannot perform that operation on integers");

                    return err;
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                instructions,
                range,
                comparison_operation,
                integer->size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }

            result_type = &boolean_singleton;
        }

        return has({
            result_type,
            new RegisterValue {
                result_register
            }
        });
    } else if(determined_type->kind == TypeKind::Boolean) {
        if(left.type->kind != TypeKind::Boolean) {
            error(scope, left_expression->range, "Expected 'bool', got '%s'", type_description(left.type));

            return err;
        }

        auto left_register = generate_in_register_boolean_value(info, context, instructions, left_expression->range, left.value);

        if(right.type->kind != TypeKind::Boolean) {
            error(scope, right_expression->range, "Expected 'bool', got '%s'", type_description(right.type));

            return err;
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
                info.default_integer_size,
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

                    return err;
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                instructions,
                range,
                comparison_operation,
                info.default_integer_size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }
        }

        return has({
            &boolean_singleton,
            new RegisterValue {
                result_register
            }
        });
    } else if(determined_type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)determined_type;

        expect(left_register, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            left_expression->range,
            left.type,
            left.value,
            *float_type,
            false
        ));

        expect(right_register, coerce_to_float_register_value(
            scope,
            context,
            instructions,
            right_expression->range,
            right.type,
            right.value,
            *float_type,
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
        Type *result_type;
        if(is_arithmetic) {
            result_register = append_float_arithmetic_operation(
                context,
                instructions,
                range,
                arithmetic_operation,
                float_type->size,
                left_register,
                right_register
            );

            result_type = float_type;
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

                    return err;
                } break;
            }

            result_register = append_float_comparison_operation(
                context,
                instructions,
                range,
                comparison_operation,
                float_type->size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }

            result_type = &boolean_singleton;
        }

        return has({
            result_type,
            new RegisterValue {
                result_register
            }
        });
    } else if(determined_type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)determined_type;

        expect(left_register, coerce_to_pointer_register_value(
            info,
            scope,
            context,
            instructions,
            left_expression->range,
            left.type,
            left.value,
            *pointer,
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
            *pointer,
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
                error(scope, range, "Cannot perform that operation on '%s'", type_description(pointer));

                return err;
            } break;
        }

        auto result_register = append_integer_comparison_operation(
            context,
            instructions,
            range,
            comparison_operation,
            info.address_integer_size,
            left_register,
            right_register
        );

        if(invert) {
            result_register = generate_boolean_invert(info, context, instructions, range, result_register);
        }

        return has({
            &boolean_singleton,
            new RegisterValue {
                result_register
            }
        });
    } else {
        abort();
    }
}

struct RuntimeDeclarationSearchValue {
    bool found;

    Type *type;
    RuntimeValue *value;
};

static DelayedResult<RuntimeDeclarationSearchValue> search_for_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    const char *name,
    ConstantScope *name_scope,
    FileRange name_range,
    Array<Statement*> statements,
    bool external
) {
    for(auto statement : statements) {
        bool matching;
        if(external) {
            matching = match_public_declaration(statement, name);
        } else {
            matching = match_declaration(statement, name);
        }

        if(matching) {
            expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, statement));

            return has({
                true,
                value.type,
                new RuntimeConstantValue {
                    value.value
                }
            });
        } else if(statement->kind == StatementKind::UsingStatement) {
            if(!external) {
                auto using_statement = (UsingStatement*)statement;

                expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, nullptr, using_statement->module));

                if(expression_value.type->kind != TypeKind::FileModule) {
                    error(scope, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                    return err;
                }

                auto file_module = (FileModuleConstant*)expression_value.value;
                assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

                expect_delayed(search_value, search_for_declaration(
                    info,
                    jobs,
                    file_module->scope,
                    context,
                    instructions,
                    name,
                    name_scope,
                    name_range,
                    file_module->scope->statements,
                    true
                ));

                if(search_value.found) {
                    return has({
                        true,
                        search_value.type,
                        search_value.value
                    });
                }
            }
        } else if(statement->kind == StatementKind::StaticIf) {
            auto static_if = (StaticIf*)statement;

            auto found = false;
            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolveStaticIf) {
                    auto resolve_static_if = (ResolveStaticIf*)job;

                    if(
                        resolve_static_if->static_if == static_if &&
                        resolve_static_if->scope == scope
                    ) {
                        found = true;

                        if(resolve_static_if->done) {
                            if(resolve_static_if->condition) {
                                expect_delayed(search_value, search_for_declaration(
                                    info,
                                    jobs,
                                    scope,
                                    context,
                                    instructions,
                                    name,
                                    name_scope,
                                    name_range,
                                    static_if->statements,
                                    false
                                ));

                                if(search_value.found) {
                                    return has({
                                        true,
                                        search_value.type,
                                        search_value.value
                                    });
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
                                return wait(resolve_static_if);
                            }
                        }
                    }
                }
            }

            assert(found);
        } else if(statement->kind == StatementKind::VariableDeclaration) {
            if(scope->is_top_level) {
                auto variable_declaration = (VariableDeclaration*)statement;

                if(strcmp(variable_declaration->name.text, name) == 0) {
                    for(auto job : *jobs) {
                        if(job->kind == JobKind::GenerateStaticVariable) {
                            auto generate_static_variable = (GenerateStaticVariable*)job;

                            if(generate_static_variable->declaration == variable_declaration) {
                                if(generate_static_variable->done) {
                                    auto address_register = append_reference_static(
                                        context,
                                        instructions,
                                        name_range,
                                        generate_static_variable->static_variable
                                    );

                                    return has({
                                        true,
                                        generate_static_variable->type,
                                        new AddressValue {
                                            address_register
                                        }
                                    });
                                } else {
                                    return wait(generate_static_variable);
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
        if(strcmp(scope_constant.name, name) == 0) {
            return has({
                true,
                scope_constant.type,
                new RuntimeConstantValue {
                    scope_constant.value
                }
            });
        }
    }

    return has({false});
}

static_profiled_function(DelayedResult<TypedRuntimeValue>, generate_expression, (
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
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

        assert(context->variable_scope_stack.count > 0);

        for(size_t i = 0; i < context->variable_scope_stack.count; i += 1) {
            auto current_scope = context->variable_scope_stack[context->variable_scope_stack.count - 1 - i];

            for(auto variable : current_scope.variables) {
                if(strcmp(variable.name.text, named_reference->name.text) == 0) {
                    return has({
                        variable.type,
                        new AddressValue {
                            variable.address_register
                        }
                    });
                }
            }

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                current_scope.constant_scope,
                context,
                instructions,
                named_reference->name.text,
                scope,
                named_reference->name.range,
                current_scope.constant_scope->statements,
                false
            ));

            if(search_value.found) {
                return has({
                    search_value.type,
                    search_value.value
                });
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
                scope,
                named_reference->name.range,
                current_scope->statements,
                false
            ));

            if(search_value.found) {
                return has({
                    search_value.type,
                    search_value.value
                });
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(strcmp(named_reference->name.text, global_constant.name) == 0) {
                return has({
                    global_constant.type,
                    new RuntimeConstantValue {
                        global_constant.value
                    }
                });
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %s", named_reference->name.text);

        return err;
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, index_reference->expression));

        expect_delayed(index, generate_expression(info, jobs, scope, context, instructions, index_reference->index));

        if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue && index.value->kind == RuntimeValueKind::RuntimeConstantValue) {
            auto expression_constant = ((RuntimeConstantValue*)expression_value.value)->value;

            auto index_constant = ((RuntimeConstantValue*)index.value)->value;

            expect(constant, evaluate_constant_index(
                info,
                scope,
                expression_value.type,
                expression_constant,
                index_reference->expression->range,
                index.type,
                index_constant,
                index_reference->index->range
            ));

            return has({
                constant.type,
                new RuntimeConstantValue {
                    constant.value
                }
            });
        }

        expect(index_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            index_reference->index->range,
            index.type,
            index.value,
            heapify<Integer>({
                info.address_integer_size,
                false
            }),
            false
        ));

        size_t base_address_register;
        Type *element_type;
        if(expression_value.type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)expression_value.type;
            element_type = array_type->element_type;

            if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                auto pointer_value = extract_constant_value(PointerConstant, expression_value.value);

                base_address_register = append_integer_constant(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.address_integer_size,
                    pointer_value->value
                );
            } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                auto register_value = (RegisterValue*)expression_value.value;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.address_integer_size,
                    register_value->register_index
                );
            } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)expression_value.value;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range,
                    info.address_integer_size,
                    address_value->address_register
                );
            } else {
                abort();
            }
        } else if(expression_value.type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)expression_value.type;
            element_type = static_array->element_type;

            if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                auto static_array_value = extract_constant_value(StaticArrayConstant, expression_value.value);

                auto static_constant = register_static_array_constant(
                    info,
                    scope,
                    context,
                    index_reference->expression->range,
                    static_array->element_type,
                    { static_array->length, static_array_value->elements }
                );

                base_address_register = append_reference_static(
                    context,
                    instructions,
                    index_reference->expression->range,
                    static_constant
                );
            } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                auto register_value = (RegisterValue*)expression_value.value;

                base_address_register = register_value->register_index;
            } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)expression_value.value;

                base_address_register = address_value->address_register;
            } else {
                abort();
            }
        }

        auto element_size_register = append_integer_constant(
            context,
            instructions,
            index_reference->range,
            info.address_integer_size,
            get_type_size(info, element_type)
        );

        auto offset = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range,
            IntegerArithmeticOperation::Operation::Multiply,
            info.address_integer_size,
            element_size_register,
            index_register
        );

        auto address_register = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range,
            IntegerArithmeticOperation::Operation::Add,
            info.address_integer_size,
            base_address_register,
            offset
        );

        return has({
            element_type,
            new AddressValue {
                address_register
            }
        });
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, member_reference->expression));

        Type *actual_type;
        RuntimeValue *actual_value;
        if(expression_value.type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)expression_value.type;
            actual_type = pointer->type;

            size_t address_register;
            if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                auto integer_value = extract_constant_value(IntegerConstant, expression_value.value);

                address_register = append_integer_constant(
                    context,
                    instructions,
                    member_reference->expression->range,
                    info.address_integer_size,
                    integer_value->value
                );
            } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                auto register_value = (RegisterValue*)expression_value.value;

                address_register = register_value->register_index;
            } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)expression_value.value;

                address_register = append_load_integer(
                    context,
                    instructions,
                    member_reference->expression->range,
                    info.address_integer_size,
                    address_value->address_register
                );
            } else {
                abort();
            }

            actual_value = new AddressValue {
                address_register
            };
        } else {
            actual_type = expression_value.type;
            actual_value = expression_value.value;
        }

        if(actual_type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)actual_type;

            if(strcmp(member_reference->name.text, "length") == 0) {
                auto type = new Integer {
                    info.address_integer_size,
                    false
                };

                RuntimeValue *value;
                if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto array_value = extract_constant_value(ArrayConstant, actual_value);

                    value = new RuntimeConstantValue {
                        new IntegerConstant {
                            array_value->length
                        }
                    };
                } else if(actual_value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)actual_value;

                    auto address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        member_reference->range,
                        register_value->register_index,
                        register_size_to_byte_size(info.address_integer_size)
                    );

                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range,
                        info.address_integer_size,
                        address_register
                    );

                    value = new RegisterValue {
                        length_register
                    };
                } else if(actual_value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)actual_value;

                    auto address_register = generate_address_offset(
                        info,
                        context,
                        instructions,
                        member_reference->range,
                        address_value->address_register,
                        register_size_to_byte_size(info.address_integer_size)
                    );

                    value = new AddressValue {
                        address_register
                    };
                } else {
                    abort();
                }

                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    value
                });
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                RuntimeValue *value;
                if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto array_value = extract_constant_value(ArrayConstant, actual_value);

                    value = new RuntimeConstantValue {
                        new PointerConstant {
                            array_value->pointer
                        }
                    };
                } else if(actual_value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)actual_value;

                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range,
                        info.address_integer_size,
                        register_value->register_index
                    );

                    value = new RegisterValue {
                        length_register
                    };
                } else if(actual_value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)actual_value;

                    value = new AddressValue {
                        address_value->address_register
                    };
                } else {
                    abort();
                }

                return has({
                    new Pointer {
                        array_type->element_type
                    },
                    value
                });
            } else {
                error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return err;
            }
        } else if(actual_type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)actual_type;

            if(strcmp(member_reference->name.text, "length") == 0) {
                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new RuntimeConstantValue {
                        new IntegerConstant {
                            static_array->length
                        }
                    }
                });
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                size_t address_regsiter;
                if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, actual_value);

                    auto static_constant = register_static_array_constant(
                        info,
                        scope,
                        context,
                        member_reference->expression->range,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    address_regsiter = append_reference_static(context, instructions, member_reference->range, static_constant);
                } else if(actual_value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)actual_value;

                    address_regsiter = register_value->register_index;
                } else if(actual_value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)actual_value;

                    address_regsiter = address_value->address_register;
                } else {
                    abort();
                }

                return has({
                    new Pointer {
                        static_array->element_type
                    },
                    new RegisterValue {
                        address_regsiter
                    }
                });
            } else {
                error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return err;
            }
        } else if(actual_type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)actual_type;

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(strcmp(struct_type->members[i].name, member_reference->name.text) == 0) {
                    auto member_type = struct_type->members[i].type;

                    if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto struct_value = extract_constant_value(StructConstant, actual_value);

                        assert(!struct_type->definition->is_union);

                        return has({
                            member_type,
                            new RuntimeConstantValue {
                                struct_value->members[i]
                            }
                        });
                    } else if(actual_value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)actual_value;

                        auto address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            member_reference->range,
                            register_value->register_index,
                            get_struct_member_offset(info, *struct_type, i)
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

                        return has({
                            member_type,
                            new RegisterValue {
                                register_index
                            }
                        });
                    } else if(actual_value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)actual_value;

                        auto address_register = generate_address_offset(
                            info,
                            context,
                            instructions,
                            member_reference->range,
                            address_value->address_register,
                            get_struct_member_offset(info, *struct_type, i)
                        );

                        return has({
                            member_type,
                            new AddressValue {
                                address_register
                            }
                        });
                    } else {
                        abort();
                    }
                }
            }

            error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return err;
        } else if(actual_type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)actual_type;

            auto undetermined_struct_value = (UndeterminedStructValue*)actual_value;

            for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                if(strcmp(undetermined_struct->members[i].name, member_reference->name.text) == 0) {
                    return has({
                        undetermined_struct->members[i].type,
                        undetermined_struct_value->members[i]
                    });
                }
            }

            error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return err;
        } else if(actual_type->kind == TypeKind::FileModule) {
            auto file_module_value = extract_constant_value(FileModuleConstant, actual_value);

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                file_module_value->scope,
                context,
                instructions,
                member_reference->name.text,
                scope,
                member_reference->name.range,
                file_module_value->scope->statements,
                true
            ));

            if(search_value.found) {
                return has({
                    search_value.type,
                    search_value.value
                });
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return err;
        } else {
            error(scope, member_reference->expression->range, "Type %s has no members", type_description(actual_type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return has({
            &undetermined_integer_singleton,
            new RuntimeConstantValue {
                new IntegerConstant {
                    integer_literal->value
                }
            }
        });
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return has({
            &undetermined_float_singleton,
            new RuntimeConstantValue {
                new FloatConstant {
                    float_literal->value
                }
            }
        });
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.count;

        auto characters = allocate<ConstantValue*>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = new IntegerConstant {
                (uint64_t)string_literal->characters[i]
            };
        }

        return has({
            new StaticArray {
                character_count,
                new Integer {
                    RegisterSize::Size8,
                    false
                }
            },
            new RuntimeConstantValue {
                new StaticArrayConstant {
                    characters
                }
            }
        });
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err;
        }

        expect_delayed(first_element, generate_expression(info, jobs, scope, context, instructions, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(scope, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return err;
        }

        auto elements = allocate<TypedRuntimeValue>(element_count);
        elements[0] = first_element;

        auto all_constant = true;
        for(size_t i = 1; i < element_count; i += 1) {
            expect_delayed(element, generate_expression(info, jobs, scope, context, instructions, array_literal->elements[i]));

            elements[i] = element;

            if(element.value->kind != RuntimeValueKind::RuntimeConstantValue) {
                all_constant = false;
            }
        }

        RuntimeValue *value;
        if(all_constant) {
            auto element_values = allocate<ConstantValue*>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                auto constant_value = ((RuntimeConstantValue*)elements[i].value)->value;

                expect(coerced_constant_value, coerce_constant_to_type(
                    info,
                    scope,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    constant_value,
                    determined_element_type,
                    false
                ));

                element_values[i] = coerced_constant_value;
            }

            value = new RuntimeConstantValue {
                new StaticArrayConstant {
                    element_values
                }
            };
        } else {
            auto element_size = get_type_size(info, determined_element_type);

            auto address_register = append_allocate_local(
                context,
                instructions,
                array_literal->range,
                array_literal->elements.count * element_size,
                get_type_alignment(info, determined_element_type)
            );

            auto element_size_register = append_integer_constant(
                context,
                instructions,
                array_literal->range,
                info.address_integer_size,
                element_size
            );

            auto element_address_register = address_register;
            for(size_t i = 0; i < element_count; i += 1) {
                if(!coerce_to_type_write(
                    info,
                    scope,
                    context,
                    instructions,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    elements[i].value,
                    determined_element_type,
                    element_address_register
                )) {
                    return err;
                }

                if(i != element_count - 1) {
                    element_address_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        array_literal->elements[i]->range,
                        IntegerArithmeticOperation::Operation::Add,
                        info.address_integer_size,
                        element_address_register,
                        element_size_register
                    );
                }
            }

            value = new RegisterValue {
                address_register
            };
        }

        return has({
            new StaticArray {
                element_count,
                determined_element_type
            },
            value
        });
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        if(struct_literal->members.count == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err;
        }

        auto member_count = struct_literal->members.count;

        auto type_members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<RuntimeValue*>(member_count);
        auto all_constant = true;

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(strcmp(struct_literal->members[i].name.text, type_members[j].name) == 0) {
                    error(scope, struct_literal->members[i].name.range, "Duplicate struct member %s", struct_literal->members[i].name.text);

                    return err;
                }
            }

            expect_delayed(member, generate_expression(info, jobs, scope, context, instructions, struct_literal->members[i].value));

            type_members[i] = {
                struct_literal->members[i].name.text,
                member.type
            };

            member_values[i] = member.value;

            if(member.value->kind != RuntimeValueKind::RuntimeConstantValue) {
                all_constant = false;
            }
        }

        RuntimeValue *value;
        if(all_constant) {
            auto constant_member_values = allocate<ConstantValue*>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                auto constant_value = ((RuntimeConstantValue*)member_values[i])->value;

                constant_member_values[i] = constant_value;
            }

            value = new RuntimeConstantValue {
                new StructConstant {
                    constant_member_values
                }
            };
        } else {
            value = new UndeterminedStructValue {
                member_values
            };
        }

        return has({
            new UndeterminedStruct {
                {
                    member_count,
                    type_members
                }
            },
            value
        });
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, function_call->expression));

        if(expression_value.type->kind == TypeKind::FunctionTypeType || expression_value.type->kind == TypeKind::PolymorphicFunction) {
            auto call_parameter_count = function_call->parameters.count;

            auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[i]));

                call_parameters[i] = parameter_value;
            }

            FunctionTypeType *function_type;
            FunctionConstant *function_value;
            if(expression_value.type->kind == TypeKind::PolymorphicFunction) {
                auto polymorphic_function_value = extract_constant_value(PolymorphicFunctionConstant, expression_value.value);

                auto declaration_parameters = polymorphic_function_value->declaration->parameters;
                auto declaration_parameter_count = declaration_parameters.count;

                if(call_parameter_count != declaration_parameter_count) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        declaration_parameter_count,
                        call_parameter_count
                    );

                    return err;
                }

                auto polymorphic_parameters = allocate<TypedConstantValue>(declaration_parameter_count);

                for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                    auto declaration_parameter = declaration_parameters[i];

                    if(declaration_parameter.is_polymorphic_determiner) {
                        polymorphic_parameters[i].type = call_parameters[i].type;
                    }

                    if(declaration_parameter.is_constant) {
                        if(call_parameters[i].value->kind != RuntimeValueKind::RuntimeConstantValue) {
                            error(
                                scope,
                                function_call->parameters[i]->range,
                                "Non-constant value provided for constant parameter '%s'",
                                declaration_parameter.name.text
                            );

                            return err;
                        }

                        auto constant_value = ((RuntimeConstantValue*)call_parameters[i].value)->value;

                        polymorphic_parameters[i] = {
                            call_parameters[i].type,
                            constant_value
                        };
                    }
                }

                auto found = false;
                for(auto job : *jobs) {
                    if(job->kind == JobKind::ResolvePolymorphicFunction) {
                        auto resolve_polymorphic_function = (ResolvePolymorphicFunction*)job;

                        if(
                            resolve_polymorphic_function->declaration == polymorphic_function_value->declaration &&
                            resolve_polymorphic_function->scope == polymorphic_function_value->scope
                        ) {
                            auto matching_polymorphic_parameters = true;
                            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                                auto declaration_parameter = declaration_parameters[i];
                                auto call_parameter = polymorphic_parameters[i];
                                auto job_parameter = resolve_polymorphic_function->parameters[i];

                                if(
                                    (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                    !types_equal(job_parameter.type, call_parameter.type)
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

                            if(resolve_polymorphic_function->done) {
                                found = true;

                                function_type = resolve_polymorphic_function->type;
                                function_value = resolve_polymorphic_function->value;

                                break;
                            } else {
                                return wait(resolve_polymorphic_function);
                            }  
                        }
                    }
                }

                if(!found) {
                    auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

                    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                        call_parameter_ranges[i] = function_call->parameters[i]->range;
                    }

                    auto resolve_polymorphic_function = new ResolvePolymorphicFunction;
                    resolve_polymorphic_function->done = false;
                    resolve_polymorphic_function->waiting_for = nullptr;
                    resolve_polymorphic_function->declaration = polymorphic_function_value->declaration;
                    resolve_polymorphic_function->parameters = polymorphic_parameters;
                    resolve_polymorphic_function->scope = polymorphic_function_value->scope;
                    resolve_polymorphic_function->call_scope = scope;
                    resolve_polymorphic_function->call_parameter_ranges = call_parameter_ranges;

                    append(jobs, (Job*)resolve_polymorphic_function);

                    return wait(resolve_polymorphic_function);
                }
            } else {
                function_type = (FunctionTypeType*)expression_value.type;
                function_value = extract_constant_value(FunctionConstant, expression_value.value);

                if(call_parameter_count != function_type->parameters.count) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        function_type->parameters.count,
                        call_parameter_count
                    );

                    return err;
                }
            }

            auto found = false;
            Function *runtime_function;
            for(auto job : *jobs) {
                if(job->kind == JobKind::GenerateFunction) {
                    auto generate_function = (GenerateFunction*)job;

                    if(
                        types_equal(generate_function->type, function_type) &&
                        generate_function->value->declaration == function_value->declaration &&
                        generate_function->value->body_scope == function_value->body_scope
                    ) {
                        found = true;

                        runtime_function = generate_function->function;

                        break;
                    }
                }
            }

            if(!found) {
                runtime_function = new Function;

                auto generate_function = new GenerateFunction;
                generate_function->done = false;
                generate_function->waiting_for = nullptr;
                generate_function->type = function_type;
                generate_function->value = function_value;

                generate_function->function = runtime_function;

                append(jobs, (Job*)generate_function);
            }

            auto has_return = function_type->return_type->kind != TypeKind::Void;

            RegisterRepresentation return_type_representation;
            if(has_return) {
                return_type_representation = get_type_representation(info, function_type->return_type);
            }

            auto instruction_parameter_count = function_type->parameters.count;
            if(has_return && !return_type_representation.is_in_register) {
                instruction_parameter_count += 1;
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(instruction_parameter_count);

            size_t runtime_parameter_index = 0;
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                if(!function_value->declaration->parameters[i].is_constant) {
                    expect(parameter_register, coerce_to_type_register(
                        info,
                        scope,
                        context,
                        instructions,
                        function_call->parameters[i]->range,
                        call_parameters[i].type,
                        call_parameters[i].value,
                        function_type->parameters[i],
                        false
                    ));

                    auto representation = get_type_representation(info, function_type->parameters[i]);

                    RegisterSize size;
                    if(representation.is_in_register) {
                        size = representation.value_size;
                    } else {
                        size = info.address_integer_size;
                    }

                    instruction_parameters[i] = {
                        size,
                        representation.is_in_register && representation.is_float,
                        parameter_register
                    };

                    runtime_parameter_index += 1;
                }
            }

            assert(runtime_parameter_index == function_type->parameters.count);

            if(has_return && !return_type_representation.is_in_register) {
                auto parameter_register = append_allocate_local(
                    context,
                    instructions,
                    function_call->range,
                    get_type_size(info, function_type->return_type),
                    get_type_alignment(info, function_type->return_type)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.address_integer_size,
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

            RuntimeValue *value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_size = return_type_representation.value_size;
                    function_call_instruction->is_return_float = return_type_representation.is_float;
                    function_call_instruction->return_register = return_register;

                    value = new RegisterValue {
                        return_register
                    };
                } else {
                    value = new RegisterValue {
                        instruction_parameters[instruction_parameter_count - 1].register_index
                    };
                }
            } else {
                value = new RuntimeConstantValue {
                    &void_constant_singleton
                };
            }

            append(instructions, (Instruction*)function_call_instruction);

            return has({
                function_type->return_type,
                value
            });
        } else if(expression_value.type->kind == TypeKind::BuiltinFunction) {
            auto builtin_function_value = extract_constant_value(BuiltinFunctionConstant, expression_value.value);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return err;
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                Type *type;
                if(parameter_value.type->kind == TypeKind::TypeType) {
                    type = extract_constant_value(TypeConstant, parameter_value.value)->type;
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(scope, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return err;
                }

                auto size = get_type_size(info, type);

                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new RuntimeConstantValue {
                        new IntegerConstant {
                            size
                        }
                    }
                });
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return err;
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                return has({
                    &type_type_singleton,
                    new RuntimeConstantValue {
                        new TypeConstant {
                            parameter_value.type
                        }
                    }
                });
            } else if(strcmp(builtin_function_value->name, "memcpy") == 0) {
                if(function_call->parameters.count != 3) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 3 got %zu", function_call->parameters.count);

                    return err;
                }

                Integer u8_type { RegisterSize::Size8, false };
                Pointer u8_pointer_type { &u8_type };

                expect_delayed(destination_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[0]));

                if(!types_equal(destination_value.type, &u8_pointer_type)) {
                    error(
                        scope,
                        function_call->parameters[0]->range,
                        "Incorrect type for parameter 0. Expected '%s', got '%s'",
                        type_description(&u8_pointer_type),
                        type_description(destination_value.type)
                    );

                    return err;
                }

                expect_delayed(source_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[1]));

                if(!types_equal(source_value.type, &u8_pointer_type)) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 1. Expected '%s', got '%s'",
                        type_description(&u8_pointer_type),
                        type_description(source_value.type)
                    );

                    return err;
                }

                Integer usize_type { info.address_integer_size, false };

                expect_delayed(size_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[2]));

                if(!types_equal(size_value.type, &usize_type)) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 2. Expected '%s', got '%s'",
                        type_description(&usize_type),
                        type_description(size_value.type)
                    );

                    return err;
                }

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

                auto size_register = generate_in_register_integer_value(
                    context,
                    instructions,
                    function_call->parameters[2]->range,
                    usize_type,
                    size_value.value
                );

                append_copy_memory(
                    context,
                    instructions,
                    function_call->range,
                    size_register,
                    source_address_register,
                    destination_address_register,
                    1
                );

                return has({
                    &void_singleton,
                    new RuntimeConstantValue {
                        &void_constant_singleton
                    }
                });
            } else {
                abort();
            }
        } else if(expression_value.type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)expression_value.type;

            if(pointer->type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%s'", type_description(expression_value.type));

                return err;
            }

            auto function = (FunctionTypeType*)pointer->type;

            auto address_register = generate_in_register_pointer_value(info, context, instructions, function_call->expression->range, expression_value.value);

            auto parameter_count = function->parameters.count;

            if(function_call->parameters.count != parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    parameter_count,
                    function_call->parameters.count
                );

                return err;
            }

            auto has_return = function->return_type->kind != TypeKind::Void;

            RegisterRepresentation return_type_representation;
            if(has_return) {
                return_type_representation = get_type_representation(info, function->return_type);
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
                    function->parameters[i],
                    false
                ));

                auto representation = get_type_representation(info, function->parameters[i]);

                RegisterSize size;
                if(representation.is_in_register) {
                    size = representation.value_size;
                } else {
                    size = info.address_integer_size;
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
                    get_type_size(info, function->return_type),
                    get_type_alignment(info, function->return_type)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.address_integer_size,
                    false,
                    parameter_register
                };
            }

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->range = function_call->range;
            function_call_instruction->address_register = address_register;
            function_call_instruction->parameters = { parameter_count, instruction_parameters };
            function_call_instruction->has_return = has_return && return_type_representation.is_in_register;

            RuntimeValue *value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_size = return_type_representation.value_size;
                    function_call_instruction->is_return_float = return_type_representation.is_float;
                    function_call_instruction->return_register = return_register;

                    value = new RegisterValue {
                        return_register
                    };
                } else {
                    value = new RegisterValue {
                        instruction_parameters[instruction_parameter_count - 1].register_index
                    };
                }
            } else {
                value = new RuntimeConstantValue {
                    &void_constant_singleton
                };
            }

            append(instructions, (Instruction*)function_call_instruction);

            return has({
                function->return_type,
                value
            });
        } else if(expression_value.type->kind == TypeKind::TypeType) {
            auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

            if(type->kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = (PolymorphicStruct*)type;
                auto definition = polymorphic_struct->definition;

                auto parameter_count = definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.count);

                    return err;
                }

                auto parameters = allocate<ConstantValue*>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.type,
                        parameter.value,
                        polymorphic_struct->parameter_types[i],
                        false
                    ));

                    parameters[i] = {
                        parameter_value
                    };
                }

                for(auto job : *jobs) {
                    if(job->kind == JobKind::ResolvePolymorphicStruct) {
                        auto resolve_polymorphic_struct = (ResolvePolymorphicStruct*)job;

                        if(resolve_polymorphic_struct->definition == definition && resolve_polymorphic_struct->parameters != nullptr) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(polymorphic_struct->parameter_types[i], parameters[i], resolve_polymorphic_struct->parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(resolve_polymorphic_struct->done) {
                                    return has({
                                        &type_type_singleton,
                                        new RuntimeConstantValue {
                                            new TypeConstant {
                                                resolve_polymorphic_struct->type
                                            }
                                        }
                                    });
                                } else {
                                    return wait(resolve_polymorphic_struct);
                                }
                            }
                        }
                    }
                }

                auto resolve_polymorphic_struct = new ResolvePolymorphicStruct;
                resolve_polymorphic_struct->done = false;
                resolve_polymorphic_struct->waiting_for = nullptr;
                resolve_polymorphic_struct->definition = definition;
                resolve_polymorphic_struct->parameters = parameters;
                resolve_polymorphic_struct->scope = polymorphic_struct->parent;

                append(jobs, (Job*)resolve_polymorphic_struct);

                return wait(resolve_polymorphic_struct);
            } else {
                error(scope, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return err;
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call '%s'", type_description(expression_value.type));

            return err;
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

        return has(result_value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                size_t address_register;
                if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto constant_value = ((RuntimeConstantValue*)expression_value.value)->value;

                    if(expression_value.type->kind == TypeKind::FunctionTypeType) {
                        auto function = (FunctionTypeType*)expression_value.type;

                        auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

                        auto found = false;
                        Function *runtime_function;
                        for(auto job : *jobs) {
                            if(job->kind == JobKind::GenerateFunction) {
                                auto generate_function = (GenerateFunction*)job;

                                if(
                                    types_equal(generate_function->type, function) &&
                                    generate_function->value->declaration == function_value->declaration &&
                                    generate_function->value->body_scope == function_value->body_scope
                                ) {
                                    found = true;

                                    runtime_function = generate_function->function;

                                    break;
                                }
                            }
                        }

                        if(!found) {
                            runtime_function = new Function;

                            auto generate_function = new GenerateFunction;
                            generate_function->done = false;
                            generate_function->waiting_for = nullptr;
                            generate_function->type = function;
                            generate_function->value = function_value;

                            generate_function->function = runtime_function;

                            append(jobs, (Job*)generate_function);
                        }

                        address_register = append_reference_static(
                            context,
                            instructions,
                            unary_operation->range,
                            runtime_function
                        );
                    } else if(expression_value.type->kind == TypeKind::TypeType) {
                        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

                        if(
                            !is_runtime_type(type) &&
                            type->kind != TypeKind::Void &&
                            type->kind != TypeKind::FunctionTypeType
                        ) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                            return err;
                        }

                        return has({
                            &type_type_singleton,
                            new RuntimeConstantValue {
                                new TypeConstant {
                                    new Pointer {
                                        type
                                    }
                                }
                            }
                        });
                    } else {
                        error(scope, unary_operation->expression->range, "Cannot take pointers to constants of type '%s'", type_description(expression_value.type));

                        return err;
                    }
                } else if(
                    expression_value.value->kind == RuntimeValueKind::RegisterValue ||
                    expression_value.value->kind == RuntimeValueKind::UndeterminedStructValue
                ) {
                    error(scope, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return err;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    address_register = address_value->address_register;
                } else {
                    abort();
                }

                return has({
                    new Pointer {
                        expression_value.type
                    },
                    new RegisterValue {
                        address_register
                    }
                });
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type->kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%s'", type_description(expression_value.type));

                    return err;
                }

                size_t register_index;
                if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto boolean_value = extract_constant_value(BooleanConstant, expression_value.value);

                    return has({
                        &boolean_singleton,
                        new RuntimeConstantValue {
                                new BooleanConstant {
                                !boolean_value->value
                            }
                        }
                    });
                } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    register_index = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    register_index = append_load_integer(
                        context,
                        instructions,
                        unary_operation->expression->range,
                        info.default_integer_size,
                        address_value->address_register
                    );
                }

                auto result_register = generate_boolean_invert(info, context, instructions, unary_operation->expression->range, register_index);

                return has({
                    &boolean_singleton,
                    new RegisterValue {
                        result_register
                    }
                });
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type->kind == TypeKind::UndeterminedInteger) {
                    auto integer_value = extract_constant_value(IntegerConstant, expression_value.value);

                    return has({
                        &undetermined_integer_singleton,
                        new RuntimeConstantValue {
                                new IntegerConstant {
                                -integer_value->value
                            }
                        }
                    });
                } else if(expression_value.type->kind == TypeKind::Integer) {
                    auto integer = (Integer*)expression_value.type;

                    size_t register_index;
                    if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto integer_value = extract_constant_value(IntegerConstant, expression_value.value);

                        return has({
                            &undetermined_integer_singleton,
                            new RuntimeConstantValue {
                                new IntegerConstant {
                                    -integer_value->value
                                }
                            }
                        });
                    } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            unary_operation->expression->range,
                            integer->size,
                            address_value->address_register
                        );
                    }

                    auto zero_register = append_integer_constant(context, instructions, unary_operation->range, integer->size, 0);

                    auto result_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range,
                        IntegerArithmeticOperation::Operation::Subtract,
                        integer->size,
                        zero_register,
                        register_index
                    );

                    return has({
                        integer,
                        new RegisterValue {
                            result_register
                        }
                    });
                } else if(expression_value.type->kind == TypeKind::FloatType) {
                    auto float_type = (FloatType*)expression_value.type;

                    size_t register_index;
                    if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto float_value = extract_constant_value(FloatConstant, expression_value.value);

                        return has({
                            float_type,
                            new RuntimeConstantValue {
                                new FloatConstant {
                                    -float_value->value
                                }
                            }
                        });
                    } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_float(
                            context,
                            instructions,
                            unary_operation->expression->range,
                            float_type->size,
                            address_value->address_register
                        );
                    }

                    auto zero_register = append_float_constant(context, instructions, unary_operation->range, float_type->size, 0.0);

                    auto result_register = append_float_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range,
                        FloatArithmeticOperation::Operation::Subtract,
                        float_type->size,
                        zero_register,
                        register_index
                    );

                    return has({
                        float_type,
                        new RegisterValue {
                            result_register
                        }
                    });
                } else if(expression_value.type->kind == TypeKind::UndeterminedFloat) {
                    auto float_value = extract_constant_value(FloatConstant, expression_value.value);

                    return has({
                        &undetermined_float_singleton,
                        new RuntimeConstantValue {
                            new FloatConstant {
                                -float_value->value
                            }
                        }
                    });
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return err;
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

        if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
            auto constant_value = ((RuntimeConstantValue*)expression_value.value)->value;

            auto constant_cast_result = evaluate_constant_cast(
                info,
                scope,
                expression_value.type,
                constant_value,
                cast->expression->range,
                target_type,
                cast->type->range,
                true
            );

            if(constant_cast_result.status) {
                return has({
                    target_type,
                    new RuntimeConstantValue {
                        constant_cast_result.value
                    }
                });
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
        } else if(target_type->kind == TypeKind::Integer) {
            auto target_integer = (Integer*)target_type;

            if(expression_value.type->kind == TypeKind::Integer) {
                auto integer = (Integer*)expression_value.type;
                size_t value_register;
                if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    value_register = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    value_register = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        integer->size,
                        address_value->address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;

                if(target_integer->size > integer->size) {
                    register_index = append_integer_upcast(
                        context,
                        instructions,
                        cast->range,
                        integer->is_signed,
                        integer->size,
                        target_integer->size,
                        value_register
                    );
                } else {
                    register_index = value_register;
                }
            } else if(expression_value.type->kind == TypeKind::FloatType) {
                auto float_type = (FloatType*)expression_value.type;
                size_t value_register;
                if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    value_register = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    value_register = append_load_float(
                        context,
                        instructions,
                        cast->expression->range,
                        float_type->size,
                        address_value->address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_truncation(
                    context,
                    instructions,
                    cast->range,
                    float_type->size,
                    target_integer->size,
                    value_register
                );
            } else if(expression_value.type->kind == TypeKind::Pointer) {
                auto pointer = (Pointer*)expression_value.type;
                if(target_integer->size == info.address_integer_size && !target_integer->is_signed) {
                    has_cast = true;

                    if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range,
                            info.address_integer_size,
                            address_value->address_register
                        );
                    } else {
                        abort();
                    }
                }
            }
        } else if(target_type->kind == TypeKind::FloatType) {
            auto target_float_type = (FloatType*)target_type;

            if(expression_value.type->kind == TypeKind::Integer) {
                auto integer = (Integer*)expression_value.type;
                size_t value_register;
                if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    value_register = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    value_register = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        integer->size,
                        address_value->address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_from_integer(
                    context,
                    instructions,
                    cast->range,
                    integer->is_signed,
                    integer->size,
                    target_float_type->size,
                    value_register
                );
            } else if(expression_value.type->kind == TypeKind::FloatType) {
                auto float_type = (FloatType*)expression_value.type;
                size_t value_register;
                if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    value_register = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    value_register = append_load_float(
                        context,
                        instructions,
                        cast->expression->range,
                        float_type->size,
                        address_value->address_register
                    );
                } else {
                    abort();
                }

                has_cast = true;
                register_index = append_float_conversion(
                    context,
                    instructions,
                    cast->range,
                    float_type->size,
                    target_float_type->size,
                    value_register
                );
            }
        } else if(target_type->kind == TypeKind::Pointer) {
            auto target_pointer = (Pointer*)target_type;

            if(expression_value.type->kind == TypeKind::Integer) {
                auto integer = (Integer*)expression_value.type;
                if(integer->size == info.address_integer_size && !integer->is_signed) {
                    has_cast = true;

                    if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range,
                            info.address_integer_size,
                            address_value->address_register
                        );
                    } else {
                        abort();
                    }
                }
            } else if(expression_value.type->kind == TypeKind::Pointer) {
                auto pointer = (Pointer*)expression_value.type;
                has_cast = true;

                if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    register_index = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    register_index = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range,
                        info.address_integer_size,
                        address_value->address_register
                    );
                } else {
                    abort();
                }
            }
        } else {
            abort();
        }

        if(has_cast) {
            return has({
                target_type,
                new RegisterValue {
                    register_index
                }
            });
        } else {
            error(scope, cast->range, "Cannot cast from '%s' to '%s'", type_description(expression_value.type), type_description(target_type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, instructions, function_call->expression));

        auto call_parameter_count = function_call->parameters.count;

        auto call_parameters = allocate<TypedRuntimeValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, instructions, function_call->parameters[i]));

            call_parameters[i] = parameter_value;
        }

        if(expression_value.type->kind == TypeKind::PolymorphicFunction) {
            auto polymorphic_function_value = extract_constant_value(PolymorphicFunctionConstant, expression_value.value);

            auto declaration_parameters = polymorphic_function_value->declaration->parameters;
            auto declaration_parameter_count = declaration_parameters.count;

            if(call_parameter_count != declaration_parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    declaration_parameter_count,
                    call_parameter_count
                );

                return err;
            }

            auto polymorphic_parameters = allocate<TypedConstantValue>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                auto declaration_parameter = declaration_parameters[i];

                if(declaration_parameter.is_polymorphic_determiner) {
                    polymorphic_parameters[i].type = call_parameters[i].type;
                }

                if(declaration_parameter.is_constant) {
                    if(call_parameters[i].value->kind != RuntimeValueKind::RuntimeConstantValue) {
                        error(
                            scope,
                            function_call->parameters[i]->range,
                            "Non-constant value provided for constant parameter '%s'",
                            declaration_parameter.name.text
                        );

                        return err;
                    }

                    auto constant_value = ((RuntimeConstantValue*)call_parameters[i].value)->value;

                    polymorphic_parameters[i] = {
                        call_parameters[i].type,
                        constant_value
                    };
                }
            }

            auto found = false;
            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolvePolymorphicFunction) {
                    auto resolve_polymorphic_function = (ResolvePolymorphicFunction*)job;

                    if(
                        resolve_polymorphic_function->declaration == polymorphic_function_value->declaration &&
                        resolve_polymorphic_function->scope == polymorphic_function_value->scope
                    ) {
                        auto matching_polymorphic_parameters = true;
                        for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                            auto declaration_parameter = declaration_parameters[i];
                            auto call_parameter = polymorphic_parameters[i];
                            auto job_parameter = resolve_polymorphic_function->parameters[i];

                            if(
                                (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                !types_equal(job_parameter.type, call_parameter.type)
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

                        if(resolve_polymorphic_function->done) {
                            found = true;

                            return has({
                                resolve_polymorphic_function->type,
                                new RuntimeConstantValue {
                                    resolve_polymorphic_function->value
                                }
                            });
                        } else {
                            return wait(resolve_polymorphic_function);
                        }  
                    }
                }
            }

            if(!found) {
                auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

                for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                    call_parameter_ranges[i] = function_call->parameters[i]->range;
                }

                auto resolve_polymorphic_function = new ResolvePolymorphicFunction;
                resolve_polymorphic_function->done = false;
                resolve_polymorphic_function->waiting_for = nullptr;
                resolve_polymorphic_function->declaration = polymorphic_function_value->declaration;
                resolve_polymorphic_function->parameters = polymorphic_parameters;
                resolve_polymorphic_function->scope = polymorphic_function_value->scope;
                resolve_polymorphic_function->call_scope = scope;
                resolve_polymorphic_function->call_parameter_ranges = call_parameter_ranges;

                append(jobs, (Job*)resolve_polymorphic_function);

                return wait(resolve_polymorphic_function);
            }
        } else if(expression_value.type->kind == TypeKind::FunctionTypeType) {
            auto function_type = (FunctionTypeType*)expression_value.type;
            auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

            if(call_parameter_count != function_type->parameters.count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    function_type->parameters.count,
                    call_parameter_count
                );

                return err;
            }

            return has({
                function_type,
                new RuntimeConstantValue {
                    function_value
                }
            });
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%s'", type_description(expression_value.type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, array_type->expression));

        if(!is_runtime_type(type)) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return err;
        }

        if(array_type->index != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, nullptr, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                heapify<Integer>({
                    info.address_integer_size,
                    false
                }),
                false
            ));

            return has({
                &type_type_singleton,
                new RuntimeConstantValue {
                    new TypeConstant {
                        new StaticArray {
                            length->value,
                            type
                        }
                    }
                }
            });
        } else {
            return has({
                &type_type_singleton,
                new RuntimeConstantValue {
                    new TypeConstant {
                        new ArrayTypeType {
                            type
                        }
                    }
                }
            });
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err;
            }

            expect_delayed(type, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, parameter.type));

            if(!is_runtime_type(type)) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return err;
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = &void_singleton;
        } else {
            expect_delayed(return_type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return err;
            }

            return_type = return_type_value;
        }

        return has({
            &type_type_singleton,
            new RuntimeConstantValue {
                new TypeConstant {
                    new FunctionTypeType {
                        { parameter_count, parameters },
                        return_type
                    }
                }
            }
        });
    } else {
        abort();
    }
}

static bool is_not_runtime_statement(Statement *statement) {
    return
        statement->kind == StatementKind::FunctionDeclaration ||
        statement->kind == StatementKind::ConstantDefinition ||
        statement->kind == StatementKind::StructDefinition ||
        statement->kind == StatementKind::StaticIf;
}

static_profiled_function(DelayedResult<void>, generate_statement, (
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Statement *statement
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

        expect_delayed_void_ret(value, generate_expression(info, jobs, scope, context, instructions, expression_statement->expression));

        return has();
    } else if(statement->kind == StatementKind::VariableDeclaration) {
        auto variable_declaration = (VariableDeclaration*)statement;

        Type *type;
        size_t address_register;

        if(variable_declaration->is_external) {
            error(scope, variable_declaration->range, "Local variables cannot be external");

            return err;
        }

        if(variable_declaration->is_no_mangle) {
            error(scope, variable_declaration->range, "Local variables cannot be no_mangle");

            return err;
        }

        if(variable_declaration->type != nullptr && variable_declaration->initializer != nullptr) {
            expect_delayed_void_ret(type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, variable_declaration->type));
            
            if(!is_runtime_type(type_value)) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return err;
            }

            type = type_value;

            expect_delayed_void_ret(initializer_value, generate_expression(info, jobs, scope, context, instructions, variable_declaration->initializer));

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                get_type_size(info, type),
                get_type_alignment(info, type)
            );

            if(!coerce_to_type_write(
                info,
                scope,
                context,
                instructions,
                variable_declaration->range,
                initializer_value.type,
                initializer_value.value,
                type,
                address_register
            )) {
                return err;
            }
        } else if(variable_declaration->type != nullptr) {
            expect_delayed_void_ret(type_value, evaluate_type_expression_runtime(info, jobs, scope, context, instructions, variable_declaration->type));

            if(!is_runtime_type(type_value)) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return err;
            }

            type = type_value;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                get_type_size(info, type),
                get_type_alignment(info, type)
            );
        } else if(variable_declaration->initializer != nullptr) {
            expect_delayed_void_ret(initializer_value, generate_expression(info, jobs, scope, context, instructions, variable_declaration->initializer));

            expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type));
            
            if(!is_runtime_type(actual_type)) {
                error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%s'", type_description(actual_type));

                return err;
            }

            type = actual_type;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range,
                get_type_size(info, type),
                get_type_alignment(info, type)
            );

            if(!coerce_to_type_write(
                info,
                scope,
                context,
                instructions,
                variable_declaration->range,
                initializer_value.type,
                initializer_value.value,
                type,
                address_register
            )) {
                return err;
            }
        } else {
            abort();
        }

        if(!add_new_variable(
            context,
            variable_declaration->name,
            address_register,
            type
        )) {
            return err;
        }

        return has();
    } else if(statement->kind == StatementKind::Assignment) {
        auto assignment = (Assignment*)statement;

        expect_delayed_void_ret(target, generate_expression(info, jobs, scope, context, instructions, assignment->target));

        size_t address_register;
        if(target.value->kind == RuntimeValueKind::AddressValue){
            auto address_value = (AddressValue*)target.value;

            address_register = address_value->address_register;
        } else {
            error(scope, assignment->target->range, "Value is not assignable");

            return err;
        }

        expect_delayed_void_ret(value, generate_expression(info, jobs, scope, context, instructions, assignment->value));

        if(!coerce_to_type_write(
            info,
            scope,
            context,
            instructions,
            assignment->range,
            value.type,
            value.value,
            target.type,
            address_register
        )) {
            return err;
        }

        return has();
    } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
        auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

        expect_delayed_void_ret(target, generate_expression(info, jobs, scope, context, instructions, binary_operation_assignment->target));

        size_t address_register;
        if(target.value->kind == RuntimeValueKind::AddressValue){
            auto address_value = (AddressValue*)target.value;

            address_register = address_value->address_register;
        } else {
            error(scope, binary_operation_assignment->target->range, "Value is not assignable");

            return err;
        }

        expect_delayed_void_ret(value, generate_binary_operation(
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

        if(!coerce_to_type_write(
            info,
            scope,
            context,
            instructions,
            binary_operation_assignment->range,
            value.type,
            value.value,
            target.type,
            address_register
        )) {
            return err;
        }

        return has();
    } else if(statement->kind == StatementKind::IfStatement) {
        auto if_statement = (IfStatement*)statement;

        auto end_jump_count = 1 + if_statement->else_ifs.count;
        auto end_jumps = allocate<Jump*>(end_jump_count);

        expect_delayed_void_ret(condition, generate_expression(info, jobs, scope, context, instructions, if_statement->condition));

        if(condition.type->kind != TypeKind::Boolean) {
            error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

            return err;
        }

        auto condition_register = generate_in_register_boolean_value(info, context, instructions, if_statement->condition->range, condition.value);

        append_branch(context, instructions, if_statement->condition->range, condition_register, instructions->count + 2);

        auto first_jump = new Jump;
        first_jump->range = if_statement->range;

        append(instructions, (Instruction*)first_jump);

        auto if_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.count);

        append(&context->variable_scope_stack, {
            if_scope,
            {}
        });

        for(auto child_statement : if_statement->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void_both(generate_statement(info, jobs, if_scope, context, instructions, child_statement));
            }
        }

        context->variable_scope_stack.count -= 1;

        auto first_end_jump = new Jump;
        first_end_jump->range = if_statement->range;

        append(instructions, (Instruction*)first_end_jump);

        end_jumps[0] = first_end_jump;

        first_jump->destination_instruction = instructions->count;

        for(size_t i = 0; i < if_statement->else_ifs.count; i += 1) {
            expect_delayed_void_ret(condition, generate_expression(info, jobs, scope, context, instructions, if_statement->else_ifs[i].condition));

            if(condition.type->kind != TypeKind::Boolean) {
                error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return err;
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
                instructions->count + 2
            );

            auto jump = new Jump;
            jump->range = if_statement->else_ifs[i].condition->range;

            append(instructions, (Instruction*)jump);

            auto else_if_scope = context->child_scopes[context->next_child_scope_index];
            context->next_child_scope_index += 1;
            assert(context->next_child_scope_index <= context->child_scopes.count);

            append(&context->variable_scope_stack, {
                else_if_scope,
                {}
            });

            for(auto child_statement : if_statement->else_ifs[i].statements) {
                if(!is_not_runtime_statement(child_statement)) {
                    expect_delayed_void_both(generate_statement(info, jobs, else_if_scope, context, instructions, child_statement));
                }
            }

            context->variable_scope_stack.count -= 1;

            auto end_jump = new Jump;
            end_jump->range = if_statement->range;

            append(instructions, (Instruction*)end_jump);

            end_jumps[i + 1] = end_jump;

            jump->destination_instruction = instructions->count;
        }

        if(if_statement->else_statements.count != 0) {
            auto else_scope = context->child_scopes[context->next_child_scope_index];
            context->next_child_scope_index += 1;
            assert(context->next_child_scope_index <= context->child_scopes.count);

            append(&context->variable_scope_stack, {
                else_scope,
                {}
            });

            for(auto child_statement : if_statement->else_statements) {
                if(!is_not_runtime_statement(child_statement)) {
                    expect_delayed_void_both(generate_statement(info, jobs, else_scope, context, instructions, child_statement));
                }
            }

            context->variable_scope_stack.count -= 1;
        }

        for(size_t i = 0; i < end_jump_count; i += 1) {
            end_jumps[i]->destination_instruction = instructions->count;
        }

        return has();
    } else if(statement->kind == StatementKind::WhileLoop) {
        auto while_loop = (WhileLoop*)statement;

        auto condition_index = instructions->count;

        expect_delayed_void_ret(condition, generate_expression(info, jobs, scope, context, instructions, while_loop->condition));

        if(condition.type->kind != TypeKind::Boolean) {
            error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %s", type_description(condition.type));

            return err;
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
            instructions->count + 2
        );

        auto jump_out = new Jump;
        jump_out->range = while_loop->condition->range;

        append(instructions, (Instruction*)jump_out);

        auto while_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.count);

        append(&context->variable_scope_stack, {
            while_scope,
            {}
        });

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        for(auto child_statement : while_loop->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void_both(generate_statement(info, jobs, while_scope, context, instructions, child_statement));
            }
        }

        auto break_jumps = to_array(context->break_jumps);

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.count -= 1;

        append_jump(
            context,
            instructions,
            while_loop->range,
            condition_index
        );

        jump_out->destination_instruction = instructions->count;

        for(auto jump : break_jumps) {
            jump->destination_instruction = instructions->count;
        }

        return has();
    } else if(statement->kind == StatementKind::ForLoop) {
        auto for_loop = (ForLoop*)statement;

        Identifier index_name;
        if(for_loop->has_index_name) {
            index_name = for_loop->index_name;
        } else {
            index_name = {
                "it",
                for_loop->range
            };
        }

        expect_delayed_void_ret(from_value, generate_expression(info, jobs, scope, context, instructions, for_loop->from));

        auto index_address_register = allocate_register(context);

        auto allocate_local = new AllocateLocal;
        allocate_local->range = for_loop->range;
        allocate_local->destination_register = index_address_register;

        append(instructions, (Instruction*)allocate_local);

        size_t condition_index;
        size_t to_register;
        Integer *index_type;
        if(from_value.type->kind == TypeKind::UndeterminedInteger) {
            auto from_integer_constant = extract_constant_value(IntegerConstant, from_value.value);

            auto from_regsiter = allocate_register(context);

            auto integer_constant = new IntegerConstantInstruction;
            integer_constant->range = for_loop->range;
            integer_constant->destination_register = from_regsiter;
            integer_constant->value = from_integer_constant->value;

            append(instructions, (Instruction*)integer_constant);

            auto store_integer = new StoreInteger;
            store_integer->range = for_loop->range;
            store_integer->source_register = from_regsiter;
            store_integer->address_register = index_address_register;

            append(instructions, (Instruction*)store_integer);

            condition_index = instructions->count;

            expect_delayed_void_ret(to_value, generate_expression(info, jobs, scope, context, instructions, for_loop->to));

            expect(determined_index_type, coerce_to_default_type(info, scope, for_loop->range, to_value.type));

            if(determined_index_type->kind == TypeKind::Integer) {
                auto integer = (Integer*)determined_index_type;

                allocate_local->size = register_size_to_byte_size(integer->size);
                allocate_local->alignment = register_size_to_byte_size(integer->size);

                integer_constant->size = integer->size;

                store_integer->size = integer->size;

                if(!check_undetermined_integer_to_integer_coercion(scope, for_loop->range, integer, (int64_t)from_integer_constant->value, false)) {
                    return err;
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
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%s'", type_description(determined_index_type));

                return err;
            }
        } else {
            expect(determined_index_type, coerce_to_default_type(info, scope, for_loop->range, from_value.type));

            if(determined_index_type->kind == TypeKind::Integer) {
                auto integer = (Integer*)determined_index_type;

                allocate_local->size = register_size_to_byte_size(integer->size);
                allocate_local->alignment = register_size_to_byte_size(integer->size);

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

                append_store_integer(context, instructions, for_loop->range, integer->size, from_register, index_address_register);

                condition_index = instructions->count;

                expect_delayed_void_ret(to_value, generate_expression(info, jobs, scope, context, instructions, for_loop->to));

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
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%s'", type_description(determined_index_type));

                return err;
            }
        }

        auto current_index_regsiter = append_load_integer(
            context,
            instructions,
            for_loop->range,
            index_type->size,
            index_address_register
        );

        IntegerComparisonOperation::Operation operation;
        if(index_type->is_signed) {
            operation = IntegerComparisonOperation::Operation::SignedGreaterThan;
        } else {
            operation = IntegerComparisonOperation::Operation::UnsignedGreaterThan;
        }

        auto condition_register = append_integer_comparison_operation(
            context,
            instructions,
            for_loop->range,
            operation,
            index_type->size,
            current_index_regsiter,
            to_register
        );

        auto branch = new Branch;
        branch->range = for_loop->range;
        branch->condition_register = condition_register;

        append(instructions, (Instruction*)branch);

        auto for_scope = context->child_scopes[context->next_child_scope_index];
        context->next_child_scope_index += 1;
        assert(context->next_child_scope_index <= context->child_scopes.count);

        append(&context->variable_scope_stack, {
            for_scope,
            {}
        });

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        if(!add_new_variable(context, index_name, index_address_register, index_type)) {
            return err;
        }

        for(auto child_statement : for_loop->statements) {
            if(!is_not_runtime_statement(child_statement)) {
                expect_delayed_void_both(generate_statement(info, jobs, for_scope, context, instructions, child_statement));
            }
        }

        auto break_jumps = to_array(context->break_jumps);

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.count -= 1;

        auto one_register = append_integer_constant(context, instructions, for_loop->range, index_type->size, 1);

        auto next_index_register = append_integer_arithmetic_operation(
            context,
            instructions,
            for_loop->range,
            IntegerArithmeticOperation::Operation::Add,
            index_type->size,
            current_index_regsiter,
            one_register
        );

        append_store_integer(context, instructions, for_loop->range, index_type->size, next_index_register, index_address_register);

        append_jump(context, instructions, for_loop->range, condition_index);

        for(auto jump : break_jumps) {
            jump->destination_instruction = instructions->count;
        }

        branch->destination_instruction = instructions->count;

        return has();
    } else if(statement->kind == StatementKind::ReturnStatement) {
        auto return_statement = (ReturnStatement*)statement;

        auto return_instruction = new ReturnInstruction;
        return_instruction->range = return_statement->range;

        if(return_statement->value != nullptr) {
            if(context->return_type->kind == TypeKind::Void) {
                error(scope, return_statement->range, "Erroneous return value");

                return err;
            } else {
                expect_delayed_void_ret(value, generate_expression(info, jobs, scope, context, instructions, return_statement->value));

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
                    if(!coerce_to_type_write(
                        info,
                        scope,
                        context,
                        instructions,
                        return_statement->value->range,
                        value.type,
                        value.value,
                        context->return_type,
                        context->return_parameter_register
                    )) {
                        return err;
                    }
                }
            }
        } else if(context->return_type->kind != TypeKind::Void) {
            error(scope, return_statement->range, "Missing return value");

            return err;
        }

        append(instructions, (Instruction*)return_instruction);

        return has();
    } else if(statement->kind == StatementKind::BreakStatement) {
        auto break_statement = (BreakStatement*)statement;

        if(!context->in_breakable_scope) {
            error(scope, break_statement->range, "Not in a break-able scope");

            return err;
        }

        auto jump = new Jump;
        jump->range = break_statement->range;

        append(instructions, (Instruction*)jump);

        append(&context->break_jumps, jump);

        return has();
    } else {
        abort();
    }
}

profiled_function(DelayedResult<Array<StaticConstant*>>, do_generate_function, (
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionTypeType *type,
    FunctionConstant *value,
    Function *function
), (
    info,
    jobs,
    type,
    value
)) {
    auto declaration = value->declaration;

    auto declaration_parameter_count = declaration->parameters.count;

    GenerationContext context {};

    const char *file_path;
    {
        auto current_scope = value->body_scope;

        while(!current_scope->is_top_level) {
            current_scope = current_scope->parent;
        }

        file_path = current_scope->file_path;
    }

    auto parameter_count = type->parameters.count;
    auto ir_parameter_count = parameter_count;

    RegisterRepresentation return_representation;
    if(type->return_type->kind != TypeKind::Void) {
        return_representation = get_type_representation(info, type->return_type);

        if(!return_representation.is_in_register) {
            ir_parameter_count += 1;
        }
    }

    auto ir_parameters = allocate<Function::Parameter>(ir_parameter_count);

    size_t parameter_index = 0;
    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
        if(!declaration->parameters[i].is_constant)  {
            auto representation = get_type_representation(info, type->parameters[parameter_index]);

            if(representation.is_in_register) {
                ir_parameters[parameter_index] = {
                    representation.value_size,
                    representation.is_float
                };
            } else {
                ir_parameters[parameter_index] = {
                    info.address_integer_size,
                    false
                };
            }

            parameter_index += 1;
        }
    }

    assert(parameter_index == parameter_count);

    if(type->return_type->kind != TypeKind::Void && !return_representation.is_in_register) {
        ir_parameters[ir_parameter_count - 1] = {
            info.address_integer_size,
            false
        };
    }

    function->name = declaration->name.text;
    function->is_no_mangle = declaration->is_no_mangle || declaration->is_external;
    function->range = declaration->range;
    function->scope = value->body_scope;
    function->parameters = { ir_parameter_count, ir_parameters };
    function->has_return = type->return_type->kind != TypeKind::Void && return_representation.is_in_register;
    function->is_external = declaration->is_external;

    if(type->return_type->kind != TypeKind::Void && return_representation.is_in_register) {
        function->return_size = return_representation.value_size;
        function->is_return_float = return_representation.is_float;
    }

    Array<StaticConstant*> static_constants;
    if(declaration->is_external) {
        static_constants = {};
        function->libraries = declaration->external_libraries;
    } else {
        GenerationContext context {};

        context.return_type = type->return_type;
        if(type->return_type->kind != TypeKind::Void && !return_representation.is_in_register) {
            context.return_parameter_register = ir_parameter_count - 1;
        }

        context.next_register = ir_parameter_count;

        append(&context.variable_scope_stack, {
            value->body_scope,
            {}
        });

        context.child_scopes = value->child_scopes;

        List<Instruction*> instructions {};

        size_t parameter_index = 0;
        for(size_t i = 0; i < declaration->parameters.count; i += 1) {
            if(!declaration->parameters[i].is_constant) {
                auto parameter_type = type->parameters[i];

                auto size = get_type_size(info, parameter_type);
                auto alignment = get_type_alignment(info, parameter_type);

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
                    generate_constant_size_copy(
                        info,
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
                expect_delayed_void_val(generate_statement(info, jobs, value->body_scope, &context, &instructions, statement));
            }
        }

        assert(context.next_child_scope_index == value->child_scopes.count);

        bool has_return_at_end;
        if(declaration->statements.count > 0) {
            auto last_statement = declaration->statements[declaration->statements.count - 1];

            has_return_at_end = last_statement->kind == StatementKind::ReturnStatement;
        } else {
            has_return_at_end = false;
        }

        if(!has_return_at_end) {
            if(type->return_type->kind != TypeKind::Void) {
                error(value->body_scope, declaration->range, "Function '%s' must end with a return", declaration->name.text);

                return err;
            } else {
                auto return_instruction = new ReturnInstruction;
                return_instruction->range = declaration->range;

                append(&instructions, (Instruction*)return_instruction);
            }
        }

        function->instructions = to_array(instructions);

        static_constants = to_array(context.static_constants);
    }

    return has(static_constants);
}

DelayedResult<StaticVariableResult> do_generate_static_variable(
    GlobalInfo info,
    List<Job*> *jobs,
    VariableDeclaration *declaration,
    ConstantScope *scope
) {
    if(declaration->is_external) {
        expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

        if(!is_runtime_type(type)) {
            error(scope, declaration->type->range, "Cannot create variables of type '%s'", type_description(type));

            return err;
        }

        auto size = get_type_size(info, type);
        auto alignment = get_type_alignment(info, type);

        auto static_variable = new StaticVariable;
        static_variable->name = declaration->name.text;
        static_variable->is_no_mangle = true;
        static_variable->range = declaration->range;
        static_variable->scope = scope;
        static_variable->size = size;
        static_variable->alignment = alignment;
        static_variable->is_external = true;

        return has({
            static_variable,
            type
        });
    } else {
        if(declaration->type != nullptr && declaration->initializer != nullptr) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

            if(!is_runtime_type(type)) {
                error(scope, declaration->type->range, "Cannot create variables of type '%s'", type_description(type));

                return err;
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

            auto size = get_type_size(info, type);
            auto alignment = get_type_alignment(info, type);

            auto data = allocate<uint8_t>(size);

            write_value(info, data, 0, type, coerced_initial_value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->is_no_mangle = declaration->is_no_mangle;
            static_variable->scope = scope;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_external = false;
            static_variable->has_initial_data = true;
            static_variable->initial_data = data;

            return has({
                static_variable,
                type
            });
        } else if(declaration->type != nullptr) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->type));

            if(!is_runtime_type(type)) {
                error(scope, declaration->type->range, "Cannot create variables of type '%s'", type_description(type));

                return err;
            }

            auto size = get_type_size(info, type);
            auto alignment = get_type_alignment(info, type);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->scope = scope;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_no_mangle = declaration->is_no_mangle;
            static_variable->is_external = false;

            return has({
                static_variable,
                type
            });
        } else if(declaration->initializer != nullptr) {
            expect_delayed(initial_value, evaluate_constant_expression(info, jobs, scope, nullptr, declaration->initializer));

            expect(type, coerce_to_default_type(info, scope, declaration->initializer->range, initial_value.type));

            if(!is_runtime_type(type)) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%s'", type_description(type));

                return err;
            }

            auto size = get_type_size(info, type);
            auto alignment = get_type_alignment(info, type);

            auto data = allocate<uint8_t>(size);

            write_value(info, data, 0, type, initial_value.value);

            auto static_variable = new StaticVariable;
            static_variable->name = declaration->name.text;
            static_variable->scope = scope;
            static_variable->size = size;
            static_variable->alignment = alignment;
            static_variable->is_no_mangle = declaration->is_no_mangle;
            static_variable->is_external = false;
            static_variable->has_initial_data = true;
            static_variable->initial_data = data;

            return has({
                static_variable,
                type
            });
        } else {
            abort();
        }
    }
}