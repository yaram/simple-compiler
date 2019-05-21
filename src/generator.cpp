#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
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

            Array<FunctionParameter> parameters;

            bool has_return_type;
            Expression return_type;

            Array<Declaration> declarations;

            Array<Statement> statements;
        } function_definition;

        Expression constant_definition;
    };
};

Array<Declaration> get_declaration_children(Declaration declaration) {
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

Result<Declaration> lookup_declaration(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, const char *name) {
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

Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors);

Result<ConstantExpressionValue> resolve_constant_named_reference(ConstantContext context, const char *name, bool print_errors) {
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

Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(context, expression.named_reference, print_errors);
        } break;

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer.is_signed = true;
            type.integer.size = IntegerSize::Bit64;

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

Result<Type> resolve_declaration_type(ConstantContext *context, Declaration declaration, bool print_errors) {
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
                    auto result = evaluate_constant_expression(*context, parameter.type, print_errors);

                    if(!result.status) {
                        return { false };
                    }
                    
                    if(result.value.type.category != TypeCategory::Type) {
                        if(print_errors) {
                            fprintf(stderr, "Value is not a type\n");
                        }

                        return { false };
                    }
                }

                Type return_type;
                if(declaration.function_definition.has_return_type) {
                    auto result = evaluate_constant_expression(*context, declaration.function_definition.return_type, print_errors);

                    if(!result.status) {
                        return { false };
                    }

                    if(result.value.type.category != TypeCategory::Type) {
                        if(print_errors) {
                            fprintf(stderr, "Value is not a type\n");
                        }

                        return { false };
                    }

                    return_type = result.value.value.type;
                } else {
                    return_type.category = TypeCategory::Void;
                }

                auto parameters = (Type*)malloc(declaration.function_definition.parameters.count * sizeof(Type));
                
                for(auto i = 0; i < declaration.function_definition.parameters.count; i += 1) {
                    auto result = evaluate_constant_expression(*context, declaration.function_definition.parameters[i].type, print_errors);

                    parameters[i] = result.value.value.type;
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

int count_declarations(Declaration declaration, bool only_resolved) {
    auto resolved_declaration_count = 0;

    if(!only_resolved || declaration.type_resolved) {
        resolved_declaration_count = 1;
    }

    for(auto child : get_declaration_children(declaration)) {
        resolved_declaration_count += count_declarations(child, only_resolved);
    }

    return resolved_declaration_count;
}

struct GenerationContext {
    char *forward_declaration_source;
    char *implementation_source;

    ConstantContext constant_context;
};

bool generate_type(char **source, Type type) {
    switch(type.category) {
        case TypeCategory::Function: {
            fprintf(stderr, "Function values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Integer: {
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
        } break;

        case TypeCategory::Type: {
            fprintf(stderr, "Type values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Void: {
            string_buffer_append(source, "void");

            return false;
        } break;

        case TypeCategory::Pointer: {
            if(!generate_type(source, *type.pointer)) {
                return false;
            }

            string_buffer_append(source, "*");

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

bool generate_constant_value(char **source, Type type, ConstantValue value) {
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
            return generate_type(source, value.type);
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

struct ExpressionValue {
    Type type;

    bool is_constant;
    ConstantValue constant_value;
};

Result<ExpressionValue> generate_expression(GenerationContext *context, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            // TODO: Variable references

            auto result = resolve_constant_named_reference(context->constant_context, expression.named_reference, true);

            if(!result.status) {
                return { false };
            }

            auto generate_result = generate_constant_value(&(context->implementation_source), result.value.type, result.value.value);

            if(!generate_result) {
                return { false };
            }

            ExpressionValue value;
            value.type = result.value.type;
            value.is_constant = true;
            value.constant_value = result.value.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IntegerLiteral: {
            char buffer[64];

            sprintf(buffer, "%dll", expression.integer_literal);

            string_buffer_append(&(context->implementation_source), buffer);

            ExpressionValue value;
            value.type.category = TypeCategory::Integer;
            value.type.integer = {
                true,
                IntegerSize::Bit64
            };
            value.is_constant = true;
            value.constant_value.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            auto result = generate_expression(context, *expression.function_call.expression);

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Function) {
                fprintf(stderr, "Cannot call a non-function\n");

                return { false };
            }

            string_buffer_append(&(context->implementation_source), "(");

            if(expression.function_call.parameters.count != result.value.type.function.parameters.count) {
                fprintf(stderr, "Incorrect number of parameters. Expected %d, got %d\n", result.value.type.function.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            for(auto i = 0; i < result.value.type.function.parameters.count; i += 1) {
                auto parameter_result = generate_expression(context, expression.function_call.parameters[i]);

                if(!parameter_result.status) {
                    return { false };
                }

                if(!types_equal(parameter_result.value.type, result.value.type.function.parameters[i])) {
                    fprintf(stderr, "Incorrect parameter type for parameter %d\n", i);

                    return { false };
                }

                if(i != result.value.type.function.parameters.count - 1) {
                    string_buffer_append(&(context->implementation_source), ",");
                }
            }

            string_buffer_append(&(context->implementation_source), ")");

            ExpressionValue value;
            value.type = *result.value.type.function.return_type;
            value.is_constant = false;

            return { 
                true,
                value
            };
        } break;

        case ExpressionType::Pointer: {
            auto result = generate_expression(context, *expression.pointer);

            if(!result.status) {
                return { false };
            }

            if(result.value.is_constant) {
                if(result.value.type.category != TypeCategory::Type) {
                    fprintf(stderr, "Cannot take pointers to constants\n");

                    return { false };
                }

                auto pointer_type = (Type*)malloc(sizeof(Type));
                *pointer_type = result.value.constant_value.type;

                ExpressionValue value;
                value.type.category = TypeCategory::Type;
                value.is_constant = true;
                value.constant_value.type.category = TypeCategory::Pointer;
                value.constant_value.type.pointer = pointer_type;

                return {
                    true,
                    value
                };
            } else {
                fprintf(stderr, "Taking pointers to runtime values not yet implemented\n");

                return { false };
            }
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

            if(!result.status) {
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

bool generate_function_signature(char **source, Declaration declaration) {
    assert(declaration.category == DeclarationCategory::FunctionDefinition);

    generate_type(source, *declaration.type.function.return_type);

    string_buffer_append(source, " ");
    string_buffer_append(source, declaration.function_definition.mangled_name);
    string_buffer_append(source, "(");
    
    for(auto i = 0; i < declaration.type.function.parameters.count; i += 1) {
        auto result = generate_type(source, declaration.type.function.parameters[i]);

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

bool generate_declaration(GenerationContext *context, Declaration declaration) {
    assert(declaration.type_resolved);

    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            if(!generate_function_signature(&(context->forward_declaration_source), declaration)) {
                return false;
            }

            string_buffer_append(&(context->forward_declaration_source), ";");

            append(&(context->constant_context.declaration_stack), declaration);

            for(auto child_declaration : declaration.function_definition.declarations) {
                if(!generate_declaration(context, child_declaration)) {
                    return false;
                }
            }

            if(!generate_function_signature(&(context->implementation_source), declaration)) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), "{");

            for(auto statement : declaration.function_definition.statements) {
                if(!generate_statement(context, statement)) {
                    return false;
                }
            }

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
            fprintf(stderr, "Only declarations are allowed in global scope\n");

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

    ConstantContext constant_context{};

    constant_context.global_constants = to_array(global_constants);

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

    GenerationContext context {
        nullptr,
        nullptr,
        constant_context
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