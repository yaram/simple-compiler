#include "ir_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "list.h"
#include "types.h"
#include "util.h"
#include "path.h"

const FileRange generated_range {
    "<generated>",
    0,
    0
};

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

    size_t address_register;
};

struct RuntimeFunctionParameter {
    Identifier name;

    Type type;
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
    IntegerSize address_integer_size;
    IntegerSize default_integer_size;

    Array<GlobalConstant> global_constants;

    Array<File> file_modules;

    bool is_top_level;

    union {
        DeterminedDeclaration determined_declaration;

        Array<Statement> top_level_statements;
    };

    Array<PolymorphicDeterminer> polymorphic_determiners;

    Type return_type;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<const char*> libraries;
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

static RegisterSize integer_size_to_register_size(IntegerSize size) {
    switch(size) {
        case IntegerSize::Size8: {
            return RegisterSize::Size8;
        } break;

        case IntegerSize::Size16: {
            return RegisterSize::Size16;
        } break;

        case IntegerSize::Size32: {
            return RegisterSize::Size32;
        } break;

        case IntegerSize::Size64: {
            return RegisterSize::Size64;
        } break;

        default: {
            abort();
        } break;
    }
}

static RegisterSize get_type_register_size(GenerationContext context, Type type) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return integer_size_to_register_size(type.integer.size);
        } break;

        case TypeCategory::Pointer: {
            return integer_size_to_register_size(context.address_integer_size);
        } break;

        case TypeCategory::Boolean: {
            return integer_size_to_register_size(context.default_integer_size);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t get_type_size(GenerationContext context, Type type) {
    auto register_size = get_type_register_size(context, type);

    switch(register_size) {
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

static Result<TypedConstantValue> evaluate_constant_binary_operation(BinaryOperator binary_operator, FileRange range, Type left_type, ConstantValue left_value, Type right_type, ConstantValue right_value) {
    if(!types_equal(left_type, right_type)) {
        error(range, "Mismatched types %s and %s", type_description(left_type), type_description(right_type));

        return { false };
    }

    TypedConstantValue result;

    switch(left_type.category) {
        case TypeCategory::Integer: {
            uint64_t left;
            uint64_t right;
            if(left_type.integer.is_signed) {
                switch(left_type.integer.size) {
                    case IntegerSize::Size8: {
                        left = (int64_t)(int8_t)left_value.integer;
                        right = (int64_t)(int8_t)right_value.integer;
                    } break;

                    case IntegerSize::Size16: {
                        left = (int64_t)(int16_t)left_value.integer;
                        right = (int64_t)(int16_t)right_value.integer;
                    } break;

                    case IntegerSize::Size32: {
                        left = (int64_t)(int32_t)left_value.integer;
                        right = (int64_t)(int32_t)right_value.integer;
                    } break;

                    case IntegerSize::Size64: {
                        left = left_value.integer;
                        right = right_value.integer;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(left_type.integer.size) {
                    case IntegerSize::Size8: {
                        left = (uint8_t)left_value.integer;
                        right = (uint8_t)right_value.integer;
                    } break;

                    case IntegerSize::Size16: {
                        left = (uint16_t)left_value.integer;
                        right = (uint16_t)right_value.integer;
                    } break;

                    case IntegerSize::Size32: {
                        left = (uint32_t)left_value.integer;
                        right = (uint32_t)right_value.integer;
                    } break;

                    case IntegerSize::Size64: {
                        left = left_value.integer;
                        right = right_value.integer;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            uint64_t result_value;

            switch(binary_operator) {
                case BinaryOperator::Addition: {
                    result_value = left + right;

                    result.type = left_type;
                } break;

                case BinaryOperator::Subtraction: {
                    result_value = left - right;

                    result.type = left_type;
                } break;

                case BinaryOperator::Equal: {
                    result.value.boolean = left == right;

                    result.type.category = TypeCategory::Boolean;
                } break;

                case BinaryOperator::Multiplication: {
                    if(left_type.integer.is_signed) {
                        result_value = (int64_t)left * (int64_t)right;
                    } else {
                        result_value = left * right;
                    }

                    result.type = left_type;
                } break;

                case BinaryOperator::Division: {
                    if(left_type.integer.is_signed) {
                        result_value = (int64_t)left / (int64_t)right;
                    } else {
                        result_value = left / right;
                    }

                    result.type = left_type;
                } break;

                case BinaryOperator::Modulo: {
                    if(left_type.integer.is_signed) {
                        result_value = (int64_t)left % (int64_t)right;
                    } else {
                        result_value = left % right;
                    }

                    result.type = left_type;
                } break;

                default: {
                    abort();
                } break;
            }

            if(result.type.category == TypeCategory::Integer) {
                if(left_type.integer.is_signed) {
                    switch(left_type.integer.size) {
                        case IntegerSize::Size8: {
                            result.value.integer = (int64_t)(int8_t)result_value;
                        } break;

                        case IntegerSize::Size16: {
                            result.value.integer = (int64_t)(int16_t)result_value;
                        } break;

                        case IntegerSize::Size32: {
                            result.value.integer = (int64_t)(int32_t)result_value;
                        } break;

                        case IntegerSize::Size64: {
                            result.value.integer = result_value;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } else {
                    switch(left_type.integer.size) {
                        case IntegerSize::Size8: {
                            result.value.integer = (uint8_t)result_value;
                        } break;

                        case IntegerSize::Size16: {
                            result.value.integer = (uint16_t)result_value;
                        } break;

                        case IntegerSize::Size32: {
                            result.value.integer = (uint32_t)result_value;
                        } break;

                        case IntegerSize::Size64: {
                            result.value.integer = result_value;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }
            }
        } break;

        case TypeCategory::Boolean: {
            result.type.category = TypeCategory::Boolean;

            switch(binary_operator) {
                case BinaryOperator::Equal: {
                    result.value.boolean = left_value.boolean == right_value.boolean;
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

    return {
        true,
        result
    };
}

static Result<ConstantValue> evaluate_constant_conversion(GenerationContext context, ConstantValue value, Type value_type, FileRange value_range, Type type, FileRange type_range) {
    ConstantValue result;

    switch(value_type.category) {
        case TypeCategory::Integer: {
            switch(type.category) {
                case TypeCategory::Integer: {
                    if(value_type.integer.is_signed && type.integer.is_signed) {
                        switch(value_type.integer.size) {
                            case IntegerSize::Size8: {
                                result.integer = (int8_t)value.integer;
                            } break;

                            case IntegerSize::Size16: {
                                result.integer = (int16_t)value.integer;
                            } break;

                            case IntegerSize::Size32: {
                                result.integer = (int32_t)value.integer;
                            } break;

                            case IntegerSize::Size64: {
                                result.integer = value.integer;
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    } else {
                        switch(value_type.integer.size) {
                            case IntegerSize::Size8: {
                                result.integer = (uint8_t)value.integer;
                            } break;

                            case IntegerSize::Size16: {
                                result.integer = (uint16_t)value.integer;
                            } break;

                            case IntegerSize::Size32: {
                                result.integer = (uint32_t)value.integer;
                            } break;

                            case IntegerSize::Size64: {
                                result.integer = value.integer;
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(value.type.integer.size == context.address_integer_size) {
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

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer = {
                context->default_integer_size,
                true
            };

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

        case ExpressionType::FunctionCall: {
            error(expression.range, "Function calls not allowed in global context");

            return { false };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, evaluate_constant_expression(context, *expression.binary_operation.left));

            expect(right, evaluate_constant_expression(context, *expression.binary_operation.right));

            expect(value, evaluate_constant_binary_operation(
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

static bool add_new_variable(GenerationContext *context, Identifier name, size_t address_register, Type type) {
    auto variable_context = &(context->variable_context_stack[context->variable_context_stack.count - 1]);

    for(auto variable : *variable_context) {
        if(strcmp(variable.name.text, name.text) == 0) {
            error(name.range, "Duplicate variable name %s", name.text);
            error(variable.name.range, "Original declared here");

            return false;
        }
    }

    append(variable_context, Variable{
        name,
        type
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

static size_t generate_register_value(GenerationContext *context, List<Instruction> *instructions, ExpressionValue value) {
    size_t register_index;

    switch(value.category) {
        case ExpressionValueCategory::Constant: {
            register_index = allocate_register(context);

            Instruction constant;
            constant.type = InstructionType::Constant;
            constant.constant.size = get_type_register_size(*context, value.type);
            constant.constant.destination_register = register_index;

            switch(value.type.category) {
                case TypeCategory::Integer: {
                    constant.constant.value = value.constant.integer;
                } break;

                case TypeCategory::Boolean: {
                    if(value.constant.boolean) {
                        constant.constant.value = 1;
                    } else {
                        constant.constant.value = 0;
                    }
                } break;

                case TypeCategory::Pointer: {
                    constant.constant.value = value.constant.pointer;
                } break;

                default: {
                    abort();
                } break;
            }

            append(instructions, constant);
        } break;

        case ExpressionValueCategory::Register: {
            register_index = value.register_;
        } break;

        case ExpressionValueCategory::Address: {
            register_index = allocate_register(context);

            Instruction load;
            load.type = InstructionType::LoadInteger;
            load.load_integer.size = get_type_register_size(*context, value.type);
            load.load_integer.address_register = value.address;
            load.load_integer.destination_register = register_index;

            append(instructions, load);
        } break;

        default: {
            abort();
        } break;
    }

    return register_index;
}

struct RegisterExpressionValue {
    Type type;

    size_t register_index;
};

static Result<ExpressionValue> generate_expression(GenerationContext *context, List<Instruction> *instructions, Expression expression);

static Result<RegisterExpressionValue> generate_register_expression(GenerationContext *context, List<Instruction> *instructions, Expression expression) {
    expect(value, generate_expression(context, instructions, expression));

    auto register_index = generate_register_value(context, instructions, value);

    return {
        true,
        {
            value.type,
            register_index
        }
    };
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
                        value.address = variable.address_register;

                        return {
                            true,
                            value
                        };
                    }
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

        case ExpressionType::MemberReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.member_reference.expression));

            switch(expression_value.type.category) {
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
            value.type.integer = {
                context->default_integer_size,
                true
            };
            value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            expect(expression_value, generate_expression(context, instructions, *expression.function_call.expression));

            if(expression_value.type.category != TypeCategory::Function) {
                error(expression.function_call.expression->range, "Cannot call %s", type_description(expression_value.type));

                return { false };
            }

            auto function_declaration = expression_value.constant.function.declaration.function_declaration;

            auto function_parameter_registers = allocate<size_t>(expression.function_call.parameters.count);

            const char *function_name;
            Type *function_parameters;
            Type function_return_type;

            if(expression_value.type.function.is_polymorphic) {
                if(expression.function_call.parameters.count != function_declaration.parameters.count) {
                    error(expression.range, "Incorrect number of parameters. Expected %zu, got %zu", function_declaration.parameters.count, expression.function_call.parameters.count);

                    return { false };
                }

                List<PolymorphicDeterminer> polymorphic_determiners{};

                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    auto parameter = function_declaration.parameters[i];

                    if(parameter.is_polymorphic_determiner) {
                        for(auto polymorphic_determiner : polymorphic_determiners) {
                            if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                error(parameter.polymorphic_determiner.range, "Duplicate polymorphic parameter %s", parameter.polymorphic_determiner.text);

                                return { false };
                            }
                        }

                        expect(value, generate_register_expression(context, instructions, expression.function_call.parameters[i]));

                        append(&polymorphic_determiners, {
                            parameter.polymorphic_determiner.text,
                            value.type
                        });

                        function_parameter_registers[i] = value.register_index;
                    }
                }

                function_parameters = allocate<Type>(expression.function_call.parameters.count);

                context->polymorphic_determiners = to_array(polymorphic_determiners);

                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    auto parameter = function_declaration.parameters[i];

                    if(parameter.is_polymorphic_determiner) {
                        for(auto polymorphic_determiner : polymorphic_determiners) {
                            if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                function_parameters[i] = polymorphic_determiner.type;
                            }

                            break;
                        }
                    } else {
                        expect(type, evaluate_type_expression(context, parameter.type));

                        function_parameters[i] = type;

                        expect(value, generate_register_expression(context, instructions, expression.function_call.parameters[i]));

                        function_parameter_registers[i] = value.register_index;
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
                        function_parameters[i]
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
                if(expression.function_call.parameters.count != expression_value.type.function.parameters.count) {
                    error(expression.range, "Incorrect number of parameters. Expected %zu, got %zu", expression_value.type.function.parameters.count, expression.function_call.parameters.count);

                    return { false };
                }

                for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                    char *parameter_source{};
                    expect(value, generate_register_expression(context, instructions, expression.function_call.parameters[i]));

                    function_parameter_registers[i] = value.register_index;
                }

                function_name = generate_mangled_name(*context, expression_value.constant.function.declaration);
                function_parameters = expression_value.type.function.parameters.elements;
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
                            function_parameters[i]
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

            size_t return_register;
            if(function_return_type.category == TypeCategory::Void) {
                return_register = allocate_register(context);
            }

            Instruction call;
            call.type = InstructionType::FunctionCall;
            call.function_call.function_name = function_name;
            call.function_call.parameter_registers = {
                expression.function_call.parameters.count,
                function_parameter_registers
            };
            if(function_return_type.category == TypeCategory::Void) {
                call.function_call.has_return = true;
                call.function_call.return_register = return_register;
            } else {
                call.function_call.has_return = false;
            }

            append(instructions, call);

            ExpressionValue value;
            value.category = ExpressionValueCategory::Register;
            value.type.category = function_return_type.category;
            if(function_return_type.category != TypeCategory::Void) {
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
                if(!types_equal(left.type, right.type)) {
                    error(expression.range, "Mismatched types %s and %s", type_description(left.type), type_description(right.type));

                    return { false };
                }

                auto left_register = generate_register_value(context, instructions, left);

                auto right_register = generate_register_value(context, instructions, right);

                auto result_register = allocate_register(context);

                Instruction operation;
                operation.type = InstructionType::BinaryOperation;
                operation.binary_operation.size = get_type_register_size(*context, left.type);
                operation.binary_operation.source_register_a = left_register;
                operation.binary_operation.source_register_b = right_register;
                operation.binary_operation.destination_register = result_register;

                Type result_type;
                switch(left.type.category) {
                    case TypeCategory::Integer: {
                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Addition: {
                                operation.binary_operation.type = BinaryOperationType::Add;

                                result_type = left.type;
                            } break;

                            case BinaryOperator::Subtraction: {
                                operation.binary_operation.type = BinaryOperationType::Subtract;

                                result_type = left.type;
                            } break;

                            case BinaryOperator::Multiplication: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedMultiply;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedMultiply;
                                }

                                result_type = left.type;
                            } break;

                            case BinaryOperator::Division: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedDivide;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedDivide;
                                }

                                result_type = left.type;
                            } break;

                            case BinaryOperator::Modulo: {
                                if(left.type.integer.is_signed) {
                                    operation.binary_operation.type = BinaryOperationType::SignedModulus;
                                } else {
                                    operation.binary_operation.type = BinaryOperationType::UnsignedModulus;
                                }

                                result_type = left.type;
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
                    auto register_index = generate_register_value(context, instructions, expression_value);

                    size_t result_regsiter_index;
                    switch(expression_value.type.category) {
                        case TypeCategory::Integer: {
                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    if(type.integer.size > expression_value.type.integer.size) {
                                        result_regsiter_index = allocate_register(context);

                                        Instruction integer_upcast;
                                        integer_upcast.type = InstructionType::IntegerUpcast;
                                        integer_upcast.integer_upcast.is_signed = expression_value.type.integer.is_signed && type.integer.is_signed;
                                        integer_upcast.integer_upcast.source_size = integer_size_to_register_size(expression_value.type.integer.size);
                                        integer_upcast.integer_upcast.source_register = register_index;
                                        integer_upcast.integer_upcast.destination_size = integer_size_to_register_size(type.integer.size);
                                        integer_upcast.integer_upcast.destination_register = result_regsiter_index;

                                        append(instructions, integer_upcast);
                                    } else {
                                        result_regsiter_index = register_index;
                                    }
                                } break;

                                case TypeCategory::Pointer: {
                                    if(expression_value.type.integer.size != context->address_integer_size) {
                                        error(expression.cast.expression->range, "Cannot cast from %s to pointer", type_description(expression_value.type));

                                        return { false };
                                    }

                                    result_regsiter_index = register_index;
                                } break;

                                default: {
                                    error(expression.cast.type->range, "Cannot cast from integer to %s", type_description(type));

                                    return { false };
                                } break;
                            }
                        } break;

                        case TypeCategory::Pointer: {
                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    if(type.integer.size != context->address_integer_size) {
                                        error(expression.cast.expression->range, "Cannot cast from pointer to %s", type_description(type));

                                        return { false };
                                    }

                                    result_regsiter_index = register_index;
                                } break;

                                case TypeCategory::Pointer: {
                                    result_regsiter_index = register_index;
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
                    value.register_ = result_regsiter_index;

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
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.fully_specified.type));

                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.allocate_local.destination_register = address_register;
                    allocate.allocate_local.size = get_type_size(*context, type);

                    append(instructions, allocate);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, type)) {
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

                    expect(initializer_value, generate_register_expression(context, instructions, statement.variable_declaration.fully_specified.initializer));

                    (*instructions)[allocate_index].allocate_local.size = get_type_size(*context, initializer_value.type);

                    Instruction store;
                    store.type = InstructionType::StoreInteger;
                    store.store_integer.size = get_type_register_size(*context, initializer_value.type);
                    store.store_integer.address_register = address_register;
                    store.store_integer.source_register = initializer_value.register_index;

                    append(instructions, store);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, initializer_value.type)) {
                        return false;
                    }

                    return true;
                } break;


                case VariableDeclarationType::FullySpecified: {
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.fully_specified.type));

                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.allocate_local.destination_register = address_register;
                    allocate.allocate_local.size = get_type_size(*context, type);

                    append(instructions, allocate);

                    expect(initializer_value, generate_register_expression(context, instructions, statement.variable_declaration.fully_specified.initializer));

                    if(!types_equal(type, initializer_value.type)) {
                        error(statement.assignment.value.range, "Incorrect assignment type. Expected %s, got %s", type_description(type), type_description(initializer_value.type));

                        return false;
                    }

                    Instruction store;
                    store.type = InstructionType::StoreInteger;
                    store.store_integer.size = get_type_register_size(*context, type);
                    store.store_integer.address_register = address_register;
                    store.store_integer.source_register = initializer_value.register_index;

                    append(instructions, store);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, type)) {
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

            expect(value, generate_register_expression(context, instructions, statement.assignment.value));

            if(!types_equal(target.type, value.type)) {
                error(statement.assignment.value.range, "Incorrect assignment type. Expected %s, got %s", type_description(target.type), type_description(value.type));

                return false;
            }

            Instruction store;
            store.type = InstructionType::StoreInteger;
            store.store_integer.size = get_type_register_size(*context, target.type);
            store.store_integer.source_register = value.register_index;
            store.store_integer.address_register = target.address;

            append(instructions, store);

            return true;
        } break;

        case StatementType::LoneIf: {
            expect(condition, generate_register_expression(context, instructions, statement.lone_if.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.lone_if.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            Instruction branch;
            branch.type = InstructionType::Branch;
            branch.branch.condition_register = condition.register_index;
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
            expect(condition, generate_register_expression(context, instructions, statement.while_loop.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.while_loop.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            Instruction branch;
            branch.type = InstructionType::Branch;
            branch.branch.condition_register = condition.register_index;
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
            expect(value, generate_register_expression(context, instructions, statement._return));

            if(!types_equal(context->return_type, value.type)) {
                error(statement._return.range, "Mismatched return type. Expected %s, got %s", type_description(context->return_type), type_description(value.type));

                return { false };
            }

            Instruction return_;
            return_.type = InstructionType::Return;
            return_.return_.value_register = value.register_index;

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

inline GlobalConstant create_base_integer_type(const char *name, IntegerSize size, bool is_signed) {
    Type type;
    type.category = TypeCategory::Integer;
    type.integer = {
        size,
        is_signed
    };

    return create_base_type(name, type);
}

Result<IR> generate_ir(Array<File> files) {
    assert(files.count > 0);

    List<GlobalConstant> global_constants{};

    auto address_integer_size = IntegerSize::Size64;

    append(&global_constants, create_base_integer_type("u8", IntegerSize::Size8, false));
    append(&global_constants, create_base_integer_type("u16", IntegerSize::Size16, false));
    append(&global_constants, create_base_integer_type("u32", IntegerSize::Size32, false));
    append(&global_constants, create_base_integer_type("u64", IntegerSize::Size64, false));

    append(&global_constants, create_base_integer_type("i8", IntegerSize::Size8, true));
    append(&global_constants, create_base_integer_type("i16", IntegerSize::Size16, true));
    append(&global_constants, create_base_integer_type("i32", IntegerSize::Size32, true));
    append(&global_constants, create_base_integer_type("i64", IntegerSize::Size64, true));

    append(&global_constants, create_base_integer_type("usize", address_integer_size, false));
    append(&global_constants, create_base_integer_type("isize", address_integer_size, true));

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
        address_integer_size,
        address_integer_size,
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

                context.is_top_level = false;
                context.determined_declaration = {
                    function.declaration,
                    function.polymorphic_determiners,
                    heapify(function.parent)
                };
                context.return_type = function.return_type;
                context.next_register = function.parameters.count;

                auto parameter_sizes = allocate<RegisterSize>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    if(!add_new_variable(&context, parameter.name, allocate_register(&context), parameter.type)) {
                        return { false };
                    }

                    parameter_sizes[i] = get_type_register_size(context, parameter.type);
                }

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
                    ir_function.return_size = get_type_register_size(context, function.return_type);
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
            to_array(context.libraries)
        }
    };
}