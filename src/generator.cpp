#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include "list.h"
#include "types.h"

void string_buffer_append(char **string_buffer, const char *string) {
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

            Array<Declaration> declarations;

            Array<Statement> statements;
        } function_definition;

        Expression constant_definition;
    };
};

Result<Declaration> lookup_declaration(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, const char *name) {
    for(auto i = 0; i < declaration_stack.count; i++) {
        auto parent_declaration = declaration_stack[declaration_stack.count - 1 - i];

        switch(parent_declaration.category) {
            case DeclarationCategory::FunctionDefinition: {
                for(auto declaration : parent_declaration.function_definition.declarations) {
                    if(declaration.type_resolved && strcmp(declaration.name, name) == 0) {
                        return {
                            true,
                            declaration
                        };
                    }
                }
            } break;

            default: {
                abort();
            } break;
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

struct ConstantValue {
    Type type;

    union {
        const char *function;

        int64_t integer;
    };
};

Result<ConstantValue> evaluate_constant_expression(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, Expression expression, bool print_errors);

Result<ConstantValue> resolve_constant_named_reference(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, const char *name, bool print_errors) {
    auto result = lookup_declaration(top_level_declarations, declaration_stack, name);

    if(!result.status) {
        if(print_errors) {
            fprintf(stderr, "Cannot find named reference %s\n", name);
        }

        return { false };
    }

    switch(result.value.category) {
        case DeclarationCategory::FunctionDefinition: {
            ConstantValue value;
            value.type.category = TypeCategory::Function;
            value.function = result.value.function_definition.mangled_name;

            return {
                true,
                value
            };
        } break;

        case DeclarationCategory::ConstantDefinition: {
            auto expression_result = evaluate_constant_expression(top_level_declarations, declaration_stack, result.value.constant_definition, print_errors);

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

Result<ConstantValue> evaluate_constant_expression(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, Expression expression, bool print_errors) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(top_level_declarations, declaration_stack, expression.named_reference, print_errors);
        } break;

        case ExpressionType::IntegerLiteral: {
            ConstantValue value;
            value.type.category = TypeCategory::Integer;
            value.type.integer.is_signed = true;
            value.type.integer.size = IntegerSize::Bit64;
            value.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            if(print_errors) {
                fprintf(stderr, "Function calls not allowed in global context\n");
            }

            return { false };
        } break;

        default: {
            abort();
        } break;
    }
}

Result<Declaration> create_declaration(List<const char*> *name_stack, Statement statement) {
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

Result<Type> resolve_declaration_type(Array<Declaration> top_level, List<Declaration> *stack, Declaration declaration, bool print_errors) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            append(stack, declaration);

            for(auto &child_declaration : declaration.function_definition.declarations) {
                if(!child_declaration.type_resolved) {
                    auto result = resolve_declaration_type(top_level, stack, child_declaration, print_errors);

                    if(result.status) {
                        child_declaration.type_resolved = true;
                        child_declaration.type = result.value;
                    }
                }
            }

            stack->count -= 1;

            Type type;
            type.category = TypeCategory::Function;

            return {
                true,
                type
            };
        } break;

        case DeclarationCategory::ConstantDefinition: {
            auto result = evaluate_constant_expression(top_level, to_array(*stack), declaration.constant_definition, print_errors);

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

int count_resolved_declarations(Declaration declaration) {
    auto resolved_declaration_count = 0;

    if(declaration.type_resolved) {
        resolved_declaration_count = 1;
    }

    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            for(auto child_declaration : declaration.function_definition.declarations) {
                resolved_declaration_count += count_resolved_declarations(child_declaration);
            }
        } break;
    }

    return resolved_declaration_count;
}

struct GenerationContext {
    char *forward_declaration_source;
    char *implementation_source;

    Array<Declaration> top_level_declarations;
    List<Declaration> declaration_stack;
};

bool generate_constant_value(char **source, ConstantValue value) {
    switch(value.type.category) {
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

        default: {
            abort();
        } break;
    }
}

bool generate_expression(GenerationContext *context, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            // TODO: Variable references

            auto result = resolve_constant_named_reference(context->top_level_declarations, to_array(context->declaration_stack), expression.named_reference, true);

            if(!result.status) {
                return { false };
            }

            return generate_constant_value(&(context->implementation_source), result.value);
        } break;

        case ExpressionType::IntegerLiteral: {
            char buffer[64];

            sprintf(buffer, "%dll", expression.integer_literal);

            string_buffer_append(&(context->implementation_source), buffer);

            return true;
        } break;

        case ExpressionType::FunctionCall: {
            auto result = generate_expression(context, *expression.function_call.expression);

            if(!result) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), "()");

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

bool generate_statement(GenerationContext *context, Statement statement) {
    switch(statement.type) {
        case StatementType::Expression: {
            auto result = generate_expression(context, statement.expression);

            if(!result) {
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

bool generate_declaration(GenerationContext *context, Declaration declaration) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            string_buffer_append(&(context->forward_declaration_source), "void ");
            string_buffer_append(&(context->forward_declaration_source), declaration.function_definition.mangled_name);
            string_buffer_append(&(context->forward_declaration_source), "();");

            append(&(context->declaration_stack), declaration);

            for(auto child_declaration : declaration.function_definition.declarations) {
                if(!generate_declaration(context, child_declaration)) {
                    return false;
                }
            }

            string_buffer_append(&(context->implementation_source), "void ");

            string_buffer_append(&(context->implementation_source), declaration.function_definition.mangled_name);
            string_buffer_append(&(context->implementation_source), "(){");

            for(auto statement : declaration.function_definition.statements) {
                if(!generate_statement(context, statement)) {
                    return false;
                }
            }

            context->declaration_stack.count -= 1;

            string_buffer_append(&(context->implementation_source), "}");

            return true;
        } break;

        case DeclarationCategory::ConstantDefinition: {
            // Only do type checking. Constants have no run-time presence.

            auto result = evaluate_constant_expression(context->top_level_declarations, to_array(context->declaration_stack), declaration.constant_definition, true);

            return result.status;
        } break;

        default: {
            abort();
        } break;
    }
}

Result<char*> generate_c_source(Array<Statement> top_level_statements) {
    List<Declaration> top_level_declarations{};

    List<const char*> name_stack{};

    for(auto top_level_statement : top_level_statements) {
        auto result = create_declaration(&name_stack, top_level_statement);

        if(result.status) {
            append(&top_level_declarations, result.value);
        } else {
            fprintf(stderr, "Only declarations are allowed in global scope\n");

            return { false };
        }
    }

    auto previous_resolved_declaration_count = 0;

    auto declaration_stack = List<Declaration>{};

    while(true) {
        for(auto &top_level_declaration : top_level_declarations) {
            if(!top_level_declaration.type_resolved) {
                auto result = resolve_declaration_type(to_array(top_level_declarations), &declaration_stack, top_level_declaration, false);

                if(result.status) {
                    top_level_declaration.type_resolved = true;
                    top_level_declaration.type = result.value;
                }
            }
        }

        auto resolved_declaration_count = 0;
        
        for(auto top_level_declaration : top_level_declarations) {
            resolved_declaration_count += count_resolved_declarations(top_level_declaration);
        }

        if(resolved_declaration_count == previous_resolved_declaration_count) {
            for(auto top_level_declaration : top_level_declarations) {
                if(!top_level_declaration.type_resolved) {
                    auto result = resolve_declaration_type(to_array(top_level_declarations), &declaration_stack, top_level_declaration, true);

                    if(!result.status) {
                        return { false };
                    }
                }
            }

            break;
        }

        previous_resolved_declaration_count = resolved_declaration_count;
    }

    GenerationContext context{
        nullptr,
        nullptr,
        to_array(top_level_declarations),
        List<Declaration>{}
    };

    for(auto top_level_declaration : top_level_declarations) {
        if(!generate_declaration(&context, top_level_declaration)) {
            return { false };
        }
    }

    char *full_source{};

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