#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
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

template <typename T>
static void error(T node, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "%s(%d:%d): ", node.source_file_path, node.line, node.character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    va_end(arguments);
}

enum struct DeclarationCategory {
    FunctionDefinition,
    ExternalFunction,
    ConstantDefinition
};

struct Declaration {
    DeclarationCategory category;

    Identifier name;

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

        struct {
            Array<FunctionParameter> parameters;

            bool has_return_type;
            Expression return_type;
        } external_function;

        Expression constant_definition;
    };
};

static Array<Declaration> get_declaration_children(Declaration declaration) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            return declaration.function_definition.declarations;
        } break;

        case DeclarationCategory::ExternalFunction: {
            return Array<Declaration>{};
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
            if(child.type_resolved && strcmp(child.name.text, name) == 0) {
                return {
                    true,
                    child
                };
            }
        }
    }

    for(auto declaration : top_level_declarations) {
        if(strcmp(declaration.name.text, name) == 0) {
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

    union {
        int64_t undetermined;

        uint8_t unsigned_8;
        uint16_t unsigned_16;
        uint32_t unsigned_32;
        uint64_t unsigned_64;

        int8_t signed_8;
        int16_t signed_16;
        int32_t signed_32;
        int64_t signed_64;
    } integer;

    Type type;

    Array<ConstantValue> array;
};

static bool constant_values_deep_equal(Type type, ConstantValue a, ConstantValue b) {
    switch(type.category) {
        case TypeCategory::Function: {
            return strcmp(a.function, b.function) == 0;
        } break;

        case TypeCategory::Integer: {
            switch(type.integer) {
                case IntegerType::Unsigned8: {
                    return a.integer.unsigned_8 == b.integer.unsigned_8;
                } break;

                case IntegerType::Unsigned16: {
                    return a.integer.unsigned_16 == b.integer.unsigned_16;
                } break;

                case IntegerType::Unsigned32: {
                    return a.integer.unsigned_32 == b.integer.unsigned_32;
                } break;

                case IntegerType::Unsigned64: {
                    return a.integer.unsigned_64 == b.integer.unsigned_64;
                } break;
                
                case IntegerType::Signed8: {
                    return a.integer.signed_8 == b.integer.signed_8;
                } break;

                case IntegerType::Signed16: {
                    return a.integer.signed_16 == b.integer.signed_16;
                } break;

                case IntegerType::Signed32: {
                    return a.integer.signed_32 == b.integer.signed_32;
                } break;

                case IntegerType::Signed64: {
                    return a.integer.signed_64 == b.integer.signed_64;
                } break;

                case IntegerType::Undetermined:
                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::Type: {
            return types_equal(a.type, b.type);
        } break;

        case TypeCategory::Array: {
            if(a.array.count != b.array.count) {
                return false;
            }

            for(size_t i = 0; i < a.array.count; i += 1) {
                if(!constant_values_deep_equal(*type.array, a.array[i], b.array[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeCategory::Void:
        case TypeCategory::Pointer:
        default: {
            abort();
        } break;
    }
}

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

static Result<ConstantExpressionValue> resolve_constant_named_reference(ConstantContext context, Identifier name, bool print_errors) {
    auto result = lookup_declaration(context.top_level_declarations, to_array(context.declaration_stack), name.text);

    if(!result.status) {
        for(auto global_constant : context.global_constants) {
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

        if(print_errors) {
            error(name, "Cannot find named reference %s", name.text);
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

        case DeclarationCategory::ExternalFunction: {
            ConstantValue value;
            value.function = result.value.name.text;

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

static Result<ConstantValue> evaluate_constant_array_index(Array<ConstantValue> array, IntegerType index_type, ConstantValue index_value, bool print_errors) {
    size_t index;
    switch(index_type) {
        case IntegerType::Undetermined: {
            if(index_value.integer.undetermined < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index %lld out of bounds", index_value.integer.undetermined);
                }

                return { false };
            }

            index = (size_t)index_value.integer.undetermined;
        } break;
        
        case IntegerType::Unsigned8: {
            index = (size_t)index_value.integer.unsigned_8;
        } break;
        
        case IntegerType::Unsigned16: {
            index = (size_t)index_value.integer.unsigned_16;
        } break;
        
        case IntegerType::Unsigned32: {
            index = (size_t)index_value.integer.unsigned_32;
        } break;
        
        case IntegerType::Unsigned64: {
            index = (size_t)index_value.integer.unsigned_64;
        } break;
        
        case IntegerType::Signed8: {
            if(index_value.integer.signed_8 < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index %hhd out of bounds", index_value.integer.signed_8);
                }

                return { false };
            }

            index = (size_t)index_value.integer.signed_8;
        } break;
        
        case IntegerType::Signed16: {
            if(index_value.integer.signed_16 < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index %hd out of bounds", index_value.integer.signed_16);
                }

                return { false };
            }

            index = (size_t)index_value.integer.signed_16;
        } break;
        
        case IntegerType::Signed32: {
            if(index_value.integer.signed_32 < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index %d out of bounds", index_value.integer.signed_32);
                }

                return { false };
            }

            index = (size_t)index_value.integer.signed_32;
        } break;
        
        case IntegerType::Signed64: {
            if(index_value.integer.signed_64 < 0) {
                if(print_errors) {
                    fprintf(stderr, "Array index %lld out of bounds", index_value.integer.signed_64);
                }

                return { false };
            }

            index = (size_t)index_value.integer.signed_64;
        } break;

        default: {
            abort();
        } break;
    }

    if(index >= array.count) {
        if(print_errors) {
            fprintf(stderr, "Array index %llu out of bounds", index);
        }

        return { false };
    }

    return {
        true,
        array[index]
    };
}

static Result<Type> evaluate_type_expression(ConstantContext context, Expression expression, bool print_errors);

static Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(context, expression.named_reference, print_errors);
        } break;

        case ExpressionType::MemberReference: {
            auto result = evaluate_constant_expression(context, *expression.member_reference.expression, print_errors);

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Array) {
                if(print_errors) {
                    error(*expression.member_reference.expression, "This type has no members");
                }

                return { false };
            }

            if(strcmp(expression.member_reference.name.text, "length") == 0) {
                Type type;
                type.category = TypeCategory::Integer;
                type.integer = IntegerType::Unsigned64;

                ConstantValue value;
                value.integer.unsigned_64 = result.value.value.array.count;

                return {
                    true,
                    {
                        type,
                        value
                    }
                };
            } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                if(print_errors) {
                    error(expression.member_reference.name, "Cannot access array pointer in constant context");
                }

                return { false };
            } else {
                if(print_errors) {
                    error(expression.member_reference.name, "No member with name %s", expression.member_reference.name.text);
                }

                return { false };
            }
        } break;

        case ExpressionType::IndexReference: {
            auto expression_result = evaluate_constant_expression(context, *expression.index_reference.expression, print_errors);

            if(!expression_result.status) {
                return { false };
            }

            if(expression_result.value.type.category != TypeCategory::Array) {
                if(print_errors) {
                    error(*expression.index_reference.expression, "Cannot index a non-array");
                }

                return { false };
            }

            auto index_result = evaluate_constant_expression(context, *expression.index_reference.index, print_errors);

            if(!index_result.status) {
                return { false };
            }

            if(index_result.value.type.category != TypeCategory::Integer) {
                if(print_errors) {
                    error(*expression.index_reference.index, "Array index not an integer");
                }

                return { false };
            }

            auto result = evaluate_constant_array_index(expression_result.value.value.array, index_result.value.type.integer, index_result.value.value, print_errors);

            if(!result.status) {
                return { false };
            }

            return {
                true,
                {
                    *expression_result.value.type.array,
                    result.value
                }
            };
        } break;

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer = IntegerType::Undetermined;

            ConstantValue value;
            value.integer.undetermined = expression.integer_literal;

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
            array_type->integer = IntegerType::Unsigned8;

            Type type;
            type.category = TypeCategory::Array;
            type.array = array_type;

            auto characters = (ConstantValue*)malloc(sizeof(ConstantValue) * expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer.unsigned_8 = expression.string_literal[i];
            }

            ConstantValue value;
            value.array = {
                expression.string_literal.count,
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
                error(expression, "Function calls not allowed in global context");
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
                    error(*expression.pointer, "Cannot take pointers to constants");
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
            error(expression, "Value is not a type");
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
        case StatementType::FunctionDeclaration: {
            if(statement.function_declaration.is_external) {
                Declaration declaration;
                declaration.category = DeclarationCategory::ExternalFunction;
                declaration.name = statement.function_declaration.name;
                declaration.type_resolved = false;

                declaration.external_function = {
                    statement.function_declaration.parameters,
                    statement.function_declaration.has_return_type,
                    statement.function_declaration.return_type
                };

                return {
                    true,
                    declaration
                };
            } else {
                List<Declaration> child_declarations{};
                List<Statement> child_statements{};

                append(name_stack, statement.function_declaration.name.text);

                for(auto child_statement : statement.function_declaration.statements) {
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
                declaration.name = statement.function_declaration.name;
                declaration.type_resolved = false;

                declaration.function_definition = {
                    mangled_name,
                    statement.function_declaration.parameters,
                    statement.function_declaration.has_return_type,
                    statement.function_declaration.return_type,
                    to_array(child_declarations),
                    to_array(child_statements)
                };

                return {
                    true,
                    declaration
                };
            }
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
            if(sibling.type_resolved && strcmp(sibling.name.text, declaration.name.text) == 0) {
                if(print_errors) {
                    error(declaration.name, "Duplicate declaration name %s", declaration.name.text);
                    error(sibling.name, "Original declared here");
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

            case DeclarationCategory::ExternalFunction: {
                for(auto parameter : declaration.external_function.parameters) {
                    auto result = evaluate_type_expression(*context, parameter.type, print_errors);

                    if(!result.status) {
                        return { false };
                    }
                }

                Type return_type;
                if(declaration.external_function.has_return_type) {
                    auto result = evaluate_type_expression(*context, declaration.external_function.return_type, print_errors);

                    if(!result.status) {
                        return { false };
                    }

                    return_type = result.value;
                } else {
                    return_type.category = TypeCategory::Void;
                }

                auto parameters = (Type*)malloc(declaration.external_function.parameters.count * sizeof(Type));
                
                for(auto i = 0; i < declaration.external_function.parameters.count; i += 1) {
                    auto result = evaluate_type_expression(*context, declaration.external_function.parameters[i].type, print_errors);

                    parameters[i] = result.value;
                }

                auto return_type_heap = (Type*)malloc(sizeof(Type));
                *return_type_heap = return_type;

                Type type;
                type.category = TypeCategory::Function;
                type.function.parameters = {
                    declaration.external_function.parameters.count,
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
    Identifier name;

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

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    List<ArrayType> array_types;

    List<ArrayConstant> array_constants;
};

static bool register_global_name(GenerationContext *context, const char *name) {
    for(auto global_name : context->global_names) {
        if(strcmp(global_name, name) == 0) {
            fprintf(stderr, "Duplicate global name %s\n", name);

            return false;
        }
    }

    append(&(context->global_names), name);

    return true;
}

static bool add_new_variable(GenerationContext *context, Identifier name, Type type) {
    auto variable_context = &(context->variable_context_stack[context->variable_context_stack.count - 1]);

    for(auto variable : *variable_context) {
        if(strcmp(variable.name.text, name.text) == 0) {
            error(name, "Duplicate variable name %s", name.text);
            error(variable.name, "Original declared here");

            return false;
        }
    }

    append(variable_context, Variable{
        name,
        type
    });

    return true;
}

static Result<const char *> maybe_register_array_constant(GenerationContext *context, Type type, Array<ConstantValue> value) {
    for(auto i = 0; i < context->array_constants.count; i += 1) {
        auto array_constant = context->array_constants[i];

        if(!types_equal(array_constant.type, type)) {
            continue;
        }

        if(array_constant.elements.count != value.count) {
            continue;
        }

        auto equal = true;
        for(size_t j = 0; j < value.count; j += 1) {
            if(!constant_values_deep_equal(type, array_constant.elements[j], value[j])) {
                equal = false;

                break;
            }
        }

        if(equal) {
            char *mangled_name{};

            string_buffer_append(&mangled_name, "_array_constant_");

            char buffer[32];
            sprintf(buffer, "%d", i);

            string_buffer_append(&mangled_name, buffer);

            return {
                true,
                mangled_name
            };
        }
    }

    char *mangled_name{};

    string_buffer_append(&mangled_name, "_array_constant_");

    char buffer[32];
    sprintf(buffer, "%d", context->array_constants.count);

    string_buffer_append(&mangled_name, buffer);

    append(&(context->array_constants), ArrayConstant {
        type,
        value
    });

    return {
        true,
        mangled_name
    };
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
            assert(type.integer != IntegerType::Undetermined);
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

static void generate_integer_type(char **source, IntegerType type) {
    switch(type) {
        case IntegerType::Undetermined: {
            abort();
        } break;

        case IntegerType::Unsigned8: {
            string_buffer_append(source, "char");
        } break;

        case IntegerType::Unsigned16: {
            string_buffer_append(source, "unsigned short");
        } break;

        case IntegerType::Unsigned32: {
            string_buffer_append(source, "unsigned int");
        } break;

        case IntegerType::Unsigned64: {
            string_buffer_append(source, "unsigned long long");
        } break;

        case IntegerType::Signed8: {
            string_buffer_append(source, "signed char");
        } break;

        case IntegerType::Signed16: {
            string_buffer_append(source, "short");
        } break;

        case IntegerType::Signed32: {
            string_buffer_append(source, "int");
        } break;

        case IntegerType::Signed64: {
            string_buffer_append(source, "long long");
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_type(GenerationContext *context, char **source, Type type) {
    switch(type.category) {
        case TypeCategory::Function: {
            fprintf(stderr, "Function values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Integer: {
            generate_integer_type(source, type.integer);

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

            switch(type.integer) {
                case IntegerType::Undetermined: {
                    sprintf(buffer, "%lld", value.integer.undetermined);
                } break;

                case IntegerType::Unsigned8: {
                    sprintf(buffer, "%hhu", value.integer.unsigned_8);
                } break;

                case IntegerType::Unsigned16: {
                    sprintf(buffer, "%hu", value.integer.unsigned_16);
                } break;

                case IntegerType::Unsigned32: {
                    sprintf(buffer, "%u", value.integer.unsigned_32);
                } break;

                case IntegerType::Unsigned64: {
                    sprintf(buffer, "%llu", value.integer.unsigned_64);
                } break;

                case IntegerType::Signed8: {
                    sprintf(buffer, "%hhd", value.integer.signed_8);
                } break;

                case IntegerType::Signed16: {
                    sprintf(buffer, "%hd", value.integer.signed_16);
                } break;

                case IntegerType::Signed32: {
                    sprintf(buffer, "%d", value.integer.signed_32);
                } break;

                case IntegerType::Signed64: {
                    sprintf(buffer, "%lld", value.integer.signed_64);
                } break;

                default: {
                    abort();
                } break;
            }

            string_buffer_append(source, buffer);

            return true;
        } break;

        case TypeCategory::Type: {
            fprintf(stderr, "Type values cannot exist at runtime\n");

            return false;
        } break;

        case TypeCategory::Array: {
            auto type_result = maybe_register_array_type(context, *type.array);

            if(!type_result.status) {
                return false;
            }

            auto constant_result = maybe_register_array_constant(context, *type.array, value.array);

            if(!constant_result.status) {
                return false;
            }

            string_buffer_append(source, "(struct ");

            string_buffer_append(source, type_result.value);

            string_buffer_append(source, "){");

            char buffer[64];

            sprintf(buffer, "%d", value.array.count);

            string_buffer_append(source, buffer);

            string_buffer_append(source, ",");

            string_buffer_append(source, constant_result.value);

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

static Result<ExpressionValue> generate_expression(GenerationContext *context, char **source, Expression expression);

static Result<ExpressionValue> generate_runtime_expression(GenerationContext *context, char **source, Expression expression) {
    auto result = generate_expression(context, source, expression);

    if(!result.status) {
        return { false };
    }

    switch(result.value.category) {
        case ExpressionValueCategory::Anonymous:
        case ExpressionValueCategory::Assignable: {
            return {
                true,
                result.value
            };
        } break;

        case ExpressionValueCategory::Constant: {
            if(!generate_constant_value(context, source, result.value.type, result.value.constant)) {
                return { false };
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Anonymous;
            value.type = result.value.type;

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

static Result<ExpressionValue> generate_expression(GenerationContext *context, char **source, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(auto i = 0; i < context->variable_context_stack.count; i++) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Assignable;
                        value.type = variable.type;

                        string_buffer_append(source, variable.name.text);

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

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type = result.value.type;
            value.constant = result.value.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::MemberReference: {
            char *expression_source{};
            auto result = generate_expression(context, &expression_source, *expression.member_reference.expression);

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Array) {
                error(*expression.member_reference.expression, "This type has no members");

                return { false };
            }

            if(strcmp(expression.member_reference.name.text, "length") == 0) {
                switch(result.value.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, "(");

                        string_buffer_append(source, expression_source);

                        string_buffer_append(source, ").length");

                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Anonymous;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = IntegerType::Unsigned64;

                        return {
                            true,
                            value
                        };
                    } break;

                    case ExpressionValueCategory::Constant: {
                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Constant;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = IntegerType::Unsigned64;
                        value.constant.integer.unsigned_64 = result.value.constant.array.count;

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
                switch(result.value.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, "(");

                        string_buffer_append(source, expression_source);

                        string_buffer_append(source, ").pointer");

                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Anonymous;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = result.value.type.array;

                        return {
                            true,
                            value
                        };
                    } break;

                    case ExpressionValueCategory::Constant: {
                        auto constant_result = maybe_register_array_constant(context, *result.value.type.array, result.value.constant.array);

                        if(!constant_result.status) {
                            return { false };
                        }

                        string_buffer_append(source, constant_result.value);

                        ExpressionValue value;
                        value.category = ExpressionValueCategory::Anonymous;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = result.value.type.array;

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
                error(expression.member_reference.name, "No member with name %s", expression.member_reference.name.text);

                return { false };
            }
        } break;

        case ExpressionType::IndexReference: {
            char *expression_source{};
            auto result = generate_expression(context, &expression_source, *expression.index_reference.expression);
            
            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Array) {
                error(*expression.index_reference.expression, "Cannot index a non-array");

                return { false };
            }

            switch(result.value.category) {
                case ExpressionValueCategory::Anonymous:
                case ExpressionValueCategory::Assignable: {
                    string_buffer_append(source, "(");

                    string_buffer_append(source, expression_source);

                    string_buffer_append(source, ")");

                    string_buffer_append(source, "[");

                    auto index_result = generate_runtime_expression(context, source, *expression.index_reference.index);

                    if(!index_result.status) {
                        return { false };
                    }

                    if(index_result.value.type.category != TypeCategory::Integer) {
                        error(*expression.index_reference.index, "Array index not an integer");

                        return { false };
                    }

                    string_buffer_append(source, "]");
                    
                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Assignable;
                    value.type = *result.value.type.array;

                    return {
                        true,
                        value
                    };
                } break;

                case ExpressionValueCategory::Constant: {
                    char *index_source{};
                    auto index_result = generate_expression(context, &index_source, *expression.index_reference.index);

                    if(!index_result.status) {
                        return { false };
                    }
                    
                    if(index_result.value.type.category != TypeCategory::Integer) {
                        error(*expression.index_reference.index, "Array index not an integer");

                        return { false };
                    }

                    switch(index_result.value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            string_buffer_append(source, "[");

                            string_buffer_append(source, index_source);

                            string_buffer_append(source, "]");
                            
                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Assignable;
                            value.type = *result.value.type.array;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            auto array_value = evaluate_constant_array_index(result.value.constant.array, index_result.value.type.integer, index_result.value.constant, true);

                            if(!array_value.status) {
                                return { false };
                            }
                            
                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Constant;
                            value.type = *result.value.type.array;
                            value.constant = array_value.value;

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

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::IntegerLiteral: {
            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Integer;
            value.type.integer = IntegerType::Undetermined;
            value.constant.integer.undetermined = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StringLiteral: {            
            auto array_type = (Type*)malloc(sizeof(Type));
            array_type->category = TypeCategory::Integer;
            array_type->integer = IntegerType::Unsigned8;

            auto characters = (ConstantValue*)malloc(sizeof(ConstantValue) * expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer.unsigned_8 = expression.string_literal[i];
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Array;
            value.type.array = array_type;
            value.constant.array = {
                expression.string_literal.count,
                characters
            };

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            string_buffer_append(source, "(");

            auto result = generate_runtime_expression(context, source, *expression.function_call.expression);

            string_buffer_append(source, ")");

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Function) {
                error(*expression.function_call.expression, "Cannot call a non-function");

                return { false };
            }

            string_buffer_append(source, "(");

            if(expression.function_call.parameters.count != result.value.type.function.parameters.count) {
                error(expression, "Incorrect number of parameters. Expected %d, got %d", result.value.type.function.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            for(auto i = 0; i < result.value.type.function.parameters.count; i += 1) {
                char *parameter_source{};
                auto parameter_result = generate_runtime_expression(context, &parameter_source, expression.function_call.parameters[i]);

                if(!parameter_result.status) {
                    return { false };
                }

                if(
                    parameter_result.value.type.category == TypeCategory::Integer &&
                    result.value.type.function.parameters[i].category == TypeCategory::Integer &&
                    parameter_result.value.type.integer == IntegerType::Undetermined
                ) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, result.value.type.function.parameters[i].integer);

                    string_buffer_append(source, ")");
                } else if(!types_equal(parameter_result.value.type, result.value.type.function.parameters[i])) {
                    error(expression.function_call.parameters[i], "Incorrect parameter type for parameter %d", i);

                    return { false };
                }

                string_buffer_append(source, "(");

                string_buffer_append(source, parameter_source);

                string_buffer_append(source, ")");

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
                    error(*expression.pointer, "Cannot take pointers anonymous values");

                    return { false };
                } break;

                case ExpressionValueCategory::Constant: {
                    if(result.value.type.category != TypeCategory::Type) {
                        error(*expression.pointer, "Cannot take pointers to constants");

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
            switch(statement.variable_declaration.type) {
                case VariableDeclarationType::Uninitialized: {
                    auto result = evaluate_type_expression(context->constant_context, statement.variable_declaration.uninitialized, true);

                    if(!result.status) {
                        return { false };
                    }

                    if(!generate_type(context, &(context->implementation_source), result.value)) {
                        return { false };
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);

                    string_buffer_append(&(context->implementation_source), ";");

                    return true;
                } break;
                
                case VariableDeclarationType::TypeElided: {
                    char *initializer_source{};
                    auto result = generate_runtime_expression(context, &initializer_source, statement.variable_declaration.type_elided);

                    if(!result.status) {
                        return { false };
                    }

                    Type actual_type;
                    if(result.value.type.category == TypeCategory::Integer && result.value.type.integer == IntegerType::Undetermined) {
                        actual_type.category = TypeCategory::Integer;
                        actual_type.integer = IntegerType::Signed64;
                    } else {
                        actual_type = result.value.type;
                    }
                    
                    if(!generate_type(context, &(context->implementation_source), actual_type)) {
                        return { false };
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);
                    
                    string_buffer_append(&(context->implementation_source), "=");

                    string_buffer_append(&(context->implementation_source), initializer_source);

                    string_buffer_append(&(context->implementation_source), ";");

                    return true;
                } break;
                
                case VariableDeclarationType::FullySpecified: {
                    auto type_result = evaluate_type_expression(context->constant_context, statement.variable_declaration.fully_specified.type, true);

                    if(!type_result.status) {
                        return { false };
                    }

                    char *initializer_source{};
                    auto initializer_result = generate_runtime_expression(context, &initializer_source, statement.variable_declaration.fully_specified.initializer);

                    if(!initializer_result.status) {
                        return { false };
                    }

                    Type actual_type;
                    if(
                        type_result.value.category == TypeCategory::Integer &&
                        initializer_result.value.type.category == TypeCategory::Integer &&
                        initializer_result.value.type.integer == IntegerType::Undetermined
                    ) {
                        actual_type.category = TypeCategory::Integer;
                        actual_type.integer = IntegerType::Signed64;
                    } else {
                        if(!types_equal(type_result.value, initializer_result.value.type)) {
                            error(statement.variable_declaration.fully_specified.initializer, "Initializer type does not match variable type");

                            return { false };
                        }

                        actual_type = type_result.value;
                    }

                    if(!generate_type(context, &(context->implementation_source), actual_type)) {
                        return { false };
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);
                    
                    string_buffer_append(&(context->implementation_source), "=");

                    string_buffer_append(&(context->implementation_source), initializer_source);

                    string_buffer_append(&(context->implementation_source), ";");

                    return true;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case StatementType::Assignment: {
            auto target_result = generate_expression(context, &(context->implementation_source), statement.assignment.target);

            if(!target_result.status) {
                return false;
            }

            if(target_result.value.category != ExpressionValueCategory::Assignable) {
                error(statement.assignment.target, "Value is not assignable");

                return false;
            }

            string_buffer_append(&(context->implementation_source), "=");

            auto value_result = generate_runtime_expression(context, &(context->implementation_source), statement.assignment.value);

            if(!value_result.status) {
                return false;
            }
            
            if(
                !(
                    target_result.value.type.category == TypeCategory::Integer &&
                    value_result.value.type.category == TypeCategory::Integer &&
                    value_result.value.type.integer == IntegerType::Undetermined
                ) &&
                !types_equal(target_result.value.type, value_result.value.type)
            ) {
                error(statement.assignment.value, "Assigning incorrect type");

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

static bool generate_function_signature(GenerationContext *context, char **source, const char *name, Type type, FunctionParameter *parameters) {
    generate_type(context, source, *type.function.return_type);

    string_buffer_append(source, " ");
    string_buffer_append(source, name);
    string_buffer_append(source, "(");
    
    for(auto i = 0; i < type.function.parameters.count; i += 1) {
        auto result = generate_type(context, source, type.function.parameters[i]);

        if(!result) {
            return false;
        }

        string_buffer_append(source, " ");

        string_buffer_append(source, parameters[i].name.text);

        if(i != type.function.parameters.count - 1) {
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

            if(!register_global_name(context, declaration.function_definition.mangled_name)) {
                return false;
            }

            if(!generate_function_signature(
                context,
                &(context->forward_declaration_source),
                declaration.function_definition.mangled_name,
                declaration.type,
                declaration.function_definition.parameters.elements
            )){
                return false;
            }

            string_buffer_append(&(context->forward_declaration_source), ";");

            append(&(context->constant_context.declaration_stack), declaration);

            for(auto child_declaration : declaration.function_definition.declarations) {
                if(!generate_declaration(context, child_declaration)) {
                    return false;
                }
            }

            if(!generate_function_signature(
                context,
                &(context->implementation_source),
                declaration.function_definition.mangled_name,
                declaration.type,
                declaration.function_definition.parameters.elements
            )){
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
        
        case DeclarationCategory::ExternalFunction: {
            assert(declaration.type.category == TypeCategory::Function);

            if(!register_global_name(context, declaration.name.text)) {
                return false;
            }

            if(!generate_function_signature(
                context,
                &(context->forward_declaration_source),
                declaration.name.text,
                declaration.type,
                declaration.external_function.parameters.elements
            )){
                return false;
            }

            string_buffer_append(&(context->forward_declaration_source), ";");

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

inline GlobalConstant create_base_integer_type(const char *name, IntegerType integer_type) {
    Type type;
    type.category = TypeCategory::Type;

    ConstantValue value;
    value.type.category = TypeCategory::Integer;
    value.type.integer = integer_type;

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
            error(top_level_statement, "Only constant declarations are allowed in global scope");

            return { false };
        }
    }

    auto previous_resolved_declaration_count = 0;

    List<GlobalConstant> global_constants{};

    append(&global_constants, create_base_integer_type("u8", IntegerType::Unsigned8));
    append(&global_constants, create_base_integer_type("u16", IntegerType::Unsigned16));
    append(&global_constants, create_base_integer_type("u32", IntegerType::Unsigned32));
    append(&global_constants, create_base_integer_type("u64", IntegerType::Unsigned64));

    append(&global_constants, create_base_integer_type("i8", IntegerType::Signed8));
    append(&global_constants, create_base_integer_type("i16", IntegerType::Signed16));
    append(&global_constants, create_base_integer_type("i32", IntegerType::Signed32));
    append(&global_constants, create_base_integer_type("i64", IntegerType::Signed64));

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

        string_buffer_append(&full_source, " *pointer;};");
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