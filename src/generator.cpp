#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "list.h"
#include "types.h"
#include "util.h"
#include "path.h"
#include "lexer.h"
#include "parser.h"

struct PolymorphicDeterminer {
    const char *name;

    Type type;
};

struct DeterminedDeclaration {
    Statement declaration;

    Array<PolymorphicDeterminer> polymorphic_determiners;

    DeterminedDeclaration *parent;
};

union ConstantValue {
    struct {
        Statement declaration;

        DeterminedDeclaration parent;

        const char *file_path;
    } function;

    uint64_t integer;

    bool boolean;

    Type type;

    size_t pointer;

    struct {
        size_t length;

        size_t pointer;
    } array;

    ConstantValue *static_array;

    ConstantValue *struct_;

    struct {
        const char *path;

        Array<Statement> statements;
    } file_module;
};

struct TypedConstantValue {
    Type type;

    ConstantValue value;
};

struct GlobalConstant {
    const char *name;

    Type type;

    ConstantValue value;
};

struct Variable {
    Identifier name;

    Type type;
    FileRange type_range;

    size_t register_index;
};

struct RuntimeFunctionParameter {
    Identifier name;

    Type type;
    FileRange type_range;
};

struct RuntimeFunction {
    const char *mangled_name;

    Array<RuntimeFunctionParameter> parameters;

    Type return_type;

    Statement declaration;

    DeterminedDeclaration parent;

    const char *file_path;

    Array<PolymorphicDeterminer> polymorphic_determiners;
};

struct DeterminedStructTypeMember {
    Identifier name;

    Type type;
    FileRange type_range;
};

struct StructType {
    const char *name;

    bool is_union;

    Array<DeterminedStructTypeMember> members;
};

struct ParsedFile {
    const char *path;

    Array<Statement> statements;
};

struct GenerationContext {
    RegisterSize address_integer_size;
    RegisterSize default_integer_size;

    Array<GlobalConstant> global_constants;

    const char *current_file_path;

    bool is_top_level;

    DeterminedDeclaration determined_declaration;

    Array<Statement> top_level_statements;

    Array<PolymorphicDeterminer> polymorphic_determiners;

    Array<Variable> parameters;
    Type return_type;
    size_t return_parameter_register;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<const char*> libraries;

    List<StaticConstant> static_constants;

    List<StructType> struct_types;

    List<ParsedFile> parsed_files;
};

static void error(const char *file_path, FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "Error: %s(%u,%u): ", file_path, range.first_line, range.first_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    if(range.first_line == range.first_character) {
        auto file = fopen(file_path, "rb");

        if(file != nullptr) {
            unsigned int current_line = 1;

            while(current_line != range.first_line) {
                auto character = fgetc(file);

                switch(character) {
                    case '\r': {
                        auto character = fgetc(file);

                        if(character == '\n') {
                            current_line += 1;
                        } else {
                            ungetc(character, file);

                            current_line += 1;
                        }
                    } break;

                    case '\n': {
                        current_line += 1;
                    } break;

                    case EOF: {
                        fclose(file);

                        va_end(arguments);

                        return;
                    } break;
                }
            }

            unsigned int skipped_spaces = 0;
            auto done_skipping_spaces = false;

            auto done = false;
            while(!done) {
                auto character = fgetc(file);

                switch(character) {
                    case '\r':
                    case '\n': {
                        done = true;
                    } break;

                    case ' ': {
                        if(!done_skipping_spaces) {
                            skipped_spaces += 1;
                        } else {
                            fprintf(stderr, "%c", character);
                        }
                    } break;

                    case EOF: {
                        fclose(file);

                        va_end(arguments);

                        return;
                    } break;

                    default: {
                        fprintf(stderr, "%c", character);

                        done_skipping_spaces = true;
                    } break;
                }
            }

            fprintf(stderr, "\n");

            for(unsigned int i = 1; i < range.first_character - skipped_spaces; i += 1) {
                fprintf(stderr, " ");
            }

            if(range.last_character - range.first_character == 0) {
                fprintf(stderr, "^");
            } else {
                for(unsigned int i = range.first_character; i <= range.last_character; i += 1) {
                    fprintf(stderr, "-");
                }
            }

            fprintf(stderr, "\n");

            fclose(file);
        }
    }

    va_end(arguments);
}

static bool match_declaration(Statement statement, const char *name) {
    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            if(strcmp(statement.function_declaration.name.text, name) == 0) {
                return true;
            }
        } break;

        case StatementType::ConstantDefinition: {
            if(strcmp(statement.constant_definition.name.text, name) == 0) {
                return true;
            }
        } break;

        case StatementType::StructDefinition: {
            if(strcmp(statement.struct_definition.name.text, name) == 0) {
                return true;
            }
        } break;

        case StatementType::Import: {
            auto import_name = path_get_file_component(statement.import);

            if(strcmp(import_name, name) == 0) {
                return true;
            }
        } break;
    }

    return false;
}

static size_t register_size_to_byte_size(RegisterSize size) {
    switch(size) {
        case RegisterSize::Size8: {
            return 1;
        } break;

        case RegisterSize::Size16: {
            return 2;
        } break;

        case RegisterSize::Size32: {
            return 4;
        } break;

        case RegisterSize::Size64: {
            return 8;
        } break;

        default: {
            abort();
        } break;
    }
}

static StructType retrieve_struct_type(GenerationContext context, const char *name) {
    for(auto struct_type : context.struct_types) {
        if(strcmp(struct_type.name, name) == 0) {
            return struct_type;
        }
    }

    abort();
}

static size_t get_type_alignment(GenerationContext context, Type type);

static size_t get_struct_alignment(GenerationContext context, StructType struct_type) {
    size_t current_alignment = 1;

    for(auto member : struct_type.members) {
        auto alignment = get_type_alignment(context, member.type);

        if(alignment > current_alignment) {
            current_alignment = alignment;
        }
    }

    return current_alignment;
}

static size_t get_type_alignment(GenerationContext context, Type type) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return register_size_to_byte_size(type.integer.size);
        } break;

        case TypeCategory::Boolean: {
            return register_size_to_byte_size(context.default_integer_size);
        } break;

        case TypeCategory::Pointer: {
            return register_size_to_byte_size(context.address_integer_size);
        } break;

        case TypeCategory::Array: {
            return register_size_to_byte_size(context.address_integer_size);
        } break;

        case TypeCategory::StaticArray: {
            return get_type_alignment(context, *type.static_array.type);
        } break;

        case TypeCategory::Struct: {
            return get_struct_alignment(context, retrieve_struct_type(context, type._struct.name));
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t get_type_size(GenerationContext context, Type type);

static size_t get_struct_size(GenerationContext context, StructType struct_type) {
    size_t current_size = 0;

    for(auto member : struct_type.members) {
        if(struct_type.is_union) {
            auto size = get_type_size(context, member.type);

            if(size > current_size) {
                current_size = size;
            }
        } else {
            auto alignment = get_type_alignment(context, member.type);

            auto alignment_difference = current_size % alignment;

            size_t offset;
            if(alignment_difference != 0) {
                offset = alignment - alignment_difference;
            } else {
                offset = 0;
            }

            auto size = get_type_size(context, member.type);

            current_size += offset + size;
        }        
    }

    return current_size;
}

static size_t get_type_size(GenerationContext context, Type type) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return register_size_to_byte_size(type.integer.size);
        } break;

        case TypeCategory::Boolean: {
            return register_size_to_byte_size(context.default_integer_size);
        } break;

        case TypeCategory::Pointer: {
            return register_size_to_byte_size(context.address_integer_size);
        } break;

        case TypeCategory::Array: {
            return 2 * register_size_to_byte_size(context.address_integer_size);
        } break;

        case TypeCategory::StaticArray: {
            return type.static_array.length * get_type_size(context, *type.static_array.type);
        } break;

        case TypeCategory::Struct: {
            return get_struct_size(context, retrieve_struct_type(context, type._struct.name));
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t get_struct_member_offset(GenerationContext context, StructType struct_type, size_t member_index) {
    if(struct_type.is_union) {
        return 0;
    }

    size_t current_offset = 0;

    if(member_index != 0) {
        for(auto i = 0; i < member_index; i += 1) {
            auto alignment = get_type_alignment(context, struct_type.members[i].type);

            auto alignment_difference = current_offset % alignment;

            size_t offset;
            if(alignment_difference != 0) {
                offset = alignment - alignment_difference;
            } else {
                offset = 0;
            }

            auto size = get_type_size(context, struct_type.members[i].type);

            current_offset += offset + size;
        }
    }
    
    auto alignment = get_type_alignment(context, struct_type.members[member_index].type);

    auto alignment_difference = current_offset % alignment;

    size_t offset;
    if(alignment_difference != 0) {
        offset = alignment - alignment_difference;
    } else {
        offset = 0;
    }

    return current_offset + offset;
}

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Expression expression);

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Statement declaration);

static Result<TypedConstantValue> resolve_constant_named_reference(GenerationContext *context, Identifier name) {
    for(auto polymorphic_determiner : context->polymorphic_determiners) {
        if(strcmp(polymorphic_determiner.name, name.text) == 0) {
            Type type;
            type.category = TypeCategory::Type;

            ConstantValue value;
            value.type = polymorphic_determiner.type;

            return {
                true,
                {
                    type,
                    value
                }
            };
        }
    }

    if(context->is_top_level) {
        for(auto statement : context->top_level_statements) {
            if(match_declaration(statement, name.text)) {
                return resolve_declaration(context, statement);
            }
        }
    } else {
        auto old_determined_declaration = context->determined_declaration;

        while(true) {
            switch(context->determined_declaration.declaration.type) {
                case StatementType::FunctionDeclaration: {
                    for(auto statement : context->determined_declaration.declaration.function_declaration.statements) {
                        if(match_declaration(statement, name.text)) {
                            auto result = resolve_declaration(context, statement);

                            if(!result.status) {
                                return { false };
                            }

                            context->determined_declaration = old_determined_declaration;

                            return {
                                true,
                                result.value
                            };
                        }
                    }

                    for(auto polymorphic_determiner : context->determined_declaration.polymorphic_determiners) {
                        if(strcmp(polymorphic_determiner.name, name.text) == 0) {
                            Type type;
                            type.category = TypeCategory::Type;

                            ConstantValue value;
                            value.type = polymorphic_determiner.type;

                            context->determined_declaration = old_determined_declaration;

                            return {
                                true,
                                {
                                    type,
                                    value
                                }
                            };
                        }
                    }
                } break;
            }

            if(context->determined_declaration.declaration.is_top_level) {
                break;
            } else {
                context->determined_declaration = *context->determined_declaration.parent;
            }
        }

        for(auto statement : context->top_level_statements) {
            if(match_declaration(statement, name.text)) {
                auto result = resolve_declaration(context, statement);

                if(!result.status) {
                    return { false };
                }

                context->determined_declaration = old_determined_declaration;

                return {
                    true,
                    result.value
                };
            }
        }

        context->determined_declaration = old_determined_declaration;
    }

    for(auto global_constant : context->global_constants) {
        if(strcmp(name.text, global_constant.name) == 0) {
            return {
                true,
                {
                    global_constant.type,
                    global_constant.value
                }
            };
        }
    }

    error(context->current_file_path, name.range, "Cannot find named reference %s", name.text);

    return { false };
}

static Result<size_t> coerce_constant_to_integer_type(
    GenerationContext context,
    FileRange range,
    TypedConstantValue value,
    RegisterSize size,
    bool is_signed,
    bool probing
) {
    switch(value.type.category) {
        case TypeCategory::Integer: {
            if(value.type.integer.is_undetermined) {
                return {
                    true,
                    value.value.integer
                };
            } else {
                if(value.type.integer.size != size || value.type.integer.is_signed != is_signed) {
                    return { false };
                }

                return {
                    true,
                    value.value.integer
                };
            }
        } break;

        default: {
            if(!probing) {
                error(context.current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), determined_integer_type_description(size, is_signed));
            }

            return { false };
        } break;
    }
}

static Result<size_t> coerce_constant_to_undetermined_integer(
    GenerationContext context,
    FileRange range,
    TypedConstantValue value,
    bool probing
) {
    switch(value.type.category) {
        case TypeCategory::Integer: {
            if(value.type.integer.is_undetermined) {
                return {
                    true,
                    value.value.integer
                };
            } else {
                switch(value.type.integer.size) {
                    case RegisterSize::Size8: {
                        return {
                            true,
                            (uint8_t)value.value.integer
                        };
                    } break;

                    case RegisterSize::Size16: {
                        return {
                            true,
                            (uint16_t)value.value.integer
                        };
                    } break;

                    case RegisterSize::Size32: {
                        return {
                            true,
                            (uint32_t)value.value.integer
                        };
                    } break;

                    case RegisterSize::Size64: {
                        return {
                            true,
                            value.value.integer
                        };
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } break;

        default: {
            if(!probing) {
                error(context.current_file_path, range, "Cannot implicitly convert '%s' to '{integer}'", type_description(value.type));
            }

            return { false };
        } break;
    }
}

static Result<ConstantValue> coerce_constant_to_type(
    GenerationContext context,
    FileRange range,
    TypedConstantValue value,
    Type type,
    bool probing
);

static Result<ConstantValue> coerce_constant_to_struct_type(
    GenerationContext context,
    FileRange range,
    TypedConstantValue value,
    StructType struct_type,
    bool probing
) {
    if(value.type.category != TypeCategory::Struct) {
        if(!probing) {
            error(context.current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), struct_type.name);
        }

        return { false };
    }

    auto value_type = value.type._struct;

    ConstantValue result_value;
    if(value_type.is_undetermined) {
        if(struct_type.is_union) {
            error(context.current_file_path, range, "Constants cannot be unions");

            return { false };
        } else {
            if(value_type.members.count != struct_type.members.count) {
                error(context.current_file_path, range, "Too many struct members. Expected %zu, got %zu", struct_type.members.count, value_type.members.count);

                return { false };
            }

            auto member_values = allocate<ConstantValue>(struct_type.members.count);

            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                if(strcmp(value_type.members[i].name, struct_type.members[i].name.text) != 0) {
                    error(context.current_file_path, range, "Incorrect struct member name. Expected '%s', got '%s", struct_type.members[i].name.text, value_type.members[i].name);

                    return { false };
                }

                expect(coerced_member_value, coerce_constant_to_type(
                    context,
                    range,
                    { value_type.members[i].type, value.value.struct_[i] },
                    struct_type.members[i].type,
                    probing
                ));

                member_values[i] = coerced_member_value;
            }

            result_value.struct_ = member_values;
        }

        return {
            true,
            result_value
        };
    } else {
        if(strcmp(value_type.name, struct_type.name) != 0) {
            if(!probing) {
                error(context.current_file_path, range, "Cannot implicitly convert '%s' to '%s'", value_type.name, struct_type.name);
            }

            return { false };
        }

        return {
            true,
            value.value
        };
    }
}

static Result<ConstantValue> coerce_constant_to_type(
    GenerationContext context,
    FileRange range,
    TypedConstantValue value,
    Type type,
    bool probing
) {
    switch(type.category) {
        case TypeCategory::Integer: {
            uint64_t result_value;
            if(type.integer.is_undetermined) {
                expect(integer_value, coerce_constant_to_undetermined_integer(context, range, value, probing));

                result_value = integer_value;
            } else {
                expect(integer_value, coerce_constant_to_integer_type(context, range, value, type.integer.size, type.integer.is_signed, probing));

                result_value = integer_value;
            }

            ConstantValue result;
            result.integer = result_value;

            return {
                true,
                result
            };
        } break;

        case TypeCategory::Pointer: {
            switch(value.type.category) {
                case TypeCategory::Integer: {
                    if(value.type.integer.is_undetermined) {
                        ConstantValue result;
                        result.pointer = value.value.integer;

                        return {
                            true,
                            result
                        };
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(types_equal(*type.pointer, *value.type.pointer)) {
                        return {
                            true,
                            value.value
                        };
                    }
                } break;
            }
        } break;

        case TypeCategory::Struct: {
            return coerce_constant_to_struct_type(context, range, value, retrieve_struct_type(context, type._struct.name), probing);
        } break;

        default: {
            if(types_equal(type, value.type)) {
                return {
                    true,
                    value.value
                };
            }
        } break;
    }

    if(!probing) {
        error(context.current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), type_description(type));
    }

    return { false };
}

static Result<TypedConstantValue> evaluate_constant_index(GenerationContext context, Type type, ConstantValue value, FileRange range, Type index_type, ConstantValue index_value, FileRange index_range) {
    if(index_type.category != TypeCategory::Integer) {
        error(context.current_file_path, index_range, "Expected an integer, got %s", type_description(index_type));
    }

    expect(index, coerce_constant_to_integer_type(context, index_range, { index_type, index_value, }, context.address_integer_size, false, false));

    switch(type.category) {
        case TypeCategory::StaticArray: {
            if(index >= type.static_array.length) {
                error(context.current_file_path, index_range, "Array index %zu out of bounds", index);

                return { false };
            }

            return {
                true,
                {
                    *type.static_array.type,
                    value.static_array[index]
                }
            };
        } break;

        default: {
            error(context.current_file_path, range, "Cannot index %s", type_description(type));

            return { false };
        } break;
    }
}

static Result<Type> determine_binary_operation_type(GenerationContext context, FileRange range, Type left, Type right) {
    if(left.category == TypeCategory::Integer && right.category == TypeCategory::Integer) {
        Type type;
        type.category = TypeCategory::Integer;

        if(left.integer.is_undetermined && right.integer.is_undetermined) {
            type.integer.is_undetermined = true;
        } else if(left.integer.is_undetermined) {
            type.integer = right.integer;
        } else {
            type.integer = left.integer;
        }

        return {
            true,
            type
        };
    } else if(left.category == TypeCategory::Boolean && right.category == TypeCategory::Boolean) {
        Type type;
        type.category = TypeCategory::Boolean;

        return {
            true,
            type
        };
    } else {
        error(context.current_file_path, range, "Mismatched types '%s' and '%s'", type_description(left), type_description(right));

        return { false };
    }
}

static Result<TypedConstantValue> evaluate_constant_binary_operation(
    GenerationContext context,
    FileRange range,
    BinaryOperator binary_operator,
    FileRange left_range,
    Type left_type,
    ConstantValue left_value,
    FileRange right_range,
    Type right_type,
    ConstantValue right_value
) {
    expect(type, determine_binary_operation_type(context, range, left_type, right_type));

    expect(coerced_left_value, coerce_constant_to_type(context, left_range, { left_type, left_value }, type, false));

    expect(coerced_right_value, coerce_constant_to_type(context, right_range, { right_type, right_value }, type, false));

    switch(type.category) {
        case TypeCategory::Integer: {
            switch(binary_operator) {
                case BinaryOperator::Addition: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = coerced_left_value.integer + coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Subtraction: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = coerced_left_value.integer - coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Multiplication: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(type.integer.is_undetermined || type.integer.is_signed) {
                        result.value.integer = (int64_t)coerced_left_value.integer * (int64_t)coerced_right_value.integer;
                    } else {
                        result.value.integer = coerced_left_value.integer * coerced_right_value.integer;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Division: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(type.integer.is_undetermined || type.integer.is_signed) {
                        result.value.integer = (int64_t)coerced_left_value.integer / (int64_t)coerced_right_value.integer;
                    } else {
                        result.value.integer = coerced_left_value.integer / coerced_right_value.integer;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Modulo: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(type.integer.is_undetermined || type.integer.is_signed) {
                        result.value.integer = (int64_t)coerced_left_value.integer % (int64_t)coerced_right_value.integer;
                    } else {
                        result.value.integer = coerced_left_value.integer % coerced_right_value.integer;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::BitwiseAnd: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = coerced_left_value.integer & coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::BitwiseOr: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(type.integer.is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = type.integer.size;
                        result.type.integer.is_signed = type.integer.is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = coerced_left_value.integer | coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Equal: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.integer == coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::NotEqual: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.integer != coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::LessThan: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.integer < coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::GreaterThan: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.integer > coerced_right_value.integer;

                    return {
                        true,
                        result
                    };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::Boolean: {
            switch(binary_operator) {
                case BinaryOperator::BooleanAnd: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.boolean && coerced_right_value.boolean;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::BooleanOr: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.boolean || coerced_right_value.boolean;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Equal: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.boolean == coerced_right_value.boolean;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::NotEqual: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = coerced_left_value.boolean != coerced_right_value.boolean;

                    return {
                        true,
                        result
                    };
                } break;

                default: {
                    error(context.current_file_path, range, "Cannot perform that operation on booleans");

                    return { false };
                } break;
            }
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<ConstantValue> evaluate_constant_conversion(GenerationContext context, ConstantValue value, Type value_type, FileRange value_range, Type type, FileRange type_range) {
    auto coerce_result = coerce_constant_to_type(
        context,
        value_range,
        { value_type, value },
        type,
        true
    );

    if(coerce_result.status) {
        return {
            true,
            coerce_result.value
        };
    }

    ConstantValue result;

    switch(value_type.category) {
        case TypeCategory::Integer: {
            switch(type.category) {
                case TypeCategory::Integer: {
                    if(value_type.integer.is_undetermined) {
                        result.integer = value.integer;
                    } else if(value_type.integer.is_signed) {
                        switch(value_type.integer.size) {
                            case RegisterSize::Size8: {
                                result.integer = (int8_t)value.integer;
                            } break;

                            case RegisterSize::Size16: {
                                result.integer = (int16_t)value.integer;
                            } break;

                            case RegisterSize::Size32: {
                                result.integer = (int32_t)value.integer;
                            } break;

                            case RegisterSize::Size64: {
                                result.integer = value.integer;
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    } else {
                        switch(value_type.integer.size) {
                            case RegisterSize::Size8: {
                                result.integer = (uint8_t)value.integer;
                            } break;

                            case RegisterSize::Size16: {
                                result.integer = (uint16_t)value.integer;
                            } break;

                            case RegisterSize::Size32: {
                                result.integer = (uint32_t)value.integer;
                            } break;

                            case RegisterSize::Size64: {
                                result.integer = value.integer;
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(value.type.integer.is_undetermined) {
                        result.pointer = value.integer;
                    } else if(value.type.integer.size == context.address_integer_size) {
                        result.pointer = value.integer;
                    } else {
                        error(context.current_file_path, value_range, "Cannot cast from %s to pointer", type_description(value_type));

                        return { false };
                    }
                } break;

                default: {
                    error(context.current_file_path, type_range, "Cannot cast integer to this type");

                    return { false };
                } break;
            }
        } break;

        case TypeCategory::Pointer: {
            switch(type.category) {
                case TypeCategory::Integer: {
                    if(type.integer.size == context.address_integer_size) {
                        result.integer = value.pointer;
                    } else {
                        error(context.current_file_path, value_range, "Cannot cast from pointer to %s", type_description(type));

                        return { false };
                    }
                } break;

                case TypeCategory::Pointer: {
                    result.pointer = value.pointer;
                } break;

                default: {
                    error(context.current_file_path, type_range, "Cannot cast pointer to %s", type_description(type));

                    return { false };
                } break;
            }
        } break;

        default: {
            error(context.current_file_path, value_range, "Cannot cast from %s", type_description(value_type));

            return { false };
        } break;
    }

    return {
        true,
        result
    };
}

struct RegisterRepresentation {
    bool is_in_register;

    RegisterSize value_size;
};

static RegisterRepresentation get_type_representation(GenerationContext context, Type type) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return {
                true,
                type.integer.size
            };
        } break;

        case TypeCategory::Boolean: {
            return {
                true,
                context.default_integer_size
            };
        } break;

        case TypeCategory::Pointer: {
            return {
                true,
                context.address_integer_size
            };
        } break;

        case TypeCategory::Array:
        case TypeCategory::StaticArray:
        case TypeCategory::Struct: {
            RegisterRepresentation representation;
            representation.is_in_register = false;

            return representation;
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<Type> coerce_to_default_type(GenerationContext context, FileRange range, Type type) {
    switch(type.category) {
        case TypeCategory::Integer: {
            if(type.integer.is_undetermined) {
                Type type;
                type.category = TypeCategory::Integer;
                type.integer = {
                    context.default_integer_size,
                    true,
                    false
                };

                return {
                    true,
                    type
                };
            } else {
                return {
                    true,
                    type
                };
            }
        } break;

        case TypeCategory::Struct: {
            if(type._struct.is_undetermined) {
                error(context.current_file_path, range, "Undetermined struct types cannot exist at runtime");

                return { false };
            }

            return {
                true,
                type
            };
        } break;

        default: {
            return {
                true,
                type
            };
        } break;
    }
}

static Result<Type> evaluate_type_expression(GenerationContext *context, Expression expression);

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(context, expression.named_reference);
        } break;

        case ExpressionType::MemberReference: {
            expect(expression_value, evaluate_constant_expression(context, *expression.member_reference.expression));

            switch(expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        Type type;
                        type.category = TypeCategory::Integer;
                        type.integer = {
                            context->address_integer_size,
                            false
                        };

                        ConstantValue value;
                        value.integer = expression_value.value.array.length;

                        return {
                            true,
                            {
                                type,
                                value
                            }
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        Type type;
                        type.category = TypeCategory::Pointer;
                        type.pointer = expression_value.type.array;

                        ConstantValue value;
                        value.pointer = expression_value.value.array.length;

                        return {
                            true,
                            {
                                type,
                                value
                            }
                        };
                    } else {
                        error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::StaticArray: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        Type type;
                        type.category = TypeCategory::Integer;
                        type.integer = {
                            context->address_integer_size,
                            false
                        };

                        ConstantValue value;
                        value.integer = expression_value.type.static_array.length;

                        return {
                            true,
                            {
                                type,
                                value
                            }
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        error(context->current_file_path, expression.member_reference.name.range, "Cannot access the 'pointer' member in a constant context");

                        return { false };
                    } else {
                        error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    if(expression_value.type._struct.is_undetermined) {
                        for(size_t i = 0; i < expression_value.type._struct.members.count; i += 1) {
                            if(strcmp(expression_value.type._struct.members[i].name, expression.member_reference.name.text) == 0) {
                                return {
                                    true,
                                    {
                                        expression_value.type._struct.members[i].type,
                                        expression_value.value.struct_[i]
                                    }
                                };
                            }
                        }
                    } else {
                        auto struct_type = retrieve_struct_type(*context, expression_value.type._struct.name);

                        if(struct_type.is_union) {
                            error(context->current_file_path, expression.member_reference.expression->range, "Cannot access constant union members");

                            return { false };
                        }

                        for(size_t i = 0; i < struct_type.members.count; i += 1) {
                            if(strcmp(struct_type.members[i].name.text, expression.member_reference.name.text) == 0) {
                                return {
                                    true,
                                    {
                                        struct_type.members[i].type,
                                        expression_value.value.struct_[i]
                                    }
                                };
                            }
                        }
                    }

                    error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                case TypeCategory::FileModule: {
                    for(auto statement : expression_value.value.file_module.statements) {
                        if(match_declaration(statement, expression.member_reference.name.text)) {
                            auto old_is_top_level = context->is_top_level;
                            auto old_determined_declaration = context->determined_declaration;
                            auto old_top_level_statements = context->top_level_statements;
                            auto old_current_file_path = context->current_file_path;

                            context->is_top_level = true;
                            context->top_level_statements = expression_value.value.file_module.statements;
                            context->current_file_path = expression_value.value.file_module.path;

                            expect(value, resolve_declaration(context, statement));

                            context->current_file_path = old_current_file_path;
                            context->top_level_statements = old_top_level_statements;
                            context->determined_declaration = old_determined_declaration;
                            context->is_top_level = old_is_top_level;

                            return {
                                true,
                                value
                            };
                        }
                    }

                    error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    error(context->current_file_path, expression.member_reference.expression->range, "%s has no members", type_description(expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, evaluate_constant_expression(context, *expression.index_reference.expression));

            expect(index, evaluate_constant_expression(context, *expression.index_reference.index));

            return evaluate_constant_index(
                *context,
                expression_value.type,
                expression_value.value,
                expression.index_reference.expression->range,
                index.type,
                index.value,
                expression.index_reference.index->range
            );
        } break;

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer.is_undetermined = true;

            ConstantValue value;
            value.integer = expression.integer_literal;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case ExpressionType::StringLiteral: {
            Type array_type;
            array_type.category = TypeCategory::Integer;
            array_type.integer = {
                RegisterSize::Size8,
                false,
                false
            };

            Type type;
            type.category = TypeCategory::StaticArray;
            type.static_array = {
                expression.string_literal.count,
                heapify(array_type)
            };

            auto characters = allocate<ConstantValue>(expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            ConstantValue value;
            value.static_array = characters;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(context->current_file_path, expression.range, "Empty array literal");

                return { false };
            }

            auto elements = allocate<TypedConstantValue>(expression.array_literal.count);

            expect(first_element, evaluate_constant_expression(context, expression.array_literal[0]));
            elements[0] = first_element;

            auto element_type = first_element.type;
            for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                expect(element, evaluate_constant_expression(context, expression.array_literal[i]));

                if(is_type_undetermined(element_type) && !is_type_undetermined(element.type)) {
                    element_type = element.type;
                }

                elements[i] = element;
            }

            expect(determined_element_type, coerce_to_default_type(*context, expression.range, element_type));

            auto element_values = allocate<ConstantValue>(expression.array_literal.count);

            for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                expect(element_value, coerce_constant_to_type(
                    *context,
                    expression.array_literal.elements[i].range,
                    elements[i],
                    element_type,
                    false
                ));

                element_values[i] = element_value;
            }

            TypedConstantValue value;
            value.type.category = TypeCategory::StaticArray;
            value.type.static_array = {
                expression.array_literal.count,
                heapify(determined_element_type)
            };
            value.value.static_array = element_values;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StructLiteral: {
            if(expression.struct_literal.count == 0) {
                error(context->current_file_path, expression.range, "Empty struct literal");

                return { false };
            }

            auto type_members = allocate<StructTypeMember>(expression.struct_literal.count);
            auto member_values = allocate<ConstantValue>(expression.struct_literal.count);

            for(size_t i = 0; i < expression.struct_literal.count; i += 1) {
                for(size_t j = 0; j < i; j += 1) {
                    if(strcmp(expression.struct_literal[i].name.text, type_members[j].name) == 0) {
                        error(context->current_file_path, expression.struct_literal[i].name.range, "Duplicate struct member %s", expression.struct_literal[i].name.text);

                        return { false };
                    }
                }

                expect(member, evaluate_constant_expression(context, expression.struct_literal[i].value));

                type_members[i] = {
                    expression.struct_literal[i].name.text,
                    member.type
                };

                member_values[i] = member.value;
            }

            Type type;
            type.category = TypeCategory::Struct;
            type._struct.is_undetermined = true;
            type._struct.members = {
                expression.struct_literal.count,
                type_members
            };

            ConstantValue value;
            value.struct_ = member_values;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case ExpressionType::FunctionCall: {
            error(context->current_file_path, expression.range, "Function calls not allowed in global context");

            return { false };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, evaluate_constant_expression(context, *expression.binary_operation.left));

            expect(right, evaluate_constant_expression(context, *expression.binary_operation.right));

            expect(value, evaluate_constant_binary_operation(
                *context,
                expression.range,
                expression.binary_operation.binary_operator,
                expression.binary_operation.left->range,
                left.type,
                left.value,
                expression.binary_operation.right->range,
                right.type,
                right.value
            ));

            return {
                true,
                value
            };
        } break;

        case ExpressionType::UnaryOperation: {
            expect(expression_value, evaluate_constant_expression(context, *expression.unary_operation.expression));

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    if(expression_value.type.category != TypeCategory::Type) {
                        error(context->current_file_path, expression.unary_operation.expression->range, "Cannot take pointers to constants of type %s", type_description(expression_value.type));

                        return { false };
                    }

                    Type type;
                    type.category = TypeCategory::Type;

                    ConstantValue value;
                    value.type.category = TypeCategory::Pointer;
                    value.type.pointer = heapify(expression_value.value.type);

                    return {
                        true,
                        {
                            type,
                            value
                        }
                    };
                } break;

                case UnaryOperator::BooleanInvert: {
                    if(expression_value.type.category != TypeCategory::Boolean) {
                        error(context->current_file_path, expression.unary_operation.expression->range, "Expected a boolean, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    ConstantValue value;
                    value.boolean = !expression_value.value.boolean;

                    return {
                        true,
                        {
                            expression_value.type,
                            value
                        }
                    };
                } break;

                case UnaryOperator::Negation: {
                    if(expression_value.type.category != TypeCategory::Integer) {
                        error(context->current_file_path, expression.unary_operation.expression->range, "Expected an integer, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    ConstantValue value;
                    value.integer = -expression_value.value.integer;

                    return {
                        true,
                        {
                            expression_value.type,
                            value
                        }
                    };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::Cast: {
            expect(expression_value, evaluate_constant_expression(context, *expression.cast.expression));

            expect(type, evaluate_type_expression(context, *expression.cast.type));

            expect(value, evaluate_constant_conversion(
                *context,
                expression_value.value,
                expression_value.type,
                expression.cast.expression->range,
                type,
                expression.cast.type->range
            ));

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case ExpressionType::ArrayType: {
            expect(type, evaluate_type_expression(context, *expression.array_type.expression));

            auto type_representation = get_type_representation(*context, type);

            if(!type_representation.is_in_register) {
                error(context->current_file_path, expression.array_type.expression->range, "Cannot have arrays of type %s", type_description(type));

                return { false };
            }

            TypedConstantValue value;
            value.type.category = TypeCategory::Type;

            if(expression.array_type.index != nullptr) {
                expect(index_value, evaluate_constant_expression(context, *expression.array_type.index));

                expect(length, coerce_constant_to_integer_type(
                    *context,
                    expression.array_type.index->range,
                    index_value,
                    context->address_integer_size,
                    false,
                    false
                ));

                value.value.type.category = TypeCategory::StaticArray;
                value.value.type.static_array = {
                    length,
                    heapify(type)
                };
            } else {
                value.value.type.category = TypeCategory::Array;
                value.value.type.array = heapify(type);
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionType: {
            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                auto parameter = expression.function_type.parameters[i];

                if(parameter.is_polymorphic_determiner) {
                    error(context->current_file_path, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                    return { false };
                }

                expect(type, evaluate_type_expression(context, parameter.type));

                parameters[i] = type;
            }

            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                expect(return_type_value, evaluate_type_expression(context, *expression.function_type.return_type));

                return_type = return_type_value;
            }

            TypedConstantValue value;
            value.type.category = TypeCategory::Type;
            value.value.type.category = TypeCategory::Function;
            value.value.type.function = {
                false,
                expression.function_type.parameters.count,
                parameters,
                heapify(return_type)
            };

            return {
                true,
                value
            };
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<Type> evaluate_type_expression(GenerationContext *context, Expression expression) {
    expect(expression_value, evaluate_constant_expression(context, expression));

    if(expression_value.type.category != TypeCategory::Type) {
        error(context->current_file_path, expression.range, "Expected a type, got %s", type_description(expression_value.type));

        return { false };
    }

    return {
        true,
        expression_value.value.type
    };
}

static bool register_global_name(GenerationContext *context, const char *name, FileRange name_range) {
    for(auto global_name : context->global_names) {
        if(strcmp(global_name, name) == 0) {
            error(context->current_file_path, name_range, "Duplicate global name %s", name);

            return false;
        }
    }

    append(&(context->global_names), name);

    return true;
}

static const char *get_declaration_name(Statement declaration){
    switch(declaration.type) {
        case StatementType::FunctionDeclaration: {
            return declaration.function_declaration.name.text;
        } break;

        case StatementType::ConstantDefinition: {
            return declaration.constant_definition.name.text;
        } break;

        case StatementType::StructDefinition: {
            return declaration.struct_definition.name.text;
        } break;

        case StatementType::Import: {
            return path_get_file_component(declaration.import);
        } break;

        default: {
            abort();
        }
    }
}

static const char* generate_mangled_name(GenerationContext context, Statement declaration) {
    char *buffer{};

    string_buffer_append(&buffer, get_declaration_name(declaration));

    if(declaration.is_top_level) {
        string_buffer_append(&buffer, "_");
        string_buffer_append(&buffer, path_get_file_component(context.current_file_path));
    } else {
        auto current = *declaration.parent;

        while(true) {
            string_buffer_append(&buffer, "_");
            string_buffer_append(&buffer, get_declaration_name(current));

            if(current.is_top_level) {
                string_buffer_append(&buffer, "_");
                string_buffer_append(&buffer, path_get_file_component(context.current_file_path));

                break;
            }
        }
    }

    return buffer;
}

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Statement declaration) {
    switch(declaration.type) {
        case StatementType::FunctionDeclaration: {
            auto parameterTypes = allocate<Type>(declaration.function_declaration.parameters.count);

            for(auto parameter : declaration.function_declaration.parameters) {
                if(parameter.is_polymorphic_determiner) {
                    Type type;
                    type.category = TypeCategory::Function;
                    type.function.is_polymorphic = true;
                    type.function.parameter_count = declaration.function_declaration.parameters.count;

                    ConstantValue value;
                    value.function = {
                        declaration,
                        context->determined_declaration,
                        context->current_file_path
                    };

                    return {
                        true,
                        {
                            type,
                            value
                        }
                    };
                }
            }

            for(size_t i = 0; i < declaration.function_declaration.parameters.count; i += 1) {
                expect(type, evaluate_type_expression(context, declaration.function_declaration.parameters[i].type));

                parameterTypes[i] = type;
            }

            Type return_type;
            if(declaration.function_declaration.has_return_type) {
                expect(return_type_value, evaluate_type_expression(context, declaration.function_declaration.return_type));

                return_type = return_type_value;
            } else {
                return_type.category = TypeCategory::Void;
            }

            Type type;
            type.category = TypeCategory::Function;
            type.function = {
                false,
                declaration.function_declaration.parameters.count,
                parameterTypes,
                heapify(return_type)
            };

            ConstantValue value;
            value.function = {
                declaration,
                context->determined_declaration,
                context->current_file_path
            };

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case StatementType::ConstantDefinition: {
            expect(expression_value, evaluate_constant_expression(context, declaration.constant_definition.expression));

            return {
                true,
                expression_value
            };
        } break;

        case StatementType::Import: {
            auto source_file_directory = path_get_directory_component(context->current_file_path);

            char *import_file_path{};

            string_buffer_append(&import_file_path, source_file_directory);
            string_buffer_append(&import_file_path, declaration.import);

            expect(import_file_path_absolute, path_relative_to_absolute(import_file_path));

            auto already_parsed = false;
            Array<Statement> statements;
            for(auto file : context->parsed_files) {
                if(strcmp(file.path, import_file_path_absolute) == 0) {
                    already_parsed = true;
                    statements = file.statements;

                    break;
                }
            }

            if(!already_parsed) {
                expect(tokens, tokenize_source(import_file_path_absolute));

                expect(new_statements, parse_tokens(import_file_path_absolute, tokens));

                append(&context->parsed_files, {
                    import_file_path_absolute,
                    new_statements
                });

                statements = new_statements;
            }

            Type type;
            type.category = TypeCategory::FileModule;

            ConstantValue value;
            value.file_module = {
                import_file_path_absolute,
                statements
            };

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case StatementType::StructDefinition: {
            auto mangled_name = generate_mangled_name(*context, declaration);

            auto is_registered = false;
            for(auto struct_type : context->struct_types) {
                if(strcmp(struct_type.name, mangled_name) == 0) {
                    is_registered = true;
                }
            }

            if(!is_registered) {
                auto members = allocate<DeterminedStructTypeMember>(declaration.struct_definition.members.count);

                for(size_t i = 0; i < declaration.struct_definition.members.count; i += 1) {
                    for(size_t j = 0; j < declaration.struct_definition.members.count; j += 1) {
                        if(j != i && strcmp(declaration.struct_definition.members[i].name.text, declaration.struct_definition.members[j].name.text) == 0) {
                            error(context->current_file_path, declaration.struct_definition.members[i].name.range, "Duplicate struct member name %s", declaration.struct_definition.members[i].name.text);

                            return { false };
                        }
                    }

                    expect(value, evaluate_type_expression(context, declaration.struct_definition.members[i].type));

                    members[i] = {
                        declaration.struct_definition.members[i].name,
                        value
                    };
                }

                append(&context->struct_types, {
                    mangled_name,
                    declaration.struct_definition.is_union,
                    {
                        declaration.struct_definition.members.count,
                        members
                    }
                });
            }

            Type type;
            type.category = TypeCategory::Type;

            ConstantValue value;
            value.type.category = TypeCategory::Struct;
            value.type._struct.is_undetermined = false;
            value.type._struct.name = mangled_name;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        default: {
            abort();
        } break;
    }
}

static bool add_new_variable(GenerationContext *context, Identifier name, size_t address_register, Type type, FileRange type_range) {
    auto variable_context = &(context->variable_context_stack[context->variable_context_stack.count - 1]);

    for(auto variable : *variable_context) {
        if(strcmp(variable.name.text, name.text) == 0) {
            error(context->current_file_path, name.range, "Duplicate variable name %s", name.text);
            error(context->current_file_path, variable.name.range, "Original declared here");

            return false;
        }
    }

    append(variable_context, Variable {
        name,
        type,
        type_range,
        address_register
    });

    return true;
}

enum struct ValueCategory {
    Constant,
    Anonymous,
    Address
};

struct TypedValue;

struct Value {
    ValueCategory category;

    union {
        struct {
            size_t register_;

            Value *undetermined_struct;
        } anonymous;

        size_t address;

        ConstantValue constant;
    };
};

struct TypedValue {
    Type type;

    Value value;
};

static size_t allocate_register(GenerationContext *context) {
    auto index = context->next_register;

    context->next_register += 1;

    return index;
}

static void write_integer(uint8_t *buffer, size_t offset, RegisterSize size, uint64_t value) {
    if(size >= RegisterSize::Size8) {
        buffer[offset] = value;
    } else if(size >= RegisterSize::Size16) {
        buffer[offset + 1] = (value >> 8);
    } else if(size >= RegisterSize::Size32) {
        buffer[offset + 2] = (value >> 16);
        buffer[offset + 3] = (value >> 24);
    } else if(size == RegisterSize::Size64) {
        buffer[offset + 4] = (value >> 32);
        buffer[offset + 5] = (value >> 40);
        buffer[offset + 6] = (value >> 48);
        buffer[offset + 7] = (value >> 56);
    } else {
        abort();
    }
}

static void write_static_array(GenerationContext context, uint8_t *data, size_t offset, Type type, Array<ConstantValue> elements);

static void write_struct(GenerationContext context, uint8_t *data, size_t offset, StructType struct_type, ConstantValue *members) {
    for(size_t i = 0; i < struct_type.members.count; i += 1) {
        auto member_offset = get_struct_member_offset(context, struct_type, i);

        auto representation = get_type_representation(context, struct_type.members[i].type);

        auto size = get_type_size(context, struct_type.members[i].type);

        if(representation.is_in_register) {
            uint64_t value;
            switch(struct_type.members[i].type.category) {
                case TypeCategory::Integer: {
                    value = members[i].integer;
                } break;

                case TypeCategory::Boolean: {
                    value = members[i].boolean;
                } break;

                case TypeCategory::Pointer: {
                    value = members[i].pointer;
                } break;

                default: {
                    abort();
                } break;
            }

            write_integer(data, offset + member_offset, representation.value_size, value);
        } else {
            switch(struct_type.members[i].type.category) {
                case TypeCategory::Array: {
                    write_integer(data, offset + member_offset, context.address_integer_size, members[i].array.pointer);
                    write_integer(data, offset + member_offset + register_size_to_byte_size(context.address_integer_size), context.address_integer_size, members[i].array.length);
                } break;

                case TypeCategory::StaticArray: {
                    write_static_array(context, data, offset + member_offset, *struct_type.members[i].type.static_array.type, { struct_type.members[i].type.static_array.length, members[i].static_array });
                } break;

                case TypeCategory::Struct: {
                    write_struct(context, data, offset + member_offset, retrieve_struct_type(context, struct_type.members[i].type._struct.name), members[i].struct_);
                } break;
            }
        }
    }
}

static void write_static_array(GenerationContext context, uint8_t *data, size_t offset, Type type, Array<ConstantValue> elements) {
    auto representation = get_type_representation(context, type);

    auto element_size = get_type_size(context, type);

    for(size_t i = 0; i < elements.count; i += 1) {
        if(representation.is_in_register) {
            uint64_t value;
            switch(type.category) {
                case TypeCategory::Integer: {
                    value = elements[i].integer;
                } break;

                case TypeCategory::Boolean: {
                    value = elements[i].boolean;
                } break;

                case TypeCategory::Pointer: {
                    value = elements[i].pointer;
                } break;

                default: {
                    abort();
                } break;
            }

            write_integer(data, offset + i * element_size, representation.value_size, value);
        } else {
            switch(type.category) {
                case TypeCategory::Array: {
                    write_integer(data, offset + i * element_size, context.address_integer_size, elements[i].array.pointer);
                    write_integer(data, offset + i * element_size + register_size_to_byte_size(context.address_integer_size), context.address_integer_size, elements[i].array.length);
                } break;

                case TypeCategory::StaticArray: {
                    write_static_array(context, data, offset + i * element_size, *type.static_array.type, { type.static_array.length, elements[i].static_array });
                } break;

                case TypeCategory::Struct: {
                    write_struct(context, data, offset + i * element_size, retrieve_struct_type(context, type._struct.name), elements[i].struct_);
                } break;
            }
        }
    }
}

static const char *register_static_array_constant(GenerationContext *context, Type type, Array<ConstantValue> elements) {
    auto element_size = get_type_size(*context, type);
    auto element_alignment = get_type_alignment(*context, type);

    auto data = allocate<uint8_t>(element_size);

    write_static_array(*context, data, 0, type, elements);

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, context->static_constants.count);

    append(&context->static_constants, {
        name_buffer,
        element_alignment,
        {
            elements.count * element_size,
            data
        }
    });

    return name_buffer;
}

static const char *register_struct_constant(GenerationContext *context, StructType struct_type, ConstantValue *members) {
    auto length = get_struct_size(*context, struct_type);
    auto alignment = get_struct_alignment(*context, struct_type);

    auto data = allocate<uint8_t>(length);

    write_struct(*context, data, 0, struct_type, members);

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, context->static_constants.count);

    append(&context->static_constants, {
        name_buffer,
        alignment,
        {
            length,
            data
        }
    });

    return name_buffer;
}

static size_t append_arithmetic_operation(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    ArithmeticOperationType type,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    Instruction arithmetic_operation;
    arithmetic_operation.type = InstructionType::ArithmeticOperation;
    arithmetic_operation.line = line;
    arithmetic_operation.arithmetic_operation = {
        type,
        size,
        source_register_a,
        source_register_b,
        destination_register
    };

    append(instructions, arithmetic_operation);

    return destination_register;
}

static size_t append_comparison_operation(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    ComparisonOperationType type,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    Instruction comparison_operation;
    comparison_operation.type = InstructionType::ComparisonOperation;
    comparison_operation.line = line;
    comparison_operation.comparison_operation = {
        type,
        size,
        source_register_a,
        source_register_b,
        destination_register
    };

    append(instructions, comparison_operation);

    return destination_register;
}

static size_t append_constant(GenerationContext *context, List<Instruction> *instructions, unsigned int line, RegisterSize size, uint64_t value) {
    auto destination_register = allocate_register(context);

    Instruction constant;
    constant.type = InstructionType::Constant;
    constant.line = line;
    constant.constant = {
        size,
        destination_register,
        value
    };

    append(instructions, constant);

    return destination_register;
}

static size_t append_integer_upcast(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    Instruction integer_upcast;
    integer_upcast.type = InstructionType::IntegerUpcast;
    integer_upcast.line = line;
    integer_upcast.integer_upcast = {
        is_signed,
        source_size,
        source_register,
        destination_size,
        destination_register
    };

    append(instructions, integer_upcast);

    return destination_register;
}

static size_t append_reference_static(GenerationContext *context, List<Instruction> *instructions, unsigned int line, const char *name) {
    auto destination_register = allocate_register(context);

    Instruction reference_static;
    reference_static.type = InstructionType::ReferenceStatic;
    reference_static.line = line;
    reference_static.reference_static = {
        name,
        destination_register
    };

    append(instructions, reference_static);

    return destination_register;
}

static size_t append_allocate_local(GenerationContext *context, List<Instruction> *instructions, unsigned int line, size_t size, size_t alignment) {
    auto destination_register = allocate_register(context);

    Instruction allocate_local;
    allocate_local.type = InstructionType::AllocateLocal;
    allocate_local.line = line;
    allocate_local.allocate_local = {
        size,
        alignment,
        destination_register
    };

    append(instructions, allocate_local);

    return destination_register;
}

static void append_branch(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    size_t condition_register,
    size_t instruction_index
) {
    Instruction branch;
    branch.type = InstructionType::Branch;
    branch.line = line;
    branch.branch = {
        condition_register,
        instruction_index
    };

    append(instructions, branch);
}

static void append_jump(GenerationContext *context, List<Instruction> *instructions, unsigned int line, size_t instruction_index) {
    Instruction jump;
    jump.type = InstructionType::Jump;
    jump.line = line;
    jump.branch = {
        instruction_index
    };

    append(instructions, jump);
}

static void append_copy_memory(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    size_t length_register,
    size_t source_address_register,
    size_t destination_address_register
) {
    Instruction copy_memory;
    copy_memory.type = InstructionType::CopyMemory;
    copy_memory.line = line;
    copy_memory.copy_memory = {
        length_register,
        source_address_register,
        destination_address_register
    };

    append(instructions, copy_memory);
}

static size_t append_load_integer(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    Instruction load_integer;
    load_integer.type = InstructionType::LoadInteger;
    load_integer.line = line;
    load_integer.load_integer = {
        size,
        address_register,
        destination_register
    };

    append(instructions, load_integer);

    return destination_register;
}

static void append_store_integer(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    Instruction store_integer;
    store_integer.type = InstructionType::StoreInteger;
    store_integer.line = line;
    store_integer.store_integer = {
        size,
        source_register,
        address_register
    };

    append(instructions, store_integer);
}

static size_t generate_address_offset(GenerationContext *context, List<Instruction> *instructions, FileRange range, size_t address_register, size_t offset) {
    auto offset_register = append_constant(
        context,
        instructions,
        range.first_line,
        context->address_integer_size,
        offset
    );

    auto final_address_register = append_arithmetic_operation(
        context,
        instructions,
        range.first_line,
        ArithmeticOperationType::Add,
        context->address_integer_size,
        address_register,
        offset_register
    );

    return final_address_register;
}

static size_t generate_boolean_invert(GenerationContext *context, List<Instruction> *instructions, FileRange range, size_t value_register) {
    auto local_register = append_allocate_local(
        context,
        instructions,
        range.first_line,
        register_size_to_byte_size(context->default_integer_size),
        register_size_to_byte_size(context->default_integer_size)
    );

    append_branch(context, instructions, range.first_line, value_register, instructions->count + 4);

    auto true_register = append_constant(context, instructions, range.first_line, context->default_integer_size, 1);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, true_register, local_register);

    append_jump(context, instructions, range.first_line, instructions->count + 3);

    auto false_register = append_constant(context, instructions, range.first_line, context->default_integer_size, 0);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range.first_line, context->default_integer_size, local_register);

    return result_register;
}

static size_t generate_in_register_constant_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, Type type, ConstantValue value) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return append_constant(context, instructions, range.first_line, type.integer.size, value.integer);
        } break;

        case TypeCategory::Boolean: {
            return append_constant(context, instructions, range.first_line, context->default_integer_size, value.boolean);
        } break;
        
        case TypeCategory::Pointer: {
            return append_constant(context, instructions, range.first_line, context->address_integer_size, value.pointer);
        } break;

        default: {
            abort();
        } break;
    }
}

static void generate_not_in_register_constant_write(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value,
    size_t address_register
) {
    switch(type.category) {
        case TypeCategory::Array: {
            auto pointer_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.pointer);

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

            auto length_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.length);

            auto length_address_register = generate_address_offset(
                context,
                instructions,
                range,
                address_register,
                register_size_to_byte_size(context->address_integer_size)
            );

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);
        } break;

        case TypeCategory::StaticArray: {
            auto constant_name = register_static_array_constant(
                context,
                *type.static_array.type,
                Array<ConstantValue> {
                    type.static_array.length,
                    value.static_array
                }
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            auto length_register = append_constant(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                type.static_array.length * get_type_size(*context, *type.static_array.type)
            );

            append_copy_memory(context, instructions, range.first_line, length_register, constant_address_register, address_register);
        } break;

        case TypeCategory::Struct: {
            auto struct_type = retrieve_struct_type(*context, type._struct.name);

            auto constant_name = register_struct_constant(
                context,
                struct_type,
                value.struct_
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            auto length_register = append_constant(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                get_struct_size(*context, struct_type)
            );

            append_copy_memory(context, instructions, range.first_line, length_register, constant_address_register, address_register);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_not_in_register_constant_value(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value
) {
    switch(type.category) {
        case TypeCategory::Array: {
            auto address_register = append_allocate_local(
                context,
                instructions,
                range.first_line,
                2 * register_size_to_byte_size(context->address_integer_size),
                register_size_to_byte_size(context->address_integer_size)
            );

            auto pointer_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.pointer);

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

            auto length_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.length);

            auto length_address_register = generate_address_offset(
                context,
                instructions,
                range,
                address_register,
                register_size_to_byte_size(context->address_integer_size)
            );

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);

            return address_register;
        } break;

        case TypeCategory::StaticArray: {
            auto constant_name = register_static_array_constant(
                context,
                *type.static_array.type,
                Array<ConstantValue> {
                    type.static_array.length,
                    value.static_array
                }
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            return constant_address_register;
        } break;

        case TypeCategory::Struct: {
            auto struct_type = retrieve_struct_type(*context, type._struct.name);

            auto constant_name = register_struct_constant(
                context,
                struct_type,
                value.struct_
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            return constant_address_register;
        } break;

        default: {
            abort();
        } break;
    }
}

static void generate_constant_value_write(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value,
    size_t address_register
) {
    auto representation = get_type_representation(*context, type);

    if(representation.is_in_register) {
        auto value_register = generate_in_register_constant_value(context, instructions, range, type, value);

        append_store_integer(context, instructions, range.first_line, representation.value_size, value_register, address_register);
    } else {
        generate_not_in_register_constant_write(context, instructions, range, type, value, address_register);
    }
}

static Result<Value> coerce_to_integer_type(
    GenerationContext context,
    FileRange range,
    TypedValue value,
    RegisterSize size,
    bool is_signed,
    bool probing
) {
    switch(value.type.category) {
        case TypeCategory::Integer: {
            if(value.type.integer.is_undetermined) {
                Value result;
                result.category = ValueCategory::Constant;
                result.constant.integer = value.value.constant.integer;

                return {
                    true,
                    result
                };
            } else {
                if(value.type.integer.size != size || value.type.integer.is_signed != is_signed) {
                    return { false };
                }

                return {
                    true,
                    value.value
                };
            }
        } break;

        default: {
            if(!probing) {
                error(context.current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), determined_integer_type_description(size, is_signed));
            }

            return { false };
        } break;
    }
}

static Result<Value> coerce_to_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    Type type,
    bool probing
);

static Result<Value> coerce_to_struct_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    StructType struct_type,
    bool probing
) {
    if(value.type.category != TypeCategory::Struct) {
        if(!probing) {
            error(context->current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), struct_type.name);
        }

        return { false };
    }

    auto value_type = value.type._struct;

    Value result_value;
    if(value_type.is_undetermined) {
        if(struct_type.is_union) {
            if(value_type.members.count != 1) {
                error(context->current_file_path, range, "Too many union members. Expected 1, got %zu", value_type.members.count);

                return { false };
            }

            auto found = false;
            size_t member_index;
            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                if(strcmp(value_type.members[0].name, struct_type.members[i].name.text) == 0) {
                    found = true;
                    member_index = i;

                    break;
                }
            }

            if(!found) {
                error(context->current_file_path, range, "Unknown union member '%s'", value_type.members[0].name);

                return { false };
            }

            Value member_value;
            switch(value.value.category) {
                case ValueCategory::Constant: {
                    member_value.category = ValueCategory::Constant;
                    member_value.constant = value.value.constant.struct_[0];
                } break;

                case ValueCategory::Anonymous: {
                    member_value = value.value.anonymous.undetermined_struct[0];
                } break;

                default: {
                    abort();
                } break;
            }

            expect(coerced_member_value, coerce_to_type(
                context,
                instructions,
                range,
                { value_type.members[0].type, member_value },
                struct_type.members[member_index].type,
                probing
            ));

            switch(coerced_member_value.category) {
                case ValueCategory::Constant: {
                    result_value.category = ValueCategory::Constant;
                    result_value.constant.struct_ = heapify(coerced_member_value.constant);
                } break;

                case ValueCategory::Anonymous: {
                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        range.first_line,
                        get_struct_size(*context, struct_type),
                        get_struct_alignment(*context, struct_type)
                    );

                    auto member_representation = get_type_representation(*context, struct_type.members[member_index].type);

                    if(member_representation.is_in_register) {
                        append_store_integer(
                            context,
                            instructions,
                            range.first_line,
                            member_representation.value_size,
                            coerced_member_value.anonymous.register_,
                            address_register
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, struct_type.members[member_index].type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            range.first_line,
                            length_register,
                            coerced_member_value.anonymous.register_,
                            address_register
                        );
                    }

                    result_value.category = ValueCategory::Anonymous;
                    result_value.anonymous.register_ = address_register;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            if(value_type.members.count != struct_type.members.count) {
                error(context->current_file_path, range, "Too many struct members. Expected %zu, got %zu", struct_type.members.count, value_type.members.count);

                return { false };
            }

            auto all_constant = false;
            auto coerced_member_values = allocate<Value>(struct_type.members.count);

            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                if(strcmp(value_type.members[i].name, struct_type.members[i].name.text) != 0) {
                    error(context->current_file_path, range, "Incorrect struct member name. Expected '%s', got '%s", struct_type.members[i].name.text, value_type.members[i].name);

                    return { false };
                }

                Value member_value;
                switch(value.value.category) {
                    case ValueCategory::Constant: {
                        member_value.category = ValueCategory::Constant;
                        member_value.constant = value.value.constant.struct_[i];
                    } break;

                    case ValueCategory::Anonymous: {
                        member_value = value.value.anonymous.undetermined_struct[i];
                    } break;

                    default: {
                        abort();
                    } break;
                }

                expect(coerced_member_value, coerce_to_type(
                    context,
                    instructions,
                    range,
                    { value_type.members[i].type, member_value },
                    struct_type.members[i].type,
                    probing
                ));

                if(coerced_member_value.category != ValueCategory::Constant) {
                    all_constant = false;
                }

                coerced_member_values[i] = coerced_member_value;
            }

            if(all_constant) {
                auto member_values = allocate<ConstantValue>(struct_type.members.count);

                for(size_t i = 0; i < struct_type.members.count; i += 1) {
                    member_values[i] = coerced_member_values[i].constant;
                }

                result_value.category = ValueCategory::Constant;
                result_value.constant.struct_ = member_values;
            } else {
                auto address_register = append_allocate_local(
                    context,
                    instructions,
                    range.first_line,
                    get_struct_size(*context, struct_type),
                    get_struct_alignment(*context, struct_type)
                );

                for(size_t i = 0; i < struct_type.members.count; i += 1) {
                    auto offset = get_struct_member_offset(*context, struct_type, i);

                    auto final_address_register = generate_address_offset(context, instructions, range, address_register, offset);

                    auto member_representation = get_type_representation(*context, struct_type.members[i].type);

                    switch(coerced_member_values[i].category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                range,
                                struct_type.members[i].type,
                                coerced_member_values[i].constant,
                                final_address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(member_representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    coerced_member_values[i].anonymous.register_,
                                    final_address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, struct_type.members[i].type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    range.first_line,
                                    length_register,
                                    coerced_member_values[i].anonymous.register_,
                                    final_address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(member_representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    coerced_member_values[i].address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    value_register,
                                    final_address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, struct_type.members[i].type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    range.first_line,
                                    length_register,
                                    coerced_member_values[i].address,
                                    final_address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }

                result_value.category = ValueCategory::Anonymous;
                result_value.anonymous.register_ = address_register;
            }
        }

        return {
            true,
            result_value
        };
    } else {
        if(strcmp(value_type.name, struct_type.name) != 0) {
            if(!probing) {
                error(context->current_file_path, range, "Cannot implicitly convert '%s' to '%s'", value_type.name, struct_type.name);
            }

            return { false };
        }

        return {
            true,
            value.value
        };
    }
}

static Result<Value> coerce_to_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    Type type,
    bool probing
) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return coerce_to_integer_type(*context, range, value, type.integer.size, type.integer.is_signed, probing);
        } break;

        case TypeCategory::Pointer: {
            switch(value.type.category) {
                case TypeCategory::Integer: {
                    if(value.type.integer.is_undetermined) {
                        Value result;
                        result.category = ValueCategory::Constant;
                        result.constant.pointer = value.value.constant.integer;

                        return {
                            true,
                            result
                        };
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(types_equal(*type.pointer, *value.type.pointer)) {
                        return {
                            true,
                            value.value
                        };
                    }
                } break;
            }
        } break;

        case TypeCategory::Struct: {
            return coerce_to_struct_type(context, instructions, range, value, retrieve_struct_type(*context, type._struct.name), probing);
        } break;

        default: {
            if(types_equal(type, value.type)) {
                return {
                    true,
                    value.value
                };
            }
        } break;
    }

    if(!probing) {
        error(context->current_file_path, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), type_description(type));
    }

    return { false };
}

static size_t generate_in_register_integer_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, RegisterSize size, Value value) {
    switch(value.category) {
        case ValueCategory::Constant: {
            return append_constant(context, instructions, range.first_line, size, value.constant.integer);
        } break;

        case ValueCategory::Anonymous: {
            return value.anonymous.register_;
        } break;

        case ValueCategory::Address: {
            return append_load_integer(context, instructions, range.first_line, size, value.address);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_in_register_boolean_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, Value value) {
    switch(value.category) {
        case ValueCategory::Constant: {
            return append_constant(context, instructions, range.first_line, context->default_integer_size, value.constant.boolean);
        } break;

        case ValueCategory::Anonymous: {
            return value.anonymous.register_;
        } break;

        case ValueCategory::Address: {
            return append_load_integer(context, instructions, range.first_line, context->default_integer_size, value.address);
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<TypedValue> generate_expression(GenerationContext *context, List<Instruction> *instructions, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Address;
                        value.type = variable.type;
                        value.value.address = variable.register_index;

                        return {
                            true,
                            value
                        };
                    }
                }
            }

            for(auto parameter : context->parameters) {
                if(strcmp(parameter.name.text, expression.named_reference.text) == 0) {
                    TypedValue value;
                    value.value.category = ValueCategory::Anonymous;
                    value.type = parameter.type;
                    value.value.address = parameter.register_index;

                    return {
                        true,
                        value
                    };
                }
            }

            expect(constant, resolve_constant_named_reference(context, expression.named_reference));

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type = constant.type;
            value.value.constant = constant.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.index_reference.expression));

            expect(index, generate_expression(context, instructions, *expression.index_reference.index));

            if(expression_value.value.category == ValueCategory::Constant && index.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_index(
                    *context,
                    expression_value.type,
                    expression_value.value.constant,
                    expression.index_reference.expression->range,
                    index.type,
                    index.value.constant,
                    expression.index_reference.index->range
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = constant.type;
                value.value.constant = constant.value;

                return {
                    true,
                    value
                };
            }

            expect(index_value, coerce_to_integer_type(
                *context,
                expression.index_reference.index->range,
                index,
                context->address_integer_size,
                false,
                false
            ));

            size_t index_register;
            switch(index.value.category) {
                case ValueCategory::Constant: {
                    index_register = append_constant(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        context->address_integer_size,
                        index_value.constant.integer
                    );
                } break;

                case ValueCategory::Anonymous: {
                    index_register = index_value.anonymous.register_;
                } break;

                case ValueCategory::Address: {
                    index_register = append_load_integer(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        context->address_integer_size,
                        index_value.address
                    );
                } break;

                default: {
                    abort();
                } break;
            }

            size_t base_address_register;
            Type element_type;
            bool assignable;
            switch(expression_value.value.category) {
                case ValueCategory::Constant: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_constant(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.constant.array.pointer
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            auto constant_name = register_static_array_constant(
                                context,
                                *expression_value.type.static_array.type,
                                Array<ConstantValue> {
                                    expression_value.type.static_array.length,
                                    expression_value.value.constant.static_array
                                }
                            );

                            base_address_register = append_reference_static(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                constant_name
                            );
                            element_type = *expression_value.type.static_array.type;
                            assignable = false;
                        } break;

                        default: {
                            error(context->current_file_path, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ValueCategory::Anonymous: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_load_integer(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.anonymous.register_
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.value.anonymous.register_;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(context->current_file_path, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ValueCategory::Address: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_load_integer(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.address
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.value.address;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(context->current_file_path, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                default: {
                    abort();
                } break;
            }

            auto element_size_register = append_constant(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                context->address_integer_size,
                get_type_size(*context, element_type)
            );

            auto offset_register = append_arithmetic_operation(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                ArithmeticOperationType::UnsignedMultiply,
                context->address_integer_size,
                index_register,
                element_size_register
            );

            auto final_address_register = append_arithmetic_operation(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                ArithmeticOperationType::Add,
                context->address_integer_size,
                base_address_register,
                offset_register
            );

            TypedValue value;
            value.type = element_type;

            if(assignable) {
                value.value.category = ValueCategory::Address;
                value.value.address = final_address_register;
            } else {
                auto representation = get_type_representation(*context, element_type);

                size_t register_index;
                if(representation.is_in_register) {
                    register_index = append_load_integer(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        representation.value_size,
                        final_address_register
                    );
                } else {
                    register_index = final_address_register;
                }

                value.value.category = ValueCategory::Anonymous;
                value.value.anonymous.register_ = register_index;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::MemberReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.member_reference.expression));

            TypedValue actual_expression_value;
            if(expression_value.type.category == TypeCategory::Pointer) {
                size_t address_register;
                switch(expression_value.value.category) {
                    case ValueCategory::Constant: {
                        address_register = append_constant(
                            context,
                            instructions,
                            expression.member_reference.expression->range.first_line,
                            context->address_integer_size,
                            expression_value.value.constant.pointer
                        );
                    } break;

                    case ValueCategory::Anonymous: {
                        address_register = expression_value.value.anonymous.register_;
                    } break;

                    case ValueCategory::Address: {
                        address_register = append_load_integer(
                            context,
                            instructions,
                            expression.member_reference.expression->range.first_line,
                            context->address_integer_size,
                            expression_value.value.address
                        );
                    } break;

                    default: {
                        abort();
                    } break;
                }

                actual_expression_value.value.category = ValueCategory::Address;
                actual_expression_value.type = *expression_value.type.pointer;
                actual_expression_value.value.address = address_register;
            } else {
                actual_expression_value = expression_value;
            }

            switch(actual_expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        TypedValue value;
                        value.value.category = actual_expression_value.value.category;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = {
                            context->address_integer_size,
                            false,
                            false
                        };

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                value.value.constant.integer = actual_expression_value.value.constant.array.length;
                            } break;

                            case ValueCategory::Anonymous: {
                                auto final_address_register = generate_address_offset(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range,
                                    expression_value.value.anonymous.register_,
                                    register_size_to_byte_size(context->address_integer_size)
                                );

                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    context->address_integer_size,
                                    final_address_register
                                );

                                value.value.anonymous.register_ = value_register;
                            } break;

                            case ValueCategory::Address: {
                                auto final_address_register = generate_address_offset(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range,
                                    expression_value.value.address,
                                    register_size_to_byte_size(context->address_integer_size)
                                );

                                value.value.address = final_address_register;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        TypedValue value;
                        value.value.category = actual_expression_value.value.category;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = actual_expression_value.type.array;

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                value.value.constant.pointer = actual_expression_value.value.constant.array.pointer;
                            } break;

                            case ValueCategory::Anonymous: {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    context->address_integer_size,
                                    actual_expression_value.value.anonymous.register_
                                );

                                value.value.anonymous.register_ = value_register;
                            } break;

                            case ValueCategory::Address: {
                                value.value.anonymous.register_ = actual_expression_value.value.address;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else {
                        error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::StaticArray: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Constant;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = {
                            context->address_integer_size,
                            false,
                            false
                        };
                        value.value.constant.integer = actual_expression_value.type.static_array.length;

                        return {
                            true,
                            value
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Anonymous;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = actual_expression_value.type.static_array.type;

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                auto constant_name = register_static_array_constant(
                                    context,
                                    *actual_expression_value.type.static_array.type,
                                    {
                                        actual_expression_value.type.static_array.length,
                                        actual_expression_value.value.constant.static_array
                                    }
                                );

                                auto register_index = append_reference_static(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    constant_name
                                );

                                value.value.anonymous.register_ = register_index;
                            } break;

                            case ValueCategory::Anonymous: {
                                value.value.anonymous.register_ = actual_expression_value.value.anonymous.register_;
                            } break;

                            case ValueCategory::Address: {
                                value.value.anonymous.register_ = actual_expression_value.value.address;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else {
                        error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    if(actual_expression_value.type._struct.is_undetermined) {
                        for(size_t i = 0; i < actual_expression_value.type._struct.members.count; i += 1) {
                            if(strcmp(actual_expression_value.type._struct.members[i].name, expression.member_reference.name.text) == 0) {
                                expect(member_type, coerce_to_default_type(
                                    *context,
                                    expression.member_reference.expression->range,
                                    actual_expression_value.type._struct.members[i].type
                                ));

                                TypedValue value;
                                value.type = member_type;

                                switch(actual_expression_value.value.category) {
                                    case ValueCategory::Constant: {
                                        value.value.category = ValueCategory::Constant;
                                        value.value.constant = actual_expression_value.value.constant.struct_[i];
                                    } break;

                                    case ValueCategory::Anonymous: {
                                        value.value.category = ValueCategory::Anonymous;

                                        auto member_value = actual_expression_value.value.anonymous.undetermined_struct[i];

                                        auto representation = get_type_representation(*context, member_type);

                                        switch(member_value.category) {
                                            case ValueCategory::Constant: {
                                                size_t result_register;
                                                if(representation.is_in_register) {
                                                    result_register = generate_in_register_constant_value(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range,
                                                        member_type,
                                                        member_value.constant
                                                    );
                                                } else {
                                                    result_register = generate_not_in_register_constant_value(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range,
                                                        member_type,
                                                        member_value.constant
                                                    );
                                                }

                                                value.value.anonymous.register_ = result_register;
                                            } break;

                                            case ValueCategory::Anonymous: {
                                                value.value.anonymous.register_ = member_value.anonymous.register_;
                                            } break;

                                            case ValueCategory::Address: {
                                                if(representation.is_in_register) {
                                                    value.value.anonymous.register_ = append_load_integer(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range.first_line,
                                                        representation.value_size,
                                                        member_value.address
                                                    );
                                                } else {
                                                    value.value.anonymous.register_ = member_value.address;
                                                }
                                            } break;

                                            default: {
                                                abort();
                                            } break;
                                        }
                                    } break;

                                    default: {
                                        abort();
                                    } break;
                                }

                                return {
                                    true,
                                    value
                                };
                            }
                        }
                    } else {
                        auto struct_type = retrieve_struct_type(*context, actual_expression_value.type._struct.name);

                        for(size_t i = 0; i < struct_type.members.count; i += 1) {
                            if(strcmp(struct_type.members[i].name.text, expression.member_reference.name.text) == 0) {
                                TypedValue value;
                                value.value.category = expression_value.value.category;
                                value.type = struct_type.members[i].type;

                                auto offset = get_struct_member_offset(*context, struct_type, i);

                                switch(expression_value.value.category) {
                                    case ValueCategory::Constant: {
                                        value.value.constant = expression_value.value.constant.struct_[i];
                                    } break;

                                    case ValueCategory::Anonymous: {
                                        auto final_address_register = generate_address_offset(
                                            context,
                                            instructions,
                                            expression.member_reference.name.range,
                                            expression_value.value.anonymous.register_,
                                            offset
                                        );

                                        auto representation = get_type_representation(*context, struct_type.members[i].type);

                                        if(representation.is_in_register) {
                                            auto value_register = append_load_integer(
                                                context,
                                                instructions,
                                                expression.member_reference.name.range.first_line,
                                                representation.value_size,
                                                final_address_register
                                            );

                                            value.value.anonymous.register_ = value_register;
                                        } else {
                                            value.value.anonymous.register_ = final_address_register;
                                        }
                                    } break;

                                    case ValueCategory::Address: {
                                        auto final_address_register = generate_address_offset(
                                            context,
                                            instructions,
                                            expression.member_reference.name.range,
                                            expression_value.value.address,
                                            offset
                                        );

                                        value.value.address = final_address_register;
                                    } break;

                                    default: {
                                        abort();
                                    } break;
                                }

                                return {
                                    true,
                                    value
                                };
                            }
                        }
                    }

                    error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                case TypeCategory::FileModule: {
                    assert(actual_expression_value.value.category == ValueCategory::Constant);

                    for(auto statement : actual_expression_value.value.constant.file_module.statements) {
                        if(match_declaration(statement, expression.member_reference.name.text)) {
                            auto old_is_top_level = context->is_top_level;
                            auto old_determined_declaration = context->determined_declaration;
                            auto old_top_level_statements = context->top_level_statements;
                            auto old_current_file_path = context->current_file_path;

                            context->is_top_level = true;
                            context->top_level_statements = actual_expression_value.value.constant.file_module.statements;
                            context->current_file_path = actual_expression_value.value.constant.file_module.path;

                            expect(constant_value, resolve_declaration(context, statement));

                            context->current_file_path = old_current_file_path;
                            context->top_level_statements = old_top_level_statements;
                            context->determined_declaration = old_determined_declaration;
                            context->is_top_level = old_is_top_level;

                            TypedValue value;
                            value.value.category = ValueCategory::Constant;
                            value.type = constant_value.type;
                            value.value.constant = constant_value.value;

                            return {
                                true,
                                value
                            };
                        }
                    }

                    error(context->current_file_path, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    error(context->current_file_path, expression.member_reference.expression->range, "Type %s has no members", type_description(actual_expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IntegerLiteral: {
            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::Integer;
            value.type.integer.is_undetermined = true;
            value.value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StringLiteral: {      
            Type array_type;
            array_type.category = TypeCategory::Integer;
            array_type.integer = {
                RegisterSize::Size8,
                false,
                false
            };

            auto characters = allocate<ConstantValue>(expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::StaticArray;
            value.type.static_array = {
                expression.string_literal.count,
                heapify(array_type)
            };
            value.value.constant.static_array = characters;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(context->current_file_path, expression.range, "Empty array literal");

                return { false };
            }

            auto element_values = allocate<TypedValue>(expression.array_literal.count);
            expect(first_element_value, generate_expression(context, instructions, expression.array_literal[0]));
            element_values[0] = first_element_value;

            auto all_constant = first_element_value.value.category == ValueCategory::Constant;
            auto element_type = first_element_value.type;
            for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                expect(element_value, generate_expression(context, instructions, expression.array_literal[i]));

                if(element_value.value.category != ValueCategory::Constant) {
                    all_constant = false;
                }

                if(is_type_undetermined(element_type) && !is_type_undetermined(element_value.type)) {
                    element_type = element_value.type;
                }

                element_values[i] = element_value;
            }

            expect(determined_element_type, coerce_to_default_type(*context, expression.range, element_type));

            if(all_constant) {
                auto elements = allocate<ConstantValue>(expression.array_literal.count);

                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    expect(element_value, coerce_constant_to_type(
                        *context,
                        expression.array_literal.elements[i].range,
                        { element_values[i].type, element_values[i].value.constant },
                        element_type,
                        false
                    ));

                    elements[i] = element_value;
                }

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(determined_element_type)
                };
                value.value.constant.static_array = elements;

                return {
                    true,
                    value
                };
            } else {
                auto element_size = get_type_size(*context, element_type);

                auto base_address_register = append_allocate_local(
                    context,
                    instructions,
                    expression.range.first_line,
                    element_size * expression.array_literal.count,
                    get_type_alignment(*context, element_type)
                );

                auto element_size_register = append_constant(
                    context,
                    instructions,
                    expression.range.first_line,
                    context->address_integer_size,
                    element_size
                );

                auto representation = get_type_representation(*context, element_type);

                size_t length_register;
                if(representation.is_in_register) {
                    length_register = append_constant(
                        context,
                        instructions,
                        expression.range.first_line,
                        context->address_integer_size,
                        get_type_size(*context, element_type)
                    );
                }

                auto address_register = base_address_register;
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    expect(element_value, coerce_to_type(
                        context,
                        instructions,
                        expression.array_literal.elements[i].range,
                        element_values[i],
                        element_type,
                        false
                    ));

                    switch(element_values[i].value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(context,
                            instructions,
                            expression.array_literal[i].range,
                            element_type,
                            element_value.constant,
                            address_register
                        );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    element_value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                append_copy_memory(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    length_register,
                                    element_value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    element_value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                append_copy_memory(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    length_register,
                                    element_value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    if(i != expression.array_literal.count - 1) {
                        auto new_address_register = append_arithmetic_operation(
                            context,
                            instructions,
                            expression.array_literal[i + 1].range.first_line,
                            ArithmeticOperationType::Add,
                            context->address_integer_size,
                            address_register,
                            element_size_register
                        );

                        address_register = new_address_register;
                    }
                }

                TypedValue value;
                value.value.category = ValueCategory::Anonymous;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(element_type)
                };
                value.value.anonymous.register_ = base_address_register;

                return {
                    true,
                    value
                };
            }
        } break;

        case ExpressionType::StructLiteral: {
            if(expression.struct_literal.count == 0) {
                error(context->current_file_path, expression.range, "Empty struct literal");

                return { false };
            }

            auto type_members = allocate<StructTypeMember>(expression.struct_literal.count);
            auto member_values = allocate<Value>(expression.struct_literal.count);
            auto all_constant = true;

            for(size_t i = 0; i < expression.struct_literal.count; i += 1) {
                for(size_t j = 0; j < i; j += 1) {
                    if(strcmp(expression.struct_literal[i].name.text, type_members[j].name) == 0) {
                        error(context->current_file_path, expression.struct_literal[i].name.range, "Duplicate struct member %s", expression.struct_literal[i].name.text);

                        return { false };
                    }
                }

                expect(member, generate_expression(context, instructions, expression.struct_literal[i].value));

                type_members[i] = {
                    expression.struct_literal[i].name.text,
                    member.type
                };

                member_values[i] = member.value;

                if(member.value.category != ValueCategory::Constant) {
                    all_constant = false;
                }
            }

            TypedValue value;
            value.type.category = TypeCategory::Struct;
            value.type._struct.is_undetermined = true;
            value.type._struct.members = {
                expression.struct_literal.count,
                type_members
            };

            if(all_constant) {
                auto constant_member_values = allocate<ConstantValue>(expression.struct_literal.count);

                for(size_t i = 0; i < expression.struct_literal.count; i += 1) {
                    constant_member_values[i] = member_values[i].constant;
                }

                value.value.category = ValueCategory::Constant;
                value.value.constant.struct_ = constant_member_values;
            } else {
                value.value.category = ValueCategory::Anonymous;
                value.value.anonymous.undetermined_struct = member_values;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            expect(expression_value, generate_expression(context, instructions, *expression.function_call.expression));

            if(expression_value.type.category != TypeCategory::Function) {
                error(context->current_file_path, expression.function_call.expression->range, "Cannot call %s", type_description(expression_value.type));

                return { false };
            }

            if(expression.function_call.parameters.count != expression_value.type.function.parameter_count) {
                error(context->current_file_path, expression.range, "Incorrect number of parameters. Expected %zu, got %zu", expression_value.type.function.parameter_count, expression.function_call.parameters.count);

                return { false };
            }

            auto parameter_count = expression.function_call.parameters.count;

            auto function_declaration = expression_value.value.constant.function.declaration.function_declaration;

            const char *function_name;
            auto function_parameter_values = allocate<TypedValue>(parameter_count);
            Type *function_parameter_types;
            Type function_return_type;

            if(expression_value.type.function.is_polymorphic) {
                List<PolymorphicDeterminer> polymorphic_determiners{};

                for(size_t i = 0; i < parameter_count; i += 1) {
                    auto parameter = function_declaration.parameters[i];

                    if(parameter.is_polymorphic_determiner) {
                        for(auto polymorphic_determiner : polymorphic_determiners) {
                            if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                error(context->current_file_path, parameter.polymorphic_determiner.range, "Duplicate polymorphic parameter %s", parameter.polymorphic_determiner.text);

                                return { false };
                            }
                        }

                        expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                        expect(determined_type, coerce_to_default_type(*context, expression.function_call.parameters[i].range, value.type));

                        append(&polymorphic_determiners, {
                            parameter.polymorphic_determiner.text,
                            determined_type
                        });

                        function_parameter_values[i] = value;
                    }
                }

                function_parameter_types = allocate<Type>(parameter_count);

                context->polymorphic_determiners = to_array(polymorphic_determiners);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    auto parameter = function_declaration.parameters[i];

                    if(parameter.is_polymorphic_determiner) {
                        for(auto polymorphic_determiner : polymorphic_determiners) {
                            if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                function_parameter_types[i] = polymorphic_determiner.type;
                            }

                            break;
                        }
                    } else {
                        expect(type, evaluate_type_expression(context, parameter.type));

                        function_parameter_types[i] = type;

                        expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                        function_parameter_values[i] = value;
                    }
                }

                if(function_declaration.has_return_type) {
                    expect(return_type, evaluate_type_expression(context, function_declaration.return_type));

                    function_return_type = return_type;
                } else {
                    function_return_type.category = TypeCategory::Void;
                }

                context->polymorphic_determiners = {};

                if(function_declaration.is_external) {
                    function_name = function_declaration.name.text;

                    for(auto library : function_declaration.external_libraries) {
                        auto has_library = false;
                        for(auto library : context->libraries) {
                            if(strcmp(library, library) == 0) {
                                has_library = true;

                                break;
                            }
                        }

                        append(&context->libraries, library);
                    }
                } else {
                    char *mangled_name_buffer{};

                    string_buffer_append(&mangled_name_buffer, "function_");

                    char buffer[32];
                    sprintf(buffer, "%zu", context->runtime_functions.count);
                    string_buffer_append(&mangled_name_buffer, buffer);

                    function_name = mangled_name_buffer;
                }

                auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(expression.function_call.parameters.count);

                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    runtime_function_parameters[i] = {
                        function_declaration.parameters[i].name,
                        function_parameter_types[i],
                        expression_value.value.constant.function.declaration.function_declaration.parameters[i].type.range
                    };
                }

                RuntimeFunction runtime_function;
                runtime_function.mangled_name = function_name;
                runtime_function.parameters = {
                    expression.function_call.parameters.count,
                    runtime_function_parameters
                };
                runtime_function.return_type = function_return_type;
                runtime_function.declaration = expression_value.value.constant.function.declaration;
                runtime_function.polymorphic_determiners = to_array(polymorphic_determiners);
                runtime_function.file_path = expression_value.value.constant.function.file_path;

                if(!expression_value.value.constant.function.declaration.is_top_level) {
                    runtime_function.parent = expression_value.value.constant.function.parent;
                }

                append(&context->runtime_functions, runtime_function);

                if(!register_global_name(context, function_name, function_declaration.name.range)) {
                    return { false };
                }
            } else {
                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    char *parameter_source{};
                    expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                    function_parameter_values[i] = value;
                }

                if(function_declaration.is_external) {
                    function_name = function_declaration.name.text;

                    for(auto library : function_declaration.external_libraries) {
                        auto has_library = false;
                        for(auto library : context->libraries) {
                            if(strcmp(library, library) == 0) {
                                has_library = true;

                                break;
                            }
                        }

                        append(&context->libraries, library);
                    }
                } else {
                    function_name = generate_mangled_name(*context, expression_value.value.constant.function.declaration);
                }

                function_parameter_types = expression_value.type.function.parameters;
                function_return_type = *expression_value.type.function.return_type;

                auto is_registered = false;
                for(auto function : context->runtime_functions) {
                    if(strcmp(function.mangled_name, function_name) == 0) {
                        is_registered = true;

                        break;
                    }
                }

                if(!is_registered) {
                    auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(expression.function_call.parameters.count);

                    for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                        runtime_function_parameters[i] = {
                            function_declaration.parameters[i].name,
                            function_parameter_types[i]
                        };
                    }

                    RuntimeFunction runtime_function;
                    runtime_function.mangled_name = function_name;
                    runtime_function.parameters = {
                        expression.function_call.parameters.count,
                        runtime_function_parameters
                    };
                    runtime_function.return_type = function_return_type;
                    runtime_function.declaration = expression_value.value.constant.function.declaration;
                    runtime_function.polymorphic_determiners = {};
                    runtime_function.file_path = expression_value.value.constant.function.file_path;

                    if(!expression_value.value.constant.function.declaration.is_top_level) {
                        runtime_function.parent = expression_value.value.constant.function.parent;
                    }

                    append(&context->runtime_functions, runtime_function);

                    if(!register_global_name(context, function_name, function_declaration.name.range)) {
                        return { false };
                    }
                }
            }

            auto total_parameter_count = parameter_count;

            bool has_return;
            bool is_return_in_register;
            if(function_return_type.category == TypeCategory::Void) {
                has_return = false;
            } else {
                has_return = true;

                auto representation = get_type_representation(*context, function_return_type);

                is_return_in_register = representation.is_in_register;

                if(!representation.is_in_register) {
                    total_parameter_count += 1;
                }
            }

            auto function_parameter_registers = allocate<size_t>(total_parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect(parameter_value, coerce_to_type(
                    context,
                    instructions,
                    expression.function_call.parameters[i].range,
                    function_parameter_values[i],
                    function_parameter_types[i],
                    false
                ));

                auto representation = get_type_representation(*context, function_parameter_types[i]);

                switch(parameter_value.category) {
                    case ValueCategory::Constant: {
                        if(representation.is_in_register) {
                            function_parameter_registers[i] = generate_in_register_constant_value(
                                context,
                                instructions,
                                expression.function_call.parameters[i].range,
                                function_parameter_types[i],
                                parameter_value.constant
                            );
                        } else {
                            function_parameter_registers[i] = generate_not_in_register_constant_value(
                                context,
                                instructions,
                                expression.function_call.parameters[i].range,
                                function_parameter_types[i],
                                parameter_value.constant
                            );
                        }
                    } break;

                    case ValueCategory::Anonymous: {
                        function_parameter_registers[i] = parameter_value.anonymous.register_;
                    } break;

                    case ValueCategory::Address: {
                        if(representation.is_in_register) {
                            auto value_register = append_load_integer(
                                context,
                                instructions,
                                expression.function_call.parameters[i].range.first_line,
                                representation.value_size,
                                parameter_value.address
                            );

                            function_parameter_registers[i] = value_register;
                        } else {
                            function_parameter_registers[i] = parameter_value.address;
                        }
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            size_t return_register;

            if(has_return && !is_return_in_register) {
                auto local_register = append_allocate_local(
                    context,
                    instructions,
                    expression.function_call.expression->range.first_line,
                    get_type_size(*context, function_return_type),
                    get_type_alignment(*context, function_return_type)
                );

                function_parameter_registers[total_parameter_count - 1] = local_register;
                return_register = local_register;
            }

            Instruction call;
            call.type = InstructionType::FunctionCall;
            call.line = expression.range.first_line;
            call.function_call.function_name = function_name;
            call.function_call.parameter_registers = {
                total_parameter_count,
                function_parameter_registers
            };
            if(has_return && is_return_in_register) {
                return_register = allocate_register(context);

                call.function_call.has_return = true;
                call.function_call.return_register = return_register;
            } else {
                call.function_call.has_return = false;
            }

            append(instructions, call);

            TypedValue value;
            value.value.category = ValueCategory::Anonymous;
            value.type = function_return_type;
            if(has_return) {
                value.value.anonymous.register_ = return_register;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, generate_expression(context, instructions, *expression.binary_operation.left));

            expect(right, generate_expression(context, instructions, *expression.binary_operation.right));

            if(left.value.category == ValueCategory::Constant && right.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_binary_operation(
                    *context,
                    expression.range,
                    expression.binary_operation.binary_operator,
                    expression.binary_operation.left->range,
                    left.type,
                    left.value.constant,
                    expression.binary_operation.right->range,
                    right.type,
                    right.value.constant
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = constant.type;
                value.value.constant = constant.value;

                return {
                    true,
                    value
                };
            } else {
                expect(type, determine_binary_operation_type(*context, expression.range, left.type, right.type));

                expect(left_value, coerce_to_type(context, instructions, expression.binary_operation.left->range, left, type, false));

                expect(right_value, coerce_to_type(context, instructions, expression.binary_operation.right->range, right, type, false));

                size_t result_register;
                Type result_type;
                switch(left.type.category) {
                    case TypeCategory::Integer: {
                        auto left_register = generate_in_register_integer_value(
                            context,
                            instructions,
                            expression.binary_operation.left->range,
                            type.integer.size,
                            left_value
                        );

                        auto right_register = generate_in_register_integer_value(
                            context,
                            instructions,
                            expression.binary_operation.right->range,
                            type.integer.size,
                            right_value
                        );

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Addition: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::Add,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Subtraction: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::Subtract,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Multiplication: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedMultiply;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedMultiply;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Division: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedDivide;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedDivide;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Modulo: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedModulus;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedModulus;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::BitwiseAnd: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseAnd,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::BitwiseOr: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseOr,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Equal: {
                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::NotEqual: {
                                auto equal_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_register = generate_boolean_invert(context,  instructions, expression.range, equal_register);

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::LessThan: {
                                ComparisonOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ComparisonOperationType::SignedLessThan;
                                } else {
                                    operation_type = ComparisonOperationType::UnsignedLessThan;
                                }

                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::GreaterThan: {
                                ComparisonOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ComparisonOperationType::SignedGreaterThan;
                                } else {
                                    operation_type = ComparisonOperationType::UnsignedGreaterThan;
                                }

                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            default: {
                                error(context->current_file_path, expression.range, "Cannot perform that operation on integers");

                                return { false };
                            } break;
                        }
                    } break;

                    case TypeCategory::Boolean: {
                        auto left_register = generate_in_register_boolean_value(
                            context,
                            instructions,
                            expression.binary_operation.left->range,
                            left_value
                        );

                        auto right_register = generate_in_register_boolean_value(
                            context,
                            instructions,
                            expression.binary_operation.right->range,
                            right_value
                        );

                        result_type.category = TypeCategory::Boolean;

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Equal: {
                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            case BinaryOperator::NotEqual: {
                                auto equal_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );

                                result_register = generate_boolean_invert(context, instructions, expression.range, equal_register);
                            } break;

                            case BinaryOperator::BooleanAnd: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseAnd,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            case BinaryOperator::BooleanOr: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseOr,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            default: {
                                error(context->current_file_path, expression.range, "Cannot perform that operation on booleans");

                                return { false };
                            } break;
                        }
                    } break;
                }

                TypedValue value;
                value.value.category = ValueCategory::Anonymous;
                value.type = result_type;
                value.value.anonymous.register_ = result_register;

                return {
                    true,
                    value
                };
            }
        } break;

        case ExpressionType::UnaryOperation: {
            expect(expression_value, generate_expression(context, instructions, *expression.unary_operation.expression));

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            switch(expression_value.type.category) {
                                case TypeCategory::Function: {
                                    auto function_declaration = expression_value.value.constant.function.declaration.function_declaration;
                                    auto parameter_count = expression_value.type.function.parameter_count;

                                    if(expression_value.type.function.is_polymorphic) {
                                        error(context->current_file_path, expression.unary_operation.expression->range, "Cannot take pointers to polymorphic functions");

                                        return { false };
                                    }

                                    const char *function_name;
                                    if(function_declaration.is_external) {
                                        function_name = function_declaration.name.text;

                                        for(auto library : function_declaration.external_libraries) {
                                            auto has_library = false;
                                            for(auto library : context->libraries) {
                                                if(strcmp(library, library) == 0) {
                                                    has_library = true;

                                                    break;
                                                }
                                            }

                                            append(&context->libraries, library);
                                        }
                                    } else {
                                        function_name = generate_mangled_name(*context, expression_value.value.constant.function.declaration);
                                    }

                                    auto is_registered = false;
                                    for(auto function : context->runtime_functions) {
                                        if(strcmp(function.mangled_name, function_name) == 0) {
                                            is_registered = true;

                                            break;
                                        }
                                    }

                                    if(!is_registered) {
                                        auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(function_declaration.parameters.count);

                                        for(size_t i = 0; i < function_declaration.parameters.count; i += 1) {
                                            runtime_function_parameters[i] = {
                                                function_declaration.parameters[i].name,
                                                expression_value.type.function.parameters[i]
                                            };
                                        }

                                        RuntimeFunction runtime_function;
                                        runtime_function.mangled_name = function_name;
                                        runtime_function.parameters = {
                                            function_declaration.parameters.count,
                                            runtime_function_parameters
                                        };
                                        runtime_function.return_type = *expression_value.type.function.return_type;
                                        runtime_function.declaration = expression_value.value.constant.function.declaration;
                                        runtime_function.polymorphic_determiners = {};
                                        runtime_function.file_path = expression_value.value.constant.function.file_path;

                                        if(!expression_value.value.constant.function.declaration.is_top_level) {
                                            runtime_function.parent = expression_value.value.constant.function.parent;
                                        }

                                        append(&context->runtime_functions, runtime_function);

                                        if(!register_global_name(context, function_name, function_declaration.name.range)) {
                                            return { false };
                                        }
                                    }

                                    auto address_regsiter = append_reference_static(
                                        context,
                                        instructions,
                                        expression.range.first_line,
                                        function_name
                                    );

                                    TypedValue value;
                                    value.value.category = ValueCategory::Anonymous;
                                    value.type.category = TypeCategory::Pointer;
                                    value.type.pointer = heapify(expression_value.type);
                                    value.value.anonymous.register_ = address_regsiter;

                                    return {
                                        true,
                                        value
                                    };
                                } break;

                                case TypeCategory::Type: {
                                    TypedValue value;
                                    value.value.category = ValueCategory::Constant;
                                    value.type.category = TypeCategory::Type;
                                    value.value.constant.type.category = TypeCategory::Pointer;
                                    value.value.constant.type.pointer = heapify(expression_value.value.constant.type);

                                    return {
                                        true,
                                        value
                                    };
                                } break;

                                default: {
                                    error(context->current_file_path, expression.unary_operation.expression->range, "Cannot take pointers to constants of type %s", type_description(expression_value.type));

                                    return { false };
                                }
                            }
                        } break;

                        case ValueCategory::Anonymous: {
                            error(context->current_file_path, expression.unary_operation.expression->range, "Cannot take pointers to anonymous values");

                            return { false };
                        } break;

                        case ValueCategory::Address: {
                            TypedValue value;
                            value.value.category = ValueCategory::Anonymous;
                            value.type.category = TypeCategory::Pointer;
                            value.type.pointer = heapify(expression_value.type);
                            value.value.anonymous.register_ = expression_value.value.anonymous.register_;

                            return {
                                true,
                                value
                            };
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }

                case UnaryOperator::BooleanInvert: {
                    if(expression_value.type.category != TypeCategory::Boolean) {
                        error(context->current_file_path, expression.unary_operation.expression->range, "Expected a boolean, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    TypedValue value;
                    value.type.category = TypeCategory::Boolean;

                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            value.value.category = ValueCategory::Constant;
                            value.value.constant.boolean = !expression_value.value.constant.boolean;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.unary_operation.expression->range.first_line,
                                context->default_integer_size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    auto result_register = generate_boolean_invert(
                        context,
                        instructions,
                        expression.range,
                        value_register
                    );

                    value.value.category = ValueCategory::Anonymous;
                    value.value.anonymous.register_ = result_register;

                    return {
                        true,
                        value
                    };
                } break;

                case UnaryOperator::Negation: {
                    if(expression_value.type.category != TypeCategory::Integer) {
                        error(context->current_file_path, expression.unary_operation.expression->range, "Expected an integer, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    TypedValue value;
                    value.type.category = TypeCategory::Integer;
                    value.type.integer = expression_value.type.integer;

                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            value.value.category = ValueCategory::Constant;
                            value.value.constant.integer = -expression_value.value.constant.integer;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.unary_operation.expression->range.first_line,
                                expression_value.type.integer.size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    auto zero_register = append_constant(
                        context,
                        instructions,
                        expression.range.first_line,
                        expression_value.type.integer.size,
                        0
                    );

                    auto result_register = append_arithmetic_operation(
                        context,
                        instructions,
                        expression.range.first_line,
                        ArithmeticOperationType::Subtract,
                        expression_value.type.integer.size,
                        zero_register,
                        value_register
                    );

                    value.value.category = ValueCategory::Anonymous;
                    value.value.anonymous.register_ = result_register;

                    return {
                        true,
                        value
                    };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::Cast: {
            expect(expression_value, generate_expression(context, instructions, *expression.cast.expression));

            expect(type, evaluate_type_expression(context, *expression.cast.type));

            if(expression_value.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_conversion(
                    *context,
                    expression_value.value.constant,
                    expression_value.type,
                    expression.cast.expression->range,
                    type,
                    expression.cast.type->range
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = type;
                value.value.constant = constant;

                return {
                    true,
                    value
                };
            }

            auto coerce_result = coerce_to_type(
                context,
                instructions,
                expression.cast.expression->range,
                expression_value,
                type,
                true
            );

            if(coerce_result.status) {
                return {
                    true,
                    {
                        type,
                        coerce_result.value
                    }
                };
            }

            size_t result_register;
            switch(expression_value.type.category) {
                case TypeCategory::Integer: {
                    switch(type.category) {
                        case TypeCategory::Integer: {
                            size_t value_register;
                            switch(expression_value.value.category) {
                                case ValueCategory::Anonymous: {
                                    value_register = expression_value.value.anonymous.register_;
                                } break;

                                case ValueCategory::Address: {
                                    value_register = append_load_integer(
                                        context,
                                        instructions,
                                        expression.cast.expression->range.first_line,
                                        expression_value.type.integer.size,
                                        expression_value.value.address
                                    );
                                } break;

                                default: {
                                    abort();
                                } break;
                            }

                            result_register = append_integer_upcast(
                                context,
                                instructions,
                                expression.range.first_line,
                                expression_value.type.integer.is_signed,
                                expression_value.type.integer.size,
                                type.integer.size,
                                value_register
                            );
                        } break;

                        case TypeCategory::Pointer: {
                            if(expression_value.type.integer.size != context->address_integer_size) {
                                error(context->current_file_path, expression.cast.expression->range, "Cannot cast from %s to pointer", type_description(expression_value.type));

                                return { false };
                            }

                            size_t value_register;
                            switch(expression_value.value.category) {
                                case ValueCategory::Anonymous: {
                                    value_register = expression_value.value.anonymous.register_;
                                } break;

                                case ValueCategory::Address: {
                                    value_register = append_load_integer(
                                        context,
                                        instructions,
                                        expression.range.first_line,
                                        context->address_integer_size,
                                        expression_value.value.address
                                    );
                                } break;

                                default: {
                                    abort();
                                } break;
                            }

                            result_register = value_register;
                        } break;

                        default: {
                            error(context->current_file_path, expression.cast.type->range, "Cannot cast from integer to %s", type_description(type));

                            return { false };
                        } break;
                    }
                } break;

                case TypeCategory::Pointer: {
                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.range.first_line,
                                context->address_integer_size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    switch(type.category) {
                        case TypeCategory::Integer: {
                            if(type.integer.size != context->address_integer_size) {
                                error(context->current_file_path, expression.cast.expression->range, "Cannot cast from pointer to %s", type_description(type));

                                return { false };
                            }

                            result_register = value_register;
                        } break;

                        case TypeCategory::Pointer: {
                            result_register = value_register;
                        } break;

                        default: {
                            error(context->current_file_path, expression.cast.type->range, "Cannot cast from pointer to %s", type_description(type));

                            return { false };
                        } break;
                    }
                } break;

                default: {
                    error(context->current_file_path, expression.cast.expression->range, "Cannot cast from %s", type_description(expression_value.type));

                    return { false };
                } break;
            }

            TypedValue value;
            value.value.category = ValueCategory::Anonymous;
            value.type = type;
            value.value.anonymous.register_ = result_register;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayType: {
            expect(type, evaluate_type_expression(context, *expression.array_type.expression));

            auto type_representation = get_type_representation(*context, type);

            if(!type_representation.is_in_register) {
                error(context->current_file_path, expression.array_type.expression->range, "Cannot have arrays of type %s", type_description(type));

                return { false };
            }

            TypedValue value;
            value.type.category = TypeCategory::Type;
            value.value.category = ValueCategory::Constant;

            if(expression.array_type.index != nullptr) {
                expect(index_value, evaluate_constant_expression(context, *expression.array_type.index));

                if(index_value.type.category != TypeCategory::Integer) {
                    error(context->current_file_path, expression.array_type.index->range, "Expected an integer, got '%s'", type_description(index_value.type));
                }

                size_t length;
                if(index_value.type.integer.is_undetermined) {
                    if((int64_t)index_value.value.integer < 0) {
                        error(context->current_file_path, expression.array_type.index->range, "Negative array length: %lld", (int64_t)index_value.value.integer);

                        return { false };
                    }

                    length = index_value.value.integer;
                } else if(index_value.type.integer.is_signed) {
                    switch(index_value.type.integer.size) {
                        case RegisterSize::Size8: {
                            if((int8_t)index_value.value.integer < 0) {
                                error(context->current_file_path, expression.array_type.index->range, "Negative array length: %hhd", (int8_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint8_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            if((int16_t)index_value.value.integer < 0) {
                                error(context->current_file_path, expression.array_type.index->range, "Negative array length: %hd", (int16_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint16_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            if((int32_t)index_value.value.integer < 0) {
                                error(context->current_file_path, expression.array_type.index->range, "Negative array length: %d", (int32_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint32_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            if((int8_t)index_value.value.integer < 0) {
                                error(context->current_file_path, expression.array_type.index->range, "Negative array length: %lld", (int64_t)index_value.value.integer);

                                return { false };
                            }

                            length = index_value.value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } else {
                    switch(index_value.type.integer.size) {
                        case RegisterSize::Size8: {
                            length = (uint8_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            length = (uint16_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            length = (uint32_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            length = index_value.value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }

                value.value.constant.type.category = TypeCategory::StaticArray;
                value.value.constant.type.static_array = {
                    length,
                    heapify(type)
                };
            } else {
                value.value.constant.type.category = TypeCategory::Array;
                value.value.constant.type.array = heapify(type);
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionType: {
            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                auto parameter = expression.function_type.parameters[i];

                if(parameter.is_polymorphic_determiner) {
                    error(context->current_file_path, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                    return { false };
                }

                expect(type, evaluate_type_expression(context, parameter.type));

                parameters[i] = type;
            }

            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                expect(return_type_value, evaluate_type_expression(context, *expression.function_type.return_type));

                return_type = return_type_value;
            }

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.value.constant.type.category = TypeCategory::Function;
            value.value.constant.type.function = {
                false,
                expression.function_type.parameters.count,
                parameters,
                heapify(return_type)
            };

            return {
                true,
                value
            };
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_statement(GenerationContext *context, List<Instruction> *instructions, Statement statement) {
    switch(statement.type) {
        case StatementType::Expression: {
            if(!generate_expression(context, instructions, statement.expression).status) {
                return false;
            }

            return true;
        } break;

        case StatementType::VariableDeclaration: {
            switch(statement.variable_declaration.type) {
                case VariableDeclarationType::Uninitialized: {
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.uninitialized));

                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        statement.range.first_line,
                        get_type_size(*context, type),
                        get_type_alignment(*context, type)
                    );

                    if(!add_new_variable(
                        context,
                        statement.variable_declaration.name,
                        address_register,
                        type,
                        statement.variable_declaration.uninitialized.range
                    )) {
                        return false;
                    }

                    return true;
                } break;

                case VariableDeclarationType::TypeElided: {
                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.line = statement.range.first_line;
                    allocate.allocate_local.destination_register = address_register;

                    auto allocate_index = append(instructions, allocate);

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.type_elided));

                    expect(coerced_type, coerce_to_default_type(*context, statement.variable_declaration.type_elided.range, initializer_value.type));

                    auto representation = get_type_representation(*context, coerced_type);

                    switch(initializer_value.value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                statement.range,
                                coerced_type,
                                initializer_value.value.constant,
                                address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    initializer_value.value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, coerced_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    initializer_value.value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    initializer_value.value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, coerced_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    initializer_value.value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    (*instructions)[allocate_index].allocate_local.size = get_type_size(*context, coerced_type);
                    (*instructions)[allocate_index].allocate_local.alignment = get_type_alignment(*context, coerced_type);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, coerced_type, statement.variable_declaration.type_elided.range)) {
                        return false;
                    }

                    return true;
                } break;


                case VariableDeclarationType::FullySpecified: {
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.fully_specified.type));

                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        statement.range.first_line,
                        get_type_size(*context, type),
                        get_type_alignment(*context, type)
                    );

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.fully_specified.initializer));

                    expect(coerced_initializer_value, coerce_to_type(
                        context,
                        instructions,
                        statement.variable_declaration.fully_specified.initializer.range,
                        initializer_value,
                        type,
                        false
                    ));

                    auto representation = get_type_representation(*context, type);

                    switch(coerced_initializer_value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                statement.range,
                                type,
                                coerced_initializer_value.constant,
                                address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_initializer_value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_initializer_value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_initializer_value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_initializer_value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, type, statement.variable_declaration.fully_specified.type.range)) {
                        return false;
                    }

                    return true;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case StatementType::Assignment: {
            expect(target, generate_expression(context, instructions, statement.assignment.target));

            if(target.value.category != ValueCategory::Address) {
                error(context->current_file_path, statement.assignment.target.range, "Value is not assignable");

                return false;
            }

            expect(value, generate_expression(context, instructions, statement.assignment.value));

            expect(coerced_value, coerce_to_type(
                context,
                instructions,
                statement.assignment.value.range,
                value,
                target.type,
                false
            ));

            auto representation = get_type_representation(*context, target.type);

            switch(coerced_value.category) {
                case ValueCategory::Constant: {
                    generate_constant_value_write(
                        context,
                        instructions,
                        statement.range,
                        target.type,
                        coerced_value.constant,
                        target.value.address
                    );
                } break;

                case ValueCategory::Anonymous: {
                    if(representation.is_in_register) {
                        append_store_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            coerced_value.anonymous.register_,
                            target.value.address
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            statement.range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, target.type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            statement.range.first_line,
                            length_register,
                            coerced_value.anonymous.register_,
                            target.value.address
                        );
                    }
                } break;

                case ValueCategory::Address: {
                    if(representation.is_in_register) {
                        auto value_register = append_load_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            coerced_value.address
                        );

                        append_store_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            value_register,
                            target.value.address
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            statement.range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, target.type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            statement.range.first_line,
                            length_register,
                            coerced_value.address,
                            target.value.address
                        );
                    }
                } break;

                default: {
                    abort();
                } break;
            }

            return true;
        } break;

        case StatementType::If: {
            auto end_jump_count = 1 + statement.if_.else_ifs.count;
            auto end_jump_indices = allocate<size_t>(end_jump_count);

            expect(condition, generate_expression(context, instructions, statement.if_.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(context->current_file_path, statement.if_.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_in_register_boolean_value(context, instructions, statement.if_.condition.range, condition.value);

            append_branch(context, instructions, statement.if_.condition.range.first_line, condition_register, instructions->count + 2);

            Instruction jump;
            jump.type = InstructionType::Jump;
            jump.line = statement.if_.condition.range.first_line;

            auto jump_index = append(instructions, jump);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.if_.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            Instruction end_jump;
            end_jump.type = InstructionType::Jump;
            end_jump.line = statement.range.first_line;

            end_jump_indices[0] = append(instructions, end_jump);

            (*instructions)[jump_index].jump.destination_instruction = instructions->count;

            for(size_t i = 0; i < statement.if_.else_ifs.count; i += 1) {
                expect(condition, generate_expression(context, instructions, statement.if_.else_ifs[i].condition));

                if(condition.type.category != TypeCategory::Boolean) {
                    error(context->current_file_path, statement.if_.else_ifs[i].condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                    return false;
                }

                auto condition_register = generate_in_register_boolean_value(
                    context,
                    instructions,
                    statement.if_.else_ifs[i].condition.range,
                    condition.value
                );

                append_branch(
                    context,
                    instructions,
                    statement.if_.else_ifs[i].condition.range.first_line,
                    condition_register,
                    instructions->count + 2
                );

                Instruction jump;
                jump.type = InstructionType::Jump;
                jump.line = statement.if_.else_ifs[i].condition.range.first_line;

                auto jump_index = append(instructions, jump);

                append(&context->variable_context_stack, List<Variable>{});

                for(auto child_statement : statement.if_.else_ifs[i].statements) {
                    if(!generate_statement(context, instructions, child_statement)) {
                        return false;
                    }
                }

                context->variable_context_stack.count -= 1;

                Instruction end_jump;
                end_jump.type = InstructionType::Jump;
                end_jump.line = statement.range.first_line;

                end_jump_indices[i + 1] = append(instructions, end_jump);

                (*instructions)[jump_index].jump.destination_instruction = instructions->count;
            }

            if(statement.if_.has_else) {
                append(&context->variable_context_stack, List<Variable>{});

                for(auto child_statement : statement.if_.else_statements) {
                    if(!generate_statement(context, instructions, child_statement)) {
                        return false;
                    }
                }
                context->variable_context_stack.count -= 1;
            }

            for(size_t i = 0; i < end_jump_count; i += 1) {
                (*instructions)[end_jump_indices[i]].jump.destination_instruction = instructions->count;
            }

            return true;
        } break;

        case StatementType::WhileLoop: {
            auto condition_index = instructions->count;

            expect(condition, generate_expression(context, instructions, statement.while_loop.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(context->current_file_path, statement.while_loop.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_in_register_boolean_value(
                context,
                instructions,
                statement.while_loop.condition.range,
                condition.value
            );

            append_branch(
                context,
                instructions,
                statement.while_loop.condition.range.first_line,
                condition_register,
                instructions->count + 2
            );

            Instruction jump_out;
            jump_out.type = InstructionType::Jump;
            jump_out.line = statement.while_loop.condition.range.first_line;

            auto jump_out_index = append(instructions, jump_out);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.while_loop.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            append_jump(
                context,
                instructions,
                statement.range.first_line,
                condition_index
            );

            (*instructions)[jump_out_index].jump.destination_instruction = instructions->count;

            return true;
        } break;

        case StatementType::Return: {
            Instruction return_;
            return_.type = InstructionType::Return;
            return_.line = statement.range.first_line;

            if(statement._return.has_value) {
                expect(value, generate_expression(context, instructions, statement._return.value));

                expect(coerced_value, coerce_to_type(
                    context,
                    instructions,
                    statement._return.value.range,
                    value,
                    context->return_type,
                    false
                ));

                if(context->return_type.category != TypeCategory::Void) {
                    auto representation = get_type_representation(*context, context->return_type);

                    switch(coerced_value.category) {
                        case ValueCategory::Constant: {
                            if(representation.is_in_register) {
                                auto value_register = generate_in_register_constant_value(
                                    context,
                                    instructions,
                                    statement.range,
                                    context->return_type,
                                    coerced_value.constant
                                );

                                return_.return_.value_register = value_register;
                            } else {
                                generate_not_in_register_constant_write(
                                    context,
                                    instructions,
                                    statement.range,
                                    context->return_type,
                                    coerced_value.constant,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                return_.return_.value_register = coerced_value.anonymous.register_;
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, context->return_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_value.anonymous.register_,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_value.address
                                );

                                return_.return_.value_register = value_register;
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, context->return_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_value.address,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }
            } else if(context->return_type.category != TypeCategory::Void) {
                error(context->current_file_path, statement.range, "Missing return value");

                return { false };
            }

            append(instructions, return_);

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

inline GlobalConstant create_base_type(const char *name, Type type) {
    Type value_type;
    value_type.category = TypeCategory::Type;

    ConstantValue value;
    value.type = type;

    return {
        name,
        value_type,
        value
    };
}

inline GlobalConstant create_base_integer_type(const char *name, RegisterSize size, bool is_signed) {
    Type type;
    type.category = TypeCategory::Integer;
    type.integer = {
        size,
        is_signed
    };

    return create_base_type(name, type);
}

Result<IR> generate_ir(const char *main_file_path, Array<Statement> main_file_statements, RegisterSize address_size, RegisterSize default_size) {
    List<GlobalConstant> global_constants{};

    append(&global_constants, create_base_integer_type("u8", RegisterSize::Size8, false));
    append(&global_constants, create_base_integer_type("u16", RegisterSize::Size16, false));
    append(&global_constants, create_base_integer_type("u32", RegisterSize::Size32, false));
    append(&global_constants, create_base_integer_type("u64", RegisterSize::Size64, false));

    append(&global_constants, create_base_integer_type("i8", RegisterSize::Size8, true));
    append(&global_constants, create_base_integer_type("i16", RegisterSize::Size16, true));
    append(&global_constants, create_base_integer_type("i32", RegisterSize::Size32, true));
    append(&global_constants, create_base_integer_type("i64", RegisterSize::Size64, true));

    append(&global_constants, create_base_integer_type("usize", address_size, false));
    append(&global_constants, create_base_integer_type("isize", address_size, true));

    Type base_boolean_type;
    base_boolean_type.category = TypeCategory::Boolean;

    append(&global_constants, create_base_type("bool", base_boolean_type));

    Type base_void_type;
    base_void_type.category = TypeCategory::Void;

    append(&global_constants, create_base_type("void", base_void_type));

    ConstantValue boolean_true_value;
    boolean_true_value.boolean = true;

    append(&global_constants, GlobalConstant {
        "true",
        base_boolean_type,
        boolean_true_value
    });

    ConstantValue boolean_false_value;
    boolean_false_value.boolean = false;

    append(&global_constants, GlobalConstant {
        "false",
        base_boolean_type,
        boolean_false_value
    });

    GenerationContext context {
        address_size,
        default_size,
        to_array(global_constants)
    };

    append(&context.parsed_files, {
        main_file_path,
        main_file_statements
    });

    auto main_found = false;
    for(auto statement : main_file_statements) {
        if(match_declaration(statement, "main")) {
            if(statement.type != StatementType::FunctionDeclaration) {
                error(main_file_path, statement.range, "'main' must be a function");

                return { false };
            }

            if(statement.function_declaration.is_external) {
                error(main_file_path, statement.range, "'main' must not be external");

                return { false };
            }

            context.is_top_level = true;
            context.top_level_statements = main_file_statements;
            context.current_file_path = main_file_path;

            expect(value, resolve_declaration(&context, statement));

            if(value.type.function.is_polymorphic) {
                error(main_file_path, statement.range, "'main' cannot be polymorphic");

                return { false };
            }

            auto runtimeParameters = allocate<RuntimeFunctionParameter>(statement.function_declaration.parameters.count);

            for(size_t i = 0; i < statement.function_declaration.parameters.count; i += 1) {
                runtimeParameters[i] = {
                    statement.function_declaration.parameters[i].name,
                    value.type.function.parameters[i]
                };
            }

            auto mangled_name = "main";

            RuntimeFunction function;
            function.mangled_name = mangled_name;
            function.parameters = {
                statement.function_declaration.parameters.count,
                runtimeParameters
            };
            function.return_type = *value.type.function.return_type;
            function.declaration = statement;
            function.polymorphic_determiners = {};
            function.file_path = main_file_path;

            append(&context.runtime_functions, function);

            if(!register_global_name(&context, mangled_name, statement.function_declaration.name.range)) {
                return { false };
            }

            main_found = true;

            break;
        }
    }

    if(!main_found) {
        fprintf(stderr, "'main' function not found\n");

        return { false };
    }

    List<Function> functions{};

    while(true) {
        auto done = true;
        RuntimeFunction function;
        for(auto runtime_function : context.runtime_functions) {
            auto generated = false;
            for(auto generated_function : functions) {
                if(strcmp(generated_function.name, runtime_function.mangled_name) == 0) {
                    generated = true;

                    break;
                }
            }

            if(!generated) {
                done = false;

                function = runtime_function;

                break;
            }
        }

        if(done) {
            break;
        } else {
            context.is_top_level = function.declaration.is_top_level;

            if(!function.declaration.is_top_level) {
                context.is_top_level = false;
                context.determined_declaration = function.parent;
            }

            context.top_level_statements = main_file_statements;
            context.current_file_path = function.file_path;

            auto total_parameter_count = function.parameters.count;

            bool has_return;
            RegisterRepresentation return_representation;                
            if(function.return_type.category == TypeCategory::Void) {
                has_return = false;
            } else {
                has_return = true;

                return_representation = get_type_representation(context, function.return_type);

                if(!return_representation.is_in_register) {
                    total_parameter_count += 1;
                }
            }

            auto parameter_sizes = allocate<RegisterSize>(total_parameter_count);

            for(size_t i = 0; i < function.parameters.count; i += 1) {
                auto representation = get_type_representation(context, function.parameters[i].type);

                if(representation.is_in_register) {
                    parameter_sizes[i] = representation.value_size;
                } else {
                    parameter_sizes[i] = address_size;
                }
            }

            if(has_return && !return_representation.is_in_register) {
                parameter_sizes[total_parameter_count - 1] = address_size;
            }

            Function ir_function;
            ir_function.name = function.mangled_name;
            ir_function.is_external = function.declaration.function_declaration.is_external;
            ir_function.parameter_sizes = {
                total_parameter_count,
                parameter_sizes
            };
            ir_function.has_return = has_return && return_representation.is_in_register;
            ir_function.file = function.file_path;
            ir_function.line = function.declaration.range.first_line;

            if(has_return && return_representation.is_in_register) {
                ir_function.return_size = return_representation.value_size;
            }

            if(!function.declaration.function_declaration.is_external) {
                append(&context.variable_context_stack, List<Variable>{});

                auto parameters = allocate<Variable>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    parameters[i] = {
                        parameter.name,
                        parameter.type,
                        parameter.type_range,
                        i
                    };
                }

                context.is_top_level = false;
                context.determined_declaration = {
                    function.declaration,
                    function.polymorphic_determiners,
                    heapify(function.parent)
                };
                context.parameters = {
                    function.parameters.count,
                    parameters
                };
                context.return_type = function.return_type;
                context.next_register = total_parameter_count;

                if(has_return && !return_representation.is_in_register) {
                    context.return_parameter_register = total_parameter_count - 1;
                }

                List<Instruction> instructions{};

                for(auto statement : function.declaration.function_declaration.statements) {
                    switch(statement.type) {
                        case StatementType::Expression:
                        case StatementType::VariableDeclaration:
                        case StatementType::Assignment:
                        case StatementType::If:
                        case StatementType::WhileLoop:
                        case StatementType::Return: {
                            if(!generate_statement(&context, &instructions, statement)) {
                                return { false };
                            }
                        } break;

                        case StatementType::Import: {
                            error(main_file_path, statement.range, "Import directive only allowed in global scope");

                            return { false };
                        } break;
                    }
                }

                auto has_return_at_end = false;
                if(function.declaration.function_declaration.statements.count > 0) {
                    has_return_at_end = function.declaration.function_declaration.statements[
                        function.declaration.function_declaration.statements.count - 1
                    ].type == StatementType::Return;
                }

                if(!has_return_at_end) {
                    if(has_return) {
                        error(function.file_path, function.declaration.range, "Function '%s' must end with a return", function.declaration.function_declaration.name.text);

                        return { false };
                    } else {
                        Instruction return_;
                        return_.type = InstructionType::Return;

                        append(&instructions, return_);
                    }
                }

                context.variable_context_stack.count -= 1;
                context.next_register = 0;

                ir_function.instructions = to_array(instructions);
            }

            append(&functions, ir_function);
        }
    }

    return {
        true,
        {
            to_array(functions),
            to_array(context.libraries),
            to_array(context.static_constants)
        }
    };
}