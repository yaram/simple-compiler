#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "list.h"
#include "types.h"
#include "util.h"
#include "path.h"

static void error(FilePosition position, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "%s(%u:%u): ", position.path, position.line, position.character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    va_end(arguments);
}

enum struct DeclarationCategory {
    FunctionDefinition,
    ExternalFunction,
    ConstantDefinition,
    StructDefinition,
    FileModuleImport,
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

        struct {
            const char *mangled_name;

            Array<StructMember> members;
        } struct_definition;

        const char *file_module_import;
    };
};

static Array<Declaration> get_declaration_children(Declaration declaration) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            return declaration.function_definition.declarations;
        } break;

        case DeclarationCategory::ExternalFunction:
        case DeclarationCategory::ConstantDefinition:
        case DeclarationCategory::StructDefinition:
        case DeclarationCategory::FileModuleImport: {
            return Array<Declaration>{};
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<Declaration> lookup_declaration(Array<Declaration> top_level_declarations, Array<Declaration> declaration_stack, const char *name) {
    for(size_t i = 0; i < declaration_stack.count; i += 1) {
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

    uint64_t integer;

    bool boolean;

    Type type;

    Array<ConstantValue> array;

    ConstantValue *static_array;

    ConstantValue *_struct;

    Array<Declaration> file_module;
};

struct FileModule {
    const char *path;

    Array<Declaration> declarations;

    Array<Statement> compiler_directives;
};

struct GlobalConstant {
    const char *name;

    Type type;

    ConstantValue value;
};

struct StructTypeMember {
    Identifier name;

    Type type;
};

struct StructType {
    const char *mangled_name;

    Array<StructTypeMember> members;
};

struct ConstantContext {
    IntegerType unsigned_size_integer_type;
    IntegerType signed_size_integer_type;
    IntegerType default_integer_type;

    Array<GlobalConstant> global_constants;

    List<StructType> *struct_types;

    Array<Declaration> top_level_declarations;
    Array<FileModule> file_modules;

    List<Declaration> declaration_stack;
};

static StructType retrieve_struct_type(ConstantContext context, const char *mangled_name) {
    for(auto struct_type : *context.struct_types) {
        if(strcmp(struct_type.mangled_name, mangled_name) == 0) {
            return struct_type;
        }
    }

    abort();
}

static bool constant_values_deep_equal(ConstantContext context, Type type, ConstantValue a, ConstantValue b) {
    switch(type.category) {
        case TypeCategory::Function: {
            return strcmp(a.function, b.function) == 0;
        } break;

        case TypeCategory::Integer: {
            switch(type.integer) {
                case IntegerType::Unsigned8: {
                    return (uint8_t)a.integer == (uint8_t)b.integer;
                } break;

                case IntegerType::Unsigned16: {
                    return (uint16_t)a.integer == (uint16_t)b.integer;
                } break;

                case IntegerType::Unsigned32: {
                    return (uint32_t)a.integer == (uint32_t)b.integer;
                } break;

                case IntegerType::Unsigned64: {
                    return a.integer == b.integer;
                } break;

                case IntegerType::Signed8: {
                    return (int8_t)a.integer == (int8_t)b.integer;
                } break;

                case IntegerType::Signed16: {
                    return (int16_t)a.integer == (int16_t)b.integer;
                } break;

                case IntegerType::Signed32: {
                    return (int32_t)a.integer == (int32_t)b.integer;
                } break;

                case IntegerType::Signed64: {
                    return (int64_t)a.integer == (int64_t)b.integer;
                } break;

                case IntegerType::Undetermined:
                default: {
                    abort();
                } break;
            }
        } break;

        case TypeCategory::Boolean: {
            return a.boolean == b.boolean;
        } break;

        case TypeCategory::Type: {
            return types_equal(a.type, b.type);
        } break;

        case TypeCategory::Array: {
            if(a.array.count != b.array.count) {
                return false;
            }

            for(size_t i = 0; i < a.array.count; i += 1) {
                if(!constant_values_deep_equal(context, *type.array, a.array[i], b.array[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeCategory::StaticArray: {
            if(a.type.static_array.length != b.type.static_array.length) {
                return false;
            }

            for(size_t i = 0; i < a.type.static_array.length; i += 1) {
                if(!constant_values_deep_equal(context, *type.array, a.static_array[i], b.static_array[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeCategory::Struct: {
            auto struct_type = retrieve_struct_type(context, type._struct);

            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                if(!constant_values_deep_equal(context, struct_type.members[i].type, a._struct[i], b._struct[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeCategory::Void:
        case TypeCategory::Pointer:
        case TypeCategory::FileModule:
        default: {
            abort();
        } break;
    }
}

struct ConstantExpressionValue {
    Type type;

    ConstantValue value;
};

static ConstantValue compiler_size_to_native_size(ConstantContext context, size_t size) {
    ConstantValue value;

    switch(context.unsigned_size_integer_type) {
        case IntegerType::Unsigned8: {
            value.integer = (uint8_t)size;
        } break;
        
        case IntegerType::Unsigned16: {
            value.integer = (uint16_t)size;
        } break;
        
        case IntegerType::Unsigned32: {
            value.integer = (uint32_t)size;
        } break;
        
        case IntegerType::Unsigned64: {
            value.integer = (uint64_t)size;
        } break;

        case IntegerType::Undetermined:
        case IntegerType::Signed8:
        case IntegerType::Signed16:
        case IntegerType::Signed32:
        case IntegerType::Signed64:
        default: {
            abort();
        } break;
    }

    return value;
}

static Result<ConstantExpressionValue> evaluate_constant_expression(ConstantContext context, Expression expression, bool print_errors);

static Result<ConstantExpressionValue> evaluate_constant_declaration(ConstantContext context, Declaration declaration, bool print_errors) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            ConstantValue value;
            value.function = declaration.function_definition.mangled_name;

            return {
                true,
                {
                    declaration.type,
                    value
                }
            };
        } break;

        case DeclarationCategory::ExternalFunction: {
            ConstantValue value;
            value.function = declaration.name.text;

            return {
                true,
                {
                    declaration.type,
                    value
                }
            };
        } break;

        case DeclarationCategory::ConstantDefinition: {
            auto expression_result = evaluate_constant_expression(context, declaration.constant_definition, print_errors);

            if(!expression_result.status) {
                return { false };
            }

            return {
                true,
                expression_result.value
            };
        } break;

        case DeclarationCategory::StructDefinition: {
            Type type;
            type.category = TypeCategory::Type;

            ConstantValue value;
            value.type.category = TypeCategory::Struct;
            value.type._struct = declaration.struct_definition.mangled_name;

            return{
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case DeclarationCategory::FileModuleImport: {
            Type type;
            type.category = TypeCategory::FileModule;

            ConstantValue value;

            for(auto file_module : context.file_modules) {
                if(strcmp(declaration.file_module_import, file_module.path) == 0) {
                    value.file_module = file_module.declarations;
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
            error(name.position, "Cannot find named reference %s", name.text);
        }

        return { false };
    }

    return evaluate_constant_declaration(context, result.value, print_errors);
}

static Result<ConstantExpressionValue> evaluate_constant_index(Type type, ConstantValue value, FilePosition position, IntegerType index_type, ConstantValue index_value, FilePosition index_position, bool print_errors) {
    size_t index;
    switch(index_type) {
        case IntegerType::Undetermined: {
            if((int64_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_position, "Array index %lld out of bounds", (int64_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int64_t)index_value.integer;
        } break;
        
        case IntegerType::Unsigned8: {
            index = (size_t)(uint8_t)index_value.integer;
        } break;
        
        case IntegerType::Unsigned16: {
            index = (size_t)(uint16_t)index_value.integer;
        } break;
        
        case IntegerType::Unsigned32: {
            index = (size_t)(uint32_t)index_value.integer;
        } break;
        
        case IntegerType::Unsigned64: {
            index = (size_t)(uint64_t)index_value.integer;
        } break;
        
        case IntegerType::Signed8: {
            if((int8_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_position, "Array index %hhd out of bounds", (int8_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int8_t)index_value.integer;
        } break;
        
        case IntegerType::Signed16: {
            if((int16_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_position, "Array index %hd out of bounds", (int16_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int16_t)index_value.integer;
        } break;
        
        case IntegerType::Signed32: {
            if((int32_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_position, "Array index %d out of bounds", (int32_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int32_t)index_value.integer;
        } break;
        
        case IntegerType::Signed64: {
            if((int64_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_position, "Array index %lld out of bounds", (int64_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int64_t)index_value.integer;
        } break;

        default: {
            abort();
        } break;
    }

    switch(type.category) {
        case TypeCategory::Function:
        case TypeCategory::Integer:
        case TypeCategory::Boolean:
        case TypeCategory::Void:
        case TypeCategory::Pointer:
        case TypeCategory::Struct:
        case TypeCategory::FileModule: {
            if(print_errors) {
                error(position, "Cannot index a non-array");
            }

            return { false };
        } break;

        case TypeCategory::Type: {
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

        case TypeCategory::Array: {
            if(index >= value.array.count) {
                if(print_errors) {
                    error(index_position, "Array index %zu out of bounds", index);
                }

                return { false };
            }

            return {
                true,
                {
                    *type.array,
                    value.array[index]
                }
            };
        } break;

        case TypeCategory::StaticArray: {
            if(index >= type.static_array.length) {
                if(print_errors) {
                    error(index_position, "Array index %zu out of bounds", index);
                }

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
            abort();
        } break;
    }
}

template <typename T>
static ConstantExpressionValue perform_constant_integer_binary_operation(BinaryOperator binary_operator, IntegerType type, IntegerType left_type, ConstantValue left_value, IntegerType right_type, ConstantValue right_value) {
    T left;
    if(left_type == IntegerType::Undetermined) {
        left = (T)(int64_t)left_value.integer;
    } else {
        left = (T)left_value.integer;
    }

    T right;
    if(right_type == IntegerType::Undetermined) {
        right = (T)(int64_t)right_value.integer;
    } else {
        right = (T)right_value.integer;
    }

    Type result_type;
    ConstantValue result_value;
    switch(binary_operator) {
        case BinaryOperator::Addition: {
            result_value.integer = (uint64_t)(left + right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::Subtraction: {
            result_value.integer = (uint64_t)(left - right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::Multiplication: {
            result_value.integer = (uint64_t)(left * right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::Division: {
            result_value.integer = (uint64_t)(left / right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::Modulo: {
            result_value.integer = (uint64_t)(left % right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::Equal: {
            result_value.boolean = left == right;

            result_type.category = TypeCategory::Boolean;
        } break;
        
        case BinaryOperator::NotEqual: {
            result_value.boolean = left != right;

            result_type.category = TypeCategory::Boolean;
        } break;

        default: {
            abort();
        } break;
    }

    return {
        result_type,
        result_value
    };
}

static Result<ConstantExpressionValue> evaluate_constant_integer_binary_operation(BinaryOperator binary_operator, FilePosition position, IntegerType left_type, ConstantValue left_value, IntegerType right_type, ConstantValue right_value, bool print_errors) {
    ConstantExpressionValue result;

    if(left_type == IntegerType::Undetermined && right_type == IntegerType::Undetermined) {
        result = perform_constant_integer_binary_operation<int64_t>(
            binary_operator,
            IntegerType::Undetermined,
            left_type,
            left_value,
            right_type,
            right_value
        );
    } else {
        IntegerType type;
        if(left_type != IntegerType::Undetermined) {
            type = left_type;
        } else if(right_type != IntegerType::Undetermined) {
            type = right_type;
        } else {
            if(left_type != right_type) {
                if(print_errors) {
                    error(position, "Mismatched types for binary operation");
                }

                return { false };
            }

            type = left_type;
        }

        ConstantExpressionValue result;
        switch(type) {
            case IntegerType::Unsigned8: {
                result = perform_constant_integer_binary_operation<uint8_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Unsigned16: {
                result = perform_constant_integer_binary_operation<uint16_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Unsigned32: {
                result = perform_constant_integer_binary_operation<uint32_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Unsigned64: {
                result = perform_constant_integer_binary_operation<uint64_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Signed8: {
                result = perform_constant_integer_binary_operation<int8_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Signed16: {
                result = perform_constant_integer_binary_operation<int16_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Signed32: {
                result = perform_constant_integer_binary_operation<int32_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Signed64: {
                result = perform_constant_integer_binary_operation<int64_t>(
                    binary_operator,
                    type,
                    left_type,
                    left_value,
                    right_type,
                    right_value
                );
            } break;

            case IntegerType::Undetermined:
            default: {
                abort();
            } break;
        }
    }

    return {
        true,
        result
    };
}

static ConstantValue determine_constant_integer(IntegerType target_type, ConstantValue undetermined_value) {
    ConstantValue value;

    switch(target_type) {
        case IntegerType::Unsigned8: {
            value.integer = (uint8_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Unsigned16: {
            value.integer = (uint16_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Unsigned32: {
            value.integer = (uint32_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Unsigned64: {
            value.integer = (uint64_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Signed8: {
            value.integer = (int8_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Signed16: {
            value.integer = (int16_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Signed32: {
            value.integer = (int32_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Signed64: {
            value.integer = (int64_t)(int64_t)undetermined_value.integer;
        } break;

        case IntegerType::Undetermined:
        default: {
            abort();
        }
    }

    return value;
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

            switch(result.value.type.category) {
                case TypeCategory::Function:
                case TypeCategory::Integer:
                case TypeCategory::Boolean:
                case TypeCategory::Type:
                case TypeCategory::Void:
                case TypeCategory::Pointer:
                case TypeCategory::StaticArray: {
                    if(print_errors) {
                        error(expression.member_reference.expression->position, "This type has no members");
                    }

                    return { false };
                } break;

                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        Type type;
                        type.category = TypeCategory::Integer;
                        type.integer = context.unsigned_size_integer_type;

                        auto value = compiler_size_to_native_size(context, result.value.value.array.count);

                        return {
                            true,
                            {
                                type,
                                value
                            }
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        if(print_errors) {
                            error(expression.member_reference.name.position, "Cannot access array pointer in constant context");
                        }

                        return { false };
                    } else {
                        if(print_errors) {
                            error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);
                        }

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    auto struct_type = retrieve_struct_type(context, result.value.type._struct);

                    for(size_t i = 0; i < struct_type.members.count; i += 1) {
                        if(strcmp(struct_type.members[i].name.text, expression.member_reference.name.text) == 0) {
                            return {
                                true,
                                {
                                    struct_type.members[i].type,
                                    result.value.value._struct[i]
                                }
                            };
                        }
                    }

                    if(print_errors) {
                        error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);
                    }
                    
                    return { false };
                } break;

                case TypeCategory::FileModule: {
                    for(auto declaration : result.value.value.file_module) {
                        if(declaration.type_resolved && strcmp(declaration.name.text, expression.member_reference.name.text) == 0) {
                            return evaluate_constant_declaration(context, declaration, print_errors);
                        }
                    }

                    if(print_errors) {
                        error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);
                    }

                    return { false };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            auto expression_result = evaluate_constant_expression(context, *expression.index_reference.expression, print_errors);

            if(!expression_result.status) {
                return { false };
            }
            
            auto index_result = evaluate_constant_expression(context, *expression.index_reference.index, print_errors);

            if(!index_result.status) {
                return { false };
            }

            if(index_result.value.type.category != TypeCategory::Integer) {
                if(print_errors) {
                    error(expression.index_reference.index->position, "Index not an integer");
                }

                return { false };
            }

            return evaluate_constant_index(
                expression_result.value.type,
                expression_result.value.value,
                expression.index_reference.expression->position,
                index_result.value.type.integer,
                index_result.value.value,
                expression.index_reference.index->position,
                print_errors
            );
        } break;

        case ExpressionType::IntegerLiteral: {
            Type type;
            type.category = TypeCategory::Integer;
            type.integer = IntegerType::Undetermined;

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
            array_type.integer = IntegerType::Unsigned8;

            Type type;
            type.category = TypeCategory::Array;
            type.array = heapify(array_type);

            auto characters = allocate<ConstantValue>(expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer = expression.string_literal[i];
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

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                if(print_errors) {
                    error(expression.position, "Empty array literal");
                }

                return { false };
            }

            Type element_type;

            auto elements = allocate<ConstantValue>(expression.array_literal.count);

            for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                auto result = evaluate_constant_expression(context, expression.array_literal[i], print_errors);

                if(!result.status) {
                    return { false };
                }

                if(i == 0) {
                    element_type = result.value.type;
                }

                ConstantValue value;
                if(
                    element_type.category == TypeCategory::Integer &&
                    result.value.type.category == TypeCategory::Integer&&
                    (
                        element_type.integer == IntegerType::Undetermined || 
                        result.value.type.integer == IntegerType::Undetermined
                    )
                ) {
                    if(element_type.integer == IntegerType::Undetermined && element_type.integer == IntegerType::Undetermined) {
                        value.integer = result.value.value.integer;
                    } else if(element_type.integer == IntegerType::Undetermined) {
                        for(size_t j = 0; j < i; j += 1) {
                            elements[j] = determine_constant_integer(result.value.type.integer, elements[j]);
                        }

                        value.integer = result.value.value.integer;

                        element_type.integer = result.value.type.integer;
                    } else {
                        value = determine_constant_integer(element_type.integer, result.value.value);
                    }
                } else {
                    if(!types_equal(element_type, result.value.type)) {
                        if(print_errors) {
                            error(expression.array_literal[i].position, "Mismatched array literal type");
                        }

                        return { false };
                    }

                    value.integer = result.value.value.integer;
                }

                elements[i] = value;
            }

            if(element_type.category == TypeCategory::Integer && element_type.integer == IntegerType::Undetermined) {
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    elements[i] = determine_constant_integer(context.default_integer_type, elements[i]);
                }

                element_type.integer = context.default_integer_type;
            }
            
            Type type;
            type.category = TypeCategory::StaticArray;
            type.static_array = {
                expression.array_literal.count,
                heapify(element_type)
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

        case ExpressionType::FunctionCall: {
            if(print_errors) {
                error(expression.position, "Function calls not allowed in global context");
            }

            return { false };
        } break;

        case ExpressionType::BinaryOperation: {
            auto left_result = evaluate_constant_expression(context, *expression.binary_operation.left, print_errors);

            if(!left_result.status) {
                return { false };
            }

            if(left_result.value.type.category != TypeCategory::Integer) {
                error(expression.binary_operation.left->position, "Cannot apply binary operation to non-integers");

                return { false };
            }

            auto right_result = evaluate_constant_expression(context, *expression.binary_operation.right, print_errors);

            if(!right_result.status) {
                return { false };
            }
            
            if(right_result.value.type.category != TypeCategory::Integer) {
                error(expression.binary_operation.right->position, "Cannot apply binary operation to non-integers");

                return { false };
            }

            auto result = evaluate_constant_integer_binary_operation(
                expression.binary_operation.binary_operator,
                expression.position,
                left_result.value.type.integer,
                left_result.value.value,
                right_result.value.type.integer,
                right_result.value.value,
                print_errors
            );

            if(!result.status) {
                return { false };
            }

            return {
                true,
                result.value
            };
        } break;

        case ExpressionType::UnaryOperation: {
            auto result = evaluate_constant_expression(context, *expression.unary_operation.expression, print_errors);
            
            if(!result.status) {
                return { false };
            }

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    if(result.value.type.category != TypeCategory::Type) {
                        if(print_errors) {
                            error(expression.unary_operation.expression->position, "Cannot take pointers to constants");
                        }

                        return { false };
                    }

                    Type type;
                    type.category = TypeCategory::Type;

                    ConstantValue value;
                    value.type.category = TypeCategory::Pointer;
                    value.type.pointer = heapify(result.value.value.type);

                    return {
                        true,
                        {
                            type,
                            value
                        }
                    };
                } break;

                case UnaryOperator::BooleanInvert: {
                    if(result.value.type.category != TypeCategory::Boolean) {
                        if(print_errors) {
                            error(expression.unary_operation.expression->position, "Cannot do boolean inversion on non-boolean");
                        }

                        return { false };
                    }

                    ConstantValue value;
                    value.boolean = !result.value.value.boolean;

                    return {
                        true,
                        {
                            result.value.type,
                            value
                        }
                    };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;
        
        case ExpressionType::ArrayType: {
            auto result = evaluate_type_expression(context, *expression.array_type, true);

            if(!result.status) {
                return { false };
            }

            ConstantExpressionValue value;
            value.type.category = TypeCategory::Type;
            value.value.type.category = TypeCategory::Array;
            value.value.type.array = heapify(result.value);

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionType: {
            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                auto result = evaluate_type_expression(context, *expression.function_type.return_type, true);

                if(!result.status) {
                    return { false };
                }

                return_type = result.value;
            }

            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                auto result = evaluate_type_expression(context, expression.function_type.parameters[i].type, true);

                if(!result.status) {
                    return { false };
                }

                parameters[i] = result.value;
            }

            ConstantExpressionValue value;
            value.type.category = TypeCategory::Type;
            value.value.type.category = TypeCategory::Function;
            value.value.type.function = {
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

static Result<Type> evaluate_type_expression(ConstantContext context, Expression expression, bool print_errors) {
    auto result = evaluate_constant_expression(context, expression, print_errors);

    if(!result.status) {
        return { false };
    }

    if(result.value.type.category != TypeCategory::Type) {
        if(print_errors) {
            error(expression.position, "Value is not a type");
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

        case StatementType::StructDefinition: {
            char* mangled_name{};

            for(auto name : *name_stack) {
                string_buffer_append(&mangled_name, name);
            }

            string_buffer_append(&mangled_name, statement.struct_definition.name.text);

            Declaration declaration;
            declaration.category = DeclarationCategory::StructDefinition;
            declaration.name = statement.struct_definition.name;
            declaration.type_resolved = false;

            declaration.struct_definition = {
                mangled_name,
                statement.struct_definition.members
            };

            return {
                true,
                declaration
            };
        } break;

        case StatementType::Expression:
        case StatementType::VariableDeclaration:
        case StatementType::Assignment:
        case StatementType::LoneIf:
        case StatementType::WhileLoop:
        case StatementType::Return: {
            return { false };
        }

        case StatementType::Import: {
            Declaration declaration;
            declaration.category = DeclarationCategory::FileModuleImport;
            declaration.type_resolved = false;

            auto name = path_get_file_component(statement.import);

            declaration.name = {
                name,
                statement.position
            };

            declaration.file_module_import = statement.import;

            return {
                true,
                declaration
            };
        } break;

        default: {
            abort();
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
                    error(declaration.name.position, "Duplicate declaration name %s", declaration.name.text);
                    error(sibling.name.position, "Original declared here");
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

                auto parameters = allocate<Type>(declaration.function_definition.parameters.count);
                
                for(size_t i = 0; i < declaration.function_definition.parameters.count; i += 1) {
                    auto result = evaluate_type_expression(*context, declaration.function_definition.parameters[i].type, print_errors);

                    parameters[i] = result.value;
                }

                Type type;
                type.category = TypeCategory::Function;
                type.function.parameters = {
                    declaration.function_definition.parameters.count,
                    parameters
                };
                type.function.return_type = heapify(return_type);

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

                auto parameters = allocate<Type>(declaration.external_function.parameters.count);
                
                for(size_t i = 0; i < declaration.external_function.parameters.count; i += 1) {
                    auto result = evaluate_type_expression(*context, declaration.external_function.parameters[i].type, print_errors);

                    parameters[i] = result.value;
                }

                Type type;
                type.category = TypeCategory::Function;
                type.function.parameters = {
                    declaration.external_function.parameters.count,
                    parameters
                };
                type.function.return_type = heapify(return_type);

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

            case DeclarationCategory::StructDefinition: {
                for(size_t i = 0; i < declaration.struct_definition.members.count; i += 1) {
                    for(size_t j = 0; j < declaration.struct_definition.members.count; j += 1) {
                        if(j != i && strcmp(declaration.struct_definition.members[i].name.text, declaration.struct_definition.members[j].name.text) == 0) {
                            if(print_errors) {
                                error(declaration.struct_definition.members[i].name.position, "Duplicate struct member name %s", declaration.struct_definition.members[i].name.text);
                            }

                            return { false };
                        }
                    }

                    auto result = evaluate_type_expression(*context, declaration.struct_definition.members[i].type, print_errors);

                    if(!result.status) {
                        return { false };
                    }
                }

                auto members = allocate<StructTypeMember>(declaration.struct_definition.members.count);

                for(size_t i = 0; i < declaration.struct_definition.members.count; i += 1) {
                    auto result = evaluate_type_expression(*context, declaration.struct_definition.members[i].type, print_errors);

                    members[i] = {
                        declaration.struct_definition.members[i].name,
                        result.value
                    };
                }

                append(context->struct_types, {
                    declaration.struct_definition.mangled_name,
                    {
                        declaration.struct_definition.members.count,
                        members
                    }
                });

                Type type;
                type.category = TypeCategory::Struct;
                type._struct = declaration.struct_definition.mangled_name;

                return {
                    true,
                    type
                };
            } break;

            case DeclarationCategory::FileModuleImport: {
                Type type;
                type.category = TypeCategory::FileModule;

                return {
                    true,
                    type
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
    FilePosition type_position;
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

    Type return_type;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    List<ArrayType> array_types;

    List<ArrayConstant> array_constants;

    List<const char*> libraries;
};

static bool register_global_name(GenerationContext *context, const char *name, FilePosition name_position) {
    for(auto global_name : context->global_names) {
        if(strcmp(global_name, name) == 0) {
            error(name_position, "Duplicate global name %s", name);

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
            error(name.position, "Duplicate variable name %s", name.text);
            error(variable.name.position, "Original declared here");

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
    for(size_t i = 0; i < context->array_constants.count; i += 1) {
        auto array_constant = context->array_constants[i];

        if(!types_equal(array_constant.type, type)) {
            continue;
        }

        if(array_constant.elements.count != value.count) {
            continue;
        }

        auto equal = true;
        for(size_t j = 0; j < value.count; j += 1) {
            if(!constant_values_deep_equal(context->constant_context, type, array_constant.elements[j], value[j])) {
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
    sprintf(buffer, "%zu", context->array_constants.count);

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

static Result<const char *> maybe_register_array_type(GenerationContext *context, Type type, FilePosition type_position) {
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
        case TypeCategory::Void:
        case TypeCategory::StaticArray:
        case TypeCategory::FileModule: {
            error(type_position, "Invalid array type\n");

            return { false };
        } break;

        case TypeCategory::Integer: {
            assert(type.integer != IntegerType::Undetermined);
        } break;

        case TypeCategory::Pointer:
        case TypeCategory::Struct: {
            
        } break;

        case TypeCategory::Array: {
            auto result = maybe_register_array_type(context, *type.array, type_position);

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

    sprintf(number_buffer, "%zu", array_types->count);

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

static bool generate_type(GenerationContext *context, char **prefix_source, char **suffix_source, Type type, FilePosition type_position) {
    switch(type.category) {
        case TypeCategory::Function: {
            error(type_position, "Function values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::Integer: {
            generate_integer_type(prefix_source, type.integer);

            return true;
        } break;

        case TypeCategory::Boolean: {
            string_buffer_append(prefix_source, "_Bool");

            return true;
        } break;

        case TypeCategory::Type: {
            error(type_position, "Type values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::Void: {
            string_buffer_append(prefix_source, "void");

            return true;
        } break;

        case TypeCategory::Pointer: {
            if(type.pointer->category == TypeCategory::Function) {
                auto function_type = type.pointer->function;

                if(!generate_type(context, prefix_source, prefix_source, *function_type.return_type, type_position)) {
                    return false;
                }

                string_buffer_append(prefix_source, "(*");

                string_buffer_append(suffix_source, ")(");

                for(size_t i = 0; i < function_type.parameters.count; i += 1) {
                    if(!generate_type(context, suffix_source, suffix_source, function_type.parameters[i], type_position)) {
                        return false;
                    }

                    if(i != function_type.parameters.count - 1) {
                        string_buffer_append(suffix_source, ",");
                    }
                }

                string_buffer_append(suffix_source, ")");
            } else {
                if(!generate_type(context, prefix_source, suffix_source, *type.pointer, type_position)) {
                    return false;
                }

                string_buffer_append(prefix_source, "*");
            }

            return true;
        } break;

        case TypeCategory::Array: {
            auto result = maybe_register_array_type(context, *type.array, type_position);

            if(!result.status) {
                return false;
            }

            string_buffer_append(prefix_source, "struct ");
            string_buffer_append(prefix_source, result.value);

            return true;
        } break;

        case TypeCategory::Struct: {
            string_buffer_append(prefix_source, "struct ");
            string_buffer_append(prefix_source, type._struct);

            return true;
        } break;

        case TypeCategory::StaticArray: {
            if(!generate_type(context, prefix_source, suffix_source, *type.static_array.type, type_position)) {
                return false;
            }

            string_buffer_append(suffix_source, "[");

            char buffer[32];
            sprintf(buffer, "%lld", type.static_array.length);
            string_buffer_append(suffix_source, buffer);

            string_buffer_append(suffix_source, "]");

            return true;
        } break;

        case TypeCategory::FileModule: {
            error(type_position, "Module values cannot exist at runtime");

            return false;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_constant_value(GenerationContext *context, char **source, Type type, ConstantValue value, FilePosition position) {
    switch(type.category) {
        case TypeCategory::Function: {
            string_buffer_append(source, value.function);

            return true;
        } break;

        case TypeCategory::Integer: {
            char buffer[64];

            switch(type.integer) {
                case IntegerType::Undetermined: {
                    sprintf(buffer, "%lld", (int64_t)value.integer);
                } break;

                case IntegerType::Unsigned8: {
                    sprintf(buffer, "%hhu", (uint8_t)value.integer);
                } break;

                case IntegerType::Unsigned16: {
                    sprintf(buffer, "%hu", (uint16_t)value.integer);
                } break;

                case IntegerType::Unsigned32: {
                    sprintf(buffer, "%u", (uint32_t)value.integer);
                } break;

                case IntegerType::Unsigned64: {
                    sprintf(buffer, "%llu", (uint64_t)value.integer);
                } break;

                case IntegerType::Signed8: {
                    sprintf(buffer, "%hhd", (int8_t)value.integer);
                } break;

                case IntegerType::Signed16: {
                    sprintf(buffer, "%hd", (int16_t)value.integer);
                } break;

                case IntegerType::Signed32: {
                    sprintf(buffer, "%d", (int32_t)value.integer);
                } break;

                case IntegerType::Signed64: {
                    sprintf(buffer, "%lld", (int64_t)value.integer);
                } break;

                default: {
                    abort();
                } break;
            }

            string_buffer_append(source, buffer);

            return true;
        } break;

        case TypeCategory::Boolean: {
            if(value.boolean) {
                string_buffer_append(source, "1");
            } else {
                string_buffer_append(source, "0");
            }

            return true;
        } break;

        case TypeCategory::Type: {
            error(position, "Type values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::Array: {
            auto type_result = maybe_register_array_type(context, *type.array, position);

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

            sprintf(buffer, "%zu", value.array.count);

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

        case TypeCategory::Struct: {
            string_buffer_append(source, "struct ");

            string_buffer_append(source, type._struct);

            string_buffer_append(source, "{");

            auto struct_type = retrieve_struct_type(context->constant_context, type._struct);

            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                if(i != struct_type.members.count - 1) {
                    if(!generate_constant_value(context, source, struct_type.members[i].type, value._struct[i], position)) {
                        return false;
                    }

                    string_buffer_append(source, ",");
                }
            }

            string_buffer_append(source, "}");

            return true;
        } break;

        case TypeCategory::Void: {
            error(position, "Void values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::FileModule: {
            error(position, "Module values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::StaticArray:
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
            if(!generate_constant_value(context, source, result.value.type, result.value.constant, expression.position)) {
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
            for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        if(variable.type.category == TypeCategory::StaticArray) {
                            auto type_result = maybe_register_array_type(context, *variable.type.static_array.type, variable.type_position);

                            if(!type_result.status) {
                                return { false };
                            }

                            string_buffer_append(source, "(struct ");

                            string_buffer_append(source, type_result.value);

                            string_buffer_append(source, "){");

                            char buffer[64];
                            sprintf(buffer, "%zu", variable.type.static_array.length);
                            string_buffer_append(source, buffer);

                            string_buffer_append(source, ",");

                            string_buffer_append(source, variable.name.text);

                            string_buffer_append(source, "}");

                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Anonymous;
                            value.type.category = TypeCategory::Array;
                            value.type.array = variable.type.static_array.type;

                            return {
                                true,
                                value
                            };
                        } else {
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

            switch(result.value.type.category) {
                case TypeCategory::Function:
                case TypeCategory::Integer:
                case TypeCategory::Boolean:
                case TypeCategory::Type:
                case TypeCategory::Void:
                case TypeCategory::Pointer:
                case TypeCategory::StaticArray: {
                    error(expression.member_reference.expression->position, "This type has no members");

                    return { false };
                } break;

                case TypeCategory::Array: {
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
                                value.type.integer = context->constant_context.unsigned_size_integer_type;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Constant: {
                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Constant;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = context->constant_context.unsigned_size_integer_type;
                                value.constant = compiler_size_to_native_size(context->constant_context, result.value.constant.array.count);

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
                        error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    switch(result.value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            auto struct_type = retrieve_struct_type(context->constant_context, result.value.type._struct);

                            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                                if(strcmp(struct_type.members[i].name.text, expression.member_reference.name.text) == 0) {
                                    string_buffer_append(source, "(");

                                    string_buffer_append(source, expression_source);

                                    string_buffer_append(source, ").");

                                    string_buffer_append(source, expression.member_reference.name.text);

                                    ExpressionValue value;
                                    value.category = result.value.category;
                                    value.type = struct_type.members[i].type;

                                    return {
                                        true,
                                        value
                                    };
                                }
                            }

                            error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);
                            
                            return { false };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            auto struct_type = retrieve_struct_type(context->constant_context, result.value.type._struct);

                            for(size_t i = 0; i < struct_type.members.count; i += 1) {
                                if(strcmp(struct_type.members[i].name.text, expression.member_reference.name.text) == 0) {
                                    ExpressionValue value;
                                    value.category = ExpressionValueCategory::Constant;
                                    value.type = struct_type.members[i].type;
                                    value.constant = result.value.constant._struct[i];

                                    return {
                                        true,
                                        value
                                    };
                                }
                            }

                            error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);
                            
                            return { false };
                        } break;

                        default: {
                            return { false };
                        } break;
                    }
                } break;

                case TypeCategory::FileModule: {
                    assert(result.value.category == ExpressionValueCategory::Constant);

                    for(auto declaration : result.value.constant.file_module) {
                        if(declaration.type_resolved && strcmp(declaration.name.text, expression.member_reference.name.text) == 0) {
                            auto result = evaluate_constant_declaration(context->constant_context, declaration, true);

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
                        }
                    }

                    error(expression.member_reference.name.position, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            char *expression_source{};
            auto result = generate_expression(context, &expression_source, *expression.index_reference.expression);
            
            if(!result.status) {
                return { false };
            }

            switch(result.value.category) {
                case ExpressionValueCategory::Anonymous:
                case ExpressionValueCategory::Assignable: {
                    if(result.value.type.category != TypeCategory::Array) {
                        error(expression.index_reference.expression->position, "Cannot index a non-array");

                        return { false };
                    }

                    string_buffer_append(source, "(");

                    string_buffer_append(source, expression_source);

                    string_buffer_append(source, ").pointer");

                    string_buffer_append(source, "[");

                    auto index_result = generate_runtime_expression(context, source, *expression.index_reference.index);

                    if(!index_result.status) {
                        return { false };
                    }

                    if(index_result.value.type.category != TypeCategory::Integer) {
                        error(expression.index_reference.index->position, "Array index not an integer");

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
                        error(expression.index_reference.index->position, "Array index not an integer");

                        return { false };
                    }

                    switch(index_result.value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            if(result.value.type.category != TypeCategory::Array) {
                                error(expression.index_reference.expression->position, "Cannot index a non-array");

                                return { false };
                            }

                            string_buffer_append(source, "(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ").pointer");

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
                            auto constant_value = evaluate_constant_index(result.value.type, result.value.constant, expression.position, index_result.value.type.integer, index_result.value.constant, expression.index_reference.index->position, true);

                            if(!constant_value.status) {
                                return { false };
                            }
                            
                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Constant;
                            value.type = constant_value.value.type;
                            value.constant = constant_value.value.value;

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
            value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StringLiteral: {
            auto characters = allocate<ConstantValue>(expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            Type array_type;
            array_type.category = TypeCategory::Integer;
            array_type.integer = IntegerType::Unsigned8;

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Array;
            value.type.array = heapify(array_type);
            value.constant.array = {
                expression.string_literal.count,
                characters
            };

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(expression.position, "Empty array literal");

                return { false };
            }

            Type element_type;

            auto elements = allocate<ConstantValue>(expression.array_literal.count);

            for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                auto result = evaluate_constant_expression(context->constant_context, expression.array_literal[i], true);

                if(!result.status) {
                    return { false };
                }

                if(i == 0) {
                    element_type = result.value.type;
                }

                ConstantValue value;
                if(
                    element_type.category == TypeCategory::Integer &&
                    result.value.type.category == TypeCategory::Integer&&
                    (
                        element_type.integer == IntegerType::Undetermined || 
                        result.value.type.integer == IntegerType::Undetermined
                    )
                ) {
                    if(element_type.integer == IntegerType::Undetermined && element_type.integer == IntegerType::Undetermined) {
                        value.integer = result.value.value.integer;
                    } else if(element_type.integer == IntegerType::Undetermined) {
                        for(size_t j = 0; j < i; j += 1) {
                            elements[j] = determine_constant_integer(result.value.type.integer, elements[j]);
                        }

                        value.integer = result.value.value.integer;

                        element_type.integer = result.value.type.integer;
                    } else {
                        value = determine_constant_integer(element_type.integer, result.value.value);
                    }
                } else {
                    if(!types_equal(element_type, result.value.type)) {
                        error(expression.array_literal[i].position, "Mismatched array literal type");

                        return { false };
                    }

                    value.integer = result.value.value.integer;
                }

                elements[i] = value;
            }

            if(element_type.category == TypeCategory::Integer && element_type.integer == IntegerType::Undetermined) {
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    elements[i] = determine_constant_integer(context->constant_context.default_integer_type, elements[i]);
                }

                element_type.integer = context->constant_context.default_integer_type;
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
        } break;

        case ExpressionType::FunctionCall: {
            string_buffer_append(source, "(");

            auto result = generate_runtime_expression(context, source, *expression.function_call.expression);

            string_buffer_append(source, ")");

            if(!result.status) {
                return { false };
            }

            if(result.value.type.category != TypeCategory::Function) {
                error(expression.function_call.expression->position, "Cannot call a non-function");

                return { false };
            }

            string_buffer_append(source, "(");

            if(expression.function_call.parameters.count != result.value.type.function.parameters.count) {
                error(expression.position, "Incorrect number of parameters. Expected %zu, got %zu", result.value.type.function.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            for(size_t i = 0; i < result.value.type.function.parameters.count; i += 1) {
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
                    error(expression.function_call.parameters[i].position, "Incorrect parameter type for parameter %d", i);

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

        case ExpressionType::BinaryOperation: {
            char *left_source{};
            auto left_result = generate_expression(context, &left_source, *expression.binary_operation.left);

            if(!left_result.status) {
                return { false };
            }

            if(left_result.value.type.category != TypeCategory::Integer) {
                error(expression.binary_operation.left->position, "Cannot apply binary operation to non-integers");

                return { false };
            }

            char *right_source{};
            auto right_result = generate_expression(context, &right_source, *expression.binary_operation.right);

            if(!right_result.status) {
                return { false };
            }
            
            if(right_result.value.type.category != TypeCategory::Integer) {
                error(expression.binary_operation.right->position, "Cannot apply binary operation to non-integers");

                return { false };
            }

            if(left_result.value.category == ExpressionValueCategory::Constant && right_result.value.category == ExpressionValueCategory::Constant) {
                auto result = evaluate_constant_integer_binary_operation(
                    expression.binary_operation.binary_operator,
                    expression.position,
                    left_result.value.type.integer,
                    left_result.value.constant,
                    right_result.value.type.integer,
                    right_result.value.constant,
                    true
                );

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
            } else {
                IntegerType type;
                if(left_result.value.type.integer != IntegerType::Undetermined && right_result.value.type.integer != IntegerType::Undetermined) {
                    if(left_result.value.type.integer != right_result.value.type.integer) {
                        error(expression.position, "Mismatched types for binary operation");

                        return { false };
                    }

                    type = left_result.value.type.integer;
                } else if(left_result.value.type.integer != IntegerType::Undetermined) {
                    type = left_result.value.type.integer;
                } else {
                    type = right_result.value.type.integer;
                }

                if(left_result.value.type.integer == IntegerType::Undetermined) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, type);

                    string_buffer_append(source, ")");
                }

                string_buffer_append(source, "(");

                switch(left_result.value.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, left_source);
                    } break;

                    case ExpressionValueCategory::Constant: {
                        if(!generate_constant_value(context, source, left_result.value.type, left_result.value.constant, expression.binary_operation.left->position)) {
                            return { false };
                        }
                    } break;

                    default: {
                        abort();
                    } break;
                }

                string_buffer_append(source, ")");

                Type result_type;
                switch(expression.binary_operation.binary_operator) {
                    case BinaryOperator::Addition: {
                        string_buffer_append(source, "+");

                        result_type.category = TypeCategory::Integer;
                        result_type.integer = type;
                    } break;
                    
                    case BinaryOperator::Subtraction: {
                        string_buffer_append(source, "-");

                        result_type.category = TypeCategory::Integer;
                        result_type.integer = type;
                    } break;
                    
                    case BinaryOperator::Multiplication: {
                        string_buffer_append(source, "*");

                        result_type.category = TypeCategory::Integer;
                        result_type.integer = type;
                    } break;
                    
                    case BinaryOperator::Division: {
                        string_buffer_append(source, "/");

                        result_type.category = TypeCategory::Integer;
                        result_type.integer = type;
                    } break;
                    
                    case BinaryOperator::Modulo: {
                        string_buffer_append(source, "%");

                        result_type.category = TypeCategory::Integer;
                        result_type.integer = type;
                    } break;
                    
                    case BinaryOperator::Equal: {
                        string_buffer_append(source, "==");

                        result_type.category = TypeCategory::Boolean;
                    } break;
                    
                    case BinaryOperator::NotEqual: {
                        string_buffer_append(source, "!=");

                        result_type.category = TypeCategory::Boolean;
                    } break;

                    default: {
                        abort();
                    } break;
                }

                if(right_result.value.type.integer == IntegerType::Undetermined) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, type);

                    string_buffer_append(source, ")");
                }

                string_buffer_append(source, "(");

                switch(right_result.value.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, right_source);
                    } break;

                    case ExpressionValueCategory::Constant: {
                        if(!generate_constant_value(context, source, right_result.value.type, right_result.value.constant, expression.binary_operation.right->position)) {
                            return { false };
                        }
                    } break;

                    default: {
                        abort();
                    } break;
                }

                string_buffer_append(source, ")");

                ExpressionValue value;
                value.category = ExpressionValueCategory::Anonymous;
                value.type = result_type;

                return {
                    true,
                    value
                };
            }
        } break;

        case ExpressionType::UnaryOperation: {
            char *expression_source{};

            auto result = generate_expression(context, &expression_source, *expression.unary_operation.expression);

            if(!result.status) {
                return { false };
            }

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    switch(result.value.category) {
                        case ExpressionValueCategory::Anonymous: {
                            error(expression.unary_operation.expression->position, "Cannot take pointers anonymous values");

                            return { false };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            if(result.value.type.category != TypeCategory::Type) {
                                error(expression.unary_operation.expression->position, "Cannot take pointers to constants");

                                return { false };
                            }

                            string_buffer_append(source, expression_source);

                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Constant;
                            value.type.category = TypeCategory::Type;
                            value.constant.type.category = TypeCategory::Pointer;
                            value.constant.type.pointer = heapify(result.value.constant.type);

                            return {
                                true,
                                value
                            };
                        } break;
                        
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "&(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Anonymous;
                            value.type.category = TypeCategory::Pointer;
                            value.type.pointer = heapify(result.value.type);

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
                
                case UnaryOperator::BooleanInvert: {
                    if(result.value.type.category != TypeCategory::Boolean) {
                        error(expression.unary_operation.expression->position, "Cannot do boolean inversion on non-boolean");

                        return { false };
                    }

                    ExpressionValue value;
                    value.type.category = TypeCategory::Boolean;

                    switch(result.value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "!(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            value.category = ExpressionValueCategory::Anonymous;
                        } break;
                        
                        case ExpressionValueCategory::Constant: {
                            value.category = ExpressionValueCategory::Constant;
                            value.constant.boolean = !result.value.constant.boolean;
                        } break;

                        default: {
                            abort();
                        } break;
                    }

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

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.constant.type.category = TypeCategory::Array;
            value.constant.type.array = heapify(result.value);

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionType: {
            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                auto result = evaluate_type_expression(context->constant_context, *expression.function_type.return_type, true);

                if(!result.status) {
                    return { false };
                }

                return_type = result.value;
            }

            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                auto result = evaluate_type_expression(context->constant_context, expression.function_type.parameters[i].type, true);

                if(!result.status) {
                    return { false };
                }

                parameters[i] = result.value;
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.constant.type.category = TypeCategory::Function;
            value.constant.type.function = {
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
                        return false;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, result.value)) {
                        return false;
                    }

                    char *type_suffix_source{};
                    if(!generate_type(context, &(context->implementation_source), &type_suffix_source, result.value, statement.variable_declaration.uninitialized.position)) {
                        return false;
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(&(context->implementation_source), " ");

                        string_buffer_append(&(context->implementation_source), type_suffix_source);
                    }

                    string_buffer_append(&(context->implementation_source), ";");

                    return true;
                } break;
                
                case VariableDeclarationType::TypeElided: {
                    char *initializer_source{};
                    auto result = generate_expression(context, &initializer_source, statement.variable_declaration.type_elided);

                    if(!result.status) {
                        return false;
                    }

                    Type actual_type;
                    if(result.value.type.category == TypeCategory::Integer && result.value.type.integer == IntegerType::Undetermined) {
                        actual_type.category = TypeCategory::Integer;
                        actual_type.integer = context->constant_context.default_integer_type;
                    } else {
                        actual_type = result.value.type;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, actual_type)) {
                        return false;
                    }
                    
                    char *type_suffix_source{};
                    if(!generate_type(context, &(context->implementation_source), &type_suffix_source, actual_type, statement.variable_declaration.type_elided.position)) {
                        return false;
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(&(context->implementation_source), " ");

                        string_buffer_append(&(context->implementation_source), type_suffix_source);
                    }
                    
                    string_buffer_append(&(context->implementation_source), "=");

                    if(result.value.category == ExpressionValueCategory::Constant) {
                        if(result.value.type.category == TypeCategory::StaticArray) {
                            string_buffer_append(&(context->implementation_source), "{");

                            for(size_t i = 0; i < result.value.type.static_array.length; i += 1) {
                                if(!generate_constant_value(
                                    context,
                                    &(context->implementation_source),
                                    *result.value.type.static_array.type,
                                    result.value.constant.static_array[i],
                                    statement.variable_declaration.type_elided.position
                                )) {
                                    return false;
                                }

                                if(i != result.value.type.static_array.length - 1) {
                                    string_buffer_append(&(context->implementation_source), ",");
                                }
                            }

                            string_buffer_append(&(context->implementation_source), "}");
                        } else {
                            if(!generate_constant_value(
                                context,
                                &(context->implementation_source),
                                result.value.type,
                                result.value.constant,
                                statement.variable_declaration.type_elided.position
                            )) {
                                return false;
                            }
                        }
                    } else {
                        string_buffer_append(&(context->implementation_source), initializer_source);
                    }

                    string_buffer_append(&(context->implementation_source), ";");

                    return true;
                } break;
                
                case VariableDeclarationType::FullySpecified: {
                    auto type_result = evaluate_type_expression(context->constant_context, statement.variable_declaration.fully_specified.type, true);

                    if(!type_result.status) {
                        return false;
                    }

                    char *initializer_source{};
                    auto initializer_result = generate_expression(context, &initializer_source, statement.variable_declaration.fully_specified.initializer);

                    if(!initializer_result.status) {
                        return false;
                    }

                    if(
                        !(
                            type_result.value.category == TypeCategory::Integer &&
                            initializer_result.value.type.category == TypeCategory::Integer &&
                            initializer_result.value.type.integer == IntegerType::Undetermined
                        ) &&
                        !types_equal(type_result.value, initializer_result.value.type)
                    ) {
                        error(statement.variable_declaration.fully_specified.initializer.position, "Assigning incorrect type");
                        
                        return false;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, type_result.value)) {
                        return false;
                    }

                    char *type_suffix_source{};
                    if(!generate_type(context, &(context->implementation_source), &type_suffix_source, type_result.value, statement.variable_declaration.fully_specified.type.position)) {
                        return false;
                    }

                    string_buffer_append(&(context->implementation_source), " ");

                    string_buffer_append(&(context->implementation_source), statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(&(context->implementation_source), " ");

                        string_buffer_append(&(context->implementation_source), type_suffix_source);
                    }
                    
                    string_buffer_append(&(context->implementation_source), "=");

                    if(initializer_result.value.category == ExpressionValueCategory::Constant) {
                        if(initializer_result.value.type.category == TypeCategory::StaticArray) {
                            string_buffer_append(&(context->implementation_source), "{");

                            for(size_t i = 0; i < initializer_result.value.type.static_array.length; i += 1) {
                                if(!generate_constant_value(
                                    context,
                                    &(context->implementation_source),
                                    *initializer_result.value.type.static_array.type,
                                    initializer_result.value.constant.static_array[i],
                                    statement.variable_declaration.fully_specified.initializer.position
                                )) {
                                    return false;
                                }

                                if(i != initializer_result.value.type.static_array.length - 1) {
                                    string_buffer_append(&(context->implementation_source), ",");
                                }
                            }

                            string_buffer_append(&(context->implementation_source), "}");
                        } else {
                            if(!generate_constant_value(
                                context,
                                &(context->implementation_source),
                                initializer_result.value.type,
                                initializer_result.value.constant,
                                statement.variable_declaration.fully_specified.initializer.position
                            )) {
                                return false;
                            }
                        }
                    } else {
                        string_buffer_append(&(context->implementation_source), initializer_source);
                    }

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
                error(statement.assignment.target.position, "Value is not assignable");

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
                error(statement.assignment.value.position, "Assigning incorrect type");

                return false;
            }

            string_buffer_append(&(context->implementation_source), ";");

            return true;
        } break;

        case StatementType::LoneIf: {
            string_buffer_append(&(context->implementation_source), "if(");

            auto result = generate_runtime_expression(context, &(context->implementation_source), statement.lone_if.condition);

            if(!result.status) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), ")");

            if(result.value.type.category != TypeCategory::Boolean) {
                error(statement.lone_if.condition.position, "Non-boolean if statement condition");

                return false;
            }

            append(&(context->variable_context_stack), List<Variable>{});

            string_buffer_append(&(context->implementation_source), "{");

            for(auto child_statement : statement.lone_if.statements) {
                if(!generate_statement(context, child_statement)) {
                    return false;
                }
            }

            string_buffer_append(&(context->implementation_source), "}");

            context->variable_context_stack.count -= 1;

            return true;
        } break;

        case StatementType::WhileLoop: {
            string_buffer_append(&(context->implementation_source), "while(");

            auto result = generate_runtime_expression(context, &(context->implementation_source), statement.lone_if.condition);

            if(!result.status) {
                return false;
            }

            string_buffer_append(&(context->implementation_source), ")");

            if(result.value.type.category != TypeCategory::Boolean) {
                error(statement.lone_if.condition.position, "Non-boolean while loop condition");

                return false;
            }

            append(&(context->variable_context_stack), List<Variable>{});

            string_buffer_append(&(context->implementation_source), "{");

            for(auto child_statement : statement.lone_if.statements) {
                if(!generate_statement(context, child_statement)) {
                    return false;
                }
            }

            string_buffer_append(&(context->implementation_source), "}");

            context->variable_context_stack.count -= 1;

            return true;
        } break;

        case StatementType::Return: {
            string_buffer_append(&(context->implementation_source), "return ");

            char *expression_source{};
            auto result = generate_runtime_expression(context, &expression_source, statement._return);

            if(!result.status) {
                return { false };
            }

            if(
                !(
                    context->return_type.category == TypeCategory::Integer &&
                    result.value.type.category == TypeCategory::Integer &&
                    result.value.type.integer == IntegerType::Undetermined
                ) &&
                !types_equal(context->return_type, result.value.type)
            ) {
                error(statement._return.position, "Mismatched return type");

                return { false };
            }

            string_buffer_append(&(context->implementation_source), expression_source);

            string_buffer_append(&(context->implementation_source), ";");

            return true;
        } break;

        case StatementType::Library: {
            for(auto library : context->libraries) {
                if(strcmp(library, statement.library) == 0) {
                    return true;
                }
            }

            append(&(context->libraries), statement.library);

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_function_signature(GenerationContext *context, char **source, const char *name, Type type, FilePosition position, FunctionParameter *parameters) {
    char *type_suffix_source{};
    if(!generate_type(context, source, &type_suffix_source, *type.function.return_type, position)) {
        return false;
    }

    string_buffer_append(source, " ");

    string_buffer_append(source, name);

    if(type_suffix_source != nullptr) {
        string_buffer_append(source, " ");

        string_buffer_append(source, type_suffix_source);
    }

    string_buffer_append(source, "(");
    
    for(size_t i = 0; i < type.function.parameters.count; i += 1) {
        char *parameter_type_suffix_source{};
        if(!generate_type(context, source, &parameter_type_suffix_source, type.function.parameters[i], parameters[i].type.position)) {
            return false;
        }

        string_buffer_append(source, " ");

        string_buffer_append(source, parameters[i].name.text);

        if(parameter_type_suffix_source != nullptr) {
            string_buffer_append(source, " ");

            string_buffer_append(source, parameter_type_suffix_source);
        }

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

            if(!register_global_name(context, declaration.function_definition.mangled_name, declaration.name.position)) {
                return false;
            }

            if(!generate_function_signature(
                context,
                &(context->forward_declaration_source),
                declaration.function_definition.mangled_name,
                declaration.type,
                declaration.name.position,
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
                declaration.name.position,
                declaration.function_definition.parameters.elements
            )){
                return false;
            }

            string_buffer_append(&(context->implementation_source), "{");

            append(&(context->variable_context_stack), List<Variable>{});

            context->return_type = *declaration.type.function.return_type;

            assert(declaration.type.function.parameters.count == declaration.function_definition.parameters.count);

            for(size_t i = 0; i < declaration.type.function.parameters.count; i += 1) {
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

            if(!register_global_name(context, declaration.name.text, declaration.name.position)) {
                return false;
            }

            if(!generate_function_signature(
                context,
                &(context->forward_declaration_source),
                declaration.name.text,
                declaration.type,
                declaration.name.position,
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

        case DeclarationCategory::FileModuleImport: {
            return true;
        } break;

        case DeclarationCategory::StructDefinition: {
            if(!register_global_name(context, declaration.name.text, declaration.name.position)) {
                return false;
            }

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

inline GlobalConstant create_base_integer_type(const char *name, IntegerType integer_type) {
    Type type;
    type.category = TypeCategory::Integer;
    type.integer = integer_type;

    return create_base_type(name, type);
}

Result<CSource> generate_c_source(Array<File> files) {
    assert(files.count > 0);

    auto file_modules = allocate<FileModule>(files.count);

    List<const char*> name_stack{};

    for(size_t i = 0; i < files.count; i += 1) {
        auto file = files[i];

        if(i != 0) {
            auto name = path_get_file_component(file.path);

            append<const char*>(&name_stack, name);
        }

        List<Declaration> declarations{};

        List<Statement> compiler_directives{};

        for(auto statement : file.statements) {
            switch(statement.type) {
                case StatementType::Library: {
                    append(&compiler_directives, statement);
                } break;

                default: {
                    auto result = create_declaration(&name_stack, statement);

                    if(result.status) {
                        append(&declarations, result.value);
                    } else {
                        error(statement.position, "Only constant declarations and compiler directives are allowed in global scope");

                        return { false };
                    }
                } break;
            }
        }

        file_modules[i] = {
            file.path,
            to_array(declarations),
            to_array(compiler_directives)
        };

        if(i != 0) {
            name_stack.count -= 1;
        }
    }

    List<GlobalConstant> global_constants{};

    auto unsigned_size_integer_type = IntegerType::Unsigned64;
    auto signed_size_integer_type = IntegerType::Signed64;

    append(&global_constants, create_base_integer_type("u8", IntegerType::Unsigned8));
    append(&global_constants, create_base_integer_type("u16", IntegerType::Unsigned16));
    append(&global_constants, create_base_integer_type("u32", IntegerType::Unsigned32));
    append(&global_constants, create_base_integer_type("u64", IntegerType::Unsigned64));

    append(&global_constants, create_base_integer_type("i8", IntegerType::Signed8));
    append(&global_constants, create_base_integer_type("i16", IntegerType::Signed16));
    append(&global_constants, create_base_integer_type("i32", IntegerType::Signed32));
    append(&global_constants, create_base_integer_type("i64", IntegerType::Signed64));

    append(&global_constants, create_base_integer_type("usize", unsigned_size_integer_type));
    append(&global_constants, create_base_integer_type("isize", signed_size_integer_type));

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

    List<StructType> struct_types{};

    auto previous_resolved_declaration_count = 0;

    while(true) {
        for(size_t i = 0; i < files.count; i += 1) {
            auto file_module = file_modules[i];

            ConstantContext constant_context {
                unsigned_size_integer_type,
                signed_size_integer_type,
                signed_size_integer_type,
                to_array(global_constants),
                &struct_types,
                file_module.declarations,
                {
                    files.count,
                    file_modules
                }
            };

            for(auto &declaration : file_module.declarations) {
                auto result = resolve_declaration_type(&constant_context, declaration, false);

                if(result.status) {
                    declaration.type_resolved = true;
                    declaration.type = result.value;
                }
            }
        }

        auto resolved_declaration_count = 0;

        for(size_t i = 0; i < files.count; i += 1) {
            for(auto declaration : file_modules[i].declarations) {
                resolved_declaration_count += count_declarations(declaration, true);
            }
        }

        if(resolved_declaration_count == previous_resolved_declaration_count) {
            auto declaration_count = 0;

            for(size_t i = 0; i < files.count; i += 1) {
                auto file_module = file_modules[i];

                ConstantContext constant_context {
                    unsigned_size_integer_type,
                    signed_size_integer_type,
                    signed_size_integer_type,
                    to_array(global_constants),
                    &struct_types,
                    file_module.declarations,
                    {
                        files.count,
                        file_modules
                    }
                };

                for(auto declaration : file_module.declarations) {
                    resolve_declaration_type(&constant_context, declaration, true);

                    declaration_count += count_declarations(declaration, false);
                }
            }

            if(declaration_count != resolved_declaration_count) {
                return { false };
            }

            break;
        } else {
            previous_resolved_declaration_count = resolved_declaration_count;
        }
    }

    GenerationContext context{};

    for(size_t i = 0; i < files.count; i += 1) {
        auto file_module = file_modules[i];

        context.constant_context = {
            unsigned_size_integer_type,
            signed_size_integer_type,
            signed_size_integer_type,
            to_array(global_constants),
            &struct_types,
            file_module.declarations,
            {
                files.count,
                file_modules
            }
        };

        for(auto declaration : file_module.declarations) {
            if(!generate_declaration(&context, declaration)) {
                return { false };
            }
        }

        for(auto compiler_directive : file_module.compiler_directives) {
            if(!generate_statement(&context, compiler_directive)) {
                return { false };
            }
        }
    }

    char *full_source{};

    FilePosition position {
        "<generated>",
        0,
        0
    };

    for(auto array_type : context.array_types) {
        string_buffer_append(&full_source, "struct ");

        string_buffer_append(&full_source, array_type.mangled_name);

        string_buffer_append(&full_source, "{long long int length;");

        Type type;
        type.category = TypeCategory::Pointer;
        type.pointer = heapify(array_type.type);

        char *type_suffix_source{};
        if(!generate_type(&context, &full_source, &type_suffix_source, type, position)) {
            return { false };
        }

        string_buffer_append(&full_source, " pointer ");

        if(type_suffix_source != nullptr) {
            string_buffer_append(&full_source, type_suffix_source);
        }

        string_buffer_append(&full_source, ";};");
    }

    for(size_t i = 0; i < context.array_constants.count; i += 1) {
        auto array_constant = context.array_constants[i];

        Type type;
        type.category = TypeCategory::StaticArray;
        type.static_array.length = array_constant.elements.count;
        type.static_array.type = heapify(array_constant.type);

        char *type_suffix_source{};
        if(!generate_type(&context, &full_source, &type_suffix_source, type, position)) {
            return { false };
        }

        string_buffer_append(&full_source, " _array_constant_");

        char buffer[32];
        sprintf(buffer, "%d", i);
        string_buffer_append(&full_source, buffer);

        if(type_suffix_source != nullptr) {
            string_buffer_append(&full_source, type_suffix_source);
        }
        
        string_buffer_append(&full_source, "={");

        for(size_t j = 0; j < array_constant.elements.count; j += 1) {
            if(!generate_constant_value(&context, &full_source, array_constant.type, array_constant.elements[j], position)) {
                return { false };
            }

            if(j != array_constant.elements.count - 1) {
                string_buffer_append(&full_source, ",");
            }
        }

        string_buffer_append(&full_source, "};");
    }

    for(auto struct_type : struct_types) {
        string_buffer_append(&full_source, "struct ");

        string_buffer_append(&full_source, struct_type.mangled_name);

        string_buffer_append(&full_source, "{");

        for(auto member : struct_type.members) {
            char *type_postfix_buffer{};
            if(!generate_type(&context, &full_source, &type_postfix_buffer, member.type, position)) {
                return { false };
            }

            string_buffer_append(&full_source, " ");

            string_buffer_append(&full_source, member.name.text);

            if(type_postfix_buffer != nullptr) {
                string_buffer_append(&full_source, " ");

                string_buffer_append(&full_source, type_postfix_buffer);
            }

            string_buffer_append(&full_source, ";");
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
        {
            full_source,
            to_array(context.libraries)
        }
    };
}