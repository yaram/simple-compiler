#include "ir_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "list.h"
#include "types.h"
#include "util.h"
#include "path.h"

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

    Array<Statement> file_module;
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

    Array<PolymorphicDeterminer> polymorphic_determiners;
};

struct GenerationContext {
    RegisterSize address_integer_size;
    RegisterSize default_integer_size;

    Array<GlobalConstant> global_constants;

    Array<File> file_modules;

    bool is_top_level;

    union {
        DeterminedDeclaration determined_declaration;

        Array<Statement> top_level_statements;
    };

    Array<PolymorphicDeterminer> polymorphic_determiners;

    Array<Variable> parameters;
    Type return_type;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<const char*> libraries;

    List<StaticConstant> static_constants;
};

static void error(FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "%s(%u:%u): ", range.path, range.start_line, range.start_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    auto file = fopen(range.path, "rb");

    if(file != nullptr) {
        if(range.start_line == range.end_line) {
            unsigned int current_line = 1;

            while(current_line != range.start_line) {
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

            auto done = false;
            while(!done) {
                auto character = fgetc(file);

                switch(character) {
                    case '\r':
                    case '\n': {
                        done = true;
                    } break;

                    case EOF: {
                        fclose(file);

                        va_end(arguments);

                        return;
                    } break;

                    default: {
                        fprintf(stderr, "%c", character);
                    } break;
                }
            }

            fprintf(stderr, "\n");

            for(unsigned int i = 1; i < range.start_character; i += 1) {
                fprintf(stderr, " ");
            }

            if(range.end_character - range.start_character == 0) {
                fprintf(stderr, "^");
            } else {
                for(unsigned int i = range.start_character; i <= range.end_character; i += 1) {
                    fprintf(stderr, "-");
                }
            }

            fprintf(stderr, "\n");
        }

        fclose(file);
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

        default: {
            abort();
        } break;
    }
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

    auto old_determined_declaration = context->determined_declaration;

    if(context->is_top_level) {
        for(auto statement : context->top_level_statements) {
            if(match_declaration(statement, name.text)) {
                return resolve_declaration(context, statement);
            }
        }
    } else {
        while(true){
            switch(context->determined_declaration.declaration.type) {
                case StatementType::FunctionDeclaration: {
                    for(auto statement : context->determined_declaration.declaration.function_declaration.statements) {
                        if(match_declaration(statement, name.text)) {
                            return resolve_declaration(context, statement);
                        }
                    }

                    for(auto polymorphic_determiner : context->determined_declaration.polymorphic_determiners) {
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
                } break;
            }


            if(context->determined_declaration.declaration.is_top_level) {
                break;
            } else {
                context->determined_declaration = *context->determined_declaration.parent;
            }
        }

        for(auto statement : context->determined_declaration.declaration.file->statements) {
            if(match_declaration(statement, name.text)) {
                return resolve_declaration(context, statement);
            }
        }
    }

    context->determined_declaration = old_determined_declaration;

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

    error(name.range, "Cannot find named reference %s", name.text);

    return { false };
}

static Result<TypedConstantValue> evaluate_constant_index(Type type, ConstantValue value, FileRange range, Type index_type, ConstantValue index_value, FileRange index_range) {
    if(index_type.category != TypeCategory::Integer) {
        error(index_range, "Expected an integer, got %s", type_description(index_type));
    }

    size_t index;
    if(index_type.integer.is_undetermined) {
        if((int64_t)index_value.integer < 0) {
            error(index_range, "Array index %lld out of bounds", (int64_t)index_value.integer);

            return { false };
        }

        index = index_value.integer;
    } else if(index_type.integer.is_signed) {
        switch(index_type.integer.size) {
            case RegisterSize::Size8: {
                if((int8_t)index_value.integer < 0) {
                    error(index_range, "Array index %hhd out of bounds", (int8_t)index_value.integer);

                    return { false };
                }

                index = (uint8_t)index_value.integer;
            } break;

            case RegisterSize::Size16: {
                if((int16_t)index_value.integer < 0) {
                    error(index_range, "Array index %hd out of bounds", (int16_t)index_value.integer);

                    return { false };
                }

                index = (uint16_t)index_value.integer;
            } break;

            case RegisterSize::Size32: {
                if((int32_t)index_value.integer < 0) {
                    error(index_range, "Array index %d out of bounds", (int32_t)index_value.integer);

                    return { false };
                }

                index = (uint32_t)index_value.integer;
            } break;

            case RegisterSize::Size64: {
                if((int8_t)index_value.integer < 0) {
                    error(index_range, "Array index %lld out of bounds", (int64_t)index_value.integer);

                    return { false };
                }

                index = index_value.integer;
            } break;

            default: {
                abort();
            } break;
        }
    } else {
        switch(index_type.integer.size) {
            case RegisterSize::Size8: {
                index = (uint8_t)index_value.integer;
            } break;

            case RegisterSize::Size16: {
                index = (uint16_t)index_value.integer;
            } break;

            case RegisterSize::Size32: {
                index = (uint32_t)index_value.integer;
            } break;

            case RegisterSize::Size64: {
                index = index_value.integer;
            } break;

            default: {
                abort();
            } break;
        }
    }

    switch(type.category) {
        case TypeCategory::Type: {
            switch(value.type.category) {
                case TypeCategory::Integer:
                case TypeCategory::Boolean:
                case TypeCategory::Pointer: {

                } break;

                default: {
                    error(range, "Cannot have arrays of type %s", type_description(type));

                    return { false };
                } break;
            }

            Type type_type;
            type_type.category = TypeCategory::Type;

            ConstantValue type_value;
            type_value.type.category = TypeCategory::StaticArray;
            type_value.type.static_array.length = index;
            type_value.type.static_array.type = heapify(value.type);

            return {
                true,
                {
                    type_type,
                    type_value
                }
            };
        } break;

        case TypeCategory::StaticArray: {
            if(index >= type.static_array.length) {
                error(index_range, "Array index %zu out of bounds", index);

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
            error(range, "Cannot index %s", type_description(type));

            return { false };
        } break;
    }
}

static Result<TypedConstantValue> evaluate_constant_binary_operation(GenerationContext context, BinaryOperator binary_operator, FileRange range, Type left_type, ConstantValue left_value, Type right_type, ConstantValue right_value) {
    switch(left_type.category) {
        case TypeCategory::Integer: {
            if(right_type.category != TypeCategory::Integer) {
                error(range, "Mismatched types %s and %s", type_description(left_type), type_description(right_type));

                return { false };
            }

            RegisterSize size;
            bool is_signed;
            bool is_undetermined;

            uint64_t left;
            uint64_t right;

            if(left_type.integer.is_undetermined && right_type.integer.is_undetermined) {
                is_undetermined = true;

                left = left_value.integer;
                right = right_value.integer;
            } else {
                is_undetermined = false;

                if(left_type.integer.is_undetermined) {
                    size = right_type.integer.size;
                    is_signed = right_type.integer.is_signed;
                } else if(right_type.integer.is_undetermined) {
                    size = left_type.integer.size;
                    is_signed = left_type.integer.is_signed;
                } else if(left_type.integer.size == right_type.integer.size && left_type.integer.is_signed == right_type.integer.is_signed) {
                    size = left_type.integer.size;
                    is_signed = left_type.integer.is_signed;
                } else {
                    error(range, "Mismatched types %s and %s", type_description(left_type), type_description(right_type));

                    return { false };
                }

                if(is_signed) {
                    switch(size) {
                        case RegisterSize::Size8: {
                            left = (int8_t)left_value.integer;
                            right = (int8_t)right_value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            left = (int16_t)left_value.integer;
                            right = (int16_t)right_value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            left = (int32_t)left_value.integer;
                            right = (int32_t)right_value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            left = left_value.integer;
                            right = right_value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } else {
                    switch(size) {
                        case RegisterSize::Size8: {
                            left = (uint8_t)left_value.integer;
                            right = (uint8_t)right_value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            left = (uint16_t)left_value.integer;
                            right = (uint16_t)right_value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            left = (uint32_t)left_value.integer;
                            right = (uint32_t)right_value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            left = left_value.integer;
                            right = right_value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }
            }

            switch(binary_operator) {
                case BinaryOperator::Addition: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = size;
                        result.type.integer.is_signed = is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = left + right;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Subtraction: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = size;
                        result.type.integer.is_signed = is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    result.value.integer = left - right;

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Multiplication: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = size;
                        result.type.integer.is_signed = is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(is_undetermined || is_signed) {
                        result.value.integer = (int64_t)left * (int64_t)right;
                    } else {
                        result.value.integer = left * right;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Division: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = size;
                        result.type.integer.is_signed = is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(is_undetermined || is_signed) {
                        result.value.integer = (int64_t)left / (int64_t)right;
                    } else {
                        result.value.integer = left / right;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Modulo: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Integer;
                    if(is_undetermined) {
                        result.type.integer.is_undetermined = true;
                    } else {
                        result.type.integer.size = size;
                        result.type.integer.is_signed = is_signed;
                        result.type.integer.is_undetermined = true;
                    }

                    if(is_undetermined || is_signed) {
                        result.value.integer = (int64_t)left % (int64_t)right;
                    } else {
                        result.value.integer = left % right;
                    }

                    return {
                        true,
                        result
                    };
                } break;

                case BinaryOperator::Equal: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = left == right;

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
            if(right_type.category != TypeCategory::Boolean) {
                error(range, "Mismatched types %s and %s", type_description(left_type), type_description(right_type));

                return { false };
            }

            switch(binary_operator) {
                case BinaryOperator::Equal: {
                    TypedConstantValue result;
                    result.type.category = TypeCategory::Boolean;
                    result.value.boolean = left_value.boolean == right_value.boolean;

                    return {
                        true,
                        result
                    };
                } break;

                default: {
                    error(range, "Cannot perform that operation on booleans");

                    return { false };
                } break;
            }
        } break;

        default: {
            error(range, "Cannot perform binary operations on %s", type_description(left_type));

            return { false };
        } break;
    }
}

static Result<ConstantValue> evaluate_constant_conversion(GenerationContext context, ConstantValue value, Type value_type, FileRange value_range, Type type, FileRange type_range) {
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
                        error(value_range, "Cannot cast from %s to pointer", type_description(value_type));

                        return { false };
                    }
                } break;

                default: {
                    error(type_range, "Cannot cast integer to this type");

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
                        error(value_range, "Cannot cast from pointer to %s", type_description(type));

                        return { false };
                    }
                } break;

                case TypeCategory::Pointer: {
                    result.pointer = value.pointer;
                } break;

                default: {
                    error(type_range, "Cannot cast pointer to %s", type_description(type));

                    return { false };
                } break;
            }
        } break;

        default: {
            error(value_range, "Cannot cast from %s", type_description(value_type));

            return { false };
        } break;
    }

    return {
        true,
        result
    };
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
                        error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::FileModule: {
                    for(auto statement : expression_value.value.file_module) {
                        if(match_declaration(statement, expression.member_reference.name.text)) {
                            auto old_is_top_level = context->is_top_level;
                            auto old_determined_declaration = context->determined_declaration;
                            auto old_top_level_statements = context->top_level_statements;

                            context->is_top_level = true;
                            context->top_level_statements = expression_value.value.file_module;

                            expect(value, resolve_declaration(context, statement));

                            context->is_top_level = old_is_top_level;
                            context->determined_declaration = old_determined_declaration;
                            context->top_level_statements = old_top_level_statements;

                            return {
                                true,
                                value
                            };
                        }
                    }

                    error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    error(expression.member_reference.expression->range, "%s has no members", type_description(expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, evaluate_constant_expression(context, *expression.index_reference.expression));

            expect(index, evaluate_constant_expression(context, *expression.index_reference.index));

            return evaluate_constant_index(
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

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(expression.range, "Empty array literal");

                return { false };
            }

            auto elements = allocate<ConstantValue>(expression.array_literal.count);

            expect(first_element, evaluate_constant_expression(context, expression.array_literal[0]));
            elements[0] = first_element.value;

            switch(first_element.type.category) {
                case TypeCategory::Integer: {
                    auto element_type = first_element.type;

                    for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                        expect(element, evaluate_constant_expression(context, expression.array_literal[i]));

                        if(element.type.category != TypeCategory::Integer) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(element_type), type_description(element.type));

                            return { false };
                        }

                        if(element_type.integer.is_undetermined) {
                            if(!element.type.integer.is_undetermined) {
                                element_type = element.type;
                            }
                        } else if(element.type.integer.size != element_type.integer.size || element.type.integer.is_signed != element_type.integer.is_signed) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(element_type), type_description(element.type));

                            return { false };
                        }

                        elements[i] = element.value;
                    }

                    Type type;
                    type.category = TypeCategory::StaticArray;
                    type.static_array = {
                        expression.array_literal.count,
                        heapify(type)
                    };

                    ConstantValue value;
                    value.static_array = elements;

                    return {
                        true,
                        {
                            type,
                            value
                        }
                    };
                } break;

                case TypeCategory::Boolean:
                case TypeCategory::Pointer: {
                    for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                        expect(element, evaluate_constant_expression(context, expression.array_literal[i]));

                        if(!types_equal(first_element.type, element.type)) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(first_element.type), type_description(element.type));

                            return { false };
                        }

                        elements[i] = element.value;
                    }

                    Type type;
                    type.category = TypeCategory::StaticArray;
                    type.static_array = {
                        expression.array_literal.count,
                        heapify(first_element.type)
                    };

                    ConstantValue value;
                    value.static_array = elements;

                    return {
                        true,
                        {
                            type,
                            value
                        }
                    };
                } break;

                default: {
                    error(expression.range, "Cannot have arrays of type %s", type_description(first_element.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::FunctionCall: {
            error(expression.range, "Function calls not allowed in global context");

            return { false };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, evaluate_constant_expression(context, *expression.binary_operation.left));

            expect(right, evaluate_constant_expression(context, *expression.binary_operation.right));

            expect(value, evaluate_constant_binary_operation(
                *context,
                expression.binary_operation.binary_operator,
                expression.range,
                left.type,
                left.value,
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
                    switch(expression_value.type.category) {
                        case TypeCategory::Type: {
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

                        case TypeCategory::Function:
                        case TypeCategory::Integer:
                        case TypeCategory::Boolean:
                        case TypeCategory::Void:
                        case TypeCategory::Pointer:
                        case TypeCategory::FileModule: {
                            error(expression.unary_operation.expression->range, "Cannot take pointers to constants of type %s", type_description(expression_value.type));

                            return { false };
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
            expect(expression_value, evaluate_type_expression(context, *expression.array_type));

            TypedConstantValue value;
            value.type.category = TypeCategory::Type;
            value.value.type.category = TypeCategory::Array;
            value.value.type.array = heapify(expression_value);

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
                    error(parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

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
                {
                    expression.function_type.parameters.count,
                    parameters
                },
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
        error(expression.range, "Expected a type, got %s", type_description(expression_value.type));

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
            error(name_range, "Duplicate global name %s", name);

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
        if(strcmp(declaration.file->path, context.file_modules[0].path) != 0)  {
            string_buffer_append(&buffer, "_");
            string_buffer_append(&buffer, path_get_file_component(declaration.file->path));
        }
    } else {
        auto current = *declaration.parent;

        while(true) {
            string_buffer_append(&buffer, "_");
            string_buffer_append(&buffer, get_declaration_name(current));

            if(current.is_top_level) {
                if(strcmp(current.file->path, context.file_modules[0].path) != 0)  {
                    string_buffer_append(&buffer, "_");
                    string_buffer_append(&buffer, path_get_file_component(current.file->path));
                }

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

                    ConstantValue value;
                    value.function = {
                        declaration,
                        context->determined_declaration
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
                {
                    declaration.function_declaration.parameters.count,
                    parameterTypes
                },
                heapify(return_type)
            };

            ConstantValue value;
            value.function = {
                declaration,
                context->determined_declaration
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
            Type type;
            type.category = TypeCategory::FileModule;

            ConstantValue value;

            for(auto file_module : context->file_modules) {
                if(strcmp(file_module.path, declaration.import) == 0) {
                    value.file_module = file_module.statements;
                }
            }

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
            error(name.range, "Duplicate variable name %s", name.text);
            error(variable.name.range, "Original declared here");

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

enum struct ExpressionValueCategory {
    Constant,
    Register,
    Address
};

struct ExpressionValue {
    ExpressionValueCategory category;

    Type type;

    union {
        size_t register_;

        size_t address;

        ConstantValue constant;
    };
};

static size_t allocate_register(GenerationContext *context) {
    auto index = context->next_register;

    context->next_register += 1;

    return index;
}

static size_t generate_boolean_register_value(GenerationContext *context, List<Instruction> *instructions, ExpressionValue value) {
    switch(value.category) {
        case ExpressionValueCategory::Constant: {
            auto register_index = allocate_register(context);

            Instruction constant;
            constant.type = InstructionType::Constant;
            constant.constant.size = context->default_integer_size;
            constant.constant.destination_register = register_index;

            if(value.constant.boolean) {
                constant.constant.value = 1;
            } else {
                constant.constant.value = 0;
            }

            append(instructions, constant);

            return register_index;
        } break;

        case ExpressionValueCategory::Register: {
            return value.register_;
        } break;

        case ExpressionValueCategory::Address: {
            auto register_index = allocate_register(context);

            Instruction load;
            load.type = InstructionType::LoadInteger;
            load.load_integer.size = context->default_integer_size;
            load.load_integer.address_register = value.address;
            load.load_integer.destination_register = register_index;

            append(instructions, load);

            return register_index;
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_pointer_register_value(GenerationContext *context, List<Instruction> *instructions, ExpressionValue value) {
    switch(value.category) {
        case ExpressionValueCategory::Constant: {
            auto register_index = allocate_register(context);

            Instruction constant;
            constant.type = InstructionType::Constant;
            constant.constant.size = context->address_integer_size;
            constant.constant.destination_register = register_index;
            constant.constant.value = value.constant.pointer;

            append(instructions, constant);

            return register_index;
        } break;

        case ExpressionValueCategory::Register: {
            return value.register_;
        } break;

        case ExpressionValueCategory::Address: {
            auto register_index = allocate_register(context);

            Instruction load;
            load.type = InstructionType::LoadInteger;
            load.load_integer.size = context->address_integer_size;
            load.load_integer.address_register = value.address;
            load.load_integer.destination_register = register_index;

            append(instructions, load);

            return register_index;
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_integer_register_value(GenerationContext *context, List<Instruction> *instructions, RegisterSize actual_size, ExpressionValue value) {
    switch(value.category) {
        case ExpressionValueCategory::Constant: {
            auto register_index = allocate_register(context);

            Instruction constant;
            constant.type = InstructionType::Constant;
            constant.constant.size = actual_size;
            constant.constant.destination_register = register_index;
            constant.constant.value = value.constant.integer;

            append(instructions, constant);

            return register_index;
        } break;

        case ExpressionValueCategory::Register: {
            return value.register_;
        } break;

        case ExpressionValueCategory::Address: {
            auto register_index = allocate_register(context);

            Instruction load;
            load.type = InstructionType::LoadInteger;
            load.load_integer.size = actual_size;
            load.load_integer.address_register = value.address;
            load.load_integer.destination_register = register_index;

            append(instructions, load);

            return register_index;
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_integer_register_value(GenerationContext *context, List<Instruction> *instructions, ExpressionValue value) {
    return generate_integer_register_value(context, instructions, value.type.integer.size, value);
}

template <typename T>
T *write_integers(Array<ConstantValue> values) {
    auto data = allocate<T>(values.count);

    for(size_t i = 0; i < values.count; i += 1) {
        data[i] = values[i].integer;
    }

    return data;
}

template <typename T>
T *write_booleans(Array<ConstantValue> values) {
    auto data = allocate<T>(values.count);

    for(size_t i = 0; i < values.count; i += 1) {
        if(values[i].boolean) {
            data[i] = 1;
        } else {
            data[i] = 0;
        }
    }

    return data;
}

template <typename T>
T *write_pointers(Array<ConstantValue> values) {
    auto data = allocate<T>(values.count);

    for(size_t i = 0; i < values.count; i += 1) {
        data[i] = values[i].pointer;
    }

    return data;
}

static const char *register_static_array_constant(GenerationContext *context, Type type, Array<ConstantValue> values) {
    uint8_t *data;
    size_t element_size;

    switch(type.category) {
        case TypeCategory::Integer: {
            switch(type.integer.size) {
                case RegisterSize::Size8: {
                    data = write_integers<uint8_t>(values);
                    element_size = 1;
                } break;

                case RegisterSize::Size16: {
                    data = (uint8_t*)write_integers<uint16_t>(values);
                    element_size = 2;
                } break;

                case RegisterSize::Size32: {
                    data = (uint8_t*)write_integers<uint32_t>(values);
                    element_size = 4;
                } break;

                case RegisterSize::Size64: {
                    data = (uint8_t*)write_integers<uint64_t>(values);
                    element_size = 8;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::Boolean: {
            switch(context->default_integer_size) {
                case RegisterSize::Size8: {
                    data = write_booleans<uint8_t>(values);
                    element_size = 1;
                } break;

                case RegisterSize::Size16: {
                    data = (uint8_t*)write_booleans<uint16_t>(values);
                    element_size = 2;
                } break;

                case RegisterSize::Size32: {
                    data = (uint8_t*)write_booleans<uint32_t>(values);
                    element_size = 4;
                } break;

                case RegisterSize::Size64: {
                    data = (uint8_t*)write_booleans<uint64_t>(values);
                    element_size = 8;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::Pointer: {
            switch(context->address_integer_size) {
                case RegisterSize::Size8: {
                    data = write_pointers<uint8_t>(values);
                    element_size = 1;
                } break;

                case RegisterSize::Size16: {
                    data = (uint8_t*)write_pointers<uint16_t>(values);
                    element_size = 2;
                } break;

                case RegisterSize::Size32: {
                    data = (uint8_t*)write_pointers<uint32_t>(values);
                    element_size = 4;
                } break;

                case RegisterSize::Size64: {
                    data = (uint8_t*)write_pointers<uint64_t>(values);
                    element_size = 8;
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

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, context->static_constants.count);

    append(&context->static_constants, {
        name_buffer,
        {
            values.count * element_size,
            data
        }
    });

    return name_buffer;
}

static void generate_array_copy(GenerationContext *context, List<Instruction> *instructions, size_t source_address_register, size_t destination_address_register) {
    auto pointer_register = allocate_register(context);

    Instruction load_pointer;
    load_pointer.type = InstructionType::LoadInteger;
    load_pointer.load_integer.size = context->address_integer_size;
    load_pointer.load_integer.address_register = source_address_register;
    load_pointer.load_integer.destination_register = pointer_register;

    append(instructions, load_pointer);

    Instruction store_pointer;
    store_pointer.type = InstructionType::StoreInteger;
    store_pointer.store_integer.size = context->address_integer_size;
    store_pointer.store_integer.source_register = pointer_register;
    store_pointer.store_integer.address_register = destination_address_register;

    append(instructions, store_pointer);

    auto offset_register = allocate_register(context);

    Instruction constant;
    constant.type = InstructionType::Constant;
    constant.constant.size = context->address_integer_size;
    constant.constant.destination_register = offset_register;
    constant.constant.value = register_size_to_byte_size(context->address_integer_size);

    append(instructions, constant);

    auto source_length_address_register = allocate_register(context);

    Instruction source_add;
    source_add.type = InstructionType::BinaryOperation;
    source_add.binary_operation.type = BinaryOperationType::Add;
    source_add.binary_operation.size = context->address_integer_size;
    source_add.binary_operation.source_register_a = source_address_register;
    source_add.binary_operation.source_register_b = offset_register;
    source_add.binary_operation.destination_register = source_length_address_register;

    append(instructions, source_add);

    auto length_register = allocate_register(context);

    Instruction load_length;
    load_length.type = InstructionType::LoadInteger;
    load_length.load_integer.size = context->address_integer_size;
    load_length.load_integer.address_register = source_length_address_register;
    load_length.load_integer.destination_register = length_register;

    append(instructions, load_length);

    auto destination_length_address_register = allocate_register(context);

    Instruction destination_add;
    destination_add.type = InstructionType::BinaryOperation;
    destination_add.binary_operation.type = BinaryOperationType::Add;
    destination_add.binary_operation.size = context->address_integer_size;
    destination_add.binary_operation.source_register_a = destination_address_register;
    destination_add.binary_operation.source_register_b = offset_register;
    destination_add.binary_operation.destination_register = destination_length_address_register;

    append(instructions, destination_add);

    Instruction store_length;
    store_length.type = InstructionType::StoreInteger;
    store_length.store_integer.size = context->address_integer_size;
    store_length.store_integer.source_register = length_register;
    store_length.store_integer.address_register = destination_length_address_register;

    append(instructions, store_length);
}

static void generate_variable_assignment(GenerationContext *context, List<Instruction> *instructions, size_t address_register, ExpressionValue value) {
    switch(value.type.category) {
        case TypeCategory::Integer: {
            auto register_index = generate_integer_register_value(context, instructions, value);

            Instruction store;
            store.type = InstructionType::StoreInteger;
            store.store_integer.size = value.type.integer.size;
            store.store_integer.address_register = address_register;
            store.store_integer.source_register = register_index;

            append(instructions, store);
        } break;

        case TypeCategory::Boolean: {
            auto register_index = generate_boolean_register_value(context, instructions, value);

            Instruction store;
            store.type = InstructionType::StoreInteger;
            store.store_integer.size = context->default_integer_size;
            store.store_integer.address_register = address_register;
            store.store_integer.source_register = register_index;

            append(instructions, store);
        } break;

        case TypeCategory::Pointer: {
            auto register_index = generate_pointer_register_value(context, instructions, value);

            Instruction store;
            store.type = InstructionType::StoreInteger;
            store.store_integer.size = context->address_integer_size;
            store.store_integer.address_register = address_register;
            store.store_integer.source_register = register_index;

            append(instructions, store);
        } break;

        case TypeCategory::Array: {
            switch(value.category) {
                case ExpressionValueCategory::Constant: {
                    auto pointer_register = allocate_register(context);

                    Instruction pointer_constant;
                    pointer_constant.type = InstructionType::Constant;
                    pointer_constant.constant.size = context->address_integer_size;
                    pointer_constant.constant.destination_register = pointer_register;
                    pointer_constant.constant.value = value.constant.array.pointer;

                    append(instructions, pointer_constant);

                    Instruction store_pointer;
                    store_pointer.type = InstructionType::StoreInteger;
                    store_pointer.store_integer.size = context->address_integer_size;
                    store_pointer.store_integer.source_register = pointer_register;
                    store_pointer.store_integer.address_register = address_register;

                    append(instructions, store_pointer);

                    auto offset_register = allocate_register(context);

                    Instruction size_constant;
                    size_constant.type = InstructionType::Constant;
                    size_constant.constant.size = context->address_integer_size;
                    size_constant.constant.destination_register = offset_register;
                    size_constant.constant.value = register_size_to_byte_size(context->address_integer_size);

                    append(instructions, size_constant);

                    auto length_register = allocate_register(context);

                    Instruction length_constant;
                    length_constant.type = InstructionType::Constant;
                    length_constant.constant.size = context->address_integer_size;
                    length_constant.constant.destination_register = pointer_register;
                    length_constant.constant.value = value.constant.array.length;

                    append(instructions, length_constant);

                    auto length_address_register = allocate_register(context);

                    Instruction add;
                    add.type = InstructionType::BinaryOperation;
                    add.binary_operation.type = BinaryOperationType::Add;
                    add.binary_operation.size = context->address_integer_size;
                    add.binary_operation.source_register_a = address_register;
                    add.binary_operation.source_register_b = offset_register;
                    add.binary_operation.destination_register = length_address_register;

                    append(instructions, add);

                    Instruction store_length;
                    store_length.type = InstructionType::StoreInteger;
                    store_length.store_integer.size = context->address_integer_size;
                    store_length.store_integer.source_register = length_register;
                    store_length.store_integer.address_register = length_address_register;

                    append(instructions, store_length);
                } break;

                case ExpressionValueCategory::Register: {
                    generate_array_copy(context, instructions, value.register_, address_register);
                } break;

                case ExpressionValueCategory::Address: {
                    generate_array_copy(context, instructions, value.address, address_register);
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::StaticArray: {
            switch(value.category) {
                case ExpressionValueCategory::Constant: {
                    auto constant_name = register_static_array_constant(
                        context,
                        *value.type.static_array.type,
                        Array<ConstantValue> {
                            value.type.static_array.length,
                            value.constant.static_array
                        }
                    );

                    auto constant_address_register = allocate_register(context);

                    Instruction reference;
                    reference.type = InstructionType::ReferenceStatic;
                    reference.reference_static.name = constant_name;
                    reference.reference_static.destination_register = constant_address_register;

                    append(instructions, reference);

                    auto length_register = allocate_register(context);

                    Instruction constant;
                    constant.type = InstructionType::Constant;
                    constant.constant.size = context->address_integer_size;
                    constant.constant.destination_register = length_register;
                    constant.constant.value = value.type.static_array.length * get_type_size(*context, *value.type.static_array.type);

                    append(instructions, constant);

                    Instruction copy;
                    copy.type = InstructionType::CopyMemory;
                    copy.copy_memory.length_register = length_register;
                    copy.copy_memory.source_address_register = constant_address_register;
                    copy.copy_memory.destination_address_register = address_register;

                    append(instructions, copy);
                } break;

                case ExpressionValueCategory::Register: {
                    auto length_register = allocate_register(context);

                    Instruction constant;
                    constant.type = InstructionType::Constant;
                    constant.constant.size = context->address_integer_size;
                    constant.constant.destination_register = length_register;
                    constant.constant.value = value.type.static_array.length * get_type_size(*context, *value.type.static_array.type);

                    append(instructions, constant);

                    Instruction copy;
                    copy.type = InstructionType::CopyMemory;
                    copy.copy_memory.length_register = length_register;
                    copy.copy_memory.source_address_register = value.register_;
                    copy.copy_memory.destination_address_register = address_register;

                    append(instructions, copy);
                } break;

                case ExpressionValueCategory::Address: {
                    auto length_register = allocate_register(context);

                    Instruction constant;
                    constant.type = InstructionType::Constant;
                    constant.constant.size = context->address_integer_size;
                    constant.constant.destination_register = length_register;
                    constant.constant.value = value.type.static_array.length * get_type_size(*context, *value.type.static_array.type);

                    append(instructions, constant);

                    Instruction copy;
                    copy.type = InstructionType::CopyMemory;
                    copy.copy_memory.length_register = length_register;
                    copy.copy_memory.source_address_register = value.address;
                    copy.copy_memory.destination_address_register = address_register;

                    append(instructions, copy);
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
}

static Result<ExpressionValue> generate_expression(GenerationContext *context, List<Instruction> *instructions, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Address;
                        value.type = variable.type;
                        value.address = variable.register_index;

                        return {
                            true,
                            value
                        };
                    }
                }
            }

            for(auto parameter : context->parameters) {
                if(strcmp(parameter.name.text, expression.named_reference.text) == 0) {
                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Register;
                    value.type = parameter.type;
                    value.address = parameter.register_index;

                    return {
                        true,
                        value
                    };
                }
            }

            expect(constant, resolve_constant_named_reference(context, expression.named_reference));

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type = constant.type;
            value.constant = constant.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.index_reference.expression));

            expect(index, generate_expression(context, instructions, *expression.index_reference.index));

            if(expression_value.category == ExpressionValueCategory::Constant && index.category == ExpressionValueCategory::Constant) {
                expect(constant, evaluate_constant_index(
                    expression_value.type,
                    expression_value.constant,
                    expression.index_reference.expression->range,
                    index.type,
                    index.constant,
                    expression.index_reference.index->range
                ));

                ExpressionValue value;
                value.category = ExpressionValueCategory::Constant;
                value.type = constant.type;
                value.constant = constant.value;

                return {
                    true,
                    value
                };
            }

            if(index.type.category != TypeCategory::Integer) {
                error(expression.index_reference.index->range, "Expected an integer, got %s", type_description(index.type));

                return { false };
            }

            size_t index_register;
            switch(index.category) {
                case ExpressionValueCategory::Constant: {
                    index_register = allocate_register(context);

                    Instruction constant;
                    constant.type = InstructionType::Constant;
                    constant.constant.size = index.type.integer.size;
                    constant.constant.destination_register = index_register;
                    constant.constant.value = index.constant.integer;

                    append(instructions, constant);
                } break;

                case ExpressionValueCategory::Register: {
                    index_register = index.register_;
                } break;

                case ExpressionValueCategory::Address: {
                    index_register = allocate_register(context);

                    Instruction load;
                    load.type = InstructionType::LoadInteger;
                    load.load_integer.size = index.type.integer.size;
                    load.load_integer.address_register = index.address;
                    load.load_integer.destination_register = index_register;

                    append(instructions, load);
                } break;

                default: {
                    abort();
                } break;
            }

            size_t base_address_register;
            Type element_type;
            bool assignable;
            switch(expression_value.category) {
                case ExpressionValueCategory::Constant: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = allocate_register(context);
                            element_type = *expression_value.type.array;
                            assignable = true;

                            Instruction constant;
                            constant.type = InstructionType::Constant;
                            constant.constant.size = context->address_integer_size;
                            constant.constant.destination_register = base_address_register;
                            constant.constant.value = expression_value.constant.array.pointer;

                            append(instructions, constant);
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = allocate_register(context);
                            element_type = *expression_value.type.static_array.type;
                            assignable = false;

                            auto constant_name = register_static_array_constant(
                                context,
                                *expression_value.type.static_array.type,
                                Array<ConstantValue> {
                                    expression_value.type.static_array.length,
                                    expression_value.constant.static_array
                                }
                            );

                            Instruction reference;
                            reference.type = InstructionType::ReferenceStatic;
                            reference.reference_static.name = constant_name;
                            reference.reference_static.destination_register = base_address_register;

                            append(instructions, reference);
                        } break;

                        default: {
                            error(expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ExpressionValueCategory::Register: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = allocate_register(context);
                            element_type = *expression_value.type.array;
                            assignable = true;

                            Instruction load;
                            load.type = InstructionType::LoadInteger;
                            load.load_integer.size = context->address_integer_size;
                            load.load_integer.address_register = expression_value.register_;
                            load.load_integer.destination_register = base_address_register;

                            append(instructions, load);
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.register_;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ExpressionValueCategory::Address: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = allocate_register(context);
                            element_type = *expression_value.type.array;
                            assignable = true;

                            Instruction load;
                            load.type = InstructionType::LoadInteger;
                            load.load_integer.size = context->address_integer_size;
                            load.load_integer.address_register = expression_value.address;
                            load.load_integer.destination_register = base_address_register;

                            append(instructions, load);
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.address;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                default: {
                    abort();
                } break;
            }

            auto element_size_register = allocate_register(context);

            Instruction constant;
            constant.type = InstructionType::Constant;
            constant.constant.size = context->address_integer_size;
            constant.constant.destination_register = element_size_register;
            constant.constant.value = get_type_size(*context, element_type);

            append(instructions, constant);

            auto offset_register = allocate_register(context);

            Instruction multiply;
            multiply.type = InstructionType::BinaryOperation;
            multiply.binary_operation.type = BinaryOperationType::UnsignedMultiply;
            multiply.binary_operation.size = context->address_integer_size;
            multiply.binary_operation.source_register_a = element_size_register;
            multiply.binary_operation.source_register_b = index_register;
            multiply.binary_operation.destination_register = offset_register;

            append(instructions, multiply);

            auto final_address_register = allocate_register(context);

            Instruction add;
            add.type = InstructionType::BinaryOperation;
            add.binary_operation.type = BinaryOperationType::Add;
            add.binary_operation.size = context->address_integer_size;
            add.binary_operation.source_register_a = base_address_register;
            add.binary_operation.source_register_b = offset_register;
            add.binary_operation.destination_register = final_address_register;

            append(instructions, add);

            ExpressionValue value;
            value.type = element_type;

            if(assignable) {
                auto register_index = allocate_register(context);

                RegisterSize size;
                switch(element_type.category) {
                    case TypeCategory::Integer: {
                        size = element_type.integer.size;
                    } break;

                    case TypeCategory::Boolean: {
                        size = context->default_integer_size;
                    } break;

                    case TypeCategory::Pointer: {
                        size = context->address_integer_size;
                    } break;

                    default: {
                        abort();
                    } break;
                }

                Instruction load;
                load.type = InstructionType::LoadInteger;
                load.load_integer.size = size;
                load.load_integer.address_register = final_address_register;
                load.load_integer.destination_register = register_index;

                append(instructions, load);

                value.category = ExpressionValueCategory::Register;
                value.register_ = register_index;
            } else {
                value.category = ExpressionValueCategory::Address;
                value.address = final_address_register;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::MemberReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.member_reference.expression));

            switch(expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        switch(expression_value.category) {
                            case ExpressionValueCategory::Constant: {
                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Constant;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = {
                                    context->address_integer_size,
                                    false
                                };
                                value.constant.integer = expression_value.constant.array.length;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Register: {
                                auto offset_register = allocate_register(context);

                                Instruction constant;
                                constant.type = InstructionType::Constant;
                                constant.constant.size = context->address_integer_size;
                                constant.constant.destination_register = offset_register;
                                constant.constant.value = register_size_to_byte_size(context->address_integer_size);

                                append(instructions, constant);

                                auto address_register = allocate_register(context);

                                Instruction add;
                                add.type = InstructionType::BinaryOperation;
                                add.binary_operation.type = BinaryOperationType::Add;
                                add.binary_operation.size = context->address_integer_size;
                                add.binary_operation.source_register_a = expression_value.register_;
                                add.binary_operation.source_register_b = offset_register;
                                add.binary_operation.destination_register = address_register;

                                append(instructions, add);

                                auto value_register = allocate_register(context);

                                Instruction load;
                                load.type = InstructionType::LoadInteger;
                                load.load_integer.size = context->address_integer_size;
                                load.load_integer.address_register = address_register;
                                load.load_integer.destination_register = value_register;

                                append(instructions, load);

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Register;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = {
                                    context->address_integer_size,
                                    false
                                };
                                value.register_ = value_register;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Address: {
                                auto offset_register = allocate_register(context);

                                Instruction constant;
                                constant.type = InstructionType::Constant;
                                constant.constant.size = context->address_integer_size;
                                constant.constant.destination_register = offset_register;
                                constant.constant.value = register_size_to_byte_size(context->address_integer_size);

                                append(instructions, constant);

                                auto address_register = allocate_register(context);

                                Instruction add;
                                add.type = InstructionType::BinaryOperation;
                                add.binary_operation.type = BinaryOperationType::Add;
                                add.binary_operation.size = context->address_integer_size;
                                add.binary_operation.source_register_a = expression_value.register_;
                                add.binary_operation.source_register_b = offset_register;
                                add.binary_operation.destination_register = address_register;

                                append(instructions, add);

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Address;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = {
                                    context->address_integer_size,
                                    false
                                };
                                value.address = address_register;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        switch(expression_value.category) {
                            case ExpressionValueCategory::Constant: {
                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Constant;
                                value.type.category = TypeCategory::Pointer;
                                value.type.pointer = expression_value.type.array;
                                value.constant.pointer = expression_value.constant.array.pointer;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Register: {
                                auto value_register = allocate_register(context);

                                Instruction load;
                                load.type = InstructionType::LoadInteger;
                                load.load_integer.size = context->address_integer_size;
                                load.load_integer.address_register = expression_value.register_;
                                load.load_integer.destination_register = value_register;

                                append(instructions, load);

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Register;
                                value.type.category = TypeCategory::Pointer;
                                value.type.pointer = expression_value.type.array;
                                value.register_ = value_register;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Address: {
                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Address;
                                value.type.category = TypeCategory::Pointer;
                                value.type.pointer = expression_value.type.array;
                                value.register_ = expression_value.address;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    } else {
                        error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::FileModule: {
                    assert(expression_value.category == ExpressionValueCategory::Constant);

                    for(auto statement : expression_value.constant.file_module) {
                        if(match_declaration(statement, expression.member_reference.name.text)) {
                            auto old_is_top_level = context->is_top_level;
                            auto old_determined_declaration = context->determined_declaration;
                            auto old_top_level_statements = context->top_level_statements;

                            context->is_top_level = true;
                            context->top_level_statements = expression_value.constant.file_module;

                            expect(constant_value, resolve_declaration(context, statement));

                            context->is_top_level = old_is_top_level;
                            context->determined_declaration = old_determined_declaration;
                            context->top_level_statements = old_top_level_statements;

                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Constant;
                            value.type = constant_value.type;
                            value.constant = constant_value.value;

                            return {
                                true,
                                value
                            };
                        }
                    }

                    error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    error(expression.member_reference.expression->range, "Type %s has no members", type_description(expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IntegerLiteral: {
            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Integer;
            value.type.integer.is_undetermined = true;
            value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(expression.range, "Empty array literal");

                return { false };
            }

            auto element_values = allocate<ExpressionValue>(expression.array_literal.count);
            expect(first_element_value, generate_expression(context, instructions, expression.array_literal[0]));
            element_values[0] = first_element_value;

            auto all_constant = first_element_value.category == ExpressionValueCategory::Constant;
            auto element_type = first_element_value.type;
            switch(first_element_value.type.category) {
                case TypeCategory::Integer: {
                    for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                        expect(element_value, generate_expression(context, instructions, expression.array_literal[i]));

                        if(element_value.category != ExpressionValueCategory::Constant) {
                            all_constant = false;
                        }

                        if(element_value.type.category != TypeCategory::Integer) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(element_type), type_description(element_value.type));

                            return { false };
                        }

                        if(element_type.integer.is_undetermined) {
                            if(!element_value.type.integer.is_undetermined) {
                                element_type = element_value.type;
                            }
                        } else if(element_value.type.integer.size != element_type.integer.size || element_value.type.integer.is_signed != element_type.integer.is_signed) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(element_type), type_description(element_value.type));

                            return { false };
                        }

                        element_values[i] = element_value;
                    }

                    if(element_type.integer.is_undetermined) {
                        element_type.integer.size = context->default_integer_size;
                        element_type.integer.is_signed = true;
                        element_type.integer.is_undetermined = false;
                    }
                } break;

                case TypeCategory::Boolean:
                case TypeCategory::Pointer: {
                    for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                        expect(element_value, generate_expression(context, instructions, expression.array_literal[i]));

                        if(element_value.category != ExpressionValueCategory::Constant) {
                            all_constant = false;
                        }

                        if(!types_equal(element_type, element_value.type)) {
                            error(expression.array_literal[i].range, "Mismatched array literal type. Expected %s, got %s", type_description(element_type), type_description(element_value.type));

                            return { false };
                        }

                        element_values[i] = element_value;
                    }
                } break;

                default: {
                    error(expression.range, "Cannot have arrays of type %s", type_description(first_element_value.type));

                    return { false };
                } break;
            }

            if(all_constant) {
                auto elements = allocate<ConstantValue>(expression.array_literal.count);
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    elements[i] = element_values[i].constant;
                }

                ExpressionValue value;
                value.category = ExpressionValueCategory::Constant;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(element_type)
                };
                value.constant.static_array = elements;

                return {
                    true,
                    value
                };
            } else {
                auto base_address_register = allocate_register(context);

                Instruction allocate;
                allocate.type = InstructionType::AllocateLocal;
                allocate.allocate_local.size = get_type_size(*context, element_type) * expression.array_literal.count;
                allocate.allocate_local.destination_register = base_address_register;

                append(instructions, allocate);

                auto element_size_register = allocate_register(context);

                Instruction constant;
                constant.type = InstructionType::Constant;
                constant.constant.size = context->address_integer_size;
                constant.constant.destination_register = element_size_register;
                constant.constant.value = get_type_size(*context, element_type);

                append(instructions, constant);

                auto address_register = base_address_register;
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    size_t value_register;
                    RegisterSize value_size;
                    switch(element_type.category) {
                        case TypeCategory::Integer: {
                            value_register = generate_integer_register_value(context, instructions, element_values[i]);
                            value_size = element_type.integer.size;
                        } break;

                        case TypeCategory::Boolean: {
                            value_register = generate_boolean_register_value(context, instructions, element_values[i]);
                            value_size = context->default_integer_size;
                        } break;

                        case TypeCategory::Pointer: {
                            value_register = generate_pointer_register_value(context, instructions, element_values[i]);
                            value_size = context->address_integer_size;
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    Instruction store;
                    store.type = InstructionType::StoreInteger;
                    store.store_integer.size = value_size;
                    store.store_integer.source_register = value_register;
                    store.store_integer.address_register = address_register;

                    append(instructions, store);

                    if(i != expression.array_literal.count - 1) {
                        auto new_address_register = allocate_register(context);

                        Instruction add;
                        add.type = InstructionType::BinaryOperation;
                        add.binary_operation.type = BinaryOperationType::Add;
                        add.binary_operation.size = context->address_integer_size;
                        add.binary_operation.source_register_a = address_register;
                        add.binary_operation.source_register_b = element_size_register;
                        add.binary_operation.destination_register = new_address_register;

                        append(instructions, add);

                        address_register = new_address_register;
                    }
                }

                ExpressionValue value;
                value.category = ExpressionValueCategory::Register;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(element_type)
                };
                value.register_ = address_register;

                return {
                    true,
                    value
                };
            }
        } break;

        case ExpressionType::FunctionCall: {
            expect(expression_value, generate_expression(context, instructions, *expression.function_call.expression));

            if(expression_value.type.category != TypeCategory::Function) {
                error(expression.function_call.expression->range, "Cannot call %s", type_description(expression_value.type));

                return { false };
            }

            if(expression.function_call.parameters.count != expression_value.type.function.parameters.count) {
                error(expression.range, "Incorrect number of parameters. Expected %zu, got %zu", expression_value.type.function.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            auto parameter_count = expression.function_call.parameters.count;

            auto function_declaration = expression_value.constant.function.declaration.function_declaration;

            const char *function_name;
            auto function_parameter_values = allocate<ExpressionValue>(parameter_count);
            Type *function_parameter_types;
            Type function_return_type;

            if(expression_value.type.function.is_polymorphic) {
                List<PolymorphicDeterminer> polymorphic_determiners{};

                for(size_t i = 0; i < parameter_count; i += 1) {
                    auto parameter = function_declaration.parameters[i];

                    if(parameter.is_polymorphic_determiner) {
                        for(auto polymorphic_determiner : polymorphic_determiners) {
                            if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                error(parameter.polymorphic_determiner.range, "Duplicate polymorphic parameter %s", parameter.polymorphic_determiner.text);

                                return { false };
                            }
                        }

                        expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                        Type actual_type;
                        if(value.type.category == TypeCategory::Integer && value.type.integer.is_undetermined) {
                            
                        } else {
                            actual_type = value.type;
                        }

                        append(&polymorphic_determiners, {
                            parameter.polymorphic_determiner.text,
                            actual_type
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

                char *mangled_name_buffer{};

                string_buffer_append(&mangled_name_buffer, "function_");

                char buffer[32];
                sprintf(buffer, "%zu", context->runtime_functions.count);
                string_buffer_append(&mangled_name_buffer, buffer);

                function_name = mangled_name_buffer;

                auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(expression.function_call.parameters.count);

                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    runtime_function_parameters[i] = {
                        function_declaration.parameters[i].name,
                        function_parameter_types[i],
                        expression_value.constant.function.declaration.function_declaration.parameters[i].type.range
                    };
                }

                RuntimeFunction runtime_function;
                runtime_function.mangled_name = mangled_name_buffer;
                runtime_function.parameters = {
                    expression.function_call.parameters.count,
                    runtime_function_parameters
                };
                runtime_function.return_type = function_return_type;
                runtime_function.declaration = expression_value.constant.function.declaration;
                runtime_function.polymorphic_determiners = to_array(polymorphic_determiners);

                if(!expression_value.constant.function.declaration.is_top_level) {
                    runtime_function.parent = expression_value.constant.function.parent;
                }

                append(&context->runtime_functions, runtime_function);

                if(!register_global_name(context, mangled_name_buffer, function_declaration.name.range)) {
                    return { false };
                }
            } else {
                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    char *parameter_source{};
                    expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                    function_parameter_values[i] = value;
                }

                function_name = generate_mangled_name(*context, expression_value.constant.function.declaration);
                function_parameter_types = expression_value.type.function.parameters.elements;
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
                    runtime_function.declaration = expression_value.constant.function.declaration;
                    runtime_function.polymorphic_determiners = {};

                    if(!expression_value.constant.function.declaration.is_top_level) {
                        runtime_function.parent = expression_value.constant.function.parent;
                    }

                    append(&context->runtime_functions, runtime_function);

                    if(!register_global_name(context, function_name, function_declaration.name.range)) {
                        return { false };
                    }
                }
            }

            auto function_parameter_registers = allocate<size_t>(parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                auto value = function_parameter_values[i];

                switch(value.type.category) {
                    case TypeCategory::Integer: {
                        if(function_parameter_types[i].category != TypeCategory::Integer) {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }

                        if(value.type.integer.is_undetermined) {
                            function_parameter_registers[i] = generate_integer_register_value(context, instructions, function_parameter_types[i].integer.size, value);
                        } else if(value.type.integer.size == function_parameter_types[i].integer.size && value.type.integer.is_signed == function_parameter_types[i].integer.is_signed) {
                            function_parameter_registers[i] = generate_integer_register_value(context, instructions, value);
                        } else {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }
                    } break;

                    case TypeCategory::Boolean: {
                        if(function_parameter_types[i].category != TypeCategory::Boolean) {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }

                        function_parameter_registers[i] = generate_boolean_register_value(context, instructions, value);
                    } break;

                    case TypeCategory::Pointer: {
                        if(function_parameter_types[i].category != TypeCategory::Pointer || !types_equal(*value.type.pointer, *function_parameter_types[i].pointer)) {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }

                        function_parameter_registers[i] = generate_pointer_register_value(context, instructions, value);
                    } break;

                    case TypeCategory::Array: {
                        if(function_parameter_types[i].category != TypeCategory::Array || !types_equal(*value.type.array, *function_parameter_types[i].array)) {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }

                        switch(value.category) {
                            case ExpressionValueCategory::Constant: {
                                auto local_register = allocate_register(context);

                                Instruction alloc;
                                alloc.type = InstructionType::AllocateLocal;
                                alloc.allocate_local.size = register_size_to_byte_size(context->address_integer_size);
                                alloc.allocate_local.destination_register = local_register;

                                append(instructions, alloc);

                                auto pointer_register = allocate_register(context);

                                Instruction pointer_constant;
                                pointer_constant.type = InstructionType::Constant;
                                pointer_constant.constant.size = context->address_integer_size;
                                pointer_constant.constant.destination_register = pointer_register;
                                pointer_constant.constant.value = value.constant.array.pointer;

                                append(instructions, pointer_constant);

                                Instruction store_pointer;
                                store_pointer.type = InstructionType::StoreInteger;
                                store_pointer.store_integer.size = context->address_integer_size;
                                store_pointer.store_integer.source_register = pointer_register;
                                store_pointer.store_integer.address_register = local_register;

                                append(instructions, store_pointer);

                                auto offset_register = allocate_register(context);

                                Instruction size_constant;
                                size_constant.type = InstructionType::Constant;
                                size_constant.constant.size = context->address_integer_size;
                                size_constant.constant.destination_register = offset_register;
                                size_constant.constant.value = register_size_to_byte_size(context->address_integer_size);

                                append(instructions, size_constant);

                                auto length_register = allocate_register(context);

                                Instruction length_constant;
                                length_constant.type = InstructionType::Constant;
                                length_constant.constant.size = context->address_integer_size;
                                length_constant.constant.destination_register = pointer_register;
                                length_constant.constant.value = value.constant.array.length;

                                append(instructions, length_constant);

                                auto length_address_register = allocate_register(context);

                                Instruction add;
                                add.type = InstructionType::BinaryOperation;
                                add.binary_operation.type = BinaryOperationType::Add;
                                add.binary_operation.size = context->address_integer_size;
                                add.binary_operation.source_register_a = local_register;
                                add.binary_operation.source_register_b = offset_register;
                                add.binary_operation.destination_register = length_address_register;

                                append(instructions, add);

                                Instruction store_length;
                                store_length.type = InstructionType::StoreInteger;
                                store_length.store_integer.size = context->address_integer_size;
                                store_length.store_integer.source_register = length_register;
                                store_length.store_integer.address_register = length_address_register;

                                append(instructions, store_length);

                                function_parameter_registers[i] = local_register;
                            } break;

                            case ExpressionValueCategory::Register: {
                                function_parameter_registers[i] = value.register_;
                            } break;

                            case ExpressionValueCategory::Address: {
                                function_parameter_registers[i] = value.address;
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    } break;

                    case TypeCategory::StaticArray: {
                        if(
                            function_parameter_types[i].category != TypeCategory::StaticArray ||
                            !types_equal(*value.type.static_array.type, *function_parameter_types[i].static_array.type) ||
                            value.type.static_array.length != function_parameter_types[i].static_array.length
                        ) {
                            error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d. Expected %s, got %s", i, type_description(function_parameter_types[i]), type_description(value.type));

                            return { false };
                        }

                        switch(value.category) {
                            case ExpressionValueCategory::Constant: {
                                auto constant_name = register_static_array_constant(
                                    context,
                                    *value.type.static_array.type,
                                    Array<ConstantValue> {
                                        value.type.static_array.length,
                                        value.constant.static_array
                                    }
                                );

                                auto constant_address_register = allocate_register(context);

                                Instruction reference;
                                reference.type = InstructionType::ReferenceStatic;
                                reference.reference_static.name = constant_name;
                                reference.reference_static.destination_register = constant_address_register;

                                append(instructions, reference);

                                function_parameter_registers[i] = constant_address_register;
                            } break;

                            case ExpressionValueCategory::Register: {
                                function_parameter_registers[i] = value.register_;
                            } break;

                            case ExpressionValueCategory::Address: {
                                function_parameter_registers[i] = value.address;
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
            }

            bool has_return;
            size_t return_register;

            Instruction call;
            call.type = InstructionType::FunctionCall;
            call.function_call.function_name = function_name;
            call.function_call.parameter_registers = {
                parameter_count,
                function_parameter_registers
            };

            switch(function_return_type.category) {
                case TypeCategory::Integer:
                case TypeCategory::Boolean:
                case TypeCategory::Pointer: {
                    has_return = true;
                    return_register = allocate_register(context);

                    call.function_call.has_return = true;
                    call.function_call.return_register = return_register;

                    append(instructions, call);
                } break;

                case TypeCategory::Void: {
                    has_return = false;

                    call.function_call.has_return = false;

                    append(instructions, call);
                } break;

                case TypeCategory::Array: {
                    has_return = true;
                    return_register = allocate_register(context);

                    Instruction alloc;
                    alloc.type = InstructionType::AllocateLocal;
                    alloc.allocate_local.size = register_size_to_byte_size(context->address_integer_size) * 2;
                    alloc.allocate_local.destination_register = return_register;

                    append(instructions, alloc);

                    auto address_return_register = allocate_register(context);

                    call.function_call.has_return = true;
                    call.function_call.return_register = address_return_register;

                    append(instructions, call);

                    generate_array_copy(context, instructions, address_return_register, return_register);
                } break;

                case TypeCategory::StaticArray: {
                    has_return = true;
                    return_register = allocate_register(context);

                    auto length = function_return_type.static_array.length * get_type_size(*context, *function_return_type.static_array.type);

                    Instruction alloc;
                    alloc.type = InstructionType::AllocateLocal;
                    alloc.allocate_local.size = length;
                    alloc.allocate_local.destination_register = return_register;

                    append(instructions, alloc);

                    auto address_return_register = allocate_register(context);

                    call.function_call.has_return = true;
                    call.function_call.return_register = address_return_register;

                    append(instructions, call);

                    auto length_register = allocate_register(context);

                    Instruction constant;
                    constant.type = InstructionType::Constant;
                    constant.constant.size = context->address_integer_size;
                    constant.constant.destination_register = length_register;
                    constant.constant.value = length;

                    append(instructions, constant);

                    Instruction copy;
                    copy.type = InstructionType::CopyMemory;
                    copy.copy_memory.length_register = length_register;
                    copy.copy_memory.source_address_register = address_return_register;
                    copy.copy_memory.destination_address_register = return_register;

                    append(instructions, copy);
                } break;

                default: {
                    abort();
                } break;
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Register;
            value.type = function_return_type;
            if(has_return) {
                value.register_ = return_register;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, generate_expression(context, instructions, *expression.binary_operation.left));

            expect(right, generate_expression(context, instructions, *expression.binary_operation.right));

            if(left.category == ExpressionValueCategory::Constant && right.category == ExpressionValueCategory::Constant) {
                expect(constant, evaluate_constant_binary_operation(
                    *context,
                    expression.binary_operation.binary_operator,
                    expression.range,
                    left.type,
                    left.constant,
                    right.type,
                    right.constant
                ));

                ExpressionValue value;
                value.category = ExpressionValueCategory::Constant;
                value.type = constant.type;
                value.constant = constant.value;

                return {
                    true,
                    value
                };
            } else {
                auto result_register = allocate_register(context);

                Instruction operation;
                operation.type = InstructionType::BinaryOperation;
                operation.binary_operation.destination_register = result_register;

                Type result_type;
                switch(left.type.category) {
                    case TypeCategory::Integer: {
                        if(right.type.category != TypeCategory::Integer) {
                            error(expression.range, "Mismatched types %s and %s", type_description(left.type), type_description(right.type));

                            return { false };
                        }

                        Type actual_type;

                        if(left.type.integer.is_undetermined && right.type.integer.is_undetermined) {
                            actual_type.category = TypeCategory::Integer;
                            actual_type.integer.size = context->default_integer_size;
                            actual_type.integer.is_signed = true;
                            actual_type.integer.is_undetermined = false;
                        } else if(left.type.integer.is_undetermined) {
                            actual_type = right.type;
                        } else if(right.type.integer.is_undetermined) {
                            actual_type = left.type;
                        } else if (left.type.integer.size != right.type.integer.size || left.type.integer.is_signed != right.type.integer.is_signed) {
                            error(expression.range, "Mismatched types %s and %s", type_description(left.type), type_description(right.type));

                            return { false };
                        }

                        operation.binary_operation.size = actual_type.integer.size;
                        operation.binary_operation.source_register_a = generate_integer_register_value(context, instructions, actual_type.integer.size, left);
                        operation.binary_operation.source_register_b = generate_integer_register_value(context, instructions, actual_type.integer.size, right);

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Addition: {
                                operation.binary_operation.type = BinaryOperationType::Add;

                                result_type = actual_type;
                            } break;

                            case BinaryOperator::Subtraction: {
                                operation.binary_operation.type = BinaryOperationType::Subtract;

                                result_type = actual_type;
                            } break;

                            case BinaryOperator::Multiplication: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedMultiply;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedMultiply;
                                }

                                result_type = actual_type;
                            } break;

                            case BinaryOperator::Division: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedDivide;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedDivide;
                                }

                                result_type = actual_type;
                            } break;

                            case BinaryOperator::Modulo: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedModulus;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedModulus;
                                }

                                result_type = actual_type;
                            } break;

                            case BinaryOperator::Equal: {
                                operation.binary_operation.type = BinaryOperationType::Equality;

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            default: {
                                error(expression.range, "Cannot perform that operation on integers");

                                return { false };
                            } break;
                        }
                    } break;

                    case TypeCategory::Boolean: {
                        if(right.type.category != TypeCategory::Boolean) {
                            error(expression.range, "Mismatched types %s and %s", type_description(left.type), type_description(right.type));

                            return { false };
                        }

                        result_type.category = TypeCategory::Boolean;

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Equal: {
                                operation.binary_operation.type = BinaryOperationType::Equality;

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            default: {
                                error(expression.range, "Cannot perform that operation on booleans");

                                return { false };
                            } break;
                        }
                    } break;

                    default: {
                        error(expression.range, "Cannot perform binary operations on %s", type_description(left.type));

                        return { false };
                    } break;
                }

                append(instructions, operation);

                ExpressionValue value;
                value.category = ExpressionValueCategory::Register;
                value.type = result_type;
                value.register_ = result_register;

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
                    switch(expression_value.category) {
                        case ExpressionValueCategory::Constant: {
                            switch(expression_value.type.category) {
                                case TypeCategory::Type: {
                                    ExpressionValue value;
                                    value.category = ExpressionValueCategory::Constant;
                                    value.type.category = TypeCategory::Type;
                                    value.constant.type.category = TypeCategory::Pointer;
                                    value.constant.type.pointer = heapify(expression_value.constant.type);

                                    return {
                                        true,
                                        value
                                    };
                                } break;

                                case TypeCategory::Function:
                                case TypeCategory::Integer:
                                case TypeCategory::Boolean:
                                case TypeCategory::Void:
                                case TypeCategory::Pointer:
                                case TypeCategory::FileModule: {
                                    error(expression.unary_operation.expression->range, "Cannot take pointers to constants of type %s", type_description(expression_value.type));

                                    return { false };
                                } break;

                                default: {
                                    abort();
                                } break;
                            }
                        } break;

                        case ExpressionValueCategory::Register: {
                            error(expression.unary_operation.expression->range, "Cannot take pointers to anonymous values");

                            return { false };
                        } break;

                        case ExpressionValueCategory::Address: {
                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Register;
                            value.type.category = TypeCategory::Pointer;
                            value.type.pointer = heapify(expression_value.type);
                            value.register_ = expression_value.register_;

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

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::Cast: {
            expect(expression_value, generate_expression(context, instructions, *expression.cast.expression));

            expect(type, evaluate_type_expression(context, *expression.cast.type));

            switch(expression_value.category) {
                case ExpressionValueCategory::Constant: {
                    expect(constant, evaluate_constant_conversion(
                        *context,
                        expression_value.constant,
                        expression_value.type,
                        expression.cast.expression->range,
                        type,
                        expression.cast.type->range
                    ));

                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Constant;
                    value.type = type;
                    value.constant = constant;

                    return {
                        true,
                        value
                    };
                } break;

                case ExpressionValueCategory::Register:
                case ExpressionValueCategory::Address: {
                    size_t result_register_index;
                    switch(expression_value.type.category) {
                        case TypeCategory::Integer: {
                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    if(expression_value.type.integer.is_undetermined) {
                                        result_register_index = generate_integer_register_value(context, instructions, type.integer.size, expression_value);
                                    } else if(type.integer.size > expression_value.type.integer.size) {
                                        auto register_index = generate_integer_register_value(context, instructions, expression_value);

                                        result_register_index = allocate_register(context);

                                        Instruction integer_upcast;
                                        integer_upcast.type = InstructionType::IntegerUpcast;
                                        integer_upcast.integer_upcast.is_signed = expression_value.type.integer.is_signed;
                                        integer_upcast.integer_upcast.source_size = expression_value.type.integer.size;
                                        integer_upcast.integer_upcast.source_register = register_index;
                                        integer_upcast.integer_upcast.destination_size = type.integer.size;
                                        integer_upcast.integer_upcast.destination_register = result_register_index;

                                        append(instructions, integer_upcast);
                                    } else {
                                        result_register_index = generate_integer_register_value(context, instructions, expression_value);
                                    }
                                } break;

                                case TypeCategory::Pointer: {
                                    if(expression_value.type.integer.is_undetermined) {
                                        result_register_index = generate_integer_register_value(context, instructions, context->address_integer_size, expression_value);
                                    } else {
                                        auto register_index = generate_integer_register_value(context, instructions, expression_value);

                                        if(expression_value.type.integer.size != context->address_integer_size) {
                                            error(expression.cast.expression->range, "Cannot cast from %s to pointer", type_description(expression_value.type));

                                            return { false };
                                        }

                                        result_register_index = register_index;
                                    }
                                } break;

                                default: {
                                    error(expression.cast.type->range, "Cannot cast from integer to %s", type_description(type));

                                    return { false };
                                } break;
                            }
                        } break;

                        case TypeCategory::Pointer: {
                            auto register_index = generate_pointer_register_value(context, instructions, expression_value);

                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    if(type.integer.size != context->address_integer_size) {
                                        error(expression.cast.expression->range, "Cannot cast from pointer to %s", type_description(type));

                                        return { false };
                                    }

                                    result_register_index = register_index;
                                } break;

                                case TypeCategory::Pointer: {
                                    result_register_index = register_index;
                                } break;

                                default: {
                                    error(expression.cast.type->range, "Cannot cast from pointer to %s", type_description(type));

                                    return { false };
                                } break;
                            }
                        } break;

                        default: {
                            error(expression.cast.expression->range, "Cannot cast from %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }

                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Register;
                    value.type = type;
                    value.register_ = result_register_index;

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

        case ExpressionType::FunctionType: {
            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                auto parameter = expression.function_type.parameters[i];

                if(parameter.is_polymorphic_determiner) {
                    error(parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

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

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.constant.type.category = TypeCategory::Function;
            value.constant.type.function = {
                false,
                {
                    expression.function_type.parameters.count,
                    parameters
                },
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

                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.allocate_local.size = get_type_size(*context, type);
                    allocate.allocate_local.destination_register = address_register;

                    append(instructions, allocate);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, type, statement.variable_declaration.uninitialized.range)) {
                        return false;
                    }

                    return true;
                } break;

                case VariableDeclarationType::TypeElided: {
                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.allocate_local.destination_register = address_register;

                    auto allocate_index = append(instructions, allocate);

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.type_elided));

                    (*instructions)[allocate_index].allocate_local.size = get_type_size(*context, initializer_value.type);

                    generate_variable_assignment(context, instructions, address_register, initializer_value);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, initializer_value.type, statement.variable_declaration.type_elided.range)) {
                        return false;
                    }

                    return true;
                } break;


                case VariableDeclarationType::FullySpecified: {
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.fully_specified.type));

                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.allocate_local.size = get_type_size(*context, type);
                    allocate.allocate_local.destination_register = address_register;

                    append(instructions, allocate);

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.fully_specified.initializer));

                    if(!types_equal(type, initializer_value.type)) {
                        error(statement.assignment.value.range, "Incorrect assignment type. Expected %s, got %s", type_description(type), type_description(initializer_value.type));

                        return false;
                    }

                    generate_variable_assignment(context, instructions, address_register, initializer_value);

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

            if(target.category != ExpressionValueCategory::Address) {
                error(statement.assignment.target.range, "Value is not assignable");

                return false;
            }

            expect(value, generate_expression(context, instructions, statement.assignment.value));

            if(!types_equal(target.type, value.type)) {
                error(statement.assignment.value.range, "Incorrect assignment type. Expected %s, got %s", type_description(target.type), type_description(value.type));

                return false;
            }

            generate_variable_assignment(context, instructions, target.address, value);

            return true;
        } break;

        case StatementType::LoneIf: {
            expect(condition, generate_expression(context, instructions, statement.lone_if.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.lone_if.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            Instruction branch;
            branch.type = InstructionType::Branch;
            branch.branch.condition_register = generate_boolean_register_value(context, instructions, condition);
            branch.branch.destination_instruction = instructions->count + 2;

            append(instructions, branch);

            Instruction jump;
            jump.type = InstructionType::Jump;

            auto jump_index = append(instructions, jump);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.lone_if.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            (*instructions)[jump_index].jump.destination_instruction = instructions->count;

            return true;
        } break;

        case StatementType::WhileLoop: {
            expect(condition, generate_expression(context, instructions, statement.while_loop.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.while_loop.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_boolean_register_value(context, instructions, condition);

            Instruction branch;
            branch.type = InstructionType::Branch;
            branch.branch.condition_register = condition_register;
            branch.branch.destination_instruction = instructions->count + 2;

            auto branch_index = append(instructions, branch);

            Instruction jump_out;
            jump_out.type = InstructionType::Jump;

            auto jump_out_index = append(instructions, jump_out);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.while_loop.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            Instruction jump_loop;
            jump_loop.type = InstructionType::Jump;
            jump_loop.jump.destination_instruction = branch_index;

            append(instructions, jump_loop);

            (*instructions)[jump_out_index].jump.destination_instruction = instructions->count;

            return true;
        } break;

        case StatementType::Return: {
            expect(value, generate_expression(context, instructions, statement._return));

            Instruction return_;
            return_.type = InstructionType::Return;

            switch(value.type.category) {
                case TypeCategory::Integer: {
                    if(context->return_type.category != TypeCategory::Integer) {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }

                    if(value.type.integer.is_undetermined) {
                        return_.return_.value_register = generate_integer_register_value(context, instructions, context->return_type.integer.size, value);
                    } else if(value.type.integer.size == context->return_type.integer.size && value.type.integer.is_signed == context->return_type.integer.is_signed) {
                        return_.return_.value_register = generate_integer_register_value(context, instructions, value);
                    } else {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }
                } break;

                case TypeCategory::Boolean: {
                    if(context->return_type.category != TypeCategory::Boolean) {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }

                    return_.return_.value_register = generate_boolean_register_value(context, instructions, value);
                } break;

                case TypeCategory::Pointer: {
                    if(context->return_type.category != TypeCategory::Pointer || !types_equal(*value.type.pointer, *context->return_type.pointer)) {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }

                    return_.return_.value_register = generate_pointer_register_value(context, instructions, value);
                } break;

                case TypeCategory::Array: {
                    if(context->return_type.category != TypeCategory::Array || !types_equal(*value.type.array, *context->return_type.array)) {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }

                    switch(value.category) {
                        case ExpressionValueCategory::Constant: {
                            auto local_register = allocate_register(context);

                            Instruction alloc;
                            alloc.type = InstructionType::AllocateLocal;
                            alloc.allocate_local.size = register_size_to_byte_size(context->address_integer_size);
                            alloc.allocate_local.destination_register = local_register;

                            append(instructions, alloc);

                            auto pointer_register = allocate_register(context);

                            Instruction pointer_constant;
                            pointer_constant.type = InstructionType::Constant;
                            pointer_constant.constant.size = context->address_integer_size;
                            pointer_constant.constant.destination_register = pointer_register;
                            pointer_constant.constant.value = value.constant.array.pointer;

                            append(instructions, pointer_constant);

                            Instruction store_pointer;
                            store_pointer.type = InstructionType::StoreInteger;
                            store_pointer.store_integer.size = context->address_integer_size;
                            store_pointer.store_integer.source_register = pointer_register;
                            store_pointer.store_integer.address_register = local_register;

                            append(instructions, store_pointer);

                            auto offset_register = allocate_register(context);

                            Instruction size_constant;
                            size_constant.type = InstructionType::Constant;
                            size_constant.constant.size = context->address_integer_size;
                            size_constant.constant.destination_register = offset_register;
                            size_constant.constant.value = register_size_to_byte_size(context->address_integer_size);

                            append(instructions, size_constant);

                            auto length_register = allocate_register(context);

                            Instruction length_constant;
                            length_constant.type = InstructionType::Constant;
                            length_constant.constant.size = context->address_integer_size;
                            length_constant.constant.destination_register = pointer_register;
                            length_constant.constant.value = value.constant.array.length;

                            append(instructions, length_constant);

                            auto length_address_register = allocate_register(context);

                            Instruction add;
                            add.type = InstructionType::BinaryOperation;
                            add.binary_operation.type = BinaryOperationType::Add;
                            add.binary_operation.size = context->address_integer_size;
                            add.binary_operation.source_register_a = local_register;
                            add.binary_operation.source_register_b = offset_register;
                            add.binary_operation.destination_register = length_address_register;

                            append(instructions, add);

                            Instruction store_length;
                            store_length.type = InstructionType::StoreInteger;
                            store_length.store_integer.size = context->address_integer_size;
                            store_length.store_integer.source_register = length_register;
                            store_length.store_integer.address_register = length_address_register;

                            append(instructions, store_length);

                            return_.return_.value_register = local_register;
                        } break;

                        case ExpressionValueCategory::Register: {
                            return_.return_.value_register = value.register_;
                        } break;

                        case ExpressionValueCategory::Address: {
                            return_.return_.value_register = value.address;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } break;

                case TypeCategory::StaticArray: {
                    if(
                        context->return_type.category != TypeCategory::StaticArray ||
                        !types_equal(*value.type.static_array.type, *context->return_type.static_array.type) ||
                        value.type.static_array.length != context->return_type.static_array.length
                    ) {
                        error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                        return { false };
                    }

                    switch(value.category) {
                        case ExpressionValueCategory::Constant: {
                            auto constant_name = register_static_array_constant(
                                context,
                                *value.type.static_array.type,
                                Array<ConstantValue> {
                                    value.type.static_array.length,
                                    value.constant.static_array
                                }
                            );

                            auto constant_address_register = allocate_register(context);

                            Instruction reference;
                            reference.type = InstructionType::ReferenceStatic;
                            reference.reference_static.name = constant_name;
                            reference.reference_static.destination_register = constant_address_register;

                            append(instructions, reference);

                            return_.return_.value_register = constant_address_register;
                        } break;

                        case ExpressionValueCategory::Register: {
                            return_.return_.value_register = value.register_;
                        } break;

                        case ExpressionValueCategory::Address: {
                            return_.return_.value_register = value.address;
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

Result<IR> generate_ir(Array<File> files, ArchitectureInfo architecute_info) {
    assert(files.count > 0);

    List<GlobalConstant> global_constants{};

    append(&global_constants, create_base_integer_type("u8", RegisterSize::Size8, false));
    append(&global_constants, create_base_integer_type("u16", RegisterSize::Size16, false));
    append(&global_constants, create_base_integer_type("u32", RegisterSize::Size32, false));
    append(&global_constants, create_base_integer_type("u64", RegisterSize::Size64, false));

    append(&global_constants, create_base_integer_type("i8", RegisterSize::Size8, true));
    append(&global_constants, create_base_integer_type("i16", RegisterSize::Size16, true));
    append(&global_constants, create_base_integer_type("i32", RegisterSize::Size32, true));
    append(&global_constants, create_base_integer_type("i64", RegisterSize::Size64, true));

    append(&global_constants, create_base_integer_type("usize", architecute_info.address_size, false));
    append(&global_constants, create_base_integer_type("isize", architecute_info.address_size, true));

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

    auto previous_resolved_declaration_count = 0;

    GenerationContext context {
        architecute_info.address_size,
        architecute_info.default_size,
        to_array(global_constants),
        files
    };

    auto main_found = false;
    for(auto statement : files[0].statements) {
        if(match_declaration(statement, "main")) {
            if(statement.type != StatementType::FunctionDeclaration) {
                error(statement.range, "'main' must be a function");

                return { false };
            }

            if(statement.function_declaration.is_external) {
                error(statement.range, "'main' must not be external");

                return { false };
            }

            context.is_top_level = true;
            context.top_level_statements = files[0].statements;

            expect(value, resolve_declaration(&context, statement));

            if(value.type.function.is_polymorphic) {
                error(statement.range, "'main' cannot be polymorphic");

                return { false };
            }

            auto runtimeParameters = allocate<RuntimeFunctionParameter>(statement.function_declaration.parameters.count);

            for(size_t i = 0; i < statement.function_declaration.parameters.count; i += 1) {
                runtimeParameters[i] = {
                    statement.function_declaration.parameters[i].name,
                    value.type.function.parameters[i]
                };
            }

            auto mangled_name = generate_mangled_name(context, statement);

            append(&context.runtime_functions, {
                mangled_name,
                {
                    statement.function_declaration.parameters.count,
                    runtimeParameters
                },
                *value.type.function.return_type,
                statement
            });

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

        for(auto function : context.runtime_functions) {
            auto generated = false;

            for(auto generated_function : functions) {
                if(strcmp(generated_function.name, function.mangled_name) == 0) {
                    generated = true;

                    break;
                }
            }

            if(!generated) {
                if(function.declaration.is_top_level) {
                    context.is_top_level = true;
                    context.top_level_statements = function.declaration.file->statements;
                } else {
                    context.is_top_level = false;
                    context.determined_declaration = function.parent;
                }

                append(&context.variable_context_stack, List<Variable>{});

                auto parameters = allocate<Variable>(function.parameters.count);
                auto parameter_sizes = allocate<RegisterSize>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    parameters[i] = {
                        parameter.name,
                        parameter.type,
                        parameter.type_range,
                        i
                    };

                    auto size = get_type_size(context, parameter.type);

                    switch(function.return_type.category) {
                        case TypeCategory::Integer: {
                            parameter_sizes[i] = function.return_type.integer.size;
                        } break;

                        case TypeCategory::Boolean: {
                            parameter_sizes[i] = architecute_info.default_size;
                        } break;

                        case TypeCategory::Pointer: {
                            parameter_sizes[i] = architecute_info.address_size;
                        } break;

                        case TypeCategory::Array: {
                            parameter_sizes[i] = architecute_info.address_size;
                        } break;

                        case TypeCategory::StaticArray: {
                            parameter_sizes[i] = architecute_info.address_size;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
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
                context.next_register = function.parameters.count;

                List<Instruction> instructions{};

                for(auto statement : function.declaration.function_declaration.statements) {
                    switch(statement.type) {
                        case StatementType::Expression:
                        case StatementType::VariableDeclaration:
                        case StatementType::Assignment:
                        case StatementType::LoneIf:
                        case StatementType::WhileLoop:
                        case StatementType::Return: {
                            if(!generate_statement(&context, &instructions, statement)) {
                                return { false };
                            }
                        } break;

                        case StatementType::Library:
                        case StatementType::Import: {
                            error(statement.range, "Compiler directives only allowed in global scope");

                            return { false };
                        } break;
                    }
                }

                context.variable_context_stack.count -= 1;
                context.next_register = 0;

                Function ir_function;
                ir_function.name = function.mangled_name;
                ir_function.parameter_sizes = {
                    function.parameters.count,
                    parameter_sizes
                };
                ir_function.instructions = to_array(instructions);

                if(function.return_type.category != TypeCategory::Void) {
                    ir_function.has_return = true;

                    switch(function.return_type.category) {
                        case TypeCategory::Integer: {
                            ir_function.return_size = function.return_type.integer.size;
                        } break;

                        case TypeCategory::Boolean: {
                            ir_function.return_size = architecute_info.default_size;
                        } break;

                        case TypeCategory::Pointer: {
                            ir_function.return_size = architecute_info.address_size;
                        } break;

                        case TypeCategory::Array: {
                            ir_function.return_size = architecute_info.address_size;
                        } break;

                        case TypeCategory::StaticArray: {
                            ir_function.return_size = architecute_info.address_size;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } else {
                    ir_function.has_return = false;
                }

                append(&functions, ir_function);

                done = false;
            }
        }

        if(done) {
            break;
        }
    }

    for(size_t i = 0; i < files.count; i += 1) {
        for(auto statement : files[i].statements) {
            switch(statement.type) {
                case StatementType::Library: {
                    auto is_added = false;

                    for(auto library : context.libraries) {
                        if(strcmp(library, statement.library) == 0) {
                            is_added = true;

                            break;
                        }
                    }

                    if(!is_added) {
                        append(&context.libraries, statement.library);
                    }
                } break;
            }
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