#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "list.h"
#include "util.h"
#include "path.h"
#include "lexer.h"
#include "parser.h"

struct Type;

struct ConstantValue;

struct ConstantParameter {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct ConstantScope {
    Array<Statement*> statements;

    Array<ConstantParameter> constant_parameters;

    bool is_top_level;

    ConstantScope *parent;

    const char *file_path;
};

enum struct TypeKind {
    FunctionTypeType,
    PolymorphicFunction,
    BuiltinFunction,
    Integer,
    UndeterminedInteger,
    Boolean,
    FloatType,
    UndeterminedFloat,
    TypeType,
    Void,
    Pointer,
    ArrayTypeType,
    StaticArray,
    StructType,
    PolymorphicStruct,
    UndeterminedStruct,
    FileModule
};

struct Type {
    TypeKind kind;

    Type(TypeKind kind) : kind { kind } {}
};

Type polymorphic_function_singleton { TypeKind::PolymorphicFunction };
Type builtin_function_singleton { TypeKind::BuiltinFunction };
Type undetermined_integer_singleton { TypeKind::UndeterminedInteger };
Type boolean_singleton { TypeKind::Boolean };
Type undetermined_float_singleton { TypeKind::UndeterminedFloat };
Type type_type_singleton { TypeKind::TypeType };
Type void_singleton { TypeKind::Void };
Type file_module_singleton { TypeKind::FileModule };

struct FunctionTypeType : Type {
    Array<Type*> parameters;

    Type *return_type;

    FunctionTypeType(
        Array<Type*> parameters,
        Type *return_type
    ) :
        Type { TypeKind::FunctionTypeType },
        parameters { parameters },
        return_type { return_type }
    {}
};

struct Integer : Type {
    RegisterSize size;

    bool is_signed;

    Integer(
        RegisterSize size,
        bool is_signed
    ) :
        Type { TypeKind::Integer },
        size { size },
        is_signed { is_signed }
    {}
};

struct FloatType : Type {
    RegisterSize size;

    FloatType(
        RegisterSize size
    ) :
        Type { TypeKind::FloatType },
        size { size }
    {}
};

struct Pointer : Type {
    Type *type;

    Pointer(
        Type *type
    ) :
        Type { TypeKind::Pointer },
        type { type }
    {}
};

struct ArrayTypeType : Type {
    Type *element_type;

    ArrayTypeType(
        Type *element_type
    ) :
        Type { TypeKind::ArrayTypeType },
        element_type { element_type }
    {}
};

struct StaticArray : Type {
    size_t length;

    Type *element_type;

    StaticArray(
        size_t length,
        Type *element_type
    ) :
        Type { TypeKind::StaticArray },
        length { length },
        element_type { element_type }
    {}
};

struct StructType : Type {
    struct Member {
        const char *name;

        Type *type;
    };

    StructDefinition *definition;

    Array<Member> members;

    StructType(
        StructDefinition *definition,
        Array<Member> members
    ) :
        Type { TypeKind::StructType },
        definition { definition },
        members { members }
    {}
};

struct PolymorphicStruct : Type {
    StructDefinition *definition;

    Type **parameter_types;

    ConstantScope parent;

    PolymorphicStruct(
        StructDefinition *definition,
        Type **parameter_types,
        ConstantScope parent
    ) :
        Type { TypeKind::PolymorphicStruct },
        definition { definition },
        parameter_types { parameter_types },
        parent { parent }
    {}
};

struct UndeterminedStruct : Type {
    struct Member {
        const char *name;

        Type *type;
    };

    Array<Member> members;

    UndeterminedStruct(
        Array<Member> members
    ) :
        Type { TypeKind::UndeterminedStruct },
        members { members }
    {}
};

static bool types_equal(Type *a, Type *b) {
    if(a->kind != b->kind) {
        return false;
    }

    auto kind = a->kind;

    if(kind == TypeKind::FunctionTypeType) {
        auto a_function_type = (FunctionTypeType*)a;
        auto b_function_type = (FunctionTypeType*)b;

        if(a_function_type->parameters.count != b_function_type->parameters.count) {
            return false;
        }

        for(size_t i = 0; i < a_function_type->parameters.count; i += 1) {
            if(!types_equal(a_function_type->parameters[i], b_function_type->parameters[i])) {
                return false;
            }
        }

        return types_equal(a_function_type->return_type, b_function_type->return_type);
    } else if(kind == TypeKind::PolymorphicFunction) {
        return false;
    } else if(kind == TypeKind::BuiltinFunction) {
        return false;
    } else if(kind == TypeKind::Integer) {
        auto a_integer = (Integer*)a;
        auto b_integer = (Integer*)b;

        return a_integer->size == b_integer->size && a_integer->is_signed == b_integer->is_signed;
    } else if(kind == TypeKind::UndeterminedInteger) {
        return true;
    } else if(kind == TypeKind::Boolean) {
        return true;
    } else if(kind == TypeKind::FloatType) {
        auto a_float_type = (FloatType*)a;
        auto b_float_type = (FloatType*)b;

        return a_float_type->size == b_float_type->size;
    } else if(kind == TypeKind::UndeterminedFloat) {
        return true;
    } else if(kind == TypeKind::TypeType) {
        return true;
    } else if(kind == TypeKind::Void) {
        return true;
    } else if(kind == TypeKind::Pointer) {
        auto a_pointer = (Pointer*)a;
        auto b_pointer = (Pointer*)b;

        return types_equal(a_pointer->type, b_pointer->type);
    } else if(kind == TypeKind::ArrayTypeType) {
        auto a_array_type = (ArrayTypeType*)a;
        auto b_array_type = (ArrayTypeType*)b;

        return types_equal(a_array_type->element_type, b_array_type->element_type);
    } else if(kind == TypeKind::StaticArray) {
        auto a_static_array = (StaticArray*)a;
        auto b_static_array = (StaticArray*)b;

        return types_equal(a_static_array->element_type, b_static_array->element_type) && a_static_array->length == b_static_array->length;
    } else if(kind == TypeKind::StructType) {
        auto a_struct = (StructType*)a;
        auto b_struct = (StructType*)b;

        if(a_struct->definition != b_struct->definition) {
            return false;
        }

        if(a_struct->members.count != b_struct->members.count) {
            return false;
        }

        for(size_t i = 0; i < a_struct->members.count; i += 1) {
            if(
                strcmp(a_struct->members[i].name, b_struct->members[i].name) != 0 ||
                !types_equal(a_struct->members[i].type, b_struct->members[i].type)
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::PolymorphicStruct) {
        auto a_polymorphic_struct = (PolymorphicStruct*)a;
        auto b_polymorphic_struct = (PolymorphicStruct*)b;

        return a_polymorphic_struct->definition != b_polymorphic_struct->definition;
    } else if(kind == TypeKind::UndeterminedStruct) {
        auto a_undetermined_struct = (UndeterminedStruct*)a;
        auto b_undetermined_struct = (UndeterminedStruct*)b;

        if(a_undetermined_struct->members.count != b_undetermined_struct->members.count) {
            return false;
        }

        for(size_t i = 0; i < a_undetermined_struct->members.count; i += 1) {
            if(
                strcmp(a_undetermined_struct->members[i].name, b_undetermined_struct->members[i].name) != 0 ||
                !types_equal(a_undetermined_struct->members[i].type, b_undetermined_struct->members[i].type)
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::FileModule) {
        return true;
    } else {
        abort();
    }
}

static const char *type_description(Type *type) {
    if(type->kind == TypeKind::FunctionTypeType) {
        auto function = (FunctionTypeType*)type;
        StringBuffer buffer {};

        string_buffer_append(&buffer, "(");

        for(size_t i = 0; i < function->parameters.count; i += 1) {
            string_buffer_append(&buffer, type_description(function->parameters[i]));

            if(i != function->parameters.count - 1) {
                string_buffer_append(&buffer, ",");
            }
        }

        string_buffer_append(&buffer, ")");

        if(function->return_type != nullptr) {
            string_buffer_append(&buffer, " -> ");
            string_buffer_append(&buffer, type_description(function->return_type));
        }

        return buffer.data;
    } else if(type->kind == TypeKind::PolymorphicFunction) {
        return "{function}";
    } else if(type->kind == TypeKind::BuiltinFunction) {
        return "{builtin}";
    } else if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;

        if(integer->is_signed) {
            switch(integer->size) {
                case RegisterSize::Size8: {
                    return "i8";
                } break;

                case RegisterSize::Size16: {
                    return "i16";
                } break;

                case RegisterSize::Size32: {
                    return "i32";
                } break;

                case RegisterSize::Size64: {
                    return "i64";
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            switch(integer->size) {
                case RegisterSize::Size8: {
                    return "u8";
                } break;

                case RegisterSize::Size16: {
                    return "u16";
                } break;

                case RegisterSize::Size32: {
                    return "u32";
                } break;

                case RegisterSize::Size64: {
                    return "u64";
                } break;

                default: {
                    abort();
                } break;
            }
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        return "{integer}";
    } else if(type->kind == TypeKind::Boolean) {
        return "bool";
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;
        switch(float_type->size) {
            case RegisterSize::Size32: {
                return "f32";
            } break;

            case RegisterSize::Size64: {
                return "f64";
            } break;

            default: {
                abort();
            } break;
        }
    } else if(type->kind == TypeKind::UndeterminedFloat) {
        return "{float}";
    } else if(type->kind == TypeKind::TypeType) {
        return "{type}";
    } else if(type->kind == TypeKind::Void) {
        return "void";
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)type;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "*");
        string_buffer_append(&buffer, type_description(pointer->type));

        return buffer.data;
    } else if(type->kind == TypeKind::ArrayTypeType) {
        auto array = (ArrayTypeType*)type;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "[]");
        string_buffer_append(&buffer, type_description(array->element_type));

        return buffer.data;
    } else if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        StringBuffer buffer {};

        string_buffer_append(&buffer, "[");
        string_buffer_append(&buffer, static_array->length);
        string_buffer_append(&buffer, "]");
        string_buffer_append(&buffer, type_description(static_array->element_type));

        return buffer.data;
    } else if(type->kind == TypeKind::StructType) {
        auto struct_type = (StructType*)type;
        return struct_type->definition->name.text;
    } else if(type->kind == TypeKind::PolymorphicStruct) {
        auto polymorphic_struct = (PolymorphicStruct*)type;
        return polymorphic_struct->definition->name.text;
    } else if(type->kind == TypeKind::UndeterminedStruct) {
        return "{struct}";
    } else if(type->kind == TypeKind::FileModule) {
        return "{module}";
    } else {
        abort();
    }
}

static bool is_runtime_type(Type *type) {
    if(
        type->kind == TypeKind::Integer ||
        type->kind == TypeKind::Boolean ||
        type->kind == TypeKind::FloatType ||
        type->kind == TypeKind::Pointer ||
        type->kind == TypeKind::ArrayTypeType ||
        type->kind == TypeKind::StaticArray ||
        type->kind == TypeKind::StructType
    ) {
        return true;
    } else {
        return false;
    }
}

enum struct ConstantValueKind {
    FunctionConstant,
    BuiltinFunctionConstant,
    IntegerConstant,
    FloatConstant,
    BooleanConstant,
    VoidConstant,
    PointerConstant,
    ArrayConstant,
    StaticArrayConstant,
    StructConstant,
    FileModuleConstant,
    TypeConstant
};

struct ConstantValue {
    ConstantValueKind kind;
};

ConstantValue void_constant_singleton { ConstantValueKind::VoidConstant };

struct FunctionConstant : ConstantValue {
    FunctionDeclaration *declaration;

    ConstantScope parent;

    FunctionConstant(
        FunctionDeclaration *declaration,
        ConstantScope parent
    ) :
    ConstantValue { ConstantValueKind::FunctionConstant },
    declaration { declaration },
    parent { parent }
    {}
};

struct BuiltinFunctionConstant : ConstantValue {
    const char *name;

    BuiltinFunctionConstant(
        const char *name
    ) :
        ConstantValue { ConstantValueKind::BuiltinFunctionConstant },
        name { name }
    {}
};

struct IntegerConstant : ConstantValue {
    uint64_t value;

    IntegerConstant(
        uint64_t value
    ) :
        ConstantValue { ConstantValueKind::IntegerConstant },
        value { value }
    {}
};

struct FloatConstant : ConstantValue {
    double value;

    FloatConstant(
        double value
    ) :
        ConstantValue { ConstantValueKind::FloatConstant },
        value { value }
    {}
};

struct BooleanConstant : ConstantValue {
    bool value;

    BooleanConstant(
        bool value
    ) :
        ConstantValue { ConstantValueKind::BooleanConstant },
        value { value }
    {}
};

struct PointerConstant : ConstantValue {
    uint64_t value;

    PointerConstant(
        uint64_t value
    ) :
        ConstantValue { ConstantValueKind::PointerConstant },
        value { value }
    {}
};

struct ArrayConstant : ConstantValue {
    uint64_t length;

    uint64_t pointer;

    ArrayConstant(
        uint64_t length,
        uint64_t pointer
    ) :
        ConstantValue { ConstantValueKind::ArrayConstant },
        length { length },
        pointer { pointer }
    {}
};

struct StaticArrayConstant : ConstantValue {
    ConstantValue **elements;

    StaticArrayConstant(
        ConstantValue **elements
    ) :
        ConstantValue { ConstantValueKind::StaticArrayConstant },
        elements { elements }
    {}
};

struct StructConstant : ConstantValue {
    ConstantValue **members;

    StructConstant(
        ConstantValue **members
    ) :
        ConstantValue { ConstantValueKind::StructConstant },
        members { members }
    {}
};

struct FileModuleConstant : ConstantValue {
    const char *path;

    Array<Statement*> statements;

    FileModuleConstant(
        const char *path,
        Array<Statement*> statements
    ) :
        ConstantValue { ConstantValueKind::FileModuleConstant },
        path { path },
        statements { statements }
    {}
};

struct TypeConstant : ConstantValue {
    Type *type;

    TypeConstant(
        Type *type
    ) :
        ConstantValue { ConstantValueKind::TypeConstant },
        type { type }
    {}
};

#define extract_constant_value(kind, value) extract_constant_value_internal<kind>(value, ConstantValueKind::kind)

template <typename T>
static T *extract_constant_value_internal(ConstantValue *value, ConstantValueKind kind) {
    assert(value->kind == kind);

    auto extracted_value = (T*)value;

    return extracted_value;
}

struct TypedConstantValue {
    Type *type;

    ConstantValue *value;
};

struct GlobalConstant {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct Variable {
    Identifier name;

    Type *type;

    size_t address_register;
};

struct RuntimeFunctionParameter {
    Identifier name;

    Type *type;
    FileRange type_range;
};

struct RuntimeFunction {
    const char *mangled_name;

    Array<RuntimeFunctionParameter> parameters;

    Type *return_type;

    FunctionDeclaration *declaration;

    Array<ConstantParameter> constant_parameters;

    ConstantScope parent;
};

struct LoadedFile {
    const char *path;

    Array<Statement*> statements;
};

struct VariableScope {
    ConstantScope constant_scope;

    List<Variable> variables;
};

struct GlobalInfo {
    Array<GlobalConstant> global_constants;

    RegisterSize address_integer_size;
    RegisterSize default_integer_size;
};

struct FunctionName {
    FunctionDeclaration *declaration;

    const char *name;
};

struct RegisteredStaticVariable {
    VariableDeclaration *declaration;

    const char *mangled_name;

    Type *type;
};

struct GenerationContext {
    Array<ConstantParameter> constant_parameters;

    Type *return_type;
    size_t return_parameter_register;

    bool in_breakable_scope;
    List<Jump*> break_jumps;

    List<VariableScope> variable_scope_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<RuntimeStatic*> statics;

    List<LoadedFile> loaded_files;

    List<RegisteredStaticVariable> static_variables;
};

static void error(ConstantScope scope, FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    auto current_scope = &scope;
    while(!current_scope->is_top_level) {
        current_scope = current_scope->parent;
    }

    fprintf(stderr, "Error: %s(%u,%u): ", current_scope->file_path, range.first_line, range.first_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    if(range.first_line == range.first_character) {
        auto file = fopen(current_scope->file_path, "rb");

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

static bool match_public_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        declaration_name = function_declaration->name.text;
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        declaration_name = constant_definition->name.text;
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        declaration_name = struct_definition->name.text;
    } else {
        return false;
    }

    return strcmp(declaration_name, name) == 0;
}

static bool match_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        declaration_name = function_declaration->name.text;
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        declaration_name = constant_definition->name.text;
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        declaration_name = struct_definition->name.text;
    } else if(statement->kind == StatementKind::Import) {
        auto import = (Import*)statement;

        declaration_name = path_get_file_component(import->path);
    } else {
        return false;
    }

    return strcmp(declaration_name, name) == 0;
}

static uint64_t register_size_to_byte_size(RegisterSize size) {
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

static uint64_t get_type_alignment(GlobalInfo info, Type *type);

static uint64_t get_struct_alignment(GlobalInfo info, StructType type) {
    size_t current_alignment = 1;

    for(auto member : type.members) {
        auto alignment = get_type_alignment(info, member.type);

        if(alignment > current_alignment) {
            current_alignment = alignment;
        }
    }

    return current_alignment;
}

static uint64_t get_type_alignment(GlobalInfo info, Type *type) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        return register_size_to_byte_size(integer->size);
    } else if(type->kind == TypeKind::Boolean) {
        return register_size_to_byte_size(info.default_integer_size);
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;
        return register_size_to_byte_size(float_type->size);
    } else if(type->kind == TypeKind::Pointer) {
        return register_size_to_byte_size(info.address_integer_size);
    } else if(type->kind == TypeKind::ArrayTypeType) {
        return register_size_to_byte_size(info.address_integer_size);
    } else if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        return get_type_alignment(info, static_array->element_type);
    } else if(type->kind == TypeKind::StructType) {
        auto struct_type = (StructType*)type;
        return get_struct_alignment(info, *struct_type);
    } else {
        abort();
    }
}

static uint64_t get_type_size(GlobalInfo info, Type *type);

static uint64_t get_struct_size(GlobalInfo info, StructType type) {
    uint64_t current_size = 0;

    for(auto member : type.members) {
        if(type.definition->is_union) {
            auto size = get_type_size(info, member.type);

            if(size > current_size) {
                current_size = size;
            }
        } else {
            auto alignment = get_type_alignment(info, member.type);

            auto alignment_difference = current_size % alignment;

            uint64_t offset;
            if(alignment_difference != 0) {
                offset = alignment - alignment_difference;
            } else {
                offset = 0;
            }

            auto size = get_type_size(info, member.type);

            current_size += offset + size;
        }        
    }

    return current_size;
}

static uint64_t get_type_size(GlobalInfo info, Type *type) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        return register_size_to_byte_size(integer->size);
    } else if(type->kind == TypeKind::Boolean) {
        return register_size_to_byte_size(info.default_integer_size);
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;
        return register_size_to_byte_size(float_type->size);
    } else if(type->kind == TypeKind::Pointer) {
        return register_size_to_byte_size(info.address_integer_size);
    } else if(type->kind == TypeKind::ArrayTypeType) {
        return 2 * register_size_to_byte_size(info.address_integer_size);
    } else if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        return static_array->length * get_type_alignment(info, static_array->element_type);
    } else if(type->kind == TypeKind::StructType) {
        auto struct_type = (StructType*)type;
        return get_struct_size(info, *struct_type);
    } else {
        abort();
    }
}

static uint64_t get_struct_member_offset(GlobalInfo info, StructType type, size_t member_index) {
    if(type.definition->is_union) {
        return 0;
    }

    uint64_t current_offset = 0;

    for(auto i = 0; i < member_index; i += 1) {
        auto alignment = get_type_alignment(info, type.members[i].type);

        auto alignment_difference = current_offset % alignment;

        uint64_t offset;
        if(alignment_difference != 0) {
            offset = alignment - alignment_difference;
        } else {
            offset = 0;
        }

        auto size = get_type_size(info, type.members[i].type);

        current_offset += offset + size;
    }
    
    auto alignment = get_type_alignment(info, type.members[member_index].type);

    auto alignment_difference = current_offset % alignment;

    uint64_t offset;
    if(alignment_difference != 0) {
        offset = alignment - alignment_difference;
    } else {
        offset = 0;
    }

    return current_offset + offset;
}

static bool check_undetermined_integer_to_integer_coercion(ConstantScope scope, FileRange range, Integer target_type, int64_t value, bool probing) {
    bool in_range;
    if(target_type.is_signed) {
        int64_t min;
        int64_t max;
        switch(target_type.size) {
            case RegisterSize::Size8: {
                min = INT8_MIN;
                max = INT8_MAX;
            } break;

            case RegisterSize::Size16: {
                min = INT16_MIN;
                max = INT16_MAX;
            } break;

            case RegisterSize::Size32: {
                min = INT32_MIN;
                max = INT32_MAX;
            } break;

            case RegisterSize::Size64: {
                min = INT64_MIN;
                max = INT64_MAX;
            } break;

            default: {
                abort();
            } break;
        }

        in_range = value >= min && value <= max;
    } else {
        if(value < 0) {
            in_range = false;
        } else {
            uint64_t max;
            switch(target_type.size) {
                case RegisterSize::Size8: {
                    max = UINT8_MAX;
                } break;

                case RegisterSize::Size16: {
                    max = UINT16_MAX;
                } break;

                case RegisterSize::Size32: {
                    max = UINT32_MAX;
                } break;

                case RegisterSize::Size64: {
                    max = UINT64_MAX;
                } break;

                default: {
                    abort();
                } break;
            }

            in_range = (uint64_t)value <= max;
        }
    }

    if(!in_range) {
        if(!probing) {
            error(scope, range, "Constant '%zd' cannot fit in '%s'. You must cast explicitly", value, type_description(&target_type));
        }

        return false;
    }

    return true;
}

static Result<IntegerConstant*> coerce_constant_to_integer_type(
    ConstantScope scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Integer target_type,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        if(integer->size != target_type.size || integer->size != target_type.size) {
            if(!probing) {
                error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(integer), type_description(&target_type));
            }

            return { false };
        }

        auto integer_value = (IntegerConstant*)value;

        return {
            true,
            integer_value
        };
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value->value, probing)) {
            return { false };
        }

        return {
            true,
            integer_value
        };
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
        }

        return { false };
    }
}

static Result<IntegerConstant*> coerce_constant_to_undetermined_integer(
    ConstantScope scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        auto integer_value = (IntegerConstant*)value;

        switch(integer->size) {
            case RegisterSize::Size8: {
                return {
                    true,
                    new IntegerConstant {
                        (uint8_t)integer_value->value
                    }
                };
            } break;

            case RegisterSize::Size16: {
                return {
                    true,
                    new IntegerConstant {
                        (uint16_t)integer_value->value
                    }
                };
            } break;

            case RegisterSize::Size32: {
                return {
                    true,
                    new IntegerConstant {
                        (uint32_t)integer_value->value
                    }
                };
            } break;

            case RegisterSize::Size64: {
                return {
                    true,
                    new IntegerConstant {
                        integer_value->value
                    }
                };
            } break;

            default: {
                abort();
            } break;
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        return {
            true,
            integer_value
        };
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%s' to '{integer}'", type_description(type));
        }

        return { false };
    }
}

static Result<PointerConstant*> coerce_constant_to_pointer_type(
    ConstantScope scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Pointer target_type,
    bool probing
) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        return {
            true,
            new PointerConstant {
                integer_value->value
            }
        };
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)type;

        if(types_equal(pointer->type, target_type.type)) {
            auto pointer_value = (PointerConstant*)value;

            return {
                true,
                pointer_value
            };
        }
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<ConstantValue*> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Type *target_type,
    bool probing
) {
    if(target_type->kind == TypeKind::Integer) {
        auto integer = (Integer*)target_type;

        expect(integer_value, coerce_constant_to_integer_type(scope, range, type, value, *integer, probing));

        return {
            true,
            integer_value
        };
    } else if(target_type->kind == TypeKind::UndeterminedInteger) {
        expect(integer_value, coerce_constant_to_undetermined_integer(scope, range, type, value, probing));

        return {
            true,
            integer_value
        };
    } else if(target_type->kind == TypeKind::FloatType) {
        auto target_float_type = (FloatType*)target_type;

        if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            return {
                true,
                new FloatConstant {
                    (double)integer_value->value
                }
            };
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            if(target_float_type->size == float_type->size) {
                return {
                    true,
                    value
                };
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            return {
                true,
                value
            };
        }
    } else if(target_type->kind == TypeKind::UndeterminedFloat) {
        if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            return {
                true,
                new FloatConstant {
                    value
                }
            };
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            return {
                true,
                value
            };
        }
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        expect(pointer_value, coerce_constant_to_pointer_type(scope, range, type, value, *target_pointer, probing));

        return {
            true,
            pointer_value
        };
    } else if(target_type->kind == TypeKind::ArrayTypeType) {
        auto target_array_type = (ArrayTypeType*)target_type;

        if(type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)type;
            if(types_equal(target_array_type->element_type, array_type->element_type)) {
                return {
                    true,
                    value
                };
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;
            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = (StructConstant*)value;

                auto pointer_result = coerce_constant_to_pointer_type(
                    scope,
                    range,
                    undetermined_struct->members[0].type,
                    undetermined_struct_value->members[0],
                    {
                        target_array_type->element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_constant_to_integer_type(
                        scope,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        {
                            info.address_integer_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        return {
                            true,
                            new ArrayConstant {
                                pointer_result.value->value,
                                length_result.value->value,
                            }
                        };
                    }
                }
            }
        }
    } else if(types_equal(type, target_type)) {
        return {
            true,
            value
        };
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return { false };
}

static Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope scope,
    Type *type,
    ConstantValue *value,
    FileRange range,
    Type *index_type,
    ConstantValue *index_value,
    FileRange index_range
) {
    expect(index, coerce_constant_to_integer_type(
        scope,
        index_range,
        index_type,
        index_value,
        {
            info.address_integer_size,
            false
        },
        false
    ));

    if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        if(index->value >= static_array->length) {
            error(scope, index_range, "Array index %zu out of bounds", index);

            return { false };
        }

        auto static_array_value = (StaticArrayConstant*)value;

        return {
            true,
            {
                static_array->element_type,
                static_array_value->elements[index->value]
            }
        };
    } else {
        error(scope, range, "Cannot index %s", type_description(type));

        return { false };
    }
}

static Result<Type*> determine_binary_operation_type(ConstantScope scope, FileRange range, Type *left, Type *right) {
    if(left->kind == TypeKind::Boolean || right->kind == TypeKind::Boolean) {
        return {
            true,
            left
        };
    } else if(left->kind == TypeKind::Pointer) {
        return {
            true,
            left
        };
    } else if(right->kind == TypeKind::Pointer) {
        return {
            true,
            right
        };
    } else if(left->kind == TypeKind::Integer && right->kind == TypeKind::Integer) {
        auto left_integer = (Integer*)left;
        auto right_integer = (Integer*)right;

        RegisterSize largest_size;
        if(left_integer->size > right_integer->size) {
            largest_size = left_integer->size;
        } else {
            largest_size = right_integer->size;
        }

        auto is_either_signed = left_integer->is_signed || right_integer->is_signed;

        return {
            true,
            new Integer {
                largest_size,
                is_either_signed
            }
        };
    } else if(left->kind == TypeKind::FloatType && right->kind == TypeKind::FloatType) {
        auto left_float = (FloatType*)left;
        auto right_float = (FloatType*)right;

        RegisterSize largest_size;
        if(left_float->size > right_float->size) {
            largest_size = left_float->size;
        } else {
            largest_size = right_float->size;
        }

        return {
            true,
            new FloatType {
                largest_size
            }
        };
    } else if(left->kind == TypeKind::FloatType) {
        return {
            true,
            left
        };
    } else if(right->kind == TypeKind::FloatType) {
        return {
            true,
            right
        };
    } else if(left->kind == TypeKind::UndeterminedFloat || right->kind == TypeKind::UndeterminedFloat) {
        return {
            true,
            left
        };
    } else if(left->kind == TypeKind::Integer) {
        return {
            true,
            left
        };
    } else if(right->kind == TypeKind::Integer) {
        return {
            true,
            right
        };
    } else if(left->kind == TypeKind::UndeterminedInteger || right->kind == TypeKind::UndeterminedInteger) {
        return {
            true,
            left
        };
    } else {
        error(scope, range, "Mismatched types '%s' and '%s'", type_description(left), type_description(right));

        return { false };
    }
}

static Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope scope,
    FileRange range,
    BinaryOperation::Operator binary_operator,
    FileRange left_range,
    Type *left_type,
    ConstantValue *left_value,
    FileRange right_range,
    Type *right_type,
    ConstantValue *right_value
) {
    expect(type, determine_binary_operation_type(scope, range, left_type, right_type));

    expect(coerced_left_value, coerce_constant_to_type(info, scope, left_range, left_type, left_value, type, false));

    expect(coerced_right_value, coerce_constant_to_type(info, scope, right_range, right_type, right_value, type, false));

    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;

        auto left = (IntegerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::IntegerConstant);

        auto right = (IntegerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::IntegerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            left->value + right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            left->value - right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Multiplication: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value * (int64_t)right->value;
                } else {
                    result = left->value * right->value;
                }

                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            result
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Division: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value / (int64_t)right->value;
                } else {
                    result = left->value / right->value;
                }

                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            result
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Modulo: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value % (int64_t)right->value;
                } else {
                    result = left->value % right->value;
                }

                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            result
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            left->value & right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return {
                    true,
                    {
                        integer,
                        new IntegerConstant {
                            left->value | right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value == right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::LessThan: {
                bool result;
                if(integer->is_signed) {
                    result = (int64_t)left->value < (int64_t)right->value;
                } else {
                    result = left->value < right->value;
                }

                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            result
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                bool result;
                if(integer->is_signed) {
                    result = (int64_t)left->value > (int64_t)right->value;
                } else {
                    result = left->value > right->value;
                }

                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            result
                        }
                    }
                };
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return { false };
            } break;
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto left = (IntegerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::IntegerConstant);

        auto right = (IntegerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::IntegerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            left->value + right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            left->value - right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            (uint64_t)((int64_t)left->value * (int64_t)right->value)
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Division: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            (uint64_t)((int64_t)left->value / (int64_t)right->value)
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Modulo: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            (uint64_t)((int64_t)left->value % (int64_t)right->value)
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            left->value & right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return {
                    true,
                    {
                        &undetermined_integer_singleton,
                        new IntegerConstant {
                            left->value | right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value == right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::LessThan: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            (int64_t)left->value < (int64_t)right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            (int64_t)left->value > (int64_t)right->value
                        }
                    }
                };
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return { false };
            } break;
        }
    } else if(type->kind == TypeKind::Boolean) {
        auto left = (BooleanConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::BooleanConstant);

        auto right = (BooleanConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::BooleanConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value && right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::BooleanOr: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value || right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value == right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on booleans");

                return { false };
            } break;
        }
    } else if(type->kind == TypeKind::FloatType || type->kind == TypeKind::UndeterminedFloat) {
        auto left = (FloatConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::FloatConstant);

        auto right = (FloatConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::FloatConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return {
                    true,
                    {
                        type,
                        new FloatConstant {
                            left->value + right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return {
                    true,
                    {
                        type,
                        new FloatConstant {
                            left->value - right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return {
                    true,
                    {
                        type,
                        new FloatConstant {
                            left->value * right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Division: {
                return {
                    true,
                    {
                        type,
                        new FloatConstant {
                            left->value / right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value == right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return { false };
            } break;
        }
    } else if(type->kind == TypeKind::Pointer) {
        auto left = (PointerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::PointerConstant);

        auto right = (PointerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::PointerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value == right->value
                        }
                    }
                };
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return {
                    true,
                    {
                        &boolean_singleton,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return { false };
            } break;
        }
    } else {
        abort();
    }
}

static Result<ConstantValue*> evaluate_constant_cast(
    GlobalInfo info,
    ConstantScope scope,
    Type *type,
    ConstantValue *value,
    FileRange value_range,
    Type *target_type,
    FileRange target_range,
    bool probing
) {
    auto coerce_result = coerce_constant_to_type(
        info,
        scope,
        value_range,
        type,
        value,
        target_type,
        true
    );

    if(coerce_result.status) {
        return {
            true,
            coerce_result.value
        };
    }

    if(target_type->kind == TypeKind::Integer) {
        auto target_integer = (Integer*)target_type;

        uint64_t result;

        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            auto integer_value = (IntegerConstant*)value;

            if(integer->is_signed) {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }        
        } else if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            result = integer_value->value;
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double from_value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            if(target_integer->is_signed) {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)from_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)from_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)from_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (int64_t)from_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)from_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)from_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)from_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (uint64_t)from_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            auto float_value = (FloatConstant*)value;

            if(target_integer->is_signed) {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)float_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)float_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)float_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (int64_t)float_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)float_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)float_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)float_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (uint64_t)float_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            if(target_integer->size == info.address_integer_size && !target_integer->is_signed) {
                auto pointer_value = (PointerConstant*)value;

                result = pointer_value->value;
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(pointer), type_description(target_integer));
                }

                return { false };
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_integer));
            }

            return { false };
        }

        return {
            true,
            new IntegerConstant {
                result
            }
        };
    } else if(target_type->kind == TypeKind::FloatType) {
        auto target_float_type = (FloatType*)target_type;

        double result;
        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            auto integer_value = (IntegerConstant*)value;

            double from_value;
            if(integer->is_signed) {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(int8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(int16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(int32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)(int64_t)integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(uint8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(uint16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(uint32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)from_value;
                } break;

                case RegisterSize::Size64: {
                    result = from_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)(int64_t)integer_value->value;
                } break;

                case RegisterSize::Size64: {
                    result = (double)(int64_t)integer_value->value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double from_value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)from_value;
                } break;

                case RegisterSize::Size64: {
                    result = from_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            auto float_value = (FloatConstant*)value;

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    result = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_float_type));
            }

            return { false };
        }

        return {
            true,
            new FloatConstant {
                result
            }
        };
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        uint64_t result;

        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            if(integer->size == info.address_integer_size && !integer->is_signed) {
                auto integer_value = (IntegerConstant*)value;

                result = integer_value->value;
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(integer), type_description(target_pointer));
                }

                return { false };
            }
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            auto pointer_value = (PointerConstant*)value;

            result = pointer_value->value;
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_pointer));
            }

            return { false };
        }

        return {
            true,
            new PointerConstant {
                result
            }
        };
    } else {
        if(!probing) {
            error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_type));
        }

        return { false };
    }
}

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

static Result<Type*> coerce_to_default_type(GlobalInfo info, ConstantScope scope, FileRange range, Type *type) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        return {
            true,
            new Integer {
                info.default_integer_size,
                true
            }
        };
    } else if(type->kind == TypeKind::UndeterminedFloat) {
        return {
            true,
            new FloatType {
                info.default_integer_size
            }
        };
    } else if(type->kind == TypeKind::UndeterminedStruct) {
        error(scope, range, "Undetermined struct types cannot exist at runtime");

        return { false };
    } else {
        return {
            true,
            type
        };
    }
}

static Result<Type*> evaluate_type_expression(GlobalInfo info, ConstantScope scope, GenerationContext *context, Expression *expression);

static Result<TypedConstantValue> resolve_declaration(GlobalInfo info, ConstantScope scope, GenerationContext *context, Statement *declaration);

static Result<TypedConstantValue> evaluate_constant_expression(GlobalInfo info, ConstantScope scope, GenerationContext *context, Expression *expression) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        for(auto constant_parameter : context->constant_parameters) {
            if(strcmp(constant_parameter.name, named_reference->name.text) == 0) {
                return {
                    true,
                    {
                        constant_parameter.type,
                        constant_parameter.value
                    }
                };
            }
        }

        auto current_scope = &scope;
        while(true) {
            for(auto statement : current_scope->statements) {
                if(match_declaration(statement, named_reference->name.text)) {
                    expect(value, resolve_declaration(info, *current_scope, context, statement));

                    return {
                        true,
                        value
                    };
                } else if(statement->kind == StatementKind::UsingStatement) {
                    auto using_statement = (UsingStatement*)statement;

                    expect(expression_value, evaluate_constant_expression(info, *current_scope, context, using_statement->module));

                    if(expression_value.type->kind != TypeKind::FileModule) {
                        error(*current_scope, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                        return { false };
                    }

                    auto file_module = (FileModuleConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

                    for(auto statement : file_module->statements) {
                        if(match_public_declaration(statement, named_reference->name.text)) {
                            ConstantScope module_scope;
                            module_scope.statements = file_module->statements;
                            module_scope.constant_parameters = {};
                            module_scope.is_top_level = true;
                            module_scope.file_path = file_module->path;

                            expect(value, resolve_declaration(info, module_scope, context, statement));

                            return {
                                true,
                                value
                            };
                        }
                    }
                }
            }

            for(auto constant_parameter : current_scope->constant_parameters) {
                if(strcmp(constant_parameter.name, named_reference->name.text) == 0) {
                    return {
                        true,
                        {
                            constant_parameter.type,
                            constant_parameter.value
                        }
                    };
                }
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(strcmp(named_reference->name.text, global_constant.name) == 0) {
                return {
                    true,
                    {
                        global_constant.type,
                        global_constant.value
                    }
                };
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %s", named_reference->name.text);

        return { false };
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect(expression_value, evaluate_constant_expression(info, scope, context, member_reference->expression));

        if(expression_value.type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)expression_value.type;
            auto array_value = (ArrayConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::ArrayConstant);

            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        array_value->length
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                return {
                    true,
                    new Pointer {
                        array_type->element_type
                    },
                    new PointerConstant {
                        array_value->pointer
                    }
                };
            } else {
                error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return { false };
            }
        } else if(expression_value.type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)expression_value.type;
            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        static_array->length
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                error(scope, member_reference->name.range, "Cannot take pointer to static array in constant context", member_reference->name.text);

                return { false };
            } else {
                error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return { false };
            }
        } else if(expression_value.type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)expression_value.type;
            auto struct_value = (StructConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::StructConstant);

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(strcmp(member_reference->name.text, struct_type->members[i].name) == 0) {
                    return {
                        true,
                        {
                            struct_type->members[i].type,
                            struct_value->members[i]
                        }
                    };
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else if(expression_value.type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)expression_value.type;
            auto undetermined_struct_value = (StructConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::StructConstant);

            for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                if(strcmp(member_reference->name.text, undetermined_struct->members[i].name) == 0) {
                    return {
                        true,
                        {
                            undetermined_struct->members[i].type,
                            undetermined_struct_value->members[i]
                        }
                    };
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else if(expression_value.type->kind == TypeKind::FileModule) {
            auto file_module_value = (FileModuleConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

            for(auto statement : file_module_value->statements) {
                if(match_public_declaration(statement, member_reference->name.text)) {
                    ConstantScope module_scope;
                    module_scope.statements = file_module_value->statements;
                    module_scope.constant_parameters = {};
                    module_scope.is_top_level = true;
                    module_scope.file_path = file_module_value->path;

                    expect(value, resolve_declaration(info, module_scope, context, statement));

                    return {
                        true,
                        value
                    };
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else {
            error(scope, member_reference->expression->range, "Type '%s' has no members", type_description(expression_value.type));

            return { false };
        }
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect(expression_value, evaluate_constant_expression(info, scope, context, index_reference->expression));

        expect(index, evaluate_constant_expression(info, scope, context, index_reference->index));

        return evaluate_constant_index(
            info,
            scope,
            expression_value.type,
            expression_value.value,
            index_reference->expression->range,
            index.type,
            index.value,
            index_reference->index->range
        );
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return {
            true,
            {
                &undetermined_integer_singleton,
                new IntegerConstant {
                    integer_literal->value
                }
            }
        };
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return {
            true,
            {
                &undetermined_float_singleton,
                new FloatConstant {
                    float_literal->value
                }
            }
        };
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.count;

        auto characters = allocate<ConstantValue*>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = new IntegerConstant {
                (uint64_t)string_literal->characters[i]
            };
        }

        return {
            true,
            {
                new StaticArray {
                    character_count,
                    new Integer {
                        RegisterSize::Size8,
                        false
                    }
                },
                new StaticArrayConstant {
                    characters
                }
            }
        };
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return { false };
        }

        expect(first_element, evaluate_constant_expression(info, scope, context, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(scope, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return { false };
        }

        auto elements = allocate<ConstantValue*>(element_count);
        elements[0] = first_element.value;

        for(size_t i = 1; i < element_count; i += 1) {
            expect(element, evaluate_constant_expression(info, scope, context, array_literal->elements[i]));

            expect(element_value, coerce_constant_to_type(
                info,
                scope,
                array_literal->elements[i]->range,
                element.type,
                element.value,
                determined_element_type,
                false
            ));

            elements[i] = element_value;
        }

        return {
            true,
            {
                new StaticArray {
                    element_count,
                    determined_element_type
                },
                new StaticArrayConstant {
                    elements
                }
            }
        };
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        auto member_count = struct_literal->members.count;

        if(member_count == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return { false };
        }

        auto members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<ConstantValue*>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            auto member_name = struct_literal->members[i].name;

            for(size_t j = 0; j < member_count; j += 1) {
                if(j != i && strcmp(member_name.text, struct_literal->members[j].name.text) == 0) {
                    error(scope, member_name.range, "Duplicate struct member %s", member_name.text);

                    return { false };
                }
            }

            expect(member, evaluate_constant_expression(info, scope, context, struct_literal->members[i].value));

            members[i] = {
                member_name.text,
                member.type
            };

            member_values[i] = member.value;
        }

        return {
            true,
            {
                new UndeterminedStruct {
                    {
                        member_count,
                        members
                    }
                },
                new StructConstant {
                    member_values
                }
            }
        };
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect(expression_value, evaluate_constant_expression(info, scope, context, function_call->expression));

        if(expression_value.type->kind == TypeKind::FunctionTypeType) {
            auto function = (FunctionTypeType*)expression_value.type;
            error(scope, function_call->range, "Function calls not allowed in global context");

            return { false };
        } else if(expression_value.type->kind == TypeKind::BuiltinFunction) {
            auto builtin_function_value = (BuiltinFunctionConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::BuiltinFunctionConstant);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, evaluate_constant_expression(info, scope, context, function_call->parameters[0]));

                Type *type;
                if(parameter_value.type->kind == TypeKind::TypeType) {
                    type = extract_constant_value(TypeConstant, parameter_value.value)->type;
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(scope, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return { false };
                }

                auto size = get_type_size(info, type);

                return {
                    true,
                    {
                        new Integer {
                            info.address_integer_size,
                            false
                        },
                        new IntegerConstant {
                            size
                        }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, evaluate_constant_expression(info, scope, context, function_call->parameters[0]));

                return {
                    true,
                    {
                        &type_type_singleton,
                        new TypeConstant { parameter_value.type }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "memcpy") == 0) {
                error(scope, function_call->range, "'memcpy' cannot be called in a constant context");

                return { false };
            } else {
                abort();
            }
        } else if(expression_value.type->kind == TypeKind::TypeType) {
            auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

            if(type->kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = (PolymorphicStruct*)type;
                auto definition = polymorphic_struct->definition;

                auto parameter_count = definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.count);

                    return { false };
                }

                auto parameters = allocate<ConstantParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect(parameter, evaluate_constant_expression(info, scope, context, function_call->parameters[i]));

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
                        definition->parameters[i].name.text,
                        polymorphic_struct->parameter_types[i],
                        parameter_value
                    };
                }

                auto member_count = definition->members.count;

                auto members = allocate<StructType::Member>(member_count);

                for(size_t i = 0; i < member_count; i += 1) {
                    for(size_t j = 0; j < member_count; j += 1) {
                        if(j != i && strcmp(definition->members[i].name.text, definition->members[j].name.text) == 0) {
                            error(polymorphic_struct->parent, definition->members[i].name.range, "Duplicate struct member name %s", definition->members[i].name.text);

                            return { false };
                        }
                    }

                    expect(type, evaluate_type_expression(info, polymorphic_struct->parent, context, definition->members[i].type));

                    if(!is_runtime_type(type)) {
                        error(polymorphic_struct->parent, definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(type));

                        return { false };
                    }

                    members[i] = {
                        definition->members[i].name.text,
                        type
                    };
                }

                return {
                    true,
                    {
                        &type_type_singleton,
                        new TypeConstant {
                            new StructType {
                                definition,
                                {
                                    member_count,
                                    members
                                }
                            }
                        }
                    }
                };
            } else {
                error(scope, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return { false };
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call non-function '%s'", type_description(expression_value.type));

            return { false };
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        expect(left, evaluate_constant_expression(info, scope, context, binary_operation->left));

        expect(right, evaluate_constant_expression(info, scope, context, binary_operation->right));

        expect(value, evaluate_constant_binary_operation(
            info,
            scope,
            binary_operation->range,
            binary_operation->binary_operator,
            binary_operation->left->range,
            left.type,
            left.value,
            binary_operation->right->range,
            right.type,
            right.value
        ));

        return {
            true,
            value
        };
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect(expression_value, evaluate_constant_expression(info, scope, context, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                if(expression_value.type->kind == TypeKind::TypeType) {
                    auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

                    if(
                        !is_runtime_type(type) &&
                        type->kind != TypeKind::Void &&
                        type->kind != TypeKind::FunctionTypeType
                    ) {
                        error(scope, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                        return { false };
                    }

                    return {
                        true,
                        {
                            &type_type_singleton,
                            new TypeConstant {
                                new Pointer {
                                    type
                                }
                            }
                        }
                    };
                } else {
                    error(scope, unary_operation->range, "Cannot take pointers at constant time");

                    return { false };
                }
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type->kind == TypeKind::Boolean) {
                    auto boolean_value = (BooleanConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::BooleanConstant);

                    return {
                        true,
                        {
                            &boolean_singleton,
                            new BooleanConstant {
                                !boolean_value->value
                            }
                        }
                    };
                } else {
                    error(scope, unary_operation->expression->range, "Expected a boolean, got '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type->kind == TypeKind::Integer || expression_value.type->kind == TypeKind::UndeterminedInteger) {
                    auto integer_value = (IntegerConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::IntegerConstant);

                    return {
                        true,
                        {
                            expression_value.type,
                            new IntegerConstant {
                                -integer_value->value
                            }
                        }
                    };
                } else if(expression_value.type->kind == TypeKind::FloatType || expression_value.type->kind == TypeKind::UndeterminedFloat) {
                    auto float_value = (FloatConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::FloatConstant);

                    return {
                        true,
                        {
                            expression_value.type,
                            new FloatConstant {
                                -float_value->value
                            }
                        }
                    };
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect(expression_value, evaluate_constant_expression(info, scope, context, cast->expression));

        expect(type, evaluate_type_expression(info, scope, context, cast->type));

        expect(value, evaluate_constant_cast(
            info,
            scope,
            expression_value.type,
            expression_value.value,
            cast->expression->range,
            type,
            cast->type->range,
            false
        ));

        return {
            true,
            {
                type,
                value
            }
        };
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect(type, evaluate_type_expression(info, scope, context, array_type->expression));

        if(!is_runtime_type(type)) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return { false };
        }

        if(array_type->index != nullptr) {
            expect(index_value, evaluate_constant_expression(info, scope, context, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    info.address_integer_size,
                    false
                },
                false
            ));

            return {
                true,
                {
                    &type_type_singleton,
                    new TypeConstant {
                        new StaticArray {
                            length->value,
                            type
                        }
                    }
                }
            };
        } else {
            return {
                true,
                {
                    &type_type_singleton,
                    new TypeConstant {
                        new ArrayTypeType {
                            type
                        }
                    }
                }
            };
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return { false };
            }

            expect(type, evaluate_type_expression(info, scope, context, parameter.type));

            if(!is_runtime_type(type)) {
                error(scope, parameter.type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return { false };
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = &void_singleton;
        } else {
            expect(return_type_value, evaluate_type_expression(info, scope, context, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return { false };
            }

            return_type = return_type_value;
        }

        return {
            true,
            {
                &type_type_singleton,
                new TypeConstant {
                    new FunctionTypeType {
                        {
                            parameter_count,
                            parameters
                        },
                        return_type
                    }
                }
            }
        };
    } else {
        abort();
    }
}

static Result<Type*> evaluate_type_expression(GlobalInfo info, ConstantScope scope, GenerationContext *context, Expression *expression) {
    expect(expression_value, evaluate_constant_expression(info, scope, context, expression));

    if(expression_value.type->kind == TypeKind::TypeType) {
        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

        return {
            true,
            type
        };
    } else {
        error(scope, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return { false };
    }
}

static bool does_runtime_static_exist(GenerationContext context, const char *name) {
    for(auto runtime_static : context.statics) {
        if(strcmp(runtime_static->name, name) == 0) {
            return true;
        }
    }

    return false;
}

static void write_value(GlobalInfo info, uint8_t *data, size_t offset, Type *type, ConstantValue *value);

static bool load_file(GlobalInfo info, GenerationContext *context, Array<Statement*> statements, const char *file_path) {
    ConstantScope scope;
    scope.statements = statements;
    scope.constant_parameters = {};
    scope.is_top_level = true;
    scope.file_path = file_path;

    for(auto statement : statements) {
        if(statement->kind == StatementKind::FunctionDeclaration) {
            auto function_declaration = (FunctionDeclaration*)statement;

            auto is_polymorphic = false;
            for(auto parameter : function_declaration->parameters) {
                if(parameter.is_polymorphic_determiner || parameter.is_constant) {
                    is_polymorphic = true;

                    break;
                }
            }

            if(is_polymorphic) {
                continue;
            }

            auto parameter_count = function_declaration->parameters.count;

            auto parameter_types = allocate<Type*>(parameter_count);
            for(size_t i = 0; i < parameter_count; i += 1) {
                expect(type, evaluate_type_expression(info, scope, context, function_declaration->parameters[i].type));

                if(!is_runtime_type(type)) {
                    error(scope, function_declaration->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

                    return { false };
                }

                parameter_types[i] = type;
            }

            Type *return_type;
            if(function_declaration->return_type) {
                expect(return_type_value, evaluate_type_expression(info, scope, context, function_declaration->return_type));

                if(!is_runtime_type(return_type_value)) {
                    error(scope, function_declaration->return_type->range, "Function parameters cannot be of type '%s'", type_description(return_type_value));

                    return { false };
                }

                return_type = return_type_value;
            } else {
                return_type = &void_singleton;
            }

            const char *mangled_name;
            if(function_declaration->is_external || function_declaration->is_no_mangle) {
                mangled_name = function_declaration->name.text;
            } else {
                StringBuffer mangled_name_buffer {};

                string_buffer_append(&mangled_name_buffer, "function_");
                string_buffer_append(&mangled_name_buffer, context->runtime_functions.count);

                mangled_name = mangled_name_buffer.data;
            }

            auto runtime_parameters = allocate<RuntimeFunctionParameter>(parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                runtime_parameters[i] = {
                    function_declaration->parameters[i].name,
                    parameter_types[i],
                    function_declaration->parameters[i].type->range
                };
            }

            append(&context->runtime_functions, {
                mangled_name,
                { parameter_count, runtime_parameters },
                return_type,
                function_declaration,
                {},
                scope
            });
        } else if(statement->kind == StatementKind::VariableDeclaration) {
            auto variable_declaration = (VariableDeclaration*)statement;

            Type *type;
            ConstantValue *initializer;

            if(variable_declaration->type != nullptr && variable_declaration->initializer != nullptr) {
                if(variable_declaration->is_external) {
                    error(scope, variable_declaration->range, "External static variables cannot have an initializer");

                    return { false };
                }

                expect(type_value, evaluate_type_expression(info, scope, context, variable_declaration->type));
                
                if(!is_runtime_type(type_value)) {
                    error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                    return { false };
                }

                type = type_value;

                expect(initializer_value, evaluate_constant_expression(info, scope, context, variable_declaration->initializer));

                expect(initializer_value_coerced, coerce_constant_to_type(
                    info,
                    scope,
                    variable_declaration->range,
                    initializer_value.type,
                    initializer_value.value,
                    type,
                    false
                ));

                initializer = initializer_value_coerced;
            } else if(variable_declaration->type != nullptr) {
                expect(type_value, evaluate_type_expression(info, scope, context, variable_declaration->type));

                if(!is_runtime_type(type_value)) {
                    error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                    return { false };
                }

                type = type_value;
            } else if(variable_declaration->initializer != nullptr) {
                if(variable_declaration->is_external) {
                    error(scope, variable_declaration->range, "External static variables cannot have an initializer");

                    return { false };
                }

                expect(initializer_value, evaluate_constant_expression(info, scope, context, variable_declaration->initializer));

                expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type));
                
                if(!is_runtime_type(actual_type)) {
                    error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%s'", type_description(actual_type));

                    return { false };
                }

                type = actual_type;

                expect(initializer_value_coerced, coerce_constant_to_type(
                    info,
                    scope,
                    variable_declaration->range,
                    initializer_value.type,
                    initializer_value.value,
                    type,
                    false
                ));

                initializer = initializer_value_coerced;
            } else {
                abort();
            }

            const char *mangled_name;
            if(variable_declaration->is_external || variable_declaration->is_no_mangle) {
                mangled_name = variable_declaration->name.text;
            } else {
                StringBuffer buffer {};

                string_buffer_append(&buffer, "variable_");
                string_buffer_append(&buffer, context->static_variables.count);

                mangled_name = buffer.data;
            }

            if(does_runtime_static_exist(*context, mangled_name)) {
                error(scope, variable_declaration->name.range, "Duplicate global name '%s'", mangled_name);

                return { false };
            }

            append(&context->static_variables, {
                variable_declaration,
                mangled_name,
                type
            });

            auto size = get_type_size(info, type);

            auto static_variable = new StaticVariable;
            static_variable->name = mangled_name;
            static_variable->size = size;
            static_variable->alignment = get_type_alignment(info, type);
            static_variable->is_external = variable_declaration->is_external;
            static_variable->has_initial_data = variable_declaration->initializer != nullptr;
            if(variable_declaration->initializer != nullptr) {
                auto initial_data = allocate<uint8_t>(size);

                write_value(info, initial_data, 0, type, initializer);

                static_variable->initial_data = initial_data;
            }

            append(&context->statics, (RuntimeStatic*)static_variable);
        } else if(statement->kind == StatementKind::Import) {
            auto import = (Import*)statement;

            auto source_file_directory = path_get_directory_component(file_path);

            StringBuffer import_file_path {};

            string_buffer_append(&import_file_path, source_file_directory);
            string_buffer_append(&import_file_path, import->path);

            expect(import_file_path_absolute, path_relative_to_absolute(import_file_path.data));

            auto already_loaded = false;
            for(auto file : context->loaded_files) {
                if(strcmp(file.path, import_file_path_absolute) == 0) {
                    already_loaded = true;

                    break;
                }
            }

            if(!already_loaded) {
                expect(tokens, tokenize_source(import_file_path_absolute));

                expect(statements, parse_tokens(import_file_path_absolute, tokens));

                append(&context->loaded_files, {
                    import_file_path_absolute,
                    statements
                });

                if(!load_file(info, context, statements, import_file_path_absolute)) {
                    return { false };
                }
            }
        }
    }

    return true;
}

static Result<TypedConstantValue> resolve_declaration(GlobalInfo info, ConstantScope scope, GenerationContext *context, Statement *declaration) {
    if(declaration->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)declaration;

        for(auto parameter : function_declaration->parameters) {
            if(parameter.is_polymorphic_determiner || parameter.is_constant) {
                return {
                    true,
                    {
                        &polymorphic_function_singleton,
                        new FunctionConstant {
                            function_declaration,
                            scope
                        }
                    }
                };
            }
        }

        auto parameter_count = function_declaration->parameters.count;

        auto parameter_types = allocate<Type*>(parameter_count);
        for(size_t i = 0; i < parameter_count; i += 1) {
            expect(type, evaluate_type_expression(info, scope, context, function_declaration->parameters[i].type));

            if(!is_runtime_type(type)) {
                error(scope, function_declaration->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return { false };
            }

            parameter_types[i] = type;
        }

        Type *return_type;
        if(function_declaration->return_type) {
            expect(return_type_value, evaluate_type_expression(info, scope, context, function_declaration->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(scope, function_declaration->return_type->range, "Function parameters cannot be of type '%s'", type_description(return_type_value));

                return { false };
            }

            return_type = return_type_value;
        } else {
            return_type = &void_singleton;
        }

        return {
            true,
            {
                new FunctionTypeType {
                    {
                        parameter_count,
                        parameter_types
                    },
                    return_type
                },
                new FunctionConstant {
                    function_declaration,
                    scope
                }
            }
        };
    } else if(declaration->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)declaration;

        expect(value, evaluate_constant_expression(info, scope, context, constant_definition->expression));

        return {
            true,
            value
        };
    } else if(declaration->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)declaration;

        auto parameter_count = struct_definition->parameters.count;

        if(parameter_count == 0) {
            auto member_count = struct_definition->members.count;

            auto members = allocate<StructType::Member>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                for(size_t j = 0; j < member_count; j += 1) {
                    if(j != i && strcmp(struct_definition->members[i].name.text, struct_definition->members[j].name.text) == 0) {
                        error(scope, struct_definition->members[i].name.range, "Duplicate struct member name %s", struct_definition->members[i].name.text);

                        return { false };
                    }
                }

                expect(type, evaluate_type_expression(info, scope, context, struct_definition->members[i].type));

                if(!is_runtime_type(type)) {
                    error(scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(type));

                    return { false };
                }

                members[i] = {
                    struct_definition->members[i].name.text,
                    type
                };
            }

            return {
                true,
                {
                    &type_type_singleton,
                    new TypeConstant {
                        new StructType {
                            struct_definition,
                            {
                                member_count,
                                members
                            }
                        }
                    }
                }
            };
        } else {
            auto parameter_types = allocate<Type*>(parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect(type, evaluate_type_expression(info, scope, context, struct_definition->parameters[i].type));

                parameter_types[i] = type;
            }

            return {
                true,
                {
                    &type_type_singleton,
                    new TypeConstant {
                        new PolymorphicStruct {
                            struct_definition,
                            parameter_types,
                            scope
                        }
                    }
                }
            };
        }
    } else if(declaration->kind == StatementKind::Import) {
        auto import = (Import*)declaration;

        auto current_scope = &scope;
        while(!current_scope->is_top_level) {
            current_scope = current_scope->parent;
        }

        auto source_file_directory = path_get_directory_component(current_scope->file_path);

        StringBuffer import_file_path {};

        string_buffer_append(&import_file_path, source_file_directory);
        string_buffer_append(&import_file_path, import->path);

        expect(import_file_path_absolute, path_relative_to_absolute(import_file_path.data));

        for(auto file : context->loaded_files) {
            if(strcmp(file.path, import_file_path_absolute) == 0) {
                return {
                    true,
                    {
                        &file_module_singleton,
                        new FileModuleConstant {
                            file.path,
                            file.statements
                        }
                    }
                };

                break;
            }
        }

        abort();
    } else {
        abort();
    }
}

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

static const char *register_static_array_constant(GlobalInfo info, GenerationContext *context, Type* element_type, Array<ConstantValue*> elements) {
    auto data_length = get_type_size(info, element_type) * elements.count;
    auto data = allocate<uint8_t>(data_length);

    write_static_array(info, data, 0, element_type, elements);

    auto number = context->statics.count;

    StringBuffer name_buffer {};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, number);

    while(does_runtime_static_exist(*context, name_buffer.data)) {
        number += 1;

        string_buffer_append(&name_buffer, "constant_");
        string_buffer_append(&name_buffer, number);
    }

    auto constant = new StaticConstant;
    constant->name = name_buffer.data;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_type_alignment(info, element_type);

    append(&context->statics, (RuntimeStatic*)constant);

    return name_buffer.data;
}

static const char *register_struct_constant(GlobalInfo info, GenerationContext *context, StructType struct_type, ConstantValue **members) {
    auto data_length = get_struct_size(info, struct_type);
    auto data = allocate<uint8_t>(data_length);

    write_struct(info, data, 0, struct_type, members);

    auto number = context->statics.count;

    StringBuffer name_buffer {};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, number);

    while(does_runtime_static_exist(*context, name_buffer.data)) {
        number += 1;

        string_buffer_append(&name_buffer, "constant_");
        string_buffer_append(&name_buffer, number);
    }

    auto constant = new StaticConstant;
    constant->name = name_buffer.data;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_struct_alignment(info, struct_type);

    append(&context->statics, (RuntimeStatic*)constant);

    return name_buffer.data;
}

static size_t append_integer_arithmetic_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    IntegerArithmeticOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto integer_arithmetic_operation = new IntegerArithmeticOperation;
    integer_arithmetic_operation->line = line;
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
    unsigned int line,
    IntegerComparisonOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto integer_comparison_operation = new IntegerComparisonOperation;
    integer_comparison_operation->line = line;
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
    unsigned int line,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto integer_upcast = new IntegerUpcast;
    integer_upcast->line = line;
    integer_upcast->is_signed = is_signed;
    integer_upcast->source_size = source_size;
    integer_upcast->source_register = source_register;
    integer_upcast->destination_size = destination_size;
    integer_upcast->destination_register = destination_register;

    append(instructions, (Instruction*)integer_upcast);

    return destination_register;
}

static size_t append_integer_constant(GenerationContext *context, List<Instruction*> *instructions, unsigned int line, RegisterSize size, uint64_t value) {
    auto destination_register = allocate_register(context);

    auto constant = new IntegerConstantInstruction;
    constant->line = line;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    append(instructions, (Instruction*)constant);

    return destination_register;
}

static size_t append_float_arithmetic_operation(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    FloatArithmeticOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto float_arithmetic_operation = new FloatArithmeticOperation;
    float_arithmetic_operation->line = line;
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
    unsigned int line,
    FloatComparisonOperation::Operation operation,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    auto float_comparison_operation = new FloatComparisonOperation;
    float_comparison_operation->line = line;
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
    unsigned int line,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_conversion = new FloatConversion;
    float_conversion->line = line;
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
    unsigned int line,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_truncation = new FloatTruncation;
    float_truncation->line = line;
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
    unsigned int line,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    auto float_from_integer = new FloatFromInteger;
    float_from_integer->line = line;
    float_from_integer->is_signed = is_signed;
    float_from_integer->source_size = source_size;
    float_from_integer->source_register = source_register;
    float_from_integer->destination_size = destination_size;
    float_from_integer->destination_register = destination_register;

    append(instructions, (Instruction*)float_from_integer);

    return destination_register;
}

static size_t append_float_constant(GenerationContext *context, List<Instruction*> *instructions, unsigned int line, RegisterSize size, double value) {
    auto destination_register = allocate_register(context);

    auto constant = new FloatConstantInstruction;
    constant->line = line;
    constant->size = size;
    constant->destination_register = destination_register;
    constant->value = value;

    append(instructions, (Instruction*)constant);

    return destination_register;
}

static size_t append_reference_static(GenerationContext *context, List<Instruction*> *instructions, unsigned int line, const char *name) {
    auto destination_register = allocate_register(context);

    auto reference_static = new ReferenceStatic;
    reference_static->line = line;
    reference_static->name = name;
    reference_static->destination_register = destination_register;

    append(instructions, (Instruction*)reference_static);

    return destination_register;
}

static size_t append_allocate_local(GenerationContext *context, List<Instruction*> *instructions, unsigned int line, size_t size, size_t alignment) {
    auto destination_register = allocate_register(context);

    auto allocate_local = new AllocateLocal;
    allocate_local->line = line;
    allocate_local->size = size;
    allocate_local->alignment = alignment;
    allocate_local->destination_register = destination_register;

    append(instructions, (Instruction*)allocate_local);

    return destination_register;
}

static void append_branch(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    size_t condition_register,
    size_t destination_instruction
) {
    auto branch = new Branch;
    branch->line = line;
    branch->condition_register = condition_register;
    branch->destination_instruction = destination_instruction;

    append(instructions, (Instruction*)branch);
}

static void append_jump(GenerationContext *context, List<Instruction*> *instructions, unsigned int line, size_t destination_instruction) {
    auto jump = new Jump;
    jump->line = line;
    jump->destination_instruction = destination_instruction;

    append(instructions, (Instruction*)jump);
}

static void append_copy_memory(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    size_t length_register,
    size_t source_address_register,
    size_t destination_address_register,
    size_t alignment
) {
    auto copy_memory = new CopyMemory;
    copy_memory->line = line;
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
    auto length_register = append_integer_constant(context, instructions, range.first_line, info.address_integer_size, length);

    append_copy_memory(context, instructions, range.first_line, length_register, source_address_register, destination_address_register, alignment);
}

static size_t append_load_integer(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    auto load_integer = new LoadInteger;
    load_integer->line = line;
    load_integer->size = size;
    load_integer->address_register = address_register;
    load_integer->destination_register = destination_register;

    append(instructions, (Instruction*)load_integer);

    return destination_register;
}

static void append_store_integer(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    auto store_integer = new StoreInteger;
    store_integer->line = line;
    store_integer->size = size;
    store_integer->source_register = source_register;
    store_integer->address_register = address_register;

    append(instructions, (Instruction*)store_integer);
}

static size_t append_load_float(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    auto load_integer = new LoadFloat;
    load_integer->line = line;
    load_integer->size = size;
    load_integer->address_register = address_register;
    load_integer->destination_register = destination_register;

    append(instructions, (Instruction*)load_integer);

    return destination_register;
}

static void append_store_float(
    GenerationContext *context,
    List<Instruction*> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    auto store_integer = new StoreFloat;
    store_integer->line = line;
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
        range.first_line,
        info.address_integer_size,
        offset
    );

    auto final_address_register = append_integer_arithmetic_operation(
        context,
        instructions,
        range.first_line,
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
        range.first_line,
        register_size_to_byte_size(info.default_integer_size),
        register_size_to_byte_size(info.default_integer_size)
    );

    append_branch(context, instructions, range.first_line, value_register, instructions->count + 4);

    auto true_register = append_integer_constant(context, instructions, range.first_line, info.default_integer_size, 1);

    append_store_integer(context, instructions, range.first_line, info.default_integer_size, true_register, local_register);

    append_jump(context, instructions, range.first_line, instructions->count + 3);

    auto false_register = append_integer_constant(context, instructions, range.first_line, info.default_integer_size, 0);

    append_store_integer(context, instructions, range.first_line, info.default_integer_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range.first_line, info.default_integer_size, local_register);

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

        return append_integer_constant(context, instructions, range.first_line, type.size, integer_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range.first_line, type.size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_boolean_value(GlobalInfo info, GenerationContext *context, List<Instruction*> *instructions, FileRange range, RuntimeValue *value) {
    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto boolean_value = extract_constant_value(BooleanConstant, value);

        return append_integer_constant(context, instructions, range.first_line, info.default_integer_size, boolean_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range.first_line, info.default_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_pointer_value(GlobalInfo info, GenerationContext *context, List<Instruction*> *instructions, FileRange range, RuntimeValue *value) {
    if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
        auto pointer_value = extract_constant_value(PointerConstant, value);

        return append_integer_constant(context, instructions, range.first_line, info.address_integer_size, pointer_value->value);
    } else if(value->kind == RuntimeValueKind::RegisterValue) {
        auto regsiter_value = (RegisterValue*)value;

        return regsiter_value->register_index;
    } else if(value->kind == RuntimeValueKind::AddressValue) {
        auto address_value = (AddressValue*)value;

        return append_load_integer(context, instructions, range.first_line, info.address_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static Result<size_t> coerce_to_integer_register_value(
    ConstantScope scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    RuntimeValue *value,
    Integer target_type,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;

        if(integer->size == target_type.size && integer->is_signed == target_type.is_signed) {
            auto register_index = generate_in_register_integer_value(context, instructions, range, target_type, value);

            return {
                true,
                register_index
            };
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = extract_constant_value(IntegerConstant, value);

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value->value, probing)) {
            return { false };
        }

        auto regsiter_index = append_integer_constant(context, instructions, range.first_line, target_type.size, integer_value->value);

        return {
            true,
            regsiter_index
        };
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<size_t> coerce_to_float_register_value(
    ConstantScope scope,
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

        auto register_index = append_float_constant(context, instructions, range.first_line, target_type.size, (double)integer_value->value);

        return {
            true,
            register_index
        };
    } else if(type->kind == TypeKind::FloatType) {
        auto float_type = (FloatType*)type;

        if(target_type.size == float_type->size) {
            size_t register_index;
            if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                auto float_value = extract_constant_value(FloatConstant, value);
;

                register_index = append_float_constant(context, instructions, range.first_line, float_type->size, float_value->value);
            } else if(value->kind == RuntimeValueKind::RegisterValue) {
                auto regsiter_value = (RegisterValue*)value;

                register_index = regsiter_value->register_index;
            } else if(value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)value;

                register_index = append_load_float(context, instructions, range.first_line, float_type->size, address_value->address_register);
            } else {
                abort();
            }

            return {
                true,
                register_index
            };
        }
    } else if(type->kind == TypeKind::UndeterminedFloat) {
        auto float_value = extract_constant_value(FloatConstant, value);
;

        auto register_index = append_float_constant(context, instructions, range.first_line, target_type.size, float_value->value);

        return {
            true,
            register_index
        };
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<size_t> coerce_to_pointer_register_value(
    GlobalInfo info,
    ConstantScope scope,
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
            range.first_line,
            info.address_integer_size,
            integer_value->value
        );

        return {
            true,
            register_index
        };
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)type;

        if(types_equal(pointer->type, target_type.type)) {
            auto register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

            return {
                true,
                register_index
            };
        }
    }

    if (!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static bool coerce_to_type_write(
    GlobalInfo info,
    ConstantScope scope,
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
    ConstantScope scope,
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
            *integer,
            probing
        ));

        return {
            true,
            register_index
        };
    } else if(target_type->kind == TypeKind::Boolean) {
        if(type->kind == TypeKind::Boolean) {
            auto register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

            return {
                true,
                register_index
            };
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

        return {
            true,
            register_index
        };
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

        return {
            true,
            register_index
        };
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

                return {
                    true,
                    register_index
                };
            }
        } else if(type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)type;

            if(types_equal(target_array->element_type, static_array->element_type)) {
                size_t pointer_register;
                if(value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, value);

                    auto constant_name = register_static_array_constant(
                        info,
                        context,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range.first_line, constant_name);
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
                    range.first_line,
                    2 * register_size_to_byte_size(info.address_integer_size),
                    register_size_to_byte_size(info.address_integer_size)
                );

                append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range.first_line, info.address_integer_size, static_array->length);

                append_store_integer(context, instructions, range.first_line, info.address_integer_size, length_register, length_address_register);

                return {
                    true,
                    address_register
                };
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
                        {
                            info.address_integer_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range.first_line,
                            2 * register_size_to_byte_size(info.address_integer_size),
                            register_size_to_byte_size(info.address_integer_size)
                        );

                        append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_result.value, address_register);

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
                            range.first_line,
                            info.address_integer_size,
                            length_result.value,
                            length_address_register
                        );

                        return {
                            true,
                            address_register
                        };
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

                return {
                    true,
                    register_index
                };
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

                    return {
                        true,
                        register_index
                    };
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
                                range.first_line,
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
                                return {
                                    true,
                                    address_register
                                };
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
                            range.first_line,
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
                            return {
                                true,
                                address_register
                            };
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

    return { false };
}

static bool coerce_to_type_write(
    GlobalInfo info,
    ConstantScope scope,
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

        expect(register_index, coerce_to_integer_register_value(scope, context, instructions, range, type, value, *integer_type, false));

        append_store_integer(context, instructions, range.first_line, integer_type->size, register_index, address_register);

        return true;
    } else if(target_type->kind == TypeKind::Boolean && type->kind == TypeKind::Boolean) {
        size_t register_index = generate_in_register_boolean_value(info, context, instructions, range, value);

        append_store_integer(context, instructions, range.first_line, info.default_integer_size, register_index, address_register);

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

        append_store_float(context, instructions, range.first_line, float_type->size, register_index, address_register);

        return true;
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = extract_constant_value(IntegerConstant, value);

            auto register_index = append_integer_constant(
                context,
                instructions,
                range.first_line,
                info.address_integer_size,
                integer_value->value
            );

            append_store_integer(
                context,
                instructions,
                range.first_line,
                info.address_integer_size,
                register_index,
                address_register
            );

            return true;
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            if(types_equal(target_pointer->type, pointer->type)) {
                size_t register_index = generate_in_register_pointer_value(info, context, instructions, range, value);

                append_store_integer(context, instructions, range.first_line, info.address_integer_size, register_index, address_register);

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
;

                    auto pointer_register = append_integer_constant(
                        context,
                        instructions,
                        range.first_line,
                        info.address_integer_size,
                        array_value->pointer
                    );

                    append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_register, address_register);

                    auto length_register = append_integer_constant(
                        context,
                        instructions,
                        range.first_line,
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

                    append_store_integer(context, instructions, range.first_line, info.address_integer_size, length_register, length_address_register);

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

                    auto constant_name = register_static_array_constant(
                        info,
                        context,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range.first_line, constant_name);
                } else if(value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)value;

                    pointer_register = register_value->register_index;
                } else if(value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)value;

                    pointer_register = address_value->address_register;
                } else {
                    abort();
                }

                append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    info,
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(info.address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range.first_line, info.address_integer_size, static_array->length);

                append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_register, length_address_register);

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
                        {
                            info.address_integer_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        append_store_integer(context, instructions, range.first_line, info.address_integer_size, pointer_result.value, address_register);

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
                            range.first_line,
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

                    auto constant_name = register_static_array_constant(
                        info,
                        context,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    source_address_register = append_reference_static(context, instructions, range.first_line, constant_name);
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

                        auto constant_name = register_struct_constant(
                            info,
                            context,
                            *struct_type,
                            struct_value->members
                        );

                        source_address_register = append_reference_static(context, instructions, range.first_line, constant_name);
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

    return { false };
}

static Result<TypedRuntimeValue> generate_expression(
    GlobalInfo info,
    ConstantScope scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
);

static Result<Type*> evaluate_type_expression_runtime(
    GlobalInfo info,
    ConstantScope scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
) {
    expect(expression_value, generate_expression(info, scope, context, instructions, expression));

    if(expression_value.type->kind == TypeKind::TypeType) {
        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

        return {
            true,
            type
        };
    } else {
        error(scope, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return { false };
    }
}

static Result<TypedRuntimeValue> generate_binary_operation(
    GlobalInfo info,
    ConstantScope scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Expression *left_expression,
    Expression *right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect(left, generate_expression(info, scope, context, instructions, left_expression));

    expect(right, generate_expression(info, scope, context, instructions, right_expression));

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

        return {
            true,
            {
                constant.type,
                new RuntimeConstantValue {
                    constant.value
                }
            }
        };
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
            *integer,
            false
        ));

        expect(right_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            right_expression->range,
            right.type,
            right.value,
            *integer,
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
                range.first_line,
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

                    return { false };
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                instructions,
                range.first_line,
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

        return {
            true,
            {
                result_type,
                new RegisterValue {
                    result_register
                }
            }
        };
    } else if(determined_type->kind == TypeKind::Boolean) {
        if(left.type->kind != TypeKind::Boolean) {
            error(scope, left_expression->range, "Expected 'bool', got '%s'", type_description(left.type));

            return { false };
        }

        auto left_register = generate_in_register_boolean_value(info, context, instructions, left_expression->range, left.value);

        if(right.type->kind != TypeKind::Boolean) {
            error(scope, right_expression->range, "Expected 'bool', got '%s'", type_description(right.type));

            return { false };
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
                range.first_line,
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

                    return { false };
                } break;
            }

            result_register = append_integer_comparison_operation(
                context,
                instructions,
                range.first_line,
                comparison_operation,
                info.default_integer_size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(info, context, instructions, range, result_register);
            }
        }

        return {
            true,
            {
                &boolean_singleton,
                new RegisterValue {
                    result_register
                }
            }
        };
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
                range.first_line,
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

                    return { false };
                } break;
            }

            result_register = append_float_comparison_operation(
                context,
                instructions,
                range.first_line,
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

        return {
            true,
            {
                result_type,
                new RegisterValue {
                    result_register
                }
            }
        };
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

                return { false };
            } break;
        }

        auto result_register = append_integer_comparison_operation(
            context,
            instructions,
            range.first_line,
            comparison_operation,
            info.address_integer_size,
            left_register,
            right_register
        );

        if(invert) {
            result_register = generate_boolean_invert(info, context, instructions, range, result_register);
        }

        return {
            true,
            {
                &boolean_singleton,
                new RegisterValue {
                    result_register
                }
            }
        };
    } else {
        abort();
    }
}

static Result<TypedRuntimeValue> generate_expression(
    GlobalInfo info,
    ConstantScope scope,
    GenerationContext *context,
    List<Instruction*> *instructions,
    Expression *expression
) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        assert(context->variable_scope_stack.count > 0);

        for(size_t i = 0; i < context->variable_scope_stack.count; i += 1) {
            auto current_scope = context->variable_scope_stack[context->variable_scope_stack.count - 1 - i];

            for(auto variable : current_scope.variables) {
                if(strcmp(variable.name.text, named_reference->name.text) == 0) {
                    return {
                        true,
                        {
                            variable.type,
                            new AddressValue {
                                variable.address_register
                            }
                        }
                    };
                }
            }

            for(auto statement : current_scope.constant_scope.statements) {
                if(match_declaration(statement, named_reference->name.text)) {
                    expect(value, resolve_declaration(info, current_scope.constant_scope, context, statement));

                    return {
                        true,
                        {
                            value.type,
                            new RuntimeConstantValue {
                                value.value
                            }
                        }
                    };
                } else if(statement->kind == StatementKind::UsingStatement) {
                    auto using_statement = (UsingStatement*)statement;

                    expect(expression_value, evaluate_constant_expression(info, current_scope.constant_scope, context, using_statement->module));

                    if(expression_value.type->kind != TypeKind::FileModule) {
                        error(current_scope.constant_scope, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                        return { false };
                    }

                    auto file_module = (FileModuleConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

                    for(auto statement : file_module->statements) {
                        if(match_public_declaration(statement, named_reference->name.text)) {
                            ConstantScope module_scope;
                            module_scope.statements = file_module->statements;
                            module_scope.constant_parameters = {};
                            module_scope.is_top_level = true;
                            module_scope.file_path = file_module->path;

                            expect(value, resolve_declaration(info, module_scope, context, statement));

                            return {
                                true,
                                {
                                    value.type,
                                    new RuntimeConstantValue {
                                        value.value
                                    }
                                }
                            };
                        } else if(statement->kind == StatementKind::VariableDeclaration) {
                            auto variable_declaration = (VariableDeclaration*)statement;

                            if(strcmp(variable_declaration->name.text, named_reference->name.text) == 0) {
                                for(auto static_variable : context->static_variables) {
                                    if(static_variable.declaration == variable_declaration) {
                                        auto address_register = append_reference_static(
                                            context,
                                            instructions,
                                            named_reference->range.first_line,
                                            static_variable.mangled_name
                                        );

                                        return {
                                            true,
                                            {
                                                static_variable.type,
                                                new AddressValue {
                                                    address_register
                                                }
                                            }
                                        };
                                    }
                                }

                                abort();
                            }
                        }
                    }
                }
            }

            for(auto constant_parameter : current_scope.constant_scope.constant_parameters) {
                if(strcmp(constant_parameter.name, named_reference->name.text) == 0) {
                    return {
                        true,
                        {
                            constant_parameter.type,
                            new RuntimeConstantValue {
                                constant_parameter.value
                            }
                        }
                    };
                }
            }
        }

        assert(!context->variable_scope_stack[0].constant_scope.is_top_level);

        auto current_scope = context->variable_scope_stack[0].constant_scope.parent;
        while(true) {
            for(auto statement : current_scope->statements) {
                if(match_declaration(statement, named_reference->name.text)) {
                    expect(value, resolve_declaration(info, *current_scope, context, statement));

                    return {
                        true,
                        {
                            value.type,
                            new RuntimeConstantValue {
                                value.value
                            }
                        }
                    };
                } else if(statement->kind == StatementKind::UsingStatement) {
                    auto using_statement = (UsingStatement*)statement;

                    expect(expression_value, evaluate_constant_expression(info, *current_scope, context, using_statement->module));

                    if(expression_value.type->kind != TypeKind::FileModule) {
                        error(*current_scope, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                        return { false };
                    }

                    auto file_module = (FileModuleConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

                    for(auto statement : file_module->statements) {
                        if(match_public_declaration(statement, named_reference->name.text)) {
                            ConstantScope module_scope;
                            module_scope.statements = file_module->statements;
                            module_scope.constant_parameters = {};
                            module_scope.is_top_level = true;
                            module_scope.file_path = file_module->path;

                            expect(value, resolve_declaration(info, module_scope, context, statement));

                            return {
                                true,
                                {
                                    value.type,
                                    new RuntimeConstantValue {
                                        value.value
                                    }
                                }
                            };
                        } else if(statement->kind == StatementKind::VariableDeclaration) {
                            auto variable_declaration = (VariableDeclaration*)statement;

                            if(strcmp(variable_declaration->name.text, named_reference->name.text) == 0) {
                                for(auto static_variable : context->static_variables) {
                                    if(static_variable.declaration == variable_declaration) {
                                        auto address_register = append_reference_static(
                                            context,
                                            instructions,
                                            named_reference->range.first_line,
                                            static_variable.mangled_name
                                        );

                                        return {
                                            true,
                                            {
                                                static_variable.type,
                                                new AddressValue {
                                                    address_register
                                                }
                                            }
                                        };
                                    }
                                }

                                abort();
                            }
                        }
                    }
                } else if(statement->kind == StatementKind::VariableDeclaration) {
                    auto variable_declaration = (VariableDeclaration*)statement;

                    if(current_scope->is_top_level && strcmp(variable_declaration->name.text, named_reference->name.text) == 0) {
                        for(auto static_variable : context->static_variables) {
                            if(static_variable.declaration == variable_declaration) {
                                auto address_register = append_reference_static(
                                    context,
                                    instructions,
                                    named_reference->range.first_line,
                                    static_variable.mangled_name
                                );

                                return {
                                    true,
                                    {
                                        static_variable.type,
                                        new AddressValue {
                                            address_register
                                        }
                                    }
                                };
                            }
                        }

                        abort();
                    }
                }
            }

            for(auto constant_parameter : current_scope->constant_parameters) {
                if(strcmp(constant_parameter.name, named_reference->name.text) == 0) {
                    return {
                        true,
                        {
                            constant_parameter.type,
                            new RuntimeConstantValue {
                                constant_parameter.value
                            }
                        }
                    };
                }
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(strcmp(named_reference->name.text, global_constant.name) == 0) {
                return {
                    true,
                    {
                        global_constant.type,
                        new RuntimeConstantValue {
                            global_constant.value
                        }
                    }
                };
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %s", named_reference->name.text);

        return { false };
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect(expression_value, generate_expression(info, scope, context, instructions, index_reference->expression));

        expect(index, generate_expression(info, scope, context, instructions, index_reference->index));

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

            return {
                true,
                {
                    constant.type,
                    new RuntimeConstantValue {
                        constant.value
                    }
                }
            };
        }

        expect(index_register, coerce_to_integer_register_value(
            scope,
            context,
            instructions,
            index_reference->index->range,
            index.type,
            index.value,
            {
                info.address_integer_size,
                false
            },
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
                    index_reference->expression->range.first_line,
                    info.address_integer_size,
                    pointer_value->value
                );
            } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                auto register_value = (RegisterValue*)expression_value.value;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    info.address_integer_size,
                    register_value->register_index
                );
            } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                auto address_value = (AddressValue*)expression_value.value;

                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
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

                auto constant_name = register_static_array_constant(
                    info,
                    context,
                    static_array->element_type,
                    { static_array->length, static_array_value->elements }
                );

                base_address_register = append_reference_static(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    constant_name
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
            index_reference->range.first_line,
            info.address_integer_size,
            get_type_size(info, element_type)
        );

        auto offset = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range.first_line,
            IntegerArithmeticOperation::Operation::Multiply,
            info.address_integer_size,
            element_size_register,
            index_register
        );

        auto address_register = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range.first_line,
            IntegerArithmeticOperation::Operation::Add,
            info.address_integer_size,
            base_address_register,
            offset
        );

        return {
            true,
            {
                element_type,
                new AddressValue {
                    address_register
                }
            }
        };
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect(expression_value, generate_expression(info, scope, context, instructions, member_reference->expression));

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
                    member_reference->expression->range.first_line,
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
                    member_reference->expression->range.first_line,
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
                        member_reference->range.first_line,
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

                return {
                    true,
                    {
                        new Integer {
                            info.address_integer_size,
                            false
                        },
                        value
                    }
                };
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
                        member_reference->range.first_line,
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

                return {
                    true,
                    {
                        new Pointer {
                            array_type->element_type
                        },
                        value
                    }
                };
            } else {
                error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return { false };
            }
        } else if(actual_type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)actual_type;

            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    {
                        new Integer {
                            info.address_integer_size,
                            false
                        },
                        new RuntimeConstantValue {
                            new IntegerConstant {
                                static_array->length
                            }
                        }
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                size_t address_regsiter;
                if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto static_array_value = extract_constant_value(StaticArrayConstant, actual_value);

                    auto constant_name = register_static_array_constant(
                        info,
                        context,
                        static_array->element_type,
                        { static_array->length, static_array_value->elements }
                    );

                    address_regsiter = append_reference_static(context, instructions, member_reference->range.first_line, constant_name);
                } else if(actual_value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)actual_value;

                    address_regsiter = register_value->register_index;
                } else if(actual_value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)actual_value;

                    address_regsiter = address_value->address_register;
                } else {
                    abort();
                }

                return {
                    true,
                    {
                        new Pointer {
                            static_array->element_type
                        },
                        new RegisterValue {
                            address_regsiter
                        }
                    }
                };
            } else {
                error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return { false };
            }
        } else if(actual_type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)actual_type;

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(strcmp(struct_type->members[i].name, member_reference->name.text) == 0) {
                    auto member_type = struct_type->members[i].type;

                    if(actual_value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto struct_value = extract_constant_value(StructConstant, actual_value);

                        assert(!struct_type->definition->is_union);

                        return {
                            true,
                            {
                                member_type,
                                new RuntimeConstantValue {
                                    struct_value->members[i]
                                }
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
                            get_struct_member_offset(info, *struct_type, i)
                        );

                        auto member_representation = get_type_representation(info, member_type);

                        size_t register_index;
                        if(member_representation.is_in_register) {
                            if(member_representation.is_float) {
                                register_index = append_load_float(
                                    context,
                                    instructions,
                                    member_reference->range.first_line,
                                    member_representation.value_size,
                                    address_register
                                );
                            } else {
                                register_index = append_load_integer(
                                    context,
                                    instructions,
                                    member_reference->range.first_line,
                                    member_representation.value_size,
                                    address_register
                                );
                            }
                        } else {
                            register_index = address_register;
                        }

                        return {
                            true,
                            {
                                member_type,
                                new RegisterValue {
                                    register_index
                                }
                            }
                        };
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

                        return {
                            true,
                            {
                                member_type,
                                new AddressValue {
                                    address_register
                                }
                            }
                        };
                    } else {
                        abort();
                    }
                }
            }

            error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return { false };
        } else if(actual_type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)actual_type;

            auto undetermined_struct_value = (UndeterminedStructValue*)actual_value;

            for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                if(strcmp(undetermined_struct->members[i].name, member_reference->name.text) == 0) {
                    return {
                        true,
                        {
                            undetermined_struct->members[i].type,
                            undetermined_struct_value->members[i]
                        }
                    };
                }
            }

            error(scope, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return { false };
        } else if(actual_type->kind == TypeKind::FileModule) {
            auto file_module_value = extract_constant_value(FileModuleConstant, actual_value);

            for(auto statement : file_module_value->statements) {
                if(match_public_declaration(statement, member_reference->name.text)) {
                    ConstantScope module_scope;
                    module_scope.statements = file_module_value->statements;
                    module_scope.constant_parameters = {};
                    module_scope.is_top_level = true;
                    module_scope.file_path = file_module_value->path;

                    expect(value, resolve_declaration(info, module_scope, context, statement));

                    return {
                        true,
                        {
                            value.type,
                            new RuntimeConstantValue {
                                value.value
                            }
                        }
                    };
                } else if(statement->kind == StatementKind::VariableDeclaration) {
                    auto variable_declaration = (VariableDeclaration*)statement;

                    if(strcmp(variable_declaration->name.text, member_reference->name.text) == 0) {
                        for(auto static_variable : context->static_variables) {
                            if(static_variable.declaration == variable_declaration) {
                                auto address_register = append_reference_static(
                                    context,
                                    instructions,
                                    member_reference->range.first_line,
                                    static_variable.mangled_name
                                );

                                return {
                                    true,
                                    {
                                        static_variable.type,
                                        new AddressValue {
                                            address_register
                                        }
                                    }
                                };
                            }
                        }

                        abort();
                    }
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else {
            error(scope, member_reference->expression->range, "Type %s has no members", type_description(actual_type));

            return { false };
        }
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return {
            true,
            {
                &undetermined_integer_singleton,
                new RuntimeConstantValue {
                    new IntegerConstant {
                        integer_literal->value
                    }
                }
            }
        };
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return {
            true,
            {
                &undetermined_float_singleton,
                new RuntimeConstantValue {
                    new FloatConstant {
                        float_literal->value
                    }
                }
            }
        };
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.count;

        auto characters = allocate<ConstantValue*>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = new IntegerConstant {
                (uint64_t)string_literal->characters[i]
            };
        }

        return {
            true,
            {
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
            }
        };
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return { false };
        }

        expect(first_element, generate_expression(info, scope, context, instructions, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(scope, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return { false };
        }

        auto elements = allocate<TypedRuntimeValue>(element_count);
        elements[0] = first_element;

        auto all_constant = true;
        for(size_t i = 1; i < element_count; i += 1) {
            expect(element, generate_expression(info, scope, context, instructions, array_literal->elements[i]));

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
                array_literal->range.first_line,
                array_literal->elements.count * element_size,
                get_type_alignment(info, determined_element_type)
            );

            auto element_size_register = append_integer_constant(
                context,
                instructions,
                array_literal->range.first_line,
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
                    return { false };
                }

                if(i != element_count - 1) {
                    element_address_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        array_literal->elements[i]->range.first_line,
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

        return {
            true,
            {
                new StaticArray {
                    element_count,
                    determined_element_type
                },
                value
            }
        };
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        if(struct_literal->members.count == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return { false };
        }

        auto member_count = struct_literal->members.count;

        auto type_members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<RuntimeValue*>(member_count);
        auto all_constant = true;

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(strcmp(struct_literal->members[i].name.text, type_members[j].name) == 0) {
                    error(scope, struct_literal->members[i].name.range, "Duplicate struct member %s", struct_literal->members[i].name.text);

                    return { false };
                }
            }

            expect(member, generate_expression(info, scope, context, instructions, struct_literal->members[i].value));

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

        return {
            true,
            {
                new UndeterminedStruct {
                    {
                        member_count,
                        type_members
                    }
                },
                value
            }
        };
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect(expression_value, generate_expression(info, scope, context, instructions, function_call->expression));

        if(expression_value.type->kind == TypeKind::FunctionTypeType) {
            auto function = (FunctionTypeType*)expression_value.type;
            auto parameter_count = function->parameters.count;

            if(function_call->parameters.count != parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    parameter_count,
                    function_call->parameters.count
                );

                return { false };
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
                expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[i]));

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
                    function_call->range.first_line,
                    get_type_size(info, function->return_type),
                    get_type_alignment(info, function->return_type)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.address_integer_size,
                    false,
                    parameter_register
                };
            }

            auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

            auto is_registered = false;
            const char *mangled_name;
            for(auto runtime_function : context->runtime_functions) {
                if(runtime_function.declaration == function_value->declaration && runtime_function.constant_parameters.count == 0) {
                    is_registered = true;
                    mangled_name = runtime_function.mangled_name;

                    break;
                }
            }

            assert(is_registered);

            auto address_register = append_reference_static(context, instructions, function_call->range.first_line, mangled_name);

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->line = function_call->range.first_line;
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

            return {
                true,
                {
                    function->return_type,
                    value
                }
            };
        } else if(expression_value.type->kind == TypeKind::PolymorphicFunction) {
            auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

            auto original_parameter_count = function_value->declaration->parameters.count;

            if(function_call->parameters.count != original_parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    original_parameter_count,
                    function_call->parameters.count
                );

                return { false };
            }

            auto parameter_types = allocate<Type*>(original_parameter_count);
            List<TypedRuntimeValue> polymorphic_runtime_parameter_values {};

            List<ConstantParameter> polymorphic_determiners {};

            for(size_t i = 0; i < original_parameter_count; i += 1) {
                auto declaration_parameter = function_value->declaration->parameters[i];

                if(declaration_parameter.is_polymorphic_determiner) {
                    expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[i]));

                    expect(determined_type, coerce_to_default_type(info, scope, function_call->parameters[i]->range, parameter_value.type));

                    if(!declaration_parameter.is_constant) {
                        append(&polymorphic_runtime_parameter_values, parameter_value);
                    }

                    parameter_types[i] = determined_type;

                    append(&polymorphic_determiners, {
                        function_value->declaration->parameters[i].polymorphic_determiner.text,
                        &type_type_singleton,
                        new TypeConstant {
                            determined_type
                        }
                    });
                }
            }

            context->constant_parameters = to_array(polymorphic_determiners);

            List<ConstantParameter> constant_parameters {};

            for(size_t i = 0; i < polymorphic_determiners.count; i += 1) {
                append(&constant_parameters, polymorphic_determiners[i]);
            }

            for(size_t i = 0; i < original_parameter_count; i += 1) {
                auto declaration_parameter = function_value->declaration->parameters[i];
                auto call_parameter = function_call->parameters[i];

                if(declaration_parameter.is_constant) {
                    if(!declaration_parameter.is_polymorphic_determiner) {
                        expect(parameter_type, evaluate_type_expression(info, function_value->parent, context, declaration_parameter.type));

                        parameter_types[i] = parameter_type;
                    }

                    expect(parameter_value, generate_expression(info, scope, context, instructions, call_parameter));

                    if(parameter_value.value->kind != RuntimeValueKind::RuntimeConstantValue) {
                        error(scope, call_parameter->range, "Expected a constant value");

                        return { false };
                    }

                    auto constant_value = ((RuntimeConstantValue*)parameter_value.value)->value;

                    expect(coerced_constant_value, coerce_constant_to_type(
                        info,
                        scope,
                        call_parameter->range,
                        parameter_value.type,
                        constant_value,
                        parameter_types[i],
                        false
                    ));

                    append(&constant_parameters, {
                        declaration_parameter.name.text,
                        parameter_types[i],
                        coerced_constant_value
                    });
                }
            }

            context->constant_parameters = to_array(constant_parameters);

            size_t runtime_parameter_count = 0;
            for(size_t i = 0; i < original_parameter_count; i += 1) {
                auto declaration_parameter = function_value->declaration->parameters[i];
                auto call_parameter = function_call->parameters[i];

                if(!declaration_parameter.is_constant) {
                    if(!declaration_parameter.is_polymorphic_determiner) {
                        expect(parameter_type, evaluate_type_expression(info, function_value->parent, context, declaration_parameter.type));

                        if(!is_runtime_type(parameter_type)) {
                            error(function_value->parent, call_parameter->range, "Non-constant function parameters cannot be of type '%s'", type_description(parameter_type));

                            return { false };
                        }

                        parameter_types[i] = parameter_type;
                    }

                    runtime_parameter_count += 1;
                }
            }

            Type *return_type;
            RegisterRepresentation return_type_representation;
            bool has_return;
            if(function_value->declaration->return_type) {
                has_return = true;

                expect(return_type_value, evaluate_type_expression(info, function_value->parent, context, function_value->declaration->return_type));

                if(!is_runtime_type(return_type_value)) {
                    error(
                        function_value->parent,
                        function_value->declaration->return_type->range,
                        "Function returns cannot be of type '%s'",
                        type_description(return_type_value)
                    );

                    return { false };
                }

                return_type_representation = get_type_representation(info, return_type_value);

                return_type = return_type_value;
            } else {
                has_return = false;

                return_type = &void_singleton;
            }

            context->constant_parameters = {};

            auto instruction_parameter_count = runtime_parameter_count;
            if(has_return && !return_type_representation.is_in_register) {
                instruction_parameter_count += 1;
            }

            auto instruction_parameters = allocate<FunctionCallInstruction::Parameter>(instruction_parameter_count);

            {
                size_t runtime_parameter_index = 0;
                size_t polymorphic_parameter_index = 0;

                for(size_t i = 0; i < original_parameter_count; i += 1) {
                    auto declaration_parameter = function_value->declaration->parameters[i];

                    if(!declaration_parameter.is_constant) {
                        Type *type;
                        RuntimeValue *value;
                        if(declaration_parameter.is_polymorphic_determiner) {
                            type = polymorphic_runtime_parameter_values[polymorphic_parameter_index].type;
                            value = polymorphic_runtime_parameter_values[polymorphic_parameter_index].value;

                            polymorphic_parameter_index += 1;
                        } else {
                            expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[i]));

                            type = parameter_value.type;
                            value = parameter_value.value;
                        }

                        expect(parameter_register, coerce_to_type_register(
                            info,
                            scope,
                            context,
                            instructions,
                            function_call->parameters[i]->range,
                            type,
                            value,
                            parameter_types[i],
                            false
                        ));

                        auto representation = get_type_representation(info, parameter_types[i]);

                        RegisterSize size;
                        if(representation.is_in_register) {
                            size = representation.value_size;
                        } else {
                            size = info.address_integer_size;
                        }

                        instruction_parameters[runtime_parameter_index] = {
                            size,
                            representation.is_in_register && representation.is_float,
                            parameter_register
                        };

                        runtime_parameter_index += 1;
                    }
                    
                }
            }

            if(has_return && !return_type_representation.is_in_register) {
                auto parameter_register = append_allocate_local(
                    context,
                    instructions,
                    function_call->range.first_line,
                    get_type_size(info, return_type),
                    get_type_alignment(info, return_type)
                );

                instruction_parameters[instruction_parameter_count - 1] = {
                    info.address_integer_size,
                    false,
                    parameter_register
                };
            }

            const char *mangled_name;
            if(function_value->declaration->is_external || function_value->declaration->is_no_mangle) {
                mangled_name = function_value->declaration->name.text;
            } else {
                StringBuffer mangled_name_buffer {};

                string_buffer_append(&mangled_name_buffer, "function_");
                string_buffer_append(&mangled_name_buffer, context->runtime_functions.count);

                mangled_name = mangled_name_buffer.data;
            }

            auto runtime_parameters = allocate<RuntimeFunctionParameter>(runtime_parameter_count);

            {
                size_t runtime_parameter_index = 0;

                for(size_t i = 0; i < original_parameter_count; i += 1) {
                    auto declaration_parameter = function_value->declaration->parameters[i];

                    if(!declaration_parameter.is_constant) {
                        FileRange type_range;
                        if(declaration_parameter.is_polymorphic_determiner) {
                            type_range = declaration_parameter.polymorphic_determiner.range;
                        } else {
                            type_range = declaration_parameter.name.range;
                        }

                        runtime_parameters[runtime_parameter_index] = {
                            declaration_parameter.name,
                            parameter_types[i],
                            type_range
                        };

                        runtime_parameter_index += 1;
                    }
                }
            }

            append(&context->runtime_functions, {
                mangled_name,
                { runtime_parameter_count, runtime_parameters },
                return_type,
                function_value->declaration,
                to_array(constant_parameters),
                function_value->parent
            });

            auto address_register = append_reference_static(context, instructions, function_call->range.first_line, mangled_name);

            auto function_call_instruction = new FunctionCallInstruction;
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

            return {
                true,
                {
                    return_type,
                    value
                }
            };
        } else if(expression_value.type->kind == TypeKind::BuiltinFunction) {
            auto builtin_function_value = extract_constant_value(BuiltinFunctionConstant, expression_value.value);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[0]));

                Type *type;
                if(parameter_value.type->kind == TypeKind::TypeType) {
                    type = extract_constant_value(TypeConstant, parameter_value.value)->type;
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(scope, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return { false };
                }

                auto size = get_type_size(info, type);

                return {
                    true,
                    {
                        new Integer {
                            info.address_integer_size,
                            false
                        },
                        new RuntimeConstantValue {
                            new IntegerConstant {
                                size
                            }
                        }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[0]));

                return {
                    true,
                    {
                        &type_type_singleton,
                        new RuntimeConstantValue {
                            new TypeConstant {
                                parameter_value.type
                            }
                        }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "memcpy") == 0) {
                if(function_call->parameters.count != 3) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 3 got %zu", function_call->parameters.count);

                    return { false };
                }

                Integer u8_type { RegisterSize::Size8, false };
                Pointer u8_pointer_type { &u8_type };

                expect(destination_value, generate_expression(info, scope, context, instructions, function_call->parameters[0]));

                if(!types_equal(destination_value.type, &u8_pointer_type)) {
                    error(
                        scope,
                        function_call->parameters[0]->range,
                        "Incorrect type for parameter 0. Expected '%s', got '%s'",
                        type_description(&u8_pointer_type),
                        type_description(destination_value.type)
                    );

                    return { false };
                }

                expect(source_value, generate_expression(info, scope, context, instructions, function_call->parameters[1]));

                if(!types_equal(source_value.type, &u8_pointer_type)) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 1. Expected '%s', got '%s'",
                        type_description(&u8_pointer_type),
                        type_description(source_value.type)
                    );

                    return { false };
                }

                Integer usize_type { info.address_integer_size, false };

                expect(size_value, generate_expression(info, scope, context, instructions, function_call->parameters[2]));

                if(!types_equal(size_value.type, &usize_type)) {
                    error(
                        scope,
                        function_call->parameters[1]->range,
                        "Incorrect type for parameter 2. Expected '%s', got '%s'",
                        type_description(&usize_type),
                        type_description(size_value.type)
                    );

                    return { false };
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
                    function_call->range.first_line,
                    size_register,
                    source_address_register,
                    destination_address_register,
                    1
                );

                return {
                    true,
                    {
                        &void_singleton,
                        new RuntimeConstantValue {
                            &void_constant_singleton
                        }
                    }
                };
            } else {
                abort();
            }
        } else if(expression_value.type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)expression_value.type;

            if(pointer->type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%s'", type_description(expression_value.type));

                return { false };
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

                return { false };
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
                expect(parameter_value, generate_expression(info, scope, context, instructions, function_call->parameters[i]));

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
                    function_call->range.first_line,
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
            function_call_instruction->line = function_call->range.first_line;
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

            return {
                true,
                {
                    function->return_type,
                    value
                }
            };
        } else if(expression_value.type->kind == TypeKind::TypeType) {
            auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

            if(type->kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = (PolymorphicStruct*)type;
                auto parameter_count = polymorphic_struct->definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        parameter_count,
                        function_call->parameters.count
                    );

                    return { false };
                }

                auto parameters = allocate<ConstantParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect(parameter_value, evaluate_constant_expression(info, scope, context, function_call->parameters[i]));

                    expect(coerced_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter_value.type,
                        parameter_value.value,
                        polymorphic_struct->parameter_types[i],
                        false
                    ));

                    parameters[i] = {
                        polymorphic_struct->definition->parameters[i].name.text,
                        polymorphic_struct->parameter_types[i],
                        coerced_value
                    };
                }

                context->constant_parameters = { parameter_count, parameters };

                auto member_count = polymorphic_struct->definition->members.count;

                auto members = allocate<StructType::Member>(member_count);

                for(size_t i = 0; i < member_count; i += 1) {
                    expect(member_type, evaluate_type_expression(info, polymorphic_struct->parent, context, polymorphic_struct->definition->members[i].type));

                    if(!is_runtime_type(member_type)) {
                        error(polymorphic_struct->parent, polymorphic_struct->definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(member_type));

                        return { false };
                    }

                    members[i] = {
                        polymorphic_struct->definition->members[i].name.text,
                        member_type
                    };
                }

                context->constant_parameters = {};

                return {
                    true,
                    {
                        &type_type_singleton,
                        new RuntimeConstantValue {
                            new TypeConstant {
                                new StructType {
                                    polymorphic_struct->definition,
                                    {
                                        member_count,
                                        members
                                    }
                                }
                            }
                        }
                    }
                };
            } else {
                error(scope, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return { false };
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call '%s'", type_description(expression_value.type));

            return { false };
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        return generate_binary_operation(
            info,
            scope,
            context,
            instructions,
            binary_operation->range,
            binary_operation->left,
            binary_operation->right,
            binary_operation->binary_operator
        );
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect(expression_value, generate_expression(info, scope, context, instructions, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                size_t address_register;
                if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto constant_value = ((RuntimeConstantValue*)expression_value.value)->value;

                    if(expression_value.type->kind == TypeKind::FunctionTypeType) {
                        auto function = (FunctionTypeType*)expression_value.type;

                        auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

                        auto is_registered = false;
                        const char *mangled_name;
                        for(auto runtime_function : context->runtime_functions) {
                            if(runtime_function.declaration == function_value->declaration && runtime_function.constant_parameters.count == 0) {
                                is_registered = true;
                                mangled_name = runtime_function.mangled_name;

                                break;
                            }
                        }

                        assert(is_registered);

                        address_register = append_reference_static(
                            context,
                            instructions,
                            unary_operation->range.first_line,
                            mangled_name
                        );
                    } else if(expression_value.type->kind == TypeKind::TypeType) {
                        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

                        if(
                            !is_runtime_type(type) &&
                            type->kind != TypeKind::Void &&
                            type->kind != TypeKind::FunctionTypeType
                        ) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                            return { false };
                        }

                        return {
                            true,
                            {
                                &type_type_singleton,
                                new RuntimeConstantValue {
                                    new TypeConstant {
                                        new Pointer {
                                            type
                                        }
                                    }
                                }
                            }
                        };
                    } else {
                        error(scope, unary_operation->expression->range, "Cannot take pointers to constants of type '%s'", type_description(expression_value.type));

                        return { false };
                    }
                } else if(
                    expression_value.value->kind == RuntimeValueKind::RegisterValue ||
                    expression_value.value->kind == RuntimeValueKind::UndeterminedStructValue
                ) {
                    error(scope, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return { false };
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    address_register = address_value->address_register;
                } else {
                    abort();
                }

                return {
                    true,
                    {
                        new Pointer {
                            expression_value.type
                        },
                        new RegisterValue {
                            address_register
                        }
                    }
                };
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type->kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%s'", type_description(expression_value.type));

                    return { false };
                }

                size_t register_index;
                if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                    auto boolean_value = extract_constant_value(BooleanConstant, expression_value.value);

                    return {
                        true,
                        {
                            &boolean_singleton,
                            new RuntimeConstantValue {
                                    new BooleanConstant {
                                    !boolean_value->value
                                }
                            }
                        }
                    };
                } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                    auto register_value = (RegisterValue*)expression_value.value;

                    register_index = register_value->register_index;
                } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                    auto address_value = (AddressValue*)expression_value.value;

                    register_index = append_load_integer(
                        context,
                        instructions,
                        unary_operation->expression->range.first_line,
                        info.default_integer_size,
                        address_value->address_register
                    );
                }

                auto result_register = generate_boolean_invert(info, context, instructions, unary_operation->expression->range, register_index);

                return {
                    true,
                    {
                        &boolean_singleton,
                        new RegisterValue {
                            result_register
                        }
                    }
                };
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type->kind == TypeKind::UndeterminedInteger) {
                    auto integer_value = extract_constant_value(IntegerConstant, expression_value.value);

                    return {
                        true,
                        {
                            &undetermined_integer_singleton,
                            new RuntimeConstantValue {
                                    new IntegerConstant {
                                    -integer_value->value
                                }
                            }
                        }
                    };
                } else if(expression_value.type->kind == TypeKind::Integer) {
                    auto integer = (Integer*)expression_value.type;

                    size_t register_index;
                    if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto integer_value = extract_constant_value(IntegerConstant, expression_value.value);

                        return {
                            true,
                            {
                                &undetermined_integer_singleton,
                                new RuntimeConstantValue {
                                    new IntegerConstant {
                                        -integer_value->value
                                    }
                                }
                            }
                        };
                    } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_integer(
                            context,
                            instructions,
                            unary_operation->expression->range.first_line,
                            integer->size,
                            address_value->address_register
                        );
                    }

                    auto zero_register = append_integer_constant(context, instructions, unary_operation->range.first_line, integer->size, 0);

                    auto result_register = append_integer_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range.first_line,
                        IntegerArithmeticOperation::Operation::Subtract,
                        integer->size,
                        zero_register,
                        register_index
                    );

                    return {
                        true,
                        {
                            integer,
                            new RegisterValue {
                                result_register
                            }
                        }
                    };
                } else if(expression_value.type->kind == TypeKind::FloatType) {
                    auto float_type = (FloatType*)expression_value.type;

                    size_t register_index;
                    if(expression_value.value->kind == RuntimeValueKind::RuntimeConstantValue) {
                        auto float_value = extract_constant_value(FloatConstant, expression_value.value);

                        return {
                            true,
                            {
                                float_type,
                                new RuntimeConstantValue {
                                    new FloatConstant {
                                        -float_value->value
                                    }
                                }
                            }
                        };
                    } else if(expression_value.value->kind == RuntimeValueKind::RegisterValue) {
                        auto register_value = (RegisterValue*)expression_value.value;

                        register_index = register_value->register_index;
                    } else if(expression_value.value->kind == RuntimeValueKind::AddressValue) {
                        auto address_value = (AddressValue*)expression_value.value;

                        register_index = append_load_float(
                            context,
                            instructions,
                            unary_operation->expression->range.first_line,
                            float_type->size,
                            address_value->address_register
                        );
                    }

                    auto zero_register = append_float_constant(context, instructions, unary_operation->range.first_line, float_type->size, 0.0);

                    auto result_register = append_float_arithmetic_operation(
                        context,
                        instructions,
                        unary_operation->range.first_line,
                        FloatArithmeticOperation::Operation::Subtract,
                        float_type->size,
                        zero_register,
                        register_index
                    );

                    return {
                        true,
                        {
                            float_type,
                            new RegisterValue {
                                result_register
                            }
                        }
                    };
                } else if(expression_value.type->kind == TypeKind::UndeterminedFloat) {
                    auto float_value = extract_constant_value(FloatConstant, expression_value.value);

                    return {
                        true,
                        {
                            &undetermined_float_singleton,
                            new RuntimeConstantValue {
                                new FloatConstant {
                                    -float_value->value
                                }
                            }
                        }
                    };
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect(expression_value, generate_expression(info, scope, context, instructions, cast->expression));

        expect(target_type, evaluate_type_expression_runtime(info, scope, context, instructions, cast->type));

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
                return {
                true,
                    {
                        target_type,
                        new RuntimeConstantValue {
                            constant_cast_result.value
                        }
                    }
                };
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
                        cast->expression->range.first_line,
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
                        cast->range.first_line,
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
                        cast->expression->range.first_line,
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
                    cast->range.first_line,
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
                            cast->expression->range.first_line,
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
                        cast->expression->range.first_line,
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
                    cast->range.first_line,
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
                        cast->expression->range.first_line,
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
                    cast->range.first_line,
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
                            cast->expression->range.first_line,
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
                        cast->expression->range.first_line,
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
            return {
                true,
                {
                    target_type,
                    new RegisterValue {
                        register_index
                    }
                }
            };
        } else {
            error(scope, cast->range, "Cannot cast from '%s' to '%s'", type_description(expression_value.type), type_description(target_type));

            return { false };
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect(type, evaluate_type_expression_runtime(info, scope, context, instructions, array_type->expression));

        if(!is_runtime_type(type)) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return { false };
        }

        if(array_type->index != nullptr) {
            expect(index_value, evaluate_constant_expression(info, scope, context, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    info.address_integer_size,
                    false
                },
                false
            ));

            return {
                true,
                {
                    &type_type_singleton,
                    new RuntimeConstantValue {
                        new TypeConstant {
                            new StaticArray {
                                length->value,
                                type
                            }
                        }
                    }
                }
            };
        } else {
            return {
                true,
                {
                    &type_type_singleton,
                    new RuntimeConstantValue {
                        new TypeConstant {
                            new ArrayTypeType {
                                type
                            }
                        }
                    }
                }
            };
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return { false };
            }

            expect(type, evaluate_type_expression_runtime(info, scope, context, instructions, parameter.type));

            if(!is_runtime_type(type)) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return { false };
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = &void_singleton;
        } else {
            expect(return_type_value, evaluate_type_expression_runtime(info, scope, context, instructions, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return { false };
            }

            return_type = return_type_value;
        }

        return {
            true,
            {
                &type_type_singleton,
                new RuntimeConstantValue {
                    new TypeConstant {
                        new FunctionTypeType {
                            { parameter_count, parameters },
                            return_type
                        }
                    }
                }
            }
        };
    } else {
        abort();
    }
}

static bool is_statement_declaration(Statement *statement) {
    return
        statement->kind == StatementKind::FunctionDeclaration ||
        statement->kind == StatementKind::ConstantDefinition ||
        statement->kind == StatementKind::StructDefinition;
}

static bool generate_statement(GlobalInfo info, ConstantScope scope, GenerationContext *context, List<Instruction*> *instructions, Statement *statement) {
    if(statement->kind == StatementKind::ExpressionStatement) {
        auto expression_statement = (ExpressionStatement*)statement;

        if(!generate_expression(info, scope, context, instructions, expression_statement->expression).status) {
            return false;
        }

        return true;
    } else if(statement->kind == StatementKind::VariableDeclaration) {
        auto variable_declaration = (VariableDeclaration*)statement;

        Type *type;
        size_t address_register;

        if(variable_declaration->is_external) {
            error(scope, variable_declaration->range, "Local variables cannot be external");

            return false;
        }

        if(variable_declaration->is_no_mangle) {
            error(scope, variable_declaration->range, "Local variables cannot be no_mangle");

            return false;
        }

        if(variable_declaration->type != nullptr && variable_declaration->initializer != nullptr) {
            expect(type_value, evaluate_type_expression_runtime(info, scope, context, instructions, variable_declaration->type));
            
            if(!is_runtime_type(type_value)) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return false;
            }

            type = type_value;

            expect(initializer_value, generate_expression(info, scope, context, instructions, variable_declaration->initializer));

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
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
                return false;
            }
        } else if(variable_declaration->type != nullptr) {
            expect(type_value, evaluate_type_expression_runtime(info, scope, context, instructions, variable_declaration->type));

            if(!is_runtime_type(type_value)) {
                error(scope, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return false;
            }

            type = type_value;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
                get_type_size(info, type),
                get_type_alignment(info, type)
            );
        } else if(variable_declaration->initializer != nullptr) {
            expect(initializer_value, generate_expression(info, scope, context, instructions, variable_declaration->initializer));

            expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type));
            
            if(!is_runtime_type(actual_type)) {
                error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%s'", type_description(actual_type));

                return false;
            }

            type = actual_type;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
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
                return false;
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
            return false;
        }

        return true;
    } else if(statement->kind == StatementKind::Assignment) {
        auto assignment = (Assignment*)statement;

        expect(target, generate_expression(info, scope, context, instructions, assignment->target));

        size_t address_register;
        if(target.value->kind == RuntimeValueKind::AddressValue){
            auto address_value = (AddressValue*)target.value;

            address_register = address_value->address_register;
        } else {
            error(scope, assignment->target->range, "Value is not assignable");

            return false;
        }

        expect(value, generate_expression(info, scope, context, instructions, assignment->value));

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
            return false;
        }

        return true;
    } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
        auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

        expect(target, generate_expression(info, scope, context, instructions, binary_operation_assignment->target));

        size_t address_register;
        if(target.value->kind == RuntimeValueKind::AddressValue){
            auto address_value = (AddressValue*)target.value;

            address_register = address_value->address_register;
        } else {
            error(scope, binary_operation_assignment->target->range, "Value is not assignable");

            return false;
        }

        expect(value, generate_binary_operation(
            info,
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
            return false;
        }

        return true;
    } else if(statement->kind == StatementKind::IfStatement) {
        auto if_statement = (IfStatement*)statement;

        auto end_jump_count = 1 + if_statement->else_ifs.count;
        auto end_jumps = allocate<Jump*>(end_jump_count);

        expect(condition, generate_expression(info, scope, context, instructions, if_statement->condition));

        if(condition.type->kind != TypeKind::Boolean) {
            error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

            return false;
        }

        auto condition_register = generate_in_register_boolean_value(info, context, instructions, if_statement->condition->range, condition.value);

        append_branch(context, instructions, if_statement->condition->range.first_line, condition_register, instructions->count + 2);

        auto first_jump = new Jump;
        first_jump->line = if_statement->range.first_line;

        append(instructions, (Instruction*)first_jump);

        ConstantScope if_scope;
        if_scope.statements = if_statement->statements;
        if_scope.constant_parameters = {};
        if_scope.is_top_level = false;
        if_scope.parent = heapify(scope);

        append(&context->variable_scope_stack, {
            if_scope,
            {}
        });

        for(auto child_statement : if_statement->statements) {
            if(!is_statement_declaration(child_statement)) {
                if(!generate_statement(info, if_scope, context, instructions, child_statement)) {
                    return false;
                }
            }
        }

        context->variable_scope_stack.count -= 1;

        auto first_end_jump = new Jump;
        first_end_jump->line = if_statement->range.first_line;

        append(instructions, (Instruction*)first_end_jump);

        end_jumps[0] = first_end_jump;

        first_jump->destination_instruction = instructions->count;

        for(size_t i = 0; i < if_statement->else_ifs.count; i += 1) {
            expect(condition, generate_expression(info, scope, context, instructions, if_statement->else_ifs[i].condition));

            if(condition.type->kind != TypeKind::Boolean) {
                error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
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
                if_statement->else_ifs[i].condition->range.first_line,
                condition_register,
                instructions->count + 2
            );

            auto jump = new Jump;
            jump->line = if_statement->else_ifs[i].condition->range.first_line;

            append(instructions, (Instruction*)jump);

            ConstantScope else_if_scope;
            else_if_scope.statements = if_statement->else_ifs[i].statements;
            else_if_scope.constant_parameters = {};
            else_if_scope.is_top_level = false;
            else_if_scope.parent = heapify(scope);

            append(&context->variable_scope_stack, {
                else_if_scope,
                {}
            });

            for(auto child_statement : if_statement->else_ifs[i].statements) {
                if(!is_statement_declaration(child_statement)) {
                    if(!generate_statement(info, else_if_scope, context, instructions, child_statement)) {
                        return false;
                    }
                }
            }

            context->variable_scope_stack.count -= 1;

            auto end_jump = new Jump;
            end_jump->line = if_statement->range.first_line;

            append(instructions, (Instruction*)end_jump);

            end_jumps[i + 1] = end_jump;

            jump->destination_instruction = instructions->count;
        }

        ConstantScope else_scope;
        else_scope.statements = if_statement->else_statements;
        else_scope.constant_parameters = {};
        else_scope.is_top_level = false;
        else_scope.parent = heapify(scope);

        append(&context->variable_scope_stack, {
            else_scope,
            {}
        });

        for(auto child_statement : if_statement->else_statements) {
            if(!is_statement_declaration(child_statement)) {
                if(!generate_statement(info, else_scope, context, instructions, child_statement)) {
                    return false;
                }
            }
        }

        context->variable_scope_stack.count -= 1;

        for(size_t i = 0; i < end_jump_count; i += 1) {
            end_jumps[i]->destination_instruction = instructions->count;
        }

        return true;
    } else if(statement->kind == StatementKind::WhileLoop) {
        auto while_loop = (WhileLoop*)statement;

        auto condition_index = instructions->count;

        expect(condition, generate_expression(info, scope, context, instructions, while_loop->condition));

        if(condition.type->kind != TypeKind::Boolean) {
            error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %s", type_description(condition.type));

            return false;
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
            while_loop->condition->range.first_line,
            condition_register,
            instructions->count + 2
        );

        auto jump_out = new Jump;
        jump_out->line = while_loop->condition->range.first_line;

        append(instructions, (Instruction*)jump_out);

        ConstantScope while_scope;
        while_scope.statements = while_loop->statements;
        while_scope.constant_parameters = {};
        while_scope.is_top_level = false;
        while_scope.parent = heapify(scope);

        append(&context->variable_scope_stack, {
            while_scope,
            {}
        });

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        for(auto child_statement : while_loop->statements) {
            if(!is_statement_declaration(child_statement)) {
                if(!generate_statement(info, while_scope, context, instructions, child_statement)) {
                    return false;
                }
            }
        }

        auto break_jumps = to_array(context->break_jumps);

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.count -= 1;

        append_jump(
            context,
            instructions,
            while_loop->range.first_line,
            condition_index
        );

        jump_out->destination_instruction = instructions->count;

        for(auto jump : break_jumps) {
            jump->destination_instruction = instructions->count;
        }

        return true;
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

        expect(from_value, generate_expression(info, scope, context, instructions, for_loop->from));

        auto index_address_register = allocate_register(context);

        auto allocate_local = new AllocateLocal;
        allocate_local->line = for_loop->range.first_line;
        allocate_local->destination_register = index_address_register;

        append(instructions, (Instruction*)allocate_local);

        size_t condition_index;
        size_t to_register;
        Integer *index_type;
        if(from_value.type->kind == TypeKind::UndeterminedInteger) {
            auto from_integer_constant = extract_constant_value(IntegerConstant, from_value.value);

            auto from_regsiter = allocate_register(context);

            auto integer_constant = new IntegerConstantInstruction;
            integer_constant->line = for_loop->range.first_line;
            integer_constant->destination_register = from_regsiter;
            integer_constant->value = from_integer_constant->value;

            append(instructions, (Instruction*)integer_constant);

            auto store_integer = new StoreInteger;
            store_integer->line = for_loop->range.first_line;
            store_integer->source_register = from_regsiter;
            store_integer->address_register = index_address_register;

            append(instructions, (Instruction*)store_integer);

            condition_index = instructions->count;

            expect(to_value, generate_expression(info, scope, context, instructions, for_loop->to));

            expect(determined_index_type, coerce_to_default_type(info, scope, for_loop->range, to_value.type));

            if(determined_index_type->kind == TypeKind::Integer) {
                auto integer = (Integer*)determined_index_type;

                allocate_local->size = register_size_to_byte_size(integer->size);
                allocate_local->alignment = register_size_to_byte_size(integer->size);

                integer_constant->size = integer->size;

                store_integer->size = integer->size;

                if(!check_undetermined_integer_to_integer_coercion(scope, for_loop->range, *integer, (int64_t)from_integer_constant->value, false)) {
                    return { false };
                }

                expect(to_register_index, coerce_to_integer_register_value(
                    scope,
                    context,
                    instructions,
                    for_loop->to->range,
                    to_value.type,
                    to_value.value,
                    *integer,
                    false
                ));

                to_register = to_register_index;
                index_type = integer;
            } else {
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%s'", type_description(determined_index_type));

                return { false };
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
                    *integer,
                    false
                ));

                append_store_integer(context, instructions, for_loop->range.first_line, integer->size, from_register, index_address_register);

                condition_index = instructions->count;

                expect(to_value, generate_expression(info, scope, context, instructions, for_loop->to));

                expect(to_register_index, coerce_to_integer_register_value(
                    scope,
                    context,
                    instructions,
                    for_loop->to->range,
                    to_value.type,
                    to_value.value,
                    *integer,
                    false
                ));

                to_register = to_register_index;
                index_type = integer;
            } else {
                error(scope, for_loop->range, "For loop index/range must be an integer. Got '%s'", type_description(determined_index_type));

                return { false };
            }
        }

        auto current_index_regsiter = append_load_integer(
            context,
            instructions,
            for_loop->range.first_line,
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
            for_loop->range.first_line,
            operation,
            index_type->size,
            current_index_regsiter,
            to_register
        );

        auto branch = new Branch;
        branch->line = for_loop->range.first_line;
        branch->condition_register = condition_register;

        append(instructions, (Instruction*)branch);

        ConstantScope body_scope;
        body_scope.statements = for_loop->statements;
        body_scope.constant_parameters = {};
        body_scope.is_top_level = false;
        body_scope.parent = heapify(scope);

        append(&context->variable_scope_stack, {
            body_scope,
            {}
        });

        auto old_in_breakable_scope = context->in_breakable_scope;
        auto old_break_jumps = context->break_jumps;

        context->in_breakable_scope = true;
        context->break_jumps = {};

        if(!add_new_variable(context, index_name, index_address_register, index_type)) {
            return { false };
        }

        for(auto child_statement : for_loop->statements) {
            if(!is_statement_declaration(child_statement)) {
                if(!generate_statement(info, body_scope, context, instructions, child_statement)) {
                    return { false };
                }
            }
        }

        auto break_jumps = to_array(context->break_jumps);

        context->in_breakable_scope = old_in_breakable_scope;
        context->break_jumps = old_break_jumps;

        context->variable_scope_stack.count -= 1;

        auto one_register = append_integer_constant(context, instructions, for_loop->range.last_line, index_type->size, 1);

        auto next_index_register = append_integer_arithmetic_operation(
            context,
            instructions,
            for_loop->range.last_line,
            IntegerArithmeticOperation::Operation::Add,
            index_type->size,
            current_index_regsiter,
            one_register
        );

        append_store_integer(context, instructions, for_loop->range.last_line, index_type->size, next_index_register, index_address_register);

        append_jump(context, instructions, for_loop->range.last_line, condition_index);

        for(auto jump : break_jumps) {
            jump->destination_instruction = instructions->count;
        }

        branch->destination_instruction = instructions->count;

        return true;
    } else if(statement->kind == StatementKind::ReturnStatement) {
        auto return_statement = (ReturnStatement*)statement;

        auto return_instruction = new ReturnInstruction;
        return_instruction->line = return_statement->range.first_line;

        if(return_statement->value != nullptr) {
            if(context->return_type->kind == TypeKind::Void) {
                error(scope, return_statement->range, "Erroneous return value");

                return { false };
            } else {
                expect(value, generate_expression(info, scope, context, instructions, return_statement->value));

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
                        return false;
                    }
                }
            }
        } else if(context->return_type->kind != TypeKind::Void) {
            error(scope, return_statement->range, "Missing return value");

            return { false };
        }

        append(instructions, (Instruction*)return_instruction);

        return true;
    } else if(statement->kind == StatementKind::BreakStatement) {
        auto break_statement = (BreakStatement*)statement;

        if(!context->in_breakable_scope) {
            error(scope, break_statement->range, "Not in a break-able scope");

            return { false };
        }

        auto jump = new Jump;
        jump->line = break_statement->range.first_line;

        append(instructions, (Instruction*)jump);

        append(&context->break_jumps, jump);

        return true;
    } else {
        abort();
    }
}

inline void append_global_type(List<GlobalConstant> *global_constants, const char *name, Type *type) {
    append(global_constants, {
        name,
        &type_type_singleton,
        new TypeConstant {
            type
        }
    });
}

inline void append_base_integer_type(List<GlobalConstant> *global_constants, const char *name, RegisterSize size, bool is_signed) {
    append_global_type(global_constants, name, new Integer { size, is_signed });
}

inline void append_builtin(List<GlobalConstant> *global_constants, const char *name) {
    append(global_constants, {
        name,
        &builtin_function_singleton,
        new BuiltinFunctionConstant {
            name
        }
    });
}

Result<IR> generate_ir(const char *main_file_path, Array<Statement*> main_file_statements, RegisterSize address_size, RegisterSize default_size) {
    List<GlobalConstant> global_constants{};

    append_base_integer_type(&global_constants, "u8", RegisterSize::Size8, false);
    append_base_integer_type(&global_constants, "u16", RegisterSize::Size16, false);
    append_base_integer_type(&global_constants, "u32", RegisterSize::Size32, false);
    append_base_integer_type(&global_constants, "u64", RegisterSize::Size64, false);

    append_base_integer_type(&global_constants, "i8", RegisterSize::Size8, true);
    append_base_integer_type(&global_constants, "i16", RegisterSize::Size16, true);
    append_base_integer_type(&global_constants, "i32", RegisterSize::Size32, true);
    append_base_integer_type(&global_constants, "i64", RegisterSize::Size64, true);

    append_base_integer_type(&global_constants, "usize", address_size, false);
    append_base_integer_type(&global_constants, "isize", address_size, true);

    append_global_type(
        &global_constants,
        "bool",
        &boolean_singleton
    );

    append_global_type(
        &global_constants,
        "void",
        &void_singleton
    );

    append_global_type(
        &global_constants,
        "f32",
        new FloatType {
            RegisterSize::Size32
        }
    );

    append_global_type(
        &global_constants,
        "f64",
        new FloatType {
            RegisterSize::Size64
        }
    );

    append(&global_constants, GlobalConstant {
        "true",
        &boolean_singleton,
        new BooleanConstant { true }
    });

    append(&global_constants, GlobalConstant {
        "false",
        &boolean_singleton,
        new BooleanConstant { false }
    });

    append_global_type(
        &global_constants,
        "type",
        &type_type_singleton
    );

    append_builtin(&global_constants, "size_of");
    append_builtin(&global_constants, "type_of");

    append_builtin(&global_constants, "memcpy");

    GlobalInfo info {
        to_array(global_constants),
        address_size,
        default_size
    };

    GenerationContext context {};

    if(!load_file(info, &context, main_file_statements, main_file_path)) {
        return { false };
    }

    append(&context.loaded_files, {
        main_file_path,
        main_file_statements
    });

    ConstantScope main_file_scope;
    main_file_scope.statements = main_file_statements;
    main_file_scope.constant_parameters = {};
    main_file_scope.is_top_level = true;
    main_file_scope.file_path = main_file_path;

    auto main_found = false;
    for(auto runtime_function : context.runtime_functions) {
        if(strcmp(runtime_function.mangled_name, "main") == 0) {
            main_found = true;
        }
    }

    if(!main_found) {
        fprintf(stderr, "Error: 'main' function not found\n");

        return { false };
    }

    List<const char*> libraries {};

    while(true) {
        auto done = true;
        RuntimeFunction function;
        for(auto runtime_function : context.runtime_functions) {
            auto generated = false;
            for(auto runtime_static : context.statics) {
                if(strcmp(runtime_static->name, runtime_function.mangled_name) == 0) {
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
            if(does_runtime_static_exist(context, function.mangled_name)) {
                error(function.parent, function.declaration->name.range, "Duplicate runtime name '%s'", function.mangled_name);

                return { false };
            }

            auto total_parameter_count = function.parameters.count;

            bool has_return;
            RegisterRepresentation return_representation;                
            if(function.return_type->kind == TypeKind::Void) {
                has_return = false;
            } else {
                has_return = true;

                return_representation = get_type_representation(info, function.return_type);

                if(!return_representation.is_in_register) {
                    total_parameter_count += 1;
                }
            }

            auto ir_parameters = allocate<Function::Parameter>(total_parameter_count);

            for(size_t i = 0; i < function.parameters.count; i += 1) {
                auto representation = get_type_representation(info, function.parameters[i].type);

                if(representation.is_in_register) {
                    ir_parameters[i] = {
                        representation.value_size,
                        representation.is_float
                    };
                } else {
                    ir_parameters[i] = {
                        address_size,
                        false
                    };
                }
            }

            if(has_return && !return_representation.is_in_register) {
                ir_parameters[total_parameter_count - 1] = {
                    address_size,
                    false
                };
            }

            auto current_scope = &function.parent;
            while(!current_scope->is_top_level) {
                current_scope = current_scope->parent;
            }

            auto ir_function = new Function;
            ir_function->name = function.mangled_name;
            ir_function->is_external = function.declaration->is_external;
            ir_function->parameters = {
                total_parameter_count,
                ir_parameters
            };
            ir_function->has_return = has_return && return_representation.is_in_register;
            ir_function->file = current_scope->file_path;
            ir_function->line = function.declaration->range.first_line;

            if(has_return && return_representation.is_in_register) {
                ir_function->return_size = return_representation.value_size;
                ir_function->is_return_float = return_representation.is_float;
            }

            context.next_register = total_parameter_count;

            if(function.declaration->is_external) {
                for(auto library : function.declaration->external_libraries) {
                    auto has_library = false;
                    for(auto existing_library : libraries) {
                        if(strcmp(existing_library, library) == 0) {
                            has_library = true;

                            break;
                        }
                    }

                    if(!has_library) {
                        append(&libraries, library);
                    }
                }
            } else {
                ConstantScope scope;
                scope.statements = function.declaration->statements;
                scope.constant_parameters = function.constant_parameters;
                scope.is_top_level = false;
                scope.parent = heapify(function.parent);

                append(&context.variable_scope_stack, {
                    scope,
                    {}
                });

                List<Instruction*> instructions{};

                auto parameters = allocate<Variable>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    parameters[i] = {
                        parameter.name,
                        parameter.type,
                        i
                    };

                    auto size = get_type_size(info, parameter.type);
                    auto alignment = get_type_alignment(info, parameter.type);

                    auto address_register = append_allocate_local(
                        &context,
                        &instructions,
                        function.declaration->range.first_line,
                        size,
                        alignment
                    );

                    auto representation = get_type_representation(info, parameter.type);

                    if(representation.is_in_register) {
                        if(representation.is_float) {
                            append_store_float(
                                &context,
                                &instructions,
                                function.declaration->range.first_line,
                                representation.value_size,
                                i,
                                address_register
                            );
                        } else {
                            append_store_integer(
                                &context,
                                &instructions,
                                function.declaration->range.first_line,
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
                            function.declaration->range,
                            size,
                            i,
                            address_register,
                            alignment
                        );
                    }

                    add_new_variable(
                        &context,
                        parameter.name,
                        address_register,
                        parameter.type
                    );
                }

                context.return_type = function.return_type;

                if(has_return && !return_representation.is_in_register) {
                    context.return_parameter_register = total_parameter_count - 1;
                }

                for(auto statement : function.declaration->statements) {
                    if(!is_statement_declaration(statement)) {
                        if(!generate_statement(info, scope, &context, &instructions, statement)) {
                            return { false };
                        }
                    }
                }

                bool has_return_at_end;
                if(function.declaration->statements.count > 0) {
                    auto last_statement = function.declaration->statements[function.declaration->statements.count - 1];

                    has_return_at_end = last_statement->kind == StatementKind::ReturnStatement;
                } else {
                    has_return_at_end = false;
                }

                if(!has_return_at_end) {
                    if(has_return) {
                        error(scope, function.declaration->range, "Function '%s' must end with a return", function.declaration->name.text);

                        return { false };
                    } else {
                        auto return_instruction = new ReturnInstruction;
                        return_instruction->line = function.declaration->range.last_line;

                        append(&instructions, (Instruction*)return_instruction);
                    }
                }

                context.variable_scope_stack.count -= 1;
                context.next_register = 0;

                ir_function->instructions = to_array(instructions);
            }

            append(&context.statics, (RuntimeStatic*)ir_function);
        }
    }

    return {
        true,
        {
            to_array(context.statics),
            to_array(libraries)
        }
    };
}