#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"
#include "types.h"

static void string_buffer_append(char **string_buffer, const char *string) {
    auto string_length = strlen(string);

    if(*string_buffer == nullptr) {
        *string_buffer = (char*)malloc(string_length + 1);

        strcpy(*string_buffer, string);
    } else {
        auto string_buffer_length = strlen(*string_buffer);

        auto new_string_buffer_length = string_buffer_length + string_length;

        auto new_string_buffer = (char*)realloc((void*)(*string_buffer), new_string_buffer_length + 1);

        strcpy(new_string_buffer, *string_buffer);

        strcat(new_string_buffer, string);

        *string_buffer = new_string_buffer;
    }
}

enum struct DeclarationCategory {
    FunctionDefinition,
    ConstantDefinition
};

struct Declaration {
    DeclarationCategory category;

    const char *name;

    bool type_resolved;
    Type type;

    union {
        struct {
            const char *mangled_name;

            Array<FunctionParameter> parameters;

            bool has_return_type;
            Expression return_type;

            Array<Declaration> declarations;

            Array<Statement> statements;
        } function_definition;

        Expression constant_definition;
    };
};

static Array<Declaration> get_declaration_children(Declaration declaration) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            return declaration.function_definition.declarations;
        } break;

        case DeclarationCategory::ConstantDefinition: {
            return Array<Declaration>{};
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<Declaration> lookup_declaration(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, const char *name) {
    for(auto i = 0; i < declaration_stack.count; i++) {
        for(auto child : get_declaration_children(declaration_stack[declaration_stack.count - 1 - i])) {
            if(child.type_resolved && strcmp(child.name, name) == 0) {
                return {
                    true,
                    child
                };
            }
        }
    }

    for(auto declaration : top_level_declarations) {
        if(strcmp(declaration.name, name) == 0) {
            return {
                true,
                declaration
            };
        }
    }

    return { false };
}

union ConstantValue {
    const char *function;

    int64_t integer;

    Type type;

    Array<ConstantValue> array;
};

struct ConstantExpressionValue {
    Type type;

    ConstantValue value;
};

struct GlobalConstant {
    const char *name;

    Type type;

    ConstantValue value;
};

struct ConstantContext {
    Array<GlobalConstant> global_constants;

    Array<Declaration> top_level_declarations;

    List<Declaration> declaration_stack;
};

static Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors);

static Result<ConstantExpressionValue> resolve_constant_named_reference(ConstantContext context, const char *name, bool print_errors) {
    auto result = lookup_declaration(context.top_level_declarations, to_array(context.declaration_stack), name);

    if(!result.status) {
        for(auto global_constant : context.global_constants) {
            if(strcmp(name, global_constant.name) == 0) {
                return {
                    true,
                    {
                        global_constant.type,
                        global_constant.value
                    }
                };
            }
        }

        if(print_errors) {
            fprintf(stderr, "Cannot find named reference %s\n", name);
        }

        return { false };
    }

    switch(result.value.category) {
        case DeclarationCategory::FunctionDefinition: {
            ConstantValue value;
            value.function = result.value.function_definition.mangled_name;

            return {
                true,
                {
                    result.value.type,
                    value
                }
            };
        } break;

        case DeclarationCategory::ConstantDefinition: {
            auto expression_result = evaluate_constant_expression(context, result.value.constant_definition, print_errors);

            if(!expression_result.status) {
                return { false };
            }

            return {
                true,
                expression_result.value
            };
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<Type> evaluate_type_expression(ConstantContext context, Expression expression, bool print_errors);

static Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(context, expression.named_reference, print_errors);
        } break;

        case ExpressionType::IndexReference: {
            auto expression_result = evaluate_constant_expression(context, *expression.index_reference.expression, print_errors);

            if(!expression_result.status) {
                return { false };
            }

            if(expression_result.value.type.category != TypeCategory::Array) {
                if(print_errors) {
                    fprintf(stderr, "Cannot index a non-array\n");
                }

                return { false };
            }

            auto index_result = evaluate_constant_expression(context, *expression.index_reference.index, print_errors);

            if(!index_result.status) {
                return { false };
            }

            if(index_result.value.type.category != TypeCategory::Integer) {
                if(print_errors) {
                    fprintf(stderr, "Array index not an integer\n");
                }

                return { false };
            }

            if(index_result.value.type.integer.is_signed && index_result.value.value.integer < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index out of bounds\n");
                }

                return { false };
            }

            auto index = (size_t)index_result.value.value.integer;

            if(index >= expression_result.value.value.array.count) {
                if(print_errors) {
                    fprintf(stderr, "Array index out of bounds\n");
                }

                return { false };
            }

            return {
                true,
                {
                    *expression_result.value.type.array,
                    expression_result.value.value.array[index]
                }
            };
        } break;

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer.determined = false;

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
            auto array_type = (Type*)malloc(sizeof(Type));
            array_type->category = TypeCategory::Integer;
            array_type->integer = {
                true,
                false,
                IntegerSize::Bit8
            };

            Type type;
            type.category = TypeCategory::Array;
            type.array = array_type;

            auto length = strlen(expression.string_literal);

            auto characters = (ConstantValue*)malloc(sizeof(ConstantValue) * length);

            for(size_t i = 0; i < length; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            ConstantValue value;
            value.array = {
                length,
                characters
            };

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case ExpressionType::FunctionCall: {
            if(print_errors) {
                fprintf(stderr, "Function calls not allowed in global context\n");
            }

            return { false };
        } break;

        case ExpressionType::Pointer: {
            auto result = evaluate_constant_expression(context, *(expression.pointer), print_errors);

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Type) {
                if(print_errors) {
                    fprintf(stderr, "Cannot take pointers to constants\n");
                }

                return { false };
            }

            auto pointer_type = (Type*)malloc(sizeof(Type));
            *pointer_type = result.value.value.type;

            Type type;
            type.category = TypeCategory::Type;

            ConstantValue value;
            value.type.category = TypeCategory::Pointer;
            value.type.pointer = pointer_type;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;
        
        case ExpressionType::ArrayType: {
            auto result = evaluate_type_expression(context, *expression.array_type, true);

            if(!result.status) {
                return { false };
            }

            auto array_type = (Type*)malloc(sizeof(Type));
            *array_type = result.value;

            ConstantExpressionValue value;
            value.type.category = TypeCategory::Type;
            value.value.type.category = TypeCategory::Array;
            value.value.type.array = array_type;

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

static Result<Type> evaluate_type_expression(ConstantContext context, Expression expression, bool print_errors) {
    auto result = evaluate_constant_expression(context, expression, print_errors);

    if(!result.status) {
        return { false };
    }

    if(result.value.type.category != TypeCategory::Type) {
        if(print_errors) {
            fprintf(stderr, "Value is not a type\n");
        }

        return { false };
    }

    return {
        true,
        result.value.value.type
    };
}

static Result<Declaration> create_declaration(List<const char*> *name_stack, Statement statement) {
    switch(statement.type) {
        case StatementType::FunctionDefinition: {
            List<Declaration> child_declarations{};
            List<Statement> child_statements{};

            append(name_stack, statement.function_definition.name);

            for(auto child_statement : statement.function_definition.statements) {
                auto result = create_declaration(name_stack, child_statement);

                if(result.status){
                    append(&child_declarations, result.value);
                } else {
                    append(&child_statements, child_statement);
                }
            }

            char* mangled_name{};

            for(auto name : *name_stack) {
                string_buffer_append(&mangled_name, name);
            }

            name_stack->count -= 1;

            Declaration declaration;
            declaration.category = DeclarationCategory::FunctionDefinition;
            declaration.name = statement.function_definition.name;
            declaration.type_resolved = false;

            declaration.function_definition = {
                mangled_name,
                statement.function_definition.parameters,
                statement.function_definition.has_return_type,
                statement.function_definition.return_type,
                to_array(child_declarations),
                to_array(child_statements)
            };

            return {
                true,
                declaration
            };
        } break;

        case StatementType::ConstantDefinition: {
            Declaration declaration;
            declaration.category = DeclarationCategory::ConstantDefinition;
            declaration.name = statement.constant_definition.name;
            declaration.type_resolved = false;
            
            declaration.constant_definition = statement.constant_definition.expression;

            return {
                true,
                declaration
            };
        } break;

        default: {
            return { false };
        } break;
    }
}

static Result<Type> resolve_declaration_type(ConstantContext *context, Declaration declaration, bool print_errors) {
    auto children = get_declaration_children(declaration);

    if(children.count > 0) {
        append(&(context->declaration_stack), declaration);

        for(auto &child : children) {
            auto result = resolve_declaration_type(context, child, print_errors);

            if(result.status) {
                child.type_resolved = true;
                child.type = result.value;
            }
        }

        context->declaration_stack.count -= 1;
    }

    if(declaration.type_resolved) {
        return {
            true,
            declaration.type
        };
    } else {
        Array<Declaration> siblings;
        if(context->declaration_stack.count == 0) {
            siblings = context->top_level_declarations;
        } else {
            siblings = get_declaration_children(context->declaration_stack[context->declaration_stack.count - 1]);
        }

        for(auto sibling : siblings) {
            if(sibling.type_resolved && strcmp(sibling.name, declaration.name) == 0) {
                if(print_errors) {
                    fprintf(stderr, "Duplicate declaration name %s\n", declaration.name);
                }

                return { false };
            }
        }

        switch(declaration.category) {
            case DeclarationCategory::FunctionDefinition: {
                for(auto parameter : declaration.function_definition.parameters) {
                    auto result = evaluate_type_expression(*context, parameter.type, print_errors);

                    if(!result.status) {
                        return { false };
                    }
                }

                Type return_type;
                if(declaration.function_definition.has_return_type) {
                    auto result = evaluate_type_expression(*context, declaration.function_definition.return_type, print_errors);

                    if(!result.status) {
                        return { false };
                    }

                    return_type = result.value;
                } else {
                    return_type.category = TypeCategory::Void;
                }

                auto parameters = (Type*)malloc(declaration.function_definition.parameters.count * sizeof(Type));
                
                for(auto i = 0; i < declaration.function_definition.parameters.count; i += 1) {
                    auto result = evaluate_type_expression(*context, declaration.function_definition.parameters[i].type, print_errors);

                    parameters[i] = result.value;
                }

                auto return_type_heap = (Type*)malloc(sizeof(Type));
                *return_type_heap = return_type;

                Type type;
                type.category = TypeCategory::Function;
                type.function.parameters = {
                    declaration.function_definition.parameters.count,
                    parameters
                };
                type.function.return_type = return_type_heap;

                return {
                    true,
                    type
                };
            } break;

            case DeclarationCategory::ConstantDefinition: {
                auto result = evaluate_constant_expression(*context, declaration.constant_definition, print_errors);

                if(!result.status) {
                    return { false };
                }

                return {
                    true,
                    result.value.type
                };
            } break;

            default: {
                abort();
            } break;
        }
    }
}

static int count_declarations(Declaration declaration, bool only_resolved) {
    auto resolved_declaration_count = 0;

    if(!only_resolved || declaration.type_resolved) {
        resolved_declaration_count = 1;
    }

    for(auto child : get_declaration_children(declaration)) {
        resolved_declaration_count += count_declarations(child, only_resolved);
    }

    return resolved_declaration_count;
}

struct Variable {
    const char *name;

    Type type;
};

struct ArrayType {
    const char *mangled_name;

    Type type;
};

struct ArrayConstant {
    Type type;

    Array<ConstantValue> elements;
};

struct GenerationContext {
    char *forward_declaration_source;
    char *implementation_source;

    ConstantContext constant_context;

    List<List<Variable>> variable_context_stack;

    List<ArrayType> array_types;

    List<ArrayConstant> array_constants;
};

static bool add_new_variable(GenerationContext *context, const char *name, Type type) {
    auto variable_context = &(context->variable_context_stack[context->variable_context_stack.count - 1]);

    for(auto variable : *variable_context) {
        if(strcmp(variable.name, name) == 0) {
            fprintf(stderr, "Duplicate variable name %s\n", name);

            return false;
        }
    }

    append(variable_context, Variable{
        name,
        type
    });

    return true;
}

static Result<const char *> maybe_register_array_type(GenerationContext *context, Type type) {
    auto array_types = &(context->array_types);

    for(auto array_type : *array_types) {
        if(types_equal(array_type.type, type)) {
            return {
                true,
                array_type.mangled_name
            };
        }
    }

    switch(type.category) {
        case TypeCategory::Function:
        case TypeCategory::Type:
        case TypeCategory::Void: {
            return { false };
        } break;

        case TypeCategory::Integer: {
            assert(type.integer.determined);
        } break;

        case TypeCategory::Pointer: {
            
        } break;

        case TypeCategory::Array: {
            auto result = maybe_register_array_type(context, *type.array);

            if(!result.status) {
                return { false };
            }
        } break;

        default: {
            abort();
        } break;
    }

    char *mangled_name_buffer{};

    string_buffer_append(&mangled_name_buffer, "_array_type_");

    char number_buffer[32];

    sprintf(number_buffer, "%d", array_types->count);

    string_buffer_append(&mangled_name_buffer, number_buffer);

    append(array_types, ArrayType {
        mangled_name_buffer,
        type
    });

    return {
        true,
        mangled_name_buffer
    };
}

static bool generate_type(GenerationContext *context, char **source, Type type) {
    switch(type.category) {
        case TypeCategory::Function: {
            fprintf(stderr, "Function values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Integer: {
            assert(type.integer.determined);

            switch(type.integer.size) {
                case IntegerSize::Bit8: {
                    if(type.integer.is_signed) {
                        string_buffer_append(source, "signed ");
                    }

                    string_buffer_append(source, "char");
                } break;

                case IntegerSize::Bit16: {
                    if(!type.integer.is_signed) {
                        string_buffer_append(source, "unsigned ");
                    }

                    string_buffer_append(source, "short");
                } break;

                case IntegerSize::Bit32: {
                    if(!type.integer.is_signed) {
                        string_buffer_append(source, "unsigned ");
                    }

                    string_buffer_append(source, "int");
                } break;

                case IntegerSize::Bit64: {
                    if(!type.integer.is_signed) {
                        string_buffer_append(source, "unsigned ");
                    }

                    string_buffer_append(source, "long long");
                } break;

                default: {
                    abort();
                } break;
            }

            return true;
        } break;

        case TypeCategory::Type: {
            fprintf(stderr, "Type values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Void: {
            string_buffer_append(source, "void");

            return true;
        } break;

        case TypeCategory::Pointer: {
            if(!generate_type(context, source, *type.pointer)) {
                return false;
            }

            string_buffer_append(source, "*");

            return true;
        } break;

        case TypeCategory::Array: {
            auto result = maybe_register_array_type(context, *type.array);

            if(!result.status) {
                return false;
            }

            string_buffer_append(source, "struct ");
            string_buffer_append(source, result.value);

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_constant_value(GenerationContext *context, char **source, Type type, ConstantValue value) {
    switch(type.category) {
        case TypeCategory::Function: {
            string_buffer_append(source, value.function);

            return true;
        } break;

        case TypeCategory::Integer: {
            char buffer[64];

            sprintf(buffer, "%lld", value.integer);

            string_buffer_append(source, buffer);

            return true;
        } break;

        case TypeCategory::Type: {
            return generate_type(context, source, value.type);
        } break;

        case TypeCategory::Array: {
            string_buffer_append(source, "{");

            char buffer[64];

            sprintf(buffer, "%d", value.array.count);

            string_buffer_append(source, buffer);

            string_buffer_append(source, ",");

            string_buffer_append(source, "_array_constant_");

            sprintf(buffer, "%d", context->array_constants.count);

            string_buffer_append(source, buffer);

            string_buffer_append(source, "}");

            append(&(context->array_constants), ArrayConstant {
                *type.array,
                value.array
            });

            return true;
        } break;

        case TypeCategory::Void: {
            fprintf(stderr, "Void values cannot exist at runtime\n");

            return false;
        } break;

        default: {
            abort();
        } break;
    }
}

enum struct ExpressionValueCategory {
    Anonymous,
    Constant,
    Assignable
};

struct ExpressionValue {
    ExpressionValueCategory category;

    Type type;

    union {
        ConstantValue constant;
    };
};

static Result<ExpressionValue> generate_expression(GenerationContext *context, char **source, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(auto i = 0; i < context->variable_context_stack.count; i++) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name, expression.named_reference) == 0) {
                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Assignable;
                        value.type = variable.type;

                        string_buffer_append(source, variable.name);

                        return {
                            true,
                            value
                        };
                    }
                }
            }

            auto result = resolve_constant_named_reference(context->constant_context, expression.named_reference, true);

            if(!result.status) {
                return { false };
            }

            auto generate_result = generate_constant_value(context, source, result.value.type, result.value.value);

            if(!generate_result) {
                return { false };
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type = result.value.type;
            value.constant = result.value.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IndexReference: {
            string_buffer_append(source, "(");

            auto expression_result = generate_expression(context, source, *expression.index_reference.expression);

            string_buffer_append(source, ")");

            if(!expression_result.status) {
                return { false };
            }

            if(expression_result.value.type.category != TypeCategory::Array) {
                fprintf(stderr, "Cannot index a non-array\n");

                return { false };
            }

            string_buffer_append(source, "[");

            auto index_result = generate_expression(context, source, *expression.index_reference.index);

            if(!index_result.status) {
                return { false };
            }

            if(index_result.value.type.category != TypeCategory::Integer) {
                fprintf(stderr, "Array index not an integer");

                return { false };
            }

            string_buffer_append(source, "]");

            ExpressionValue value;
            value.category = ExpressionValueCategory::Assignable;
            value.type = *expression_result.value.type.array;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IntegerLiteral: {
            char buffer[64];

            sprintf(buffer, "%lld", expression.integer_literal);

            string_buffer_append(source, buffer);

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Integer;
            value.type.integer.determined = false;
            value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StringLiteral: {            
            auto array_type = (Type*)malloc(sizeof(Type));
            array_type->category = TypeCategory::Integer;
            array_type->integer = {
                true,
                false,
                IntegerSize::Bit8
            };

            auto length = strlen(expression.string_literal);

            auto characters = (ConstantValue*)malloc(sizeof(ConstantValue) * length);

            for(size_t i = 0; i < length; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Array;
            value.type.array = array_type;
            value.constant.array = {
                length,
                characters
            };

            if(!generate_constant_value(context, source, value.type, value.constant)) {
                return { false };
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            string_buffer_append(source, "(");

            auto result = generate_expression(context, source, *expression.function_call.expression);

            string_buffer_append(source, ")");

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Function) {
                fprintf(stderr, "Cannot call a non-function\n");

                return { false };
            }

            string_buffer_append(source, "(");

            if(expression.function_call.parameters.count != result.value.type.function.parameters.count) {
                fprintf(stderr, "Incorrect number of parameters. Expected %d, got %d\n", result.value.type.function.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            for(auto i = 0; i < result.value.type.function.parameters.count; i += 1) {
                auto parameter_result = generate_expression(context, source, expression.function_call.parameters[i]);

                if(!parameter_result.status) {
                    return { false };
                }

                if(!types_equal(parameter_result.value.type, result.value.type.function.parameters[i])) {
                    fprintf(stderr, "Incorrect parameter type for parameter %d\n", i);

                    return { false };
                }

                if(i != result.value.type.function.parameters.count - 1) {
                    string_buffer_append(source, ",");
                }
            }

            string_buffer_append(source, ")");

            ExpressionValue value;
            value.category = ExpressionValueCategory::Anonymous;
            value.type = *result.value.type.function.return_type;

            return { 
                true,
                value
            };
        } break;

        case ExpressionType::Pointer: {
            char *buffer{};

            auto result = generate_expression(context, &buffer, *expression.pointer);

            if(!result.status) {
                return { false };
            }

            switch(result.value.category) {
                case ExpressionValueCategory::Anonymous: {
                    fprintf(stderr, "Cannot take pointers anonymous values\n");

                    return { false };
                } break;

                case ExpressionValueCategory::Constant: {
                    if(result.value.type.category != TypeCategory::Type) {
                        fprintf(stderr, "Cannot take pointers to constants\n");

                        return { false };
                    }

                    string_buffer_append(source, buffer);

                    auto pointer_type = (Type*)malloc(sizeof(Type));
                    *pointer_type = result.value.constant.type;

                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Constant;
                    value.type.category = TypeCategory::Type;
                    value.constant.type.category = TypeCategory::Pointer;
                    value.constant.type.pointer = pointer_type;

                    return {
                        true,
                        value
                    };
                } break;
                
                case ExpressionValueCategory::Assignable: {
                    string_buffer_append(source, "&(");

                    string_buffer_append(source, buffer);

                    string_buffer_append(source, ")");

                    auto pointer_type = (Type*)malloc(sizeof(Type));
                    *pointer_type = result.value.type;

                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Anonymous;
                    value.type.category = TypeCategory::Pointer;
                    value.type.pointer = pointer_type;

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

        case ExpressionType::ArrayType: {
            auto result = evaluate_type_expression(context->constant_context, *expression.array_type, true);

            if(!result.status) {
                return { false };
            }

            auto array_type = (Type*)malloc(sizeof(Type));
            *array_type = result.value;

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.constant.type.category = TypeCategory::Array;
            value.constant.type.array = array_type;

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

static bool generate_statement(GenerationContext *context, Statement statement) {
    switch(statement.type) {
        case StatementType::Expression: {
            auto result = generate_expression(context, &(context->implementation_source), statement.expression);

            if(!result.status) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), ";");

            return true;
        } break;

        case StatementType::VariableDeclaration: {
            Type type;
            if(statement.variable_declaration.has_type) {
                auto result = evaluate_type_expression(context->constant_context, statement.variable_declaration.type, true);

                if(!result.status) {
                    return { false };
                }

                type = result.value;
            }

            Type initialzer_type;
            char *initializer_source{};
            if(statement.variable_declaration.has_initializer) {
                auto result = generate_expression(context, &initializer_source, statement.variable_declaration.initializer);

                if(!result.status) {
                    return { false };
                }

                initialzer_type = result.value.type;
            }

            assert(statement.variable_declaration.has_initializer || statement.variable_declaration.has_type);

            if(statement.variable_declaration.has_initializer && statement.variable_declaration.has_type) {
                if(!types_equal(type, initialzer_type)) {
                    fprintf(stderr, "Initializer type does not match variable type\n");

                    return { false };
                }
            } else if(statement.variable_declaration.has_initializer) {
                type = initialzer_type;

                if(type.category == TypeCategory::Integer && !type.integer.determined) {
                    type.integer.determined = true;
                    type.integer.is_signed = true;
                    type.integer.size = IntegerSize::Bit64;
                }
            }

            if(!add_new_variable(context, statement.variable_declaration.name, type)) {
                return false;
            }

            if(!generate_type(context, &(context->implementation_source), type)) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), " ");

            string_buffer_append(&(context->implementation_source), statement.variable_declaration.name);

            if(statement.variable_declaration.has_initializer) {
                string_buffer_append(&(context->implementation_source), "=");

                string_buffer_append(&(context->implementation_source), initializer_source);
            }

            string_buffer_append(&(context->implementation_source), ";");

            return true;
        } break;

        case StatementType::Assignment: {
            auto target_result = generate_expression(context, &(context->implementation_source), statement.assignment.target);

            if(!target_result.status) {
                return false;
            }

            if(target_result.value.category != ExpressionValueCategory::Assignable) {
                fprintf(stderr, "Value is not assignable\n");

                return false;
            }

            string_buffer_append(&(context->implementation_source), "=");

            auto value_result = generate_expression(context, &(context->implementation_source), statement.assignment.value);

            if(!value_result.status) {
                return false;
            }

            if(!types_equal(target_result.value.type, value_result.value.type)) {
                fprintf(stderr, "Assigning incorrect type\n");

                return false;
            }

            string_buffer_append(&(context->implementation_source), ";");

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_function_signature(GenerationContext *context, char **source, Declaration declaration) {
    assert(declaration.category == DeclarationCategory::FunctionDefinition);

    generate_type(context, source, *declaration.type.function.return_type);

    string_buffer_append(source, " ");
    string_buffer_append(source, declaration.function_definition.mangled_name);
    string_buffer_append(source, "(");
    
    for(auto i = 0; i < declaration.type.function.parameters.count; i += 1) {
        auto result = generate_type(context, source, declaration.type.function.parameters[i]);

        if(!result) {
            return false;
        }

        string_buffer_append(source, " ");

        string_buffer_append(source, declaration.function_definition.parameters[i].name);

        if(i != declaration.function_definition.parameters.count - 1) {
            string_buffer_append(source, ",");
        }
    }

    string_buffer_append(source, ")");

    return true;
}

static bool generate_declaration(GenerationContext *context, Declaration declaration) {
    assert(declaration.type_resolved);

    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            assert(declaration.type.category == TypeCategory::Function);

            if(!generate_function_signature(context, &(context->forward_declaration_source), declaration)) {
                return false;
            }

            string_buffer_append(&(context->forward_declaration_source), ";");

            append(&(context->constant_context.declaration_stack), declaration);

            for(auto child_declaration : declaration.function_definition.declarations) {
                if(!generate_declaration(context, child_declaration)) {
                    return false;
                }
            }

            if(!generate_function_signature(context, &(context->implementation_source), declaration)) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), "{");

            append(&(context->variable_context_stack), List<Variable>{});

            assert(declaration.type.function.parameters.count == declaration.function_definition.parameters.count);

            for(auto i = 0; i < declaration.type.function.parameters.count; i += 1) {
                if(!add_new_variable(context, declaration.function_definition.parameters[i].name, declaration.type.function.parameters[i])) {
                    return false;
                }
            }

            for(auto statement : declaration.function_definition.statements) {
                if(!generate_statement(context, statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            context->constant_context.declaration_stack.count -= 1;

            string_buffer_append(&(context->implementation_source), "}");

            return true;
        } break;

        case DeclarationCategory::ConstantDefinition: {
            // Only do type checking. Constants have no run-time presence.

            auto result = evaluate_constant_expression(context->constant_context, declaration.constant_definition, true);

            return result.status;
        } break;

        default: {
            abort();
        } break;
    }
}

inline GlobalConstant create_base_integer_type(const char *name, bool is_signed, IntegerSize size) {
    Type type;
    type.category = TypeCategory::Type;

    ConstantValue value;
    value.type.category = TypeCategory::Integer;
    value.type.integer.is_signed = is_signed;
    value.type.integer.size = size;

    return {
        name,
        type,
        value
    };
}

Result<char*> generate_c_source(Array<Statement> top_level_statements) {
    List<Declaration> top_level_declarations{};

    List<const char*> name_stack{};

    for(auto top_level_statement : top_level_statements) {
        auto result = create_declaration(&name_stack, top_level_statement);

        if(result.status) {
            append(&top_level_declarations, result.value);
        } else {
            fprintf(stderr, "Only constant declarations are allowed in global scope\n");

            return { false };
        }
    }

    auto previous_resolved_declaration_count = 0;

    List<GlobalConstant> global_constants{};

    append(&global_constants, create_base_integer_type("u8", false, IntegerSize::Bit8));
    append(&global_constants, create_base_integer_type("u16", false, IntegerSize::Bit16));
    append(&global_constants, create_base_integer_type("u32", false, IntegerSize::Bit32));
    append(&global_constants, create_base_integer_type("u64", false, IntegerSize::Bit64));

    append(&global_constants, create_base_integer_type("i8", true, IntegerSize::Bit8));
    append(&global_constants, create_base_integer_type("i16", true, IntegerSize::Bit16));
    append(&global_constants, create_base_integer_type("i32", true, IntegerSize::Bit32));
    append(&global_constants, create_base_integer_type("i64", true, IntegerSize::Bit64));

    ConstantContext constant_context {
        to_array(global_constants),
        to_array(top_level_declarations)
    };

    while(true) {
        for(auto &top_level_declaration : top_level_declarations) {
            auto result = resolve_declaration_type(&constant_context, top_level_declaration, false);

            if(result.status) {
                top_level_declaration.type_resolved = true;
                top_level_declaration.type = result.value;
            }
        }

        auto resolved_declaration_count = 0;
        
        for(auto top_level_declaration : top_level_declarations) {
            resolved_declaration_count += count_declarations(top_level_declaration, true);
        }

        if(resolved_declaration_count == previous_resolved_declaration_count) {
            for(auto top_level_declaration : top_level_declarations) {
                resolve_declaration_type(&constant_context, top_level_declaration, true);
            }

            auto declaration_count = 0;
            for(auto top_level_declaration : top_level_declarations) {
                declaration_count += count_declarations(top_level_declaration, false);
            }

            if(declaration_count != resolved_declaration_count) {
                return { false };
            }

            break;
        }

        previous_resolved_declaration_count = resolved_declaration_count;
    }

    GenerationContext context{};
    context.constant_context = constant_context;

    for(auto top_level_declaration : top_level_declarations) {
        if(!generate_declaration(&context, top_level_declaration)) {
            return { false };
        }
    }

    char *full_source{};

    for(auto array_type : context.array_types) {
        string_buffer_append(&full_source, "struct ");

        string_buffer_append(&full_source, array_type.mangled_name);

        string_buffer_append(&full_source, "{long long int length;");

        if(!generate_type(&context, &full_source, array_type.type)) {
            return { false };
        }

        string_buffer_append(&full_source, " *elements;};");
    }

    for(auto i = 0; i < context.array_constants.count; i += 1) {
        auto array_constant = context.array_constants[i];

        if(!generate_type(&context, &full_source, array_constant.type)) {
            return { false };
        }

        string_buffer_append(&full_source, " _array_constant_");

        char buffer[32];

        sprintf(buffer, "%d", i);

        string_buffer_append(&full_source, buffer);

        string_buffer_append(&full_source, "[");

        sprintf(buffer, "%llu", array_constant.elements.count);

        string_buffer_append(&full_source, buffer);

        string_buffer_append(&full_source, "]");
        
        string_buffer_append(&full_source, "={");

        for(size_t j = 0; j < array_constant.elements.count; j += 1) {
            if(!generate_constant_value(&context, &full_source, array_constant.type, array_constant.elements[j])) {
                return { false };
            }

            if(j != array_constant.elements.count - 1) {
                string_buffer_append(&full_source, ",");
            }
        }

        string_buffer_append(&full_source, "};");
    }

    if(context.forward_declaration_source != nullptr) {
        string_buffer_append(&full_source, context.forward_declaration_source);
    }

    if(context.implementation_source != nullptr) {
        string_buffer_append(&full_source, context.implementation_source);
    }

    return {
        true,
        full_source
    };
}