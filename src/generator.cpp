#include "generator.h"
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

struct Declaration;

union ConstantValue {
    const char *function;

    uint64_t integer;

    bool boolean;

    Type type;

    size_t pointer;

    Array<ConstantValue> array;

    ConstantValue *static_array;

    ConstantValue *_struct;

    Array<Declaration> file_module;
};

struct TypedConstantValue {
    Type type;

    ConstantValue value;
};

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

    bool is_top_level;

    union {
        Array<Declaration> file_module;

        Declaration *parent;
    };

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

struct Variable {
    Identifier name;

    Type type;
    FileRange type_range;
};

struct RuntimeFunctionParameter {
    Identifier name;

    Type type;
};

enum struct CDeclarationType {
    ArrayType,
    StructType,
    Function,
    ExternalFunction,
    ArrayConstant
};

struct CDeclaration {
    CDeclarationType type;

    const char *mangled_name;

    union {
        Type array_type;

        Array<StructTypeMember> struct_type;

        struct {
            Array<RuntimeFunctionParameter> parameters;

            Type return_type;

            Array<Statement> statements;

            Declaration declaration;
        } function;

        struct {
            Array<RuntimeFunctionParameter> parameters;

            Type return_type;
        } external_function;

        struct {
            Type type;

            Array<ConstantValue> elements;
        } array_constant;
    };
};

struct GenerationContext {
    IntegerType unsigned_size_integer_type;
    IntegerType signed_size_integer_type;
    IntegerType default_integer_type;

    Array<GlobalConstant> global_constants;

    Array<FileModule> file_modules;

    Type return_type;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    List<CDeclaration> c_declarations;

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

static Array<Declaration> get_declaration_children(Declaration declaration) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            return declaration.function_definition.declarations;
        } break;

        default: {
            return Array<Declaration>{};
        } break;
    }
}

static Result<Declaration> lookup_declaration(Declaration from, const char *name) {
    if(from.is_top_level) {
        for(auto declaration : from.file_module) {
            if(strcmp(declaration.name.text, name) == 0) {
                return {
                    true,
                    declaration
                };
            }
        }
    } else {
        auto current = *from.parent;

        while(!current.is_top_level) {
            for(auto child : get_declaration_children(current)) {
                if(strcmp(child.name.text, name) == 0) {
                    return {
                        true,
                        child
                    };
                }
            }

            current = *current.parent;
        }

        for(auto declaration : current.file_module) {
            if(strcmp(declaration.name.text, name) == 0) {
                return {
                    true,
                    declaration
                };
            }
        }
    }

    return { false };
}

static CDeclaration retrieve_c_declaration(GenerationContext context, const char *mangled_name) {
    for(auto c_declaration : context.c_declarations) {
        if(strcmp(c_declaration.mangled_name, mangled_name) == 0) {
            return c_declaration;
        }
    }

    abort();
}

static bool constant_values_deep_equal(GenerationContext context, Type type, ConstantValue a, ConstantValue b) {
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
        
        case TypeCategory::Pointer: {
            return a.pointer == b.pointer;
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
            auto c_declaration = retrieve_c_declaration(context, type._struct);

            assert(c_declaration.type == CDeclarationType::StructType);

            for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                if(!constant_values_deep_equal(context, c_declaration.struct_type[i].type, a._struct[i], b._struct[i])) {
                    return false;
                }
            }

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static ConstantValue compiler_size_to_native_size(GenerationContext context, size_t size) {
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

        default: {
            abort();
        } break;
    }

    return value;
}

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Declaration from, Expression expression, bool print_errors);

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Declaration declaration, bool print_errors);

static Result<TypedConstantValue> resolve_constant_named_reference(GenerationContext *context, Declaration from, Identifier name, bool print_errors) {
    auto result = lookup_declaration(from, name.text);

    if(!result.status) {
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

        if(print_errors) {
            error(name.range, "Cannot find named reference %s", name.text);
        }

        return { false };
    }

    return resolve_declaration(context, result.value, print_errors);
}

static Result<TypedConstantValue> evaluate_constant_index(Type type, ConstantValue value, FileRange range, IntegerType index_type, ConstantValue index_value, FileRange index_range, bool print_errors) {
    size_t index;
    switch(index_type) {
        case IntegerType::Undetermined: {
            if((int64_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_range, "Array index %lld out of bounds", (int64_t)index_value.integer);
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
                    error(index_range, "Array index %hhd out of bounds", (int8_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int8_t)index_value.integer;
        } break;
        
        case IntegerType::Signed16: {
            if((int16_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_range, "Array index %hd out of bounds", (int16_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int16_t)index_value.integer;
        } break;
        
        case IntegerType::Signed32: {
            if((int32_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_range, "Array index %d out of bounds", (int32_t)index_value.integer);
                }

                return { false };
            }

            index = (size_t)(int32_t)index_value.integer;
        } break;
        
        case IntegerType::Signed64: {
            if((int64_t)index_value.integer < 0) {
                if(print_errors) {
                    error(index_range, "Array index %lld out of bounds", (int64_t)index_value.integer);
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
                    error(index_range, "Array index %zu out of bounds", index);
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
                    error(index_range, "Array index %zu out of bounds", index);
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
            if(print_errors) {
                error(range, "Cannot index a non-array");
            }

            return { false };
        } break;
    }
}

template <typename T>
static TypedConstantValue perform_constant_integer_binary_operation(BinaryOperator binary_operator, IntegerType type, IntegerType left_type, ConstantValue left_value, IntegerType right_type, ConstantValue right_value) {
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
        
        case BinaryOperator::BitwiseAnd: {
            result_value.integer = (uint64_t)(left & right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
        } break;
        
        case BinaryOperator::BitwiseOr: {
            result_value.integer = (uint64_t)(left | right);

            result_type.category = TypeCategory::Integer;
            result_type.integer = type;
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

static Result<TypedConstantValue> evaluate_constant_binary_operation(BinaryOperator binary_operator, FileRange range, Type left_type, ConstantValue left_value, Type right_type, ConstantValue right_value, bool print_errors) {
    if(
        !(
            left_type.category == TypeCategory::Integer && right_type.category == TypeCategory::Integer &&
            (left_type.integer == IntegerType::Undetermined || right_type.integer == IntegerType::Undetermined)
        ) &&
        !types_equal(left_type, right_type)
    ) {
        if(print_errors) {
            error(range, "Mismatched types for binary operation");
        }

        return { false };
    }

    TypedConstantValue result;


    switch(left_type.category) {
        case TypeCategory::Integer: {
            if(left_type.integer == IntegerType::Undetermined && right_type.integer == IntegerType::Undetermined) {
                result = perform_constant_integer_binary_operation<int64_t>(
                    binary_operator,
                    IntegerType::Undetermined,
                    left_type.integer,
                    left_value,
                    right_type.integer,
                    right_value
                );
            } else {
                IntegerType type;
                if(left_type.integer != IntegerType::Undetermined) {
                    type = left_type.integer;
                } else if(right_type.integer != IntegerType::Undetermined) {
                    type = right_type.integer;
                }

                TypedConstantValue result;
                switch(type) {
                    case IntegerType::Unsigned8: {
                        result = perform_constant_integer_binary_operation<uint8_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Unsigned16: {
                        result = perform_constant_integer_binary_operation<uint16_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Unsigned32: {
                        result = perform_constant_integer_binary_operation<uint32_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Unsigned64: {
                        result = perform_constant_integer_binary_operation<uint64_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Signed8: {
                        result = perform_constant_integer_binary_operation<int8_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Signed16: {
                        result = perform_constant_integer_binary_operation<int16_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Signed32: {
                        result = perform_constant_integer_binary_operation<int32_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    case IntegerType::Signed64: {
                        result = perform_constant_integer_binary_operation<int64_t>(
                            binary_operator,
                            type,
                            left_type.integer,
                            left_value,
                            right_type.integer,
                            right_value
                        );
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } break;

        case TypeCategory::Boolean: {
            result.type.category = TypeCategory::Boolean;

            switch(binary_operator) {
                case BinaryOperator::Equal: {
                    result.value.boolean = left_value.boolean == right_value.boolean;
                } break;

                case BinaryOperator::NotEqual: {
                    result.value.boolean = left_value.boolean != right_value.boolean;
                } break;

                case BinaryOperator::BooleanAnd: {
                    result.value.boolean = left_value.boolean && right_value.boolean;
                } break;

                case BinaryOperator::BooleanOr: {
                    result.value.boolean = left_value.boolean || right_value.boolean;
                } break;

                default: {
                    if(print_errors) {
                        error(range, "Cannot perform that operation on booleans");
                    }

                    return { false };
                } break;
            }
        } break;

        default: {
            if(print_errors) {
                error(range, "Cannot perform binary operations on this type");
            }

            return { false };
        } break;
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

        default: {
            abort();
        }
    }

    return value;
}

static Result<ConstantValue> evaluate_constant_conversion(GenerationContext context, ConstantValue value, Type value_type, FileRange value_range, Type type, FileRange type_range, bool print_errors) {
    ConstantValue result;

    switch(value_type.category) {
        case TypeCategory::Integer: {
            switch(type.category) {
                case TypeCategory::Integer: {
                    switch(value_type.integer) {
                        case IntegerType::Undetermined: {
                            result.integer = (uint64_t)(int64_t)value.integer;
                        } break;

                        case IntegerType::Unsigned8: {
                            result.integer = (uint64_t)(uint8_t)value.integer;
                        } break;

                        case IntegerType::Unsigned16: {
                            result.integer = (uint64_t)(uint16_t)value.integer;
                        } break;

                        case IntegerType::Unsigned32: {
                            result.integer = (uint64_t)(uint32_t)value.integer;
                        } break;

                        case IntegerType::Unsigned64: {
                            result.integer = value.integer;
                        } break;

                        case IntegerType::Signed8: {
                            result.integer = (uint64_t)(int8_t)value.integer;
                        } break;

                        case IntegerType::Signed16: {
                            result.integer = (uint64_t)(int16_t)value.integer;
                        } break;

                        case IntegerType::Signed32: {
                            result.integer = (uint64_t)(int32_t)value.integer;
                        } break;

                        case IntegerType::Signed64: {
                            result.integer = (uint64_t)(int64_t)value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(value_type.integer == IntegerType::Undetermined) {
                        result.pointer = (int64_t)value.integer;
                    } else if(value.type.integer == context.unsigned_size_integer_type) {
                        result.pointer = value.integer;
                    } else {
                        if(print_errors) {
                            error(value_range, "Cannot cast to pointer from this integer type");
                        }

                        return { false };
                    }
                } break;

                default: {
                    if(print_errors) {
                        error(type_range, "Cannot cast integer to this type");
                    }

                    return { false };
                } break;
            }
        } break;

        case TypeCategory::Pointer: {
            switch(type.category) {
                case TypeCategory::Integer: {
                    if(type.integer == context.unsigned_size_integer_type) {
                        result.pointer = value.integer;
                    } else {
                        if(print_errors) {
                            error(value_range, "Cannot cast from pointer to this integer type");
                        }

                        return { false };
                    }
                } break;

                case TypeCategory::Pointer: {
                    result.pointer = value.pointer;
                } break;

                default: {
                    if(print_errors) {
                        error(type_range, "Cannot cast pointer to this type");
                    }

                    return { false };
                } break;
            }
        } break;

        default: {
            if(print_errors) {
                error(value_range, "Cannot cast from this type");
            }

            return { false };
        } break;
    }

    return {
        true,
        result
    };
}

static Result<Type> evaluate_type_expression(GenerationContext *context, Declaration from, Expression expression, bool print_errors);

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Declaration from, Expression expression, bool print_errors) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            return resolve_constant_named_reference(context, from, expression.named_reference, print_errors);
        } break;

        case ExpressionType::MemberReference: {
            expect(expression_value, evaluate_constant_expression(context, from, *expression.member_reference.expression, print_errors));

            switch(expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        Type type;
                        type.category = TypeCategory::Integer;
                        type.integer = context->unsigned_size_integer_type;

                        auto value = compiler_size_to_native_size(*context, expression_value.value.array.count);

                        return {
                            true,
                            {
                                type,
                                value
                            }
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        if(print_errors) {
                            error(expression.member_reference.name.range, "Cannot access array pointer in constant context");
                        }

                        return { false };
                    } else {
                        if(print_errors) {
                            error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);
                        }

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    auto c_declaration = retrieve_c_declaration(*context, expression_value.type._struct);

                    assert(c_declaration.type == CDeclarationType::StructType);

                    for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                        if(strcmp(c_declaration.struct_type[i].name.text, expression.member_reference.name.text) == 0) {
                            return {
                                true,
                                {
                                    c_declaration.struct_type[i].type,
                                    expression_value.value._struct[i]
                                }
                            };
                        }
                    }

                    if(print_errors) {
                        error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);
                    }
                    
                    return { false };
                } break;

                case TypeCategory::FileModule: {
                    for(auto declaration : expression_value.value.file_module) {
                        if(strcmp(declaration.name.text, expression.member_reference.name.text) == 0) {
                            expect(value, resolve_declaration(context, declaration, print_errors));

                            return {
                                true,
                                value
                            };
                        }
                    }

                    if(print_errors) {
                        error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);
                    }

                    return { false };
                } break;

                default: {
                    if(print_errors) {
                        error(expression.member_reference.expression->range, "This type has no members");
                    }

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, evaluate_constant_expression(context, from, *expression.index_reference.expression, print_errors));
            
            expect(index, evaluate_constant_expression(context, from, *expression.index_reference.index, print_errors));

            if(index.type.category != TypeCategory::Integer) {
                if(print_errors) {
                    error(expression.index_reference.index->range, "Index not an integer");
                }

                return { false };
            }

            return evaluate_constant_index(
                expression_value.type,
                expression_value.value,
                expression.index_reference.expression->range,
                index.type.integer,
                index.value,
                expression.index_reference.index->range,
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
                    error(expression.range, "Empty array literal");
                }

                return { false };
            }

            Type element_type;

            auto elements = allocate<ConstantValue>(expression.array_literal.count);

            for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                expect(element, evaluate_constant_expression(context, from, expression.array_literal[i], print_errors));

                if(i == 0) {
                    element_type = element.type;
                }

                ConstantValue value;
                if(
                    element_type.category == TypeCategory::Integer &&
                    element.type.category == TypeCategory::Integer&&
                    (
                        element_type.integer == IntegerType::Undetermined || 
                        element.type.integer == IntegerType::Undetermined
                    )
                ) {
                    if(element_type.integer == IntegerType::Undetermined && element_type.integer == IntegerType::Undetermined) {
                        value.integer = element.value.integer;
                    } else if(element_type.integer == IntegerType::Undetermined) {
                        for(size_t j = 0; j < i; j += 1) {
                            elements[j] = determine_constant_integer(element.type.integer, elements[j]);
                        }

                        value.integer = element.value.integer;

                        element_type.integer = element.type.integer;
                    } else {
                        value = determine_constant_integer(element_type.integer, element.value);
                    }
                } else {
                    if(!types_equal(element_type, element.value.type)) {
                        if(print_errors) {
                            error(expression.array_literal[i].range, "Mismatched array literal type");
                        }

                        return { false };
                    }

                    value.integer = element.value.integer;
                }

                elements[i] = value;
            }

            if(element_type.category == TypeCategory::Integer && element_type.integer == IntegerType::Undetermined) {
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    elements[i] = determine_constant_integer(context->default_integer_type, elements[i]);
                }

                element_type.integer = context->default_integer_type;
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
                error(expression.range, "Function calls not allowed in global context");
            }

            return { false };
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, evaluate_constant_expression(context, from, *expression.binary_operation.left, print_errors));

            expect(right, evaluate_constant_expression(context, from, *expression.binary_operation.right, print_errors));

            expect(value, evaluate_constant_binary_operation(
                expression.binary_operation.binary_operator,
                expression.range,
                left.type,
                left.value,
                right.type,
                right.value,
                print_errors
            ));

            return {
                true,
                value
            };
        } break;

        case ExpressionType::UnaryOperation: {
            expect(expression_value, evaluate_constant_expression(context, from, *expression.unary_operation.expression, print_errors));

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    if(expression_value.type.category != TypeCategory::Type) {
                        if(print_errors) {
                            error(expression.unary_operation.expression->range, "Cannot take pointers to constants of this type");
                        }

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
                        if(print_errors) {
                            error(expression.unary_operation.expression->range, "Cannot do boolean inversion on non-boolean");
                        }

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
                        if(print_errors) {
                            error(expression.unary_operation.expression->range, "Cannot do negation on non-integer");
                        }

                        return { false };
                    }

                    ConstantValue value;
                    
                    switch(expression_value.type.integer) {
                        case IntegerType::Undetermined: {
                            value.integer = (uint64_t)-(int64_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Unsigned8: {
                            value.integer = (uint64_t)-(uint8_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Unsigned16: {
                            value.integer = (uint64_t)-(uint16_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Unsigned32: {
                            value.integer = (uint64_t)-(uint32_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Unsigned64: {
                            value.integer = (uint64_t)-(uint64_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Signed8: {
                            value.integer = (uint64_t)-(int8_t)(int64_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Signed16: {
                            value.integer = (uint64_t)-(int16_t)(int64_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Signed32: {
                            value.integer = (uint64_t)-(int32_t)(int64_t)expression_value.value.integer;
                        } break;

                        case IntegerType::Signed64: {
                            value.integer = (uint64_t)-(int64_t)(int64_t)expression_value.value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }

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
            expect(expression_value, evaluate_constant_expression(context, from, *expression.cast.expression, print_errors));

            expect(type, evaluate_type_expression(context, from, *expression.cast.type, print_errors));

            expect(value, evaluate_constant_conversion(
                *context,
                expression_value.value,
                expression_value.type,
                expression.cast.expression->range,
                type,
                expression.cast.type->range,
                print_errors
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
            expect(expression_value, evaluate_type_expression(context, from, *expression.array_type, true));

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
            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                expect(return_type_value, evaluate_type_expression(context, from, *expression.function_type.return_type, true));

                return_type = return_type_value;
            }

            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                expect(parameter, evaluate_type_expression(context, from, expression.function_type.parameters[i].type, true));

                parameters[i] = parameter;
            }

            TypedConstantValue value;
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

static Result<Type> evaluate_type_expression(GenerationContext *context, Declaration from, Expression expression, bool print_errors) {
    expect(expression_value, evaluate_constant_expression(context, from, expression, print_errors));

    if(expression_value.type.category != TypeCategory::Type) {
        if(print_errors) {
            error(expression.range, "Value is not a type");
        }

        return { false };
    }

    return {
        true,
        expression_value.value.type
    };
}

static Result<Declaration> create_declaration(List<const char*> *name_stack, Statement statement) {
    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            if(statement.function_declaration.is_external) {
                Declaration declaration;
                declaration.category = DeclarationCategory::ExternalFunction;
                declaration.name = statement.function_declaration.name;

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

            declaration.struct_definition = {
                mangled_name,
                statement.struct_definition.members
            };

            return {
                true,
                declaration
            };
        } break;

        case StatementType::Import: {
            Declaration declaration;
            declaration.category = DeclarationCategory::FileModuleImport;

            auto name = path_get_file_component(statement.import);

            declaration.name = {
                name,
                statement.range
            };

            declaration.file_module_import = statement.import;

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

static void set_child_declaration_parents(Declaration *declaration) {
    switch(declaration->category) {
        case DeclarationCategory::FunctionDefinition: {
            for(auto &child : declaration->function_definition.declarations) {
                child.parent = declaration;

                child.is_top_level = false;

                set_child_declaration_parents(&child);
            }
        } break;
    }
}

static bool register_c_declaration(GenerationContext *context, CDeclaration c_declaration) {
    for(auto it : context->c_declarations) {
        if(it.mangled_name == c_declaration.mangled_name) {
            fprintf(stderr, "Duplicate mangled name %s\n", c_declaration.mangled_name);

            return false;
        }
    }

    append(&context->c_declarations, c_declaration);

    return true;
}

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Declaration declaration, bool print_errors) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            auto parameterTypes = allocate<Type>(declaration.function_definition.parameters.count);
            auto runtimeParameters = allocate<RuntimeFunctionParameter>(declaration.function_definition.parameters.count);
            
            for(size_t i = 0; i < declaration.function_definition.parameters.count; i += 1) {
                expect(type, evaluate_type_expression(context, declaration, declaration.function_definition.parameters[i].type, print_errors));

                parameterTypes[i] = type;
                runtimeParameters[i] = {
                    declaration.function_definition.parameters[i].name,
                    type
                };
            }

            Type return_type;
            if(declaration.function_definition.has_return_type) {
                expect(return_type_value, evaluate_type_expression(context, declaration, declaration.function_definition.return_type, print_errors));

                return_type = return_type_value;
            } else {
                return_type.category = TypeCategory::Void;
            }

            auto is_registered = false;
            for(auto c_declaration : context->c_declarations) {
                if(c_declaration.type == CDeclarationType::Function && strcmp(c_declaration.mangled_name, declaration.function_definition.mangled_name) == 0) {
                    is_registered = true;

                    break;
                }
            }

            if(!is_registered) {
                CDeclaration c_declaration;
                c_declaration.type = CDeclarationType::Function;
                c_declaration.mangled_name = declaration.function_definition.mangled_name;

                c_declaration.function = {
                    {
                        declaration.function_definition.parameters.count,
                        runtimeParameters
                    },
                    return_type,
                    declaration.function_definition.statements,
                    declaration
                };

                if(!register_c_declaration(context, c_declaration)) {
                    return { false };
                }
            }

            Type type;
            type.category = TypeCategory::Function;
            type.function.parameters = {
                declaration.function_definition.parameters.count,
                parameterTypes
            };
            type.function.return_type = heapify(return_type);

            ConstantValue value;
            value.function = declaration.function_definition.mangled_name;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case DeclarationCategory::ExternalFunction: {
            auto parameterTypes = allocate<Type>(declaration.external_function.parameters.count);
            auto runtimeParameters = allocate<RuntimeFunctionParameter>(declaration.external_function.parameters.count);
            
            for(size_t i = 0; i < declaration.external_function.parameters.count; i += 1) {
                expect(type, evaluate_type_expression(context, declaration, declaration.external_function.parameters[i].type, print_errors));

                parameterTypes[i] = type;
                runtimeParameters[i] = {
                    declaration.external_function.parameters[i].name,
                    type
                };
            }

            Type return_type;
            if(declaration.external_function.has_return_type) {
                expect(return_type_value, evaluate_type_expression(context, declaration, declaration.external_function.return_type, print_errors));

                return_type = return_type_value;
            } else {
                return_type.category = TypeCategory::Void;
            }

            auto is_registered = false;
            for(auto c_declaration : context->c_declarations) {
                if(c_declaration.type == CDeclarationType::Function && strcmp(c_declaration.mangled_name, declaration.name.text) == 0) {
                    is_registered = true;

                    break;
                }
            }

            if(!is_registered) {
                CDeclaration c_declaration;
                c_declaration.type = CDeclarationType::ExternalFunction;
                c_declaration.mangled_name = declaration.name.text;

                c_declaration.external_function = {
                    {
                        declaration.external_function.parameters.count,
                        runtimeParameters
                    },
                    return_type
                };

                if(!register_c_declaration(context, c_declaration)) {
                    return { false };
                }
            }

            Type type;
            type.category = TypeCategory::Function;
            type.function.parameters = {
                declaration.external_function.parameters.count,
                parameterTypes
            };
            type.function.return_type = heapify(return_type);

            ConstantValue value;
            value.function = declaration.name.text;

            return {
                true,
                {
                    type,
                    value
                }
            };
        } break;

        case DeclarationCategory::ConstantDefinition: {
            expect(expression_value, evaluate_constant_expression(context, declaration, declaration.constant_definition, print_errors));

            return {
                true,
                expression_value
            };
        } break;

        case DeclarationCategory::StructDefinition: {
            for(size_t i = 0; i < declaration.struct_definition.members.count; i += 1) {
                for(size_t j = 0; j < declaration.struct_definition.members.count; j += 1) {
                    if(j != i && strcmp(declaration.struct_definition.members[i].name.text, declaration.struct_definition.members[j].name.text) == 0) {
                        if(print_errors) {
                            error(declaration.struct_definition.members[i].name.range, "Duplicate struct member name %s", declaration.struct_definition.members[i].name.text);
                        }

                        return { false };
                    }
                }

                if(!evaluate_type_expression(context, declaration, declaration.struct_definition.members[i].type, print_errors).status) {
                    return { false };
                }
            }

            auto members = allocate<StructTypeMember>(declaration.struct_definition.members.count);

            for(size_t i = 0; i < declaration.struct_definition.members.count; i += 1) {
                auto result = evaluate_type_expression(context, declaration, declaration.struct_definition.members[i].type, print_errors);

                members[i] = {
                    declaration.struct_definition.members[i].name,
                    result.value
                };
            }

            auto is_registered = false;
            for(auto c_declaration : context->c_declarations) {
                if(c_declaration.type == CDeclarationType::StructType && strcmp(c_declaration.mangled_name, declaration.struct_definition.mangled_name) == 0) {
                    is_registered = true;
                }
            }

            if(!is_registered) {
                CDeclaration c_declaration;
                c_declaration.type = CDeclarationType::StructType;
                c_declaration.mangled_name = declaration.struct_definition.mangled_name;

                c_declaration.struct_type = {
                    declaration.struct_definition.members.count,
                    members
                };

                if(!register_c_declaration(context, c_declaration)) {
                    return { false };
                }
            }

            Type type;
            type.category = TypeCategory::Type;

            ConstantValue value;
            value.type.category = TypeCategory::Struct;
            value.type._struct = declaration.struct_definition.mangled_name;

            return {
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

            for(auto file_module : context->file_modules) {
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

static bool add_new_variable(GenerationContext *context, Identifier name, Type type) {
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

static Result<const char *> register_array_constant(GenerationContext *context, Type type, Array<ConstantValue> value) {
    char *mangled_name{};

    string_buffer_append(&mangled_name, "_array_constant_");

    char buffer[32];
    sprintf(buffer, "%zu", context->c_declarations.count);

    string_buffer_append(&mangled_name, buffer);

    CDeclaration c_declaration;
    c_declaration.type = CDeclarationType::ArrayConstant;
    c_declaration.mangled_name = mangled_name;

    c_declaration.array_constant = {
        type,
        value
    };

    if(!register_c_declaration(context, c_declaration)) {
        return { false };
    }

    return {
        true,
        mangled_name
    };
}

static Result<const char *> maybe_register_array_type(GenerationContext *context, Type type, FileRange type_range) {
    for(auto c_declaration : context->c_declarations) {
        if(c_declaration.type == CDeclarationType::ArrayType && types_equal(c_declaration.array_type, type)) {
            return {
                true,
                c_declaration.mangled_name
            };
        }
    }

    switch(type.category) {
        case TypeCategory::Integer: {
            assert(type.integer != IntegerType::Undetermined);
        } break;

        case TypeCategory::Pointer:
        case TypeCategory::Struct: {
            
        } break;

        case TypeCategory::Array: {
            if(!maybe_register_array_type(context, *type.array, type_range).status) {
                return { false };
            }
        } break;

        default: {
            error(type_range, "Invalid array type\n");

            return { false };
        } break;
    }

    char *mangled_name_buffer{};

    string_buffer_append(&mangled_name_buffer, "_array_type_");

    char number_buffer[32];

    sprintf(number_buffer, "%zu", context->c_declarations.count);

    string_buffer_append(&mangled_name_buffer, number_buffer);

    CDeclaration c_declaration;
    c_declaration.type = CDeclarationType::ArrayType;
    c_declaration.mangled_name = mangled_name_buffer;

    c_declaration.array_type = type;

    if(!register_c_declaration(context, c_declaration)) {
        return { false };
    }

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

static bool generate_type(GenerationContext *context, char **prefix_source, char **suffix_source, Type type, FileRange type_range) {
    switch(type.category) {
        case TypeCategory::Function: {
            error(type_range, "Function values cannot exist at runtime");

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
            error(type_range, "Type values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::Void: {
            string_buffer_append(prefix_source, "void");

            return true;
        } break;

        case TypeCategory::Pointer: {
            if(type.pointer->category == TypeCategory::Function) {
                auto function_type = type.pointer->function;

                if(!generate_type(context, prefix_source, prefix_source, *function_type.return_type, type_range)) {
                    return false;
                }

                string_buffer_append(prefix_source, "(*");

                string_buffer_append(suffix_source, ")(");

                for(size_t i = 0; i < function_type.parameters.count; i += 1) {
                    if(!generate_type(context, suffix_source, suffix_source, function_type.parameters[i], type_range)) {
                        return false;
                    }

                    if(i != function_type.parameters.count - 1) {
                        string_buffer_append(suffix_source, ",");
                    }
                }

                string_buffer_append(suffix_source, ")");
            } else {
                if(!generate_type(context, prefix_source, suffix_source, *type.pointer, type_range)) {
                    return false;
                }

                string_buffer_append(prefix_source, "*");
            }

            return true;
        } break;

        case TypeCategory::Array: {
            expect(mangled_name, maybe_register_array_type(context, *type.array, type_range));

            string_buffer_append(prefix_source, "struct ");
            string_buffer_append(prefix_source, mangled_name);

            return true;
        } break;

        case TypeCategory::Struct: {
            string_buffer_append(prefix_source, "struct ");
            string_buffer_append(prefix_source, type._struct);

            return true;
        } break;

        case TypeCategory::StaticArray: {
            if(!generate_type(context, prefix_source, suffix_source, *type.static_array.type, type_range)) {
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
            error(type_range, "Module values cannot exist at runtime");

            return false;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_constant_value(GenerationContext *context, char **source, Type type, ConstantValue value, FileRange range) {
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
            error(range, "Type values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::Pointer: {
            char buffer[64];

            sprintf(buffer, "%zu", value.pointer);

            string_buffer_append(source, buffer);

            return true;
        } break;

        case TypeCategory::Array: {
            expect(type_mangled_name, maybe_register_array_type(context, *type.array, range));

            expect(constant_mangled_name, register_array_constant(context, *type.array, value.array));

            string_buffer_append(source, "(struct ");

            string_buffer_append(source, type_mangled_name);

            string_buffer_append(source, "){");

            char buffer[64];

            sprintf(buffer, "%zu", value.array.count);

            string_buffer_append(source, buffer);

            string_buffer_append(source, ",");

            string_buffer_append(source, constant_mangled_name);

            string_buffer_append(source, "}");

            return true;
        } break;

        case TypeCategory::Struct: {
            string_buffer_append(source, "struct ");

            string_buffer_append(source, type._struct);

            string_buffer_append(source, "{");

            auto c_declaration = retrieve_c_declaration(*context, type._struct);

            assert(c_declaration.type == CDeclarationType::StructType);

            for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                if(i != c_declaration.struct_type.count - 1) {
                    if(!generate_constant_value(context, source, c_declaration.struct_type[i].type, value._struct[i], range)) {
                        return false;
                    }

                    string_buffer_append(source, ",");
                }
            }

            string_buffer_append(source, "}");

            return true;
        } break;

        case TypeCategory::Void: {
            error(range, "Void values cannot exist at runtime");

            return false;
        } break;

        case TypeCategory::FileModule: {
            error(range, "Module values cannot exist at runtime");

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

static Result<ExpressionValue> generate_expression(GenerationContext *context, Declaration from, char **source, Expression expression);

static Result<ExpressionValue> generate_runtime_expression(GenerationContext *context, Declaration from, char **source, Expression expression) {
    expect(expression_value, generate_expression(context, from, source, expression));

    switch(expression_value.category) {
        case ExpressionValueCategory::Anonymous:
        case ExpressionValueCategory::Assignable: {
            return {
                true,
                expression_value
            };
        } break;

        case ExpressionValueCategory::Constant: {
            if(!generate_constant_value(context, source, expression_value.type, expression_value.constant, expression.range)) {
                return { false };
            }

            ExpressionValue value;
            value.category = ExpressionValueCategory::Anonymous;
            value.type = expression_value.type;

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

static Result<ExpressionValue> generate_expression(GenerationContext *context, Declaration from, char **source, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        if(variable.type.category == TypeCategory::StaticArray) {
                            expect(mangled_name, maybe_register_array_type(context, *variable.type.static_array.type, variable.type_range));

                            string_buffer_append(source, "(struct ");

                            string_buffer_append(source, mangled_name);

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

            expect(constant, resolve_constant_named_reference(context, from, expression.named_reference, true));

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
            char *expression_source{};
            expect(expression_value, generate_expression(context, from, &expression_source, *expression.member_reference.expression));

            switch(expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        switch(expression_value.category) {
                            case ExpressionValueCategory::Anonymous:
                            case ExpressionValueCategory::Assignable: {
                                string_buffer_append(source, "(");

                                string_buffer_append(source, expression_source);

                                string_buffer_append(source, ").length");

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Anonymous;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = context->unsigned_size_integer_type;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Constant: {
                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Constant;
                                value.type.category = TypeCategory::Integer;
                                value.type.integer = context->unsigned_size_integer_type;
                                value.constant = compiler_size_to_native_size(*context, expression_value.constant.array.count);

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
                            case ExpressionValueCategory::Anonymous:
                            case ExpressionValueCategory::Assignable: {
                                string_buffer_append(source, "(");

                                string_buffer_append(source, expression_source);

                                string_buffer_append(source, ").pointer");

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Anonymous;
                                value.type.category = TypeCategory::Pointer;
                                value.type.pointer = expression_value.type.array;

                                return {
                                    true,
                                    value
                                };
                            } break;

                            case ExpressionValueCategory::Constant: {
                                expect(mangled_name, register_array_constant(context, *expression_value.type.array, expression_value.constant.array));

                                string_buffer_append(source, mangled_name);

                                ExpressionValue value;
                                value.category = ExpressionValueCategory::Anonymous;
                                value.type.category = TypeCategory::Pointer;
                                value.type.pointer = expression_value.type.array;

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

                case TypeCategory::Struct: {
                    switch(expression_value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            auto c_declaration = retrieve_c_declaration(*context, expression_value.type._struct);

                            assert(c_declaration.type == CDeclarationType::StructType);

                            for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                                if(strcmp(c_declaration.struct_type[i].name.text, expression.member_reference.name.text) == 0) {
                                    string_buffer_append(source, "(");

                                    string_buffer_append(source, expression_source);

                                    string_buffer_append(source, ").");

                                    string_buffer_append(source, expression.member_reference.name.text);

                                    ExpressionValue value;
                                    value.category = expression_value.category;
                                    value.type = c_declaration.struct_type[i].type;

                                    return {
                                        true,
                                        value
                                    };
                                }
                            }

                            error(expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);
                            
                            return { false };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            auto c_declaration = retrieve_c_declaration(*context, expression_value.type._struct);

                            assert(c_declaration.type == CDeclarationType::StructType);

                            for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                                if(strcmp(c_declaration.struct_type[i].name.text, expression.member_reference.name.text) == 0) {
                                    ExpressionValue value;
                                    value.category = ExpressionValueCategory::Constant;
                                    value.type = c_declaration.struct_type[i].type;
                                    value.constant = expression_value.constant._struct[i];

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
                            return { false };
                        } break;
                    }
                } break;

                case TypeCategory::FileModule: {
                    assert(expression_value.category == ExpressionValueCategory::Constant);

                    for(auto declaration : expression_value.constant.file_module) {
                        if(strcmp(declaration.name.text, expression.member_reference.name.text) == 0) {
                            expect(constant_value, resolve_declaration(context, declaration, true));

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
                    error(expression.member_reference.expression->range, "This type has no members");

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IndexReference: {
            char *expression_source{};
            expect(expression_value, generate_expression(context, from, &expression_source, *expression.index_reference.expression));

            switch(expression_value.category) {
                case ExpressionValueCategory::Anonymous:
                case ExpressionValueCategory::Assignable: {
                    if(expression_value.type.category != TypeCategory::Array) {
                        error(expression.index_reference.expression->range, "Cannot index a non-array");

                        return { false };
                    }

                    string_buffer_append(source, "(");

                    string_buffer_append(source, expression_source);

                    string_buffer_append(source, ").pointer");

                    string_buffer_append(source, "[");

                    expect(index, generate_runtime_expression(context, from, source, *expression.index_reference.index));

                    if(index.type.category != TypeCategory::Integer) {
                        error(expression.index_reference.index->range, "Array index not an integer");

                        return { false };
                    }

                    string_buffer_append(source, "]");
                    
                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Assignable;
                    value.type = *expression_value.type.array;

                    return {
                        true,
                        value
                    };
                } break;

                case ExpressionValueCategory::Constant: {
                    char *index_source{};
                    expect(index, generate_expression(context, from, &index_source, *expression.index_reference.index));
                    
                    if(index.type.category != TypeCategory::Integer) {
                        error(expression.index_reference.index->range, "Array index not an integer");

                        return { false };
                    }

                    switch(index.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            if(expression_value.type.category != TypeCategory::Array) {
                                error(expression.index_reference.expression->range, "Cannot index a non-array");

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
                            value.type = *expression_value.type.array;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            expect(constant, evaluate_constant_index(
                                expression_value.type,
                                expression_value.constant,
                                expression.index_reference.expression->range,
                                index.type.integer,
                                index.constant,
                                expression.index_reference.index->range,
                                true
                            ));
                            
                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Constant;
                            value.type = constant.type;
                            value.constant = constant.value;

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
                error(expression.range, "Empty array literal");

                return { false };
            }

            Type element_type;

            auto elements = allocate<ConstantValue>(expression.array_literal.count);

            for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                expect(element, evaluate_constant_expression(context, from, expression.array_literal[i], true));

                if(i == 0) {
                    element_type = element.type;
                }

                ConstantValue value;
                if(
                    element_type.category == TypeCategory::Integer &&
                    element.type.category == TypeCategory::Integer&&
                    (
                        element_type.integer == IntegerType::Undetermined || 
                        element.type.integer == IntegerType::Undetermined
                    )
                ) {
                    if(element_type.integer == IntegerType::Undetermined && element_type.integer == IntegerType::Undetermined) {
                        value.integer = element.value.integer;
                    } else if(element_type.integer == IntegerType::Undetermined) {
                        for(size_t j = 0; j < i; j += 1) {
                            elements[j] = determine_constant_integer(element.type.integer, elements[j]);
                        }

                        value.integer = element.value.integer;

                        element_type.integer = element.type.integer;
                    } else {
                        value = determine_constant_integer(element_type.integer, element.value);
                    }
                } else {
                    if(!types_equal(element_type, element.type)) {
                        error(expression.array_literal[i].range, "Mismatched array literal type");

                        return { false };
                    }

                    value.integer = element.value.integer;
                }

                elements[i] = value;
            }

            if(element_type.category == TypeCategory::Integer && element_type.integer == IntegerType::Undetermined) {
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    elements[i] = determine_constant_integer(context->default_integer_type, elements[i]);
                }

                element_type.integer = context->default_integer_type;
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

            expect(expression_value, generate_runtime_expression(context, from, source, *expression.function_call.expression));

            string_buffer_append(source, ")");

            if(expression_value.type.category != TypeCategory::Function) {
                error(expression.function_call.expression->range, "Cannot call a non-function");

                return { false };
            }

            auto function_type = expression_value.type.function;

            string_buffer_append(source, "(");

            if(expression.function_call.parameters.count != function_type.parameters.count) {
                error(expression.range, "Incorrect number of parameters. Expected %zu, got %zu", function_type.parameters.count, expression.function_call.parameters.count);

                return { false };
            }

            for(size_t i = 0; i < function_type.parameters.count; i += 1) {
                char *parameter_source{};
                expect(parameter, generate_runtime_expression(context, from, &parameter_source, expression.function_call.parameters[i]));

                if(
                    parameter.type.category == TypeCategory::Integer &&
                    function_type.parameters[i].category == TypeCategory::Integer &&
                    parameter.type.integer == IntegerType::Undetermined
                ) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, function_type.parameters[i].integer);

                    string_buffer_append(source, ")");
                } else if(!types_equal(parameter.type, function_type.parameters[i])) {
                    error(expression.function_call.parameters[i].range, "Incorrect parameter type for parameter %d", i);

                    return { false };
                }

                string_buffer_append(source, "(");

                string_buffer_append(source, parameter_source);

                string_buffer_append(source, ")");

                if(i != function_type.parameters.count - 1) {
                    string_buffer_append(source, ",");
                }
            }

            string_buffer_append(source, ")");

            ExpressionValue value;
            value.category = ExpressionValueCategory::Anonymous;
            value.type = *function_type.return_type;

            return { 
                true,
                value
            };
        } break;

        case ExpressionType::BinaryOperation: {
            char *left_source{};
            expect(left, generate_expression(context, from, &left_source, *expression.binary_operation.left));

            char *right_source{};
            expect(right, generate_expression(context, from, &right_source, *expression.binary_operation.right));

            if(left.category == ExpressionValueCategory::Constant && right.category == ExpressionValueCategory::Constant) {
                expect(constant, evaluate_constant_binary_operation(
                    expression.binary_operation.binary_operator,
                    expression.range,
                    left.type,
                    left.constant,
                    right.type,
                    right.constant,
                    true
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
                if(
                    !(
                        left.type.category == TypeCategory::Integer && right.type.category == TypeCategory::Integer &&
                        (left.type.integer == IntegerType::Undetermined || right.type.integer == IntegerType::Undetermined)
                    ) &&
                    !types_equal(left.type, right.type)
                ) {
                    error(expression.range, "Mismatched types for binary operation");

                    return { false };
                }

                IntegerType integer_type;
                if(left.type.integer != IntegerType::Undetermined) {
                    integer_type = left.type.integer;
                } else {
                    integer_type = right.type.integer;
                }

                if(left.type.category == TypeCategory::Integer && left.type.integer == IntegerType::Undetermined) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, integer_type);

                    string_buffer_append(source, ")");
                }

                string_buffer_append(source, "(");

                switch(left.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, left_source);
                    } break;

                    case ExpressionValueCategory::Constant: {
                        if(!generate_constant_value(context, source, left.type, left.constant, expression.binary_operation.left->range)) {
                            return { false };
                        }
                    } break;

                    default: {
                        abort();
                    } break;
                }

                string_buffer_append(source, ")");

                Type result_type;
                switch(left.type.category) {
                    case TypeCategory::Integer: {
                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Addition: {
                                string_buffer_append(source, "+");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::Subtraction: {
                                string_buffer_append(source, "-");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::Multiplication: {
                                string_buffer_append(source, "*");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::Division: {
                                string_buffer_append(source, "/");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::Modulo: {
                                string_buffer_append(source, "%");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::Equal: {
                                string_buffer_append(source, "==");

                                result_type.category = TypeCategory::Boolean;
                            } break;
                            
                            case BinaryOperator::NotEqual: {
                                string_buffer_append(source, "!=");

                                result_type.category = TypeCategory::Boolean;
                            } break;
                            
                            case BinaryOperator::BitwiseAnd: {
                                string_buffer_append(source, "&");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;
                            
                            case BinaryOperator::BitwiseOr: {
                                string_buffer_append(source, "|");

                                result_type.category = TypeCategory::Integer;
                                result_type.integer = integer_type;
                            } break;

                            case BinaryOperator::BooleanAnd:
                            case BinaryOperator::BooleanOr:
                            default: {
                                abort();
                            } break;
                        }
                    } break;

                    case TypeCategory::Boolean: {
                        result_type.category = TypeCategory::Boolean;

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Equal: {
                                string_buffer_append(source, "==");
                            } break;

                            case BinaryOperator::NotEqual: {
                                string_buffer_append(source, "!=");
                            } break;

                            case BinaryOperator::BooleanAnd: {
                                string_buffer_append(source, "&&");
                            } break;

                            case BinaryOperator::BooleanOr: {
                                string_buffer_append(source, "||");
                            } break;

                            default: {
                                error(expression.range, "Cannot perform that operation on booleans");

                                return { false };
                            } break;
                        }
                    } break;

                    default: {
                        error(expression.range, "Cannot perform binary operations on this type");

                        return { false };
                    } break;
                }

                if(right.type.category == TypeCategory::Integer && right.type.integer == IntegerType::Undetermined) {
                    string_buffer_append(source, "(");

                    generate_integer_type(source, integer_type);

                    string_buffer_append(source, ")");
                }

                string_buffer_append(source, "(");

                switch(right.category) {
                    case ExpressionValueCategory::Anonymous:
                    case ExpressionValueCategory::Assignable: {
                        string_buffer_append(source, right_source);
                    } break;

                    case ExpressionValueCategory::Constant: {
                        if(!generate_constant_value(context, source, right.type, right.constant, expression.binary_operation.right->range)) {
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

            expect(expression_value, generate_expression(context, from, &expression_source, *expression.unary_operation.expression));

            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    switch(expression_value.category) {
                        case ExpressionValueCategory::Anonymous: {
                            error(expression.unary_operation.expression->range, "Cannot take pointers anonymous values");

                            return { false };
                        } break;

                        case ExpressionValueCategory::Constant: {
                            switch(expression_value.type.category) {
                                case TypeCategory::Function: {
                                    string_buffer_append(source, "&");

                                    string_buffer_append(source, expression_value.constant.function);

                                    ExpressionValue value;
                                    value.category = ExpressionValueCategory::Anonymous;
                                    value.type.category = TypeCategory::Pointer;
                                    value.type.pointer = heapify(expression_value.type);

                                    return {
                                        true,
                                        value
                                    };
                                } break;

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

                                default: {
                                    error(expression.unary_operation.expression->range, "Cannot take pointers to constants of this type");

                                    return { false };
                                } break;
                            }
                        } break;
                        
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "&(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            ExpressionValue value;
                            value.category = ExpressionValueCategory::Anonymous;
                            value.type.category = TypeCategory::Pointer;
                            value.type.pointer = heapify(expression_value.type);

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
                    if(expression_value.type.category != TypeCategory::Boolean) {
                        error(expression.unary_operation.expression->range, "Cannot do boolean inversion on non-boolean");

                        return { false };
                    }

                    ExpressionValue value;
                    value.type.category = TypeCategory::Boolean;

                    switch(expression_value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "!(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            value.category = ExpressionValueCategory::Anonymous;
                        } break;
                        
                        case ExpressionValueCategory::Constant: {
                            value.category = ExpressionValueCategory::Constant;
                            value.constant.boolean = !expression_value.constant.boolean;
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
                
                case UnaryOperator::Negation: {
                    if(expression_value.type.category != TypeCategory::Integer) {
                        error(expression.unary_operation.expression->range, "Cannot do negation on non-integer");

                        return { false };
                    }

                    ExpressionValue value;
                    value.type.category = TypeCategory::Integer;
                    value.type.integer = expression_value.type.integer;

                    switch(expression_value.category) {
                        case ExpressionValueCategory::Anonymous:
                        case ExpressionValueCategory::Assignable: {
                            string_buffer_append(source, "-(");

                            string_buffer_append(source, expression_source);

                            string_buffer_append(source, ")");

                            value.category = ExpressionValueCategory::Anonymous;
                        } break;
                        
                        case ExpressionValueCategory::Constant: {
                            value.category = ExpressionValueCategory::Constant;

                            switch(expression_value.type.integer) {
                                case IntegerType::Undetermined: {
                                    value.constant.integer = (uint64_t)-(int64_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Unsigned8: {
                                    value.constant.integer = (uint64_t)-(uint8_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Unsigned16: {
                                    value.constant.integer = (uint64_t)-(uint16_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Unsigned32: {
                                    value.constant.integer = (uint64_t)-(uint32_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Unsigned64: {
                                    value.constant.integer = (uint64_t)-(uint64_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Signed8: {
                                    value.constant.integer = (uint64_t)-(int8_t)(int64_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Signed16: {
                                    value.constant.integer = (uint64_t)-(int16_t)(int64_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Signed32: {
                                    value.constant.integer = (uint64_t)-(int32_t)(int64_t)expression_value.constant.integer;
                                } break;

                                case IntegerType::Signed64: {
                                    value.constant.integer = (uint64_t)-(int64_t)(int64_t)expression_value.constant.integer;
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
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::Cast: {
            char *expression_source{};
            expect(expression_value, generate_expression(context, from, &expression_source, *expression.cast.expression));

            expect(type, evaluate_type_expression(context, from, *expression.cast.type, true));

            switch(expression_value.category) {
                case ExpressionValueCategory::Anonymous:
                case ExpressionValueCategory::Assignable: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Integer: {
                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    
                                } break;

                                case TypeCategory::Pointer: {
                                    if(
                                        expression_value.type.integer != IntegerType::Undetermined && 
                                        expression_value.type.integer != context->unsigned_size_integer_type
                                    ) {
                                        error(expression.cast.expression->range, "Cannot cast to pointer from this integer type");

                                        return { false };
                                    }
                                } break;

                                default: {
                                    error(expression.cast.type->range, "Cannot cast integer to this type");

                                    return { false };
                                } break;
                            }
                        } break;

                        case TypeCategory::Pointer: {
                            switch(type.category) {
                                case TypeCategory::Integer: {
                                    if(type.integer != context->unsigned_size_integer_type) {
                                        error(expression.cast.expression->range, "Cannot cast from pointer to this integer type");

                                        return { false };
                                    }
                                } break;

                                case TypeCategory::Pointer: {
                                    
                                } break;

                                default: {
                                    error(expression.cast.type->range, "Cannot cast pointer to this type");

                                    return { false };
                                } break;
                            }
                        } break;

                        default: {
                            error(expression.cast.expression->range, "Cannot cast from this type");

                            return { false };
                        } break;
                    }

                    string_buffer_append(source, "(");

                    generate_type(context, source, source, type, expression.cast.type->range);

                    string_buffer_append(source, ")(");

                    string_buffer_append(source, expression_source);

                    string_buffer_append(source, ")");

                    ExpressionValue value;
                    value.category = ExpressionValueCategory::Anonymous;
                    value.type = type;

                    return {
                        true,
                        value
                    };
                } break;

                case ExpressionValueCategory::Constant: {
                    expect(constant, evaluate_constant_conversion(
                        *context,
                        expression_value.constant,
                        expression_value.type,
                        expression.cast.expression->range,
                        type,
                        expression.cast.type->range,
                        true
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

                default: {
                    abort();
                } break;
            }
        } break;

        case ExpressionType::ArrayType: {
            expect(type, evaluate_type_expression(context, from, *expression.array_type, true));

            ExpressionValue value;
            value.category = ExpressionValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.constant.type.category = TypeCategory::Array;
            value.constant.type.array = heapify(type);

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
                expect(return_type_value, evaluate_type_expression(context, from, *expression.function_type.return_type, true));

                return_type = return_type_value;
            }

            auto parameters = allocate<Type>(expression.function_type.parameters.count);

            for(size_t i = 0; i < expression.function_type.parameters.count; i += 1) {
                expect(parameter, evaluate_type_expression(context, from, expression.function_type.parameters[i].type, true));

                parameters[i] = parameter;
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

static bool generate_default_value(GenerationContext *context, char **source, Type type, FileRange range) {
    switch(type.category) {
        case TypeCategory::Integer: {
            string_buffer_append(source, "0");

            return true;
        } break;

        case TypeCategory::Boolean: {
            string_buffer_append(source, "false");

            return true;
        } break;

        case TypeCategory::Pointer: {
            string_buffer_append(source, "0");

            return true;
        } break;

        case TypeCategory::Array: {
            expect(mangled_name, maybe_register_array_type(context, *type.array, range));

            string_buffer_append(source, "(struct ");

            string_buffer_append(source, mangled_name);

            string_buffer_append(source, "){0,0}");

            return true;
        } break;

        case TypeCategory::Struct: {
            auto c_declaration = retrieve_c_declaration(*context, type._struct);

            assert(c_declaration.type == CDeclarationType::StructType);

            string_buffer_append(source, "(struct ");

            string_buffer_append(source, c_declaration.mangled_name);

            string_buffer_append(source, "){");

            for(size_t i = 0; i < c_declaration.struct_type.count; i += 1) {
                if(!generate_default_value(context, source, c_declaration.struct_type[i].type, range)) {
                    return false;
                }

                if(i != c_declaration.struct_type.count - 1) {
                    string_buffer_append(source, ",");
                }
            }

            string_buffer_append(source, "}");

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_statement(GenerationContext *context, char **source, Declaration from, Statement statement) {
    switch(statement.type) {
        case StatementType::Expression: {
            if(!generate_expression(context, from, source, statement.expression).status) {
                return false;
            }

            string_buffer_append(source, ";");

            return true;
        } break;

        case StatementType::VariableDeclaration: {
            switch(statement.variable_declaration.type) {
                case VariableDeclarationType::Uninitialized: {
                    expect(type, evaluate_type_expression(context, from, statement.variable_declaration.uninitialized, true));

                    if(!add_new_variable(context, statement.variable_declaration.name, type)) {
                        return false;
                    }

                    char *type_suffix_source{};
                    if(!generate_type(context, source, &type_suffix_source, type, statement.variable_declaration.uninitialized.range)) {
                        return false;
                    }

                    string_buffer_append(source, " ");

                    string_buffer_append(source, statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(source, " ");

                        string_buffer_append(source, type_suffix_source);
                    }

                    string_buffer_append(source, "=");

                    if(!generate_default_value(context, source, type, statement.variable_declaration.uninitialized.range)) {
                        return false;
                    }

                    string_buffer_append(source, ";");

                    return true;
                } break;
                
                case VariableDeclarationType::TypeElided: {
                    char *initializer_source{};
                    expect(initial_value, generate_expression(context, from, &initializer_source, statement.variable_declaration.type_elided));

                    Type actual_type;
                    if(initial_value.type.category == TypeCategory::Integer && initial_value.type.integer == IntegerType::Undetermined) {
                        actual_type.category = TypeCategory::Integer;
                        actual_type.integer = context->default_integer_type;
                    } else {
                        actual_type = initial_value.type;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, actual_type)) {
                        return false;
                    }
                    
                    char *type_suffix_source{};
                    if(!generate_type(context, source, &type_suffix_source, actual_type, statement.variable_declaration.type_elided.range)) {
                        return false;
                    }

                    string_buffer_append(source, " ");

                    string_buffer_append(source, statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(source, " ");

                        string_buffer_append(source, type_suffix_source);
                    }
                    
                    string_buffer_append(source, "=");

                    if(initial_value.category == ExpressionValueCategory::Constant) {
                        if(initial_value.type.category == TypeCategory::StaticArray) {
                            string_buffer_append(source, "{");

                            for(size_t i = 0; i < initial_value.type.static_array.length; i += 1) {
                                if(!generate_constant_value(
                                    context,
                                    source,
                                    *initial_value.type.static_array.type,
                                    initial_value.constant.static_array[i],
                                    statement.variable_declaration.type_elided.range
                                )) {
                                    return false;
                                }

                                if(i != initial_value.type.static_array.length - 1) {
                                    string_buffer_append(source, ",");
                                }
                            }

                            string_buffer_append(source, "}");
                        } else {
                            if(!generate_constant_value(
                                context,
                                source,
                                initial_value.type,
                                initial_value.constant,
                                statement.variable_declaration.type_elided.range
                            )) {
                                return false;
                            }
                        }
                    } else {
                        string_buffer_append(source, initializer_source);
                    }

                    string_buffer_append(source, ";");

                    return true;
                } break;
                
                case VariableDeclarationType::FullySpecified: {
                    expect(type, evaluate_type_expression(context, from, statement.variable_declaration.fully_specified.type, true));

                    char *initializer_source{};
                    expect(initial_value, generate_expression(context, from, &initializer_source, statement.variable_declaration.fully_specified.initializer));

                    if(
                        !(
                            type.category == TypeCategory::Integer &&
                            initial_value.type.category == TypeCategory::Integer &&
                            initial_value.type.integer == IntegerType::Undetermined
                        ) &&
                        !types_equal(type, initial_value.type)
                    ) {
                        error(statement.variable_declaration.fully_specified.initializer.range, "Assigning incorrect type");
                        
                        return false;
                    }

                    if(!add_new_variable(context, statement.variable_declaration.name, type)) {
                        return false;
                    }

                    char *type_suffix_source{};
                    if(!generate_type(context, source, &type_suffix_source, type, statement.variable_declaration.fully_specified.type.range)) {
                        return false;
                    }

                    string_buffer_append(source, " ");

                    string_buffer_append(source, statement.variable_declaration.name.text);

                    if(type_suffix_source != nullptr) {
                        string_buffer_append(source, " ");

                        string_buffer_append(source, type_suffix_source);
                    }
                    
                    string_buffer_append(source, "=");

                    if(initial_value.category == ExpressionValueCategory::Constant) {
                        if(initial_value.type.category == TypeCategory::StaticArray) {
                            string_buffer_append(source, "{");

                            for(size_t i = 0; i < initial_value.type.static_array.length; i += 1) {
                                if(!generate_constant_value(
                                    context,
                                    source,
                                    *initial_value.type.static_array.type,
                                    initial_value.constant.static_array[i],
                                    statement.variable_declaration.fully_specified.initializer.range
                                )) {
                                    return false;
                                }

                                if(i != initial_value.type.static_array.length - 1) {
                                    string_buffer_append(source, ",");
                                }
                            }

                            string_buffer_append(source, "}");
                        } else {
                            if(!generate_constant_value(
                                context,
                                source,
                                initial_value.type,
                                initial_value.constant,
                                statement.variable_declaration.fully_specified.initializer.range
                            )) {
                                return false;
                            }
                        }
                    } else {
                        string_buffer_append(source, initializer_source);
                    }

                    string_buffer_append(source, ";");

                    return true;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case StatementType::Assignment: {
            expect(target, generate_expression(context, from, source, statement.assignment.target));

            if(target.category != ExpressionValueCategory::Assignable) {
                error(statement.assignment.target.range, "Value is not assignable");

                return false;
            }

            string_buffer_append(source, "=");

            expect(value, generate_runtime_expression(context, from, source, statement.assignment.value));
            
            if(
                !(
                    target.type.category == TypeCategory::Integer &&
                    value.type.category == TypeCategory::Integer &&
                    value.type.integer == IntegerType::Undetermined
                ) &&
                !types_equal(target.type, value.type)
            ) {
                error(statement.assignment.value.range, "Assigning incorrect type");

                return false;
            }

            string_buffer_append(source, ";");

            return true;
        } break;

        case StatementType::LoneIf: {
            string_buffer_append(source, "if(");

            expect(condition, generate_runtime_expression(context, from, source, statement.lone_if.condition));

            string_buffer_append(source, ")");

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.lone_if.condition.range, "Non-boolean if statement condition");

                return false;
            }

            append(&(context->variable_context_stack), List<Variable>{});

            string_buffer_append(source, "{");

            for(auto child_statement : statement.lone_if.statements) {
                if(!generate_statement(context, source, from, child_statement)) {
                    return false;
                }
            }

            string_buffer_append(source, "}");

            context->variable_context_stack.count -= 1;

            return true;
        } break;

        case StatementType::WhileLoop: {
            string_buffer_append(source, "while(");

            expect(condition, generate_runtime_expression(context, from, source, statement.while_loop.condition));

            string_buffer_append(source, ")");

            if(condition.type.category != TypeCategory::Boolean) {
                error(statement.while_loop.condition.range, "Non-boolean while loop condition");

                return false;
            }

            append(&(context->variable_context_stack), List<Variable>{});

            string_buffer_append(source, "{");

            for(auto child_statement : statement.while_loop.statements) {
                if(!generate_statement(context, source, from, child_statement)) {
                    return false;
                }
            }

            string_buffer_append(source, "}");

            context->variable_context_stack.count -= 1;

            return true;
        } break;

        case StatementType::Return: {
            string_buffer_append(source, "return ");

            char *expression_source{};
            expect(expression_value, generate_runtime_expression(context, from, &expression_source, statement._return));

            if(
                !(
                    context->return_type.category == TypeCategory::Integer &&
                    expression_value.type.category == TypeCategory::Integer &&
                    expression_value.type.integer == IntegerType::Undetermined
                ) &&
                !types_equal(context->return_type, expression_value.type)
            ) {
                error(statement._return.range, "Mismatched return type");

                return { false };
            }

            string_buffer_append(source, expression_source);

            string_buffer_append(source, ";");

            return true;
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_compiler_directive(GenerationContext *context, Statement directive) {
    switch(directive.type) {
        case StatementType::Library: {
            for(auto library : context->libraries) {
                if(strcmp(library, directive.library) == 0) {
                    return true;
                }
            }

            append(&(context->libraries), directive.library);

            return true;
        } break;
    }
}

static bool generate_function_signature(GenerationContext *context, char **source, const char *name, Type return_type, Array<RuntimeFunctionParameter> parameters) {
    char *type_suffix_source{};
    if(!generate_type(context, source, &type_suffix_source, return_type, generated_range)) {
        return false;
    }

    string_buffer_append(source, " ");

    string_buffer_append(source, name);

    if(type_suffix_source != nullptr) {
        string_buffer_append(source, " ");

        string_buffer_append(source, type_suffix_source);
    }

    string_buffer_append(source, "(");
    
    for(size_t i = 0; i < parameters.count; i += 1) {
        char *parameter_type_suffix_source{};
        if(!generate_type(context, source, &parameter_type_suffix_source, parameters[i].type, generated_range)) {
            return false;
        }

        string_buffer_append(source, " ");

        string_buffer_append(source, parameters[i].name.text);

        if(parameter_type_suffix_source != nullptr) {
            string_buffer_append(source, " ");

            string_buffer_append(source, parameter_type_suffix_source);
        }

        if(i != parameters.count - 1) {
            string_buffer_append(source, ",");
        }
    }

    string_buffer_append(source, ")");

    return true;
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
                        error(statement.range, "Only constant declarations and compiler directives are allowed in global scope");

                        return { false };
                    }
                } break;
            }
        }

        for(auto &declaration : declarations) {
            declaration.is_top_level = true;
            declaration.file_module = to_array(declarations);

            set_child_declaration_parents(&declaration);
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

    auto previous_resolved_declaration_count = 0;

    GenerationContext context {
        unsigned_size_integer_type,
        signed_size_integer_type,
        signed_size_integer_type,
        to_array(global_constants),
        {
            files.count,
            file_modules
        }
    };

    auto main_found = false;
    for(auto declaration : file_modules[0].declarations) {
        if(strcmp(declaration.name.text, "main") == 0) {
            if(declaration.category != DeclarationCategory::FunctionDefinition) {
                error(declaration.name.range, "'main' must be a function");

                return { false };
            }

            if(!resolve_declaration(&context, declaration, true).status) {
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

    char *forward_declaration_source{};
    char *implementation_source{};

    while(true) {
        auto done = true;

        for(size_t i = 0; i < context.c_declarations.count; i += 1) {
            auto c_declaration = context.c_declarations[i];

            if(c_declaration.type == CDeclarationType::Function) {
                if(!generate_function_signature(
                    &context,
                    &forward_declaration_source,
                    c_declaration.mangled_name,
                    c_declaration.function.return_type,
                    c_declaration.function.parameters
                )){
                    return { false };
                }

                string_buffer_append(&forward_declaration_source, ";");

                if(!generate_function_signature(
                    &context,
                    &implementation_source,
                    c_declaration.mangled_name,
                    c_declaration.function.return_type,
                    c_declaration.function.parameters
                )){
                    return { false };
                }

                string_buffer_append(&implementation_source, "{");

                append(&context.variable_context_stack, List<Variable>{});

                context.return_type = c_declaration.function.return_type;

                for(auto parameter : c_declaration.function.parameters) {
                    if(!add_new_variable(&context, parameter.name, parameter.type)) {
                        return { false };
                    }
                }

                for(auto statement : c_declaration.function.statements) {
                    if(!generate_statement(&context, &implementation_source, c_declaration.function.declaration, statement)) {
                        return { false };
                    }
                }

                context.variable_context_stack.count -= 1;

                string_buffer_append(&implementation_source, "}");

                // Remove c declaration from list

                for(size_t j = i; j < context.c_declarations.count - 1; j += 1) {
                    context.c_declarations[j] = context.c_declarations[j + 1];
                }

                context.c_declarations.count -= 1;

                done = false;

                break;
            }
        }

        if(done) {
            break;
        }
    }

    char *full_source{};

    for(auto c_declaration : context.c_declarations) {
        switch(c_declaration.type) {
            case CDeclarationType::ArrayType: {
                string_buffer_append(&full_source, "struct ");

                string_buffer_append(&full_source, c_declaration.mangled_name);

                string_buffer_append(&full_source, "{long long int length;");

                Type type;
                type.category = TypeCategory::Pointer;
                type.pointer = heapify(c_declaration.array_type);

                char *type_suffix_source{};
                if(!generate_type(&context, &full_source, &type_suffix_source, type, generated_range)) {
                    return { false };
                }

                string_buffer_append(&full_source, " pointer ");

                if(type_suffix_source != nullptr) {
                    string_buffer_append(&full_source, type_suffix_source);
                }

                string_buffer_append(&full_source, ";};");
            } break;

            case CDeclarationType::StructType: {
                string_buffer_append(&full_source, "struct ");

                string_buffer_append(&full_source, c_declaration.mangled_name);

                string_buffer_append(&full_source, "{");

                for(auto member : c_declaration.struct_type) {
                    char *type_postfix_buffer{};
                    if(!generate_type(&context, &full_source, &type_postfix_buffer, member.type, generated_range)) {
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
            } break;

            case CDeclarationType::ExternalFunction: {
                if(!generate_function_signature(
                    &context,
                    &forward_declaration_source,
                    c_declaration.mangled_name,
                    c_declaration.external_function.return_type,
                    c_declaration.external_function.parameters
                )){
                    return { false };
                }

                string_buffer_append(&forward_declaration_source, ";");
            } break;

            case CDeclarationType::ArrayConstant: {
                Type type;
                type.category = TypeCategory::StaticArray;
                type.static_array.length = c_declaration.array_constant.elements.count;
                type.static_array.type = heapify(c_declaration.array_constant.type);

                char *type_suffix_source{};
                if(!generate_type(&context, &full_source, &type_suffix_source, type, generated_range)) {
                    return { false };
                }

                string_buffer_append(&full_source, " ");

                string_buffer_append(&full_source, c_declaration.mangled_name);

                string_buffer_append(&full_source, " ");

                if(type_suffix_source != nullptr) {
                    string_buffer_append(&full_source, type_suffix_source);
                }
                
                string_buffer_append(&full_source, "={");

                for(size_t j = 0; j < c_declaration.array_constant.elements.count; j += 1) {
                    if(!generate_constant_value(&context, &full_source, c_declaration.array_constant.type, c_declaration.array_constant.elements[j], generated_range)) {
                        return { false };
                    }

                    if(j != c_declaration.array_constant.elements.count - 1) {
                        string_buffer_append(&full_source, ",");
                    }
                }

                string_buffer_append(&full_source, "};");
            } break;

            default: {
                abort();
            } break;
        }
    }

    if(forward_declaration_source != nullptr) {
        string_buffer_append(&full_source, forward_declaration_source);
    }

    if(implementation_source != nullptr) {
        string_buffer_append(&full_source, implementation_source);
    }

    for(size_t i = 0; i < files.count; i += 1) {
        for(auto compiler_directive : file_modules[i].compiler_directives) {
            if(!generate_compiler_directive(&context, compiler_directive)) {
                return { false };
            }
        }
    }

    return {
        true,
        {
            full_source,
            to_array(context.libraries)
        }
    };
}