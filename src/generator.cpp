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

struct DeterminedDeclaration;

struct DeclarationParent {
    DeterminedDeclaration *parent;

    const char *file_path;
    Array<Statement*> top_level_statements;
};

struct DeterminedDeclaration {
    Statement *declaration;

    Array<ConstantParameter> constant_parameters;

    DeclarationParent parent;
};

struct Value {
    virtual ~Value() {}
};

struct ConstantValue : Value {
    virtual ~ConstantValue() {}
};

struct Type : ConstantValue {
    virtual ~Type() {}
};

struct FunctionTypeType : Type {
    Array<Type*> parameters;

    Type *return_type;

    FunctionTypeType(
        Array<Type*> parameters,
        Type *return_type
    ) :
    parameters { parameters },
    return_type { return_type }
    {}
};

struct PolymorphicFunction : Type {};

struct BuiltinFunction : Type {};

struct Integer : Type {
    RegisterSize size;

    bool is_signed;

    Integer(
        RegisterSize size,
        bool is_signed
    ) :
    size { size },
    is_signed { is_signed }
    {}
};

struct UndeterminedInteger : Type {};

struct Boolean : Type {};

struct FloatType : Type {
    RegisterSize size;

    FloatType(
        RegisterSize size
    ) :
    size { size }
    {}
};

struct UndeterminedFloat : Type {};

struct TypeType : Type {};

struct Void : Type {};

struct Pointer : Type {
    Type *type;

    Pointer(
        Type *type
    ) :
    type { type }
    {}
};

struct ArrayTypeType : Type {
    Type *element_type;

    ArrayTypeType(
        Type *element_type
    ) :
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
    definition { definition },
    members { members }
    {}
};

struct PolymorphicStruct : Type {
    StructDefinition *definition;

    Type **parameter_types;

    DeclarationParent parent;

    PolymorphicStruct(
        StructDefinition *definition,
        Type **parameter_types,
        DeclarationParent parent
    ) :
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
    members { members }
    {}
};

struct FileModule : Type {};

static bool types_equal(Type *a, Type *b) {
    if(auto a_function_type = dynamic_cast<FunctionTypeType*>(a)) {
        if(auto b_function_type = dynamic_cast<FunctionTypeType*>(b)) {
            if(a_function_type->parameters.count != b_function_type->parameters.count) {
                return false;
            }

            for(size_t i = 0; i < a_function_type->parameters.count; i += 1) {
                if(!types_equal(a_function_type->parameters[i], b_function_type->parameters[i])) {
                    return false;
                }
            }

            return types_equal(a_function_type->return_type, b_function_type->return_type);
        } else {
            return false;
        }
    } else if(dynamic_cast<PolymorphicFunction*>(a)) {
        return false;
    } else if(dynamic_cast<BuiltinFunction*>(a)) {
        return false;
    } else if(auto a_integer = dynamic_cast<Integer*>(a)) {
        if(auto b_integer = dynamic_cast<Integer*>(b)) {
            return a_integer->size == b_integer->size && a_integer->is_signed == b_integer->is_signed;
        } else {
            return false;
        }
    } else if(dynamic_cast<UndeterminedInteger*>(a)) {
        return dynamic_cast<UndeterminedInteger*>(b);
    } else if(dynamic_cast<Boolean*>(a)) {
        return dynamic_cast<Boolean*>(b);
    } else if(auto a_float_type = dynamic_cast<FloatType*>(a)) {
        if(auto b_float_type = dynamic_cast<FloatType*>(b)) {
            return a_float_type->size == b_float_type->size;
        } else {
            return false;
        }
    } else if(dynamic_cast<UndeterminedFloat*>(a)) {
        return dynamic_cast<UndeterminedFloat*>(b);
    } else if(dynamic_cast<TypeType*>(a)) {
        return dynamic_cast<TypeType*>(b);
    } else if(dynamic_cast<Void*>(a)) {
        return dynamic_cast<Void*>(b);
    } else if(auto a_pointer = dynamic_cast<Pointer*>(a)) {
        if(auto b_pointer = dynamic_cast<Pointer*>(b)) {
            return types_equal(a_pointer->type, b_pointer->type);
        } else {
            return false;
        }
    } else if(auto a_array = dynamic_cast<ArrayTypeType*>(a)) {
        if(auto b_array = dynamic_cast<ArrayTypeType*>(b)) {
            return types_equal(a_array->element_type, b_array->element_type);
        } else {
            return false;
        }
    } else if(auto a_static_array = dynamic_cast<StaticArray*>(a)) {
        if(auto b_static_array = dynamic_cast<StaticArray*>(b)) {
            return types_equal(a_static_array->element_type, b_static_array->element_type) && a_static_array->length == b_static_array->length;
        } else {
            return false;
        }
    } else if(auto a_struct = dynamic_cast<StructType*>(a)) {
        if(auto b_struct = dynamic_cast<StructType*>(b)) {
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
        } else {
            return false;
        }
    } else if(auto a_polymorphic_struct = dynamic_cast<PolymorphicStruct*>(a)) {
        if(auto b_polymorphic_struct = dynamic_cast<PolymorphicStruct*>(b)) {
            return a_polymorphic_struct->definition != b_polymorphic_struct->definition;
        } else {
            return false;
        }
    } else if(auto a_undetermined_struct = dynamic_cast<UndeterminedStruct*>(a)) {
        if(auto b_undetermined_struct = dynamic_cast<UndeterminedStruct*>(b)) {
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
        } else {
            return false;
        }
    } else if(dynamic_cast<FileModule*>(a)) {
        return dynamic_cast<FileModule*>(b);
    } else {
        abort();
    }
}

static const char *type_description(Type *type) {
    if(auto function = dynamic_cast<FunctionTypeType*>(type)) {
        char *buffer{};

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
        return buffer;
    } else if(dynamic_cast<PolymorphicFunction*>(type)) {
        return "{function}";
    } else if(dynamic_cast<BuiltinFunction*>(type)) {
        return "{builtin}";
    } else if(auto integer = dynamic_cast<Integer*>(type)) {
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
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        return "{integer}";
    } else if(dynamic_cast<Boolean*>(type)) {
        return "bool";
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
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
    } else if(dynamic_cast<UndeterminedFloat*>(type)) {
        return "{float}";
    } else if(dynamic_cast<TypeType*>(type)) {
        return "{type}";
    } else if(dynamic_cast<Void*>(type)) {
        return "void";
    } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
        char *buffer{};

        string_buffer_append(&buffer, "*");
        string_buffer_append(&buffer, type_description(pointer->type));

        return buffer;
    } else if(auto array = dynamic_cast<ArrayTypeType*>(type)) {
        char *buffer{};

        string_buffer_append(&buffer, "[]");
        string_buffer_append(&buffer, type_description(array->element_type));

        return buffer;
    } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
        char *buffer{};

        string_buffer_append(&buffer, "[");
        string_buffer_append(&buffer, static_array->length);
        string_buffer_append(&buffer, "]");
        string_buffer_append(&buffer, type_description(static_array->element_type));

        return buffer;
    } else if(auto struct_type = dynamic_cast<StructType*>(type)) {
        return struct_type->definition->name.text;
    } else if(auto polymorphic_struct = dynamic_cast<PolymorphicStruct*>(type)) {
        return polymorphic_struct->definition->name.text;
    } else if(dynamic_cast<UndeterminedStruct*>(type)) {
        return "{struct}";
    } else if(dynamic_cast<FileModule*>(type)) {
        return "{module}";
    } else {
        abort();
    }
}

static bool is_runtime_type(Type *type) {
    if(
        dynamic_cast<Integer*>(type) ||
        dynamic_cast<Boolean*>(type) ||
        dynamic_cast<FloatType*>(type) ||
        dynamic_cast<Pointer*>(type) ||
        dynamic_cast<ArrayTypeType*>(type) ||
        dynamic_cast<StaticArray*>(type) ||
        dynamic_cast<StructType*>(type)
    ) {
        return true;
    } else {
        return false;
    }
}

struct FunctionConstant : ConstantValue {
    const char *mangled_name;

    FunctionDeclaration *declaration;

    DeclarationParent parent;

    FunctionConstant() {}

    FunctionConstant(
        const char *mangled_name,
        FunctionDeclaration *declaration,
        DeclarationParent parent
    ) :
    mangled_name { mangled_name },
    declaration { declaration },
    parent { parent }
    {}
};

struct BuiltinFunctionConstant : ConstantValue {
    const char *name;

    BuiltinFunctionConstant() {}

    BuiltinFunctionConstant(
        const char *name
    ) :
    name { name }
    {}
};

struct PolymorphicFunctionConstant : ConstantValue {
    FunctionDeclaration *declaration;

    DeclarationParent parent;

    PolymorphicFunctionConstant() {}

    PolymorphicFunctionConstant(
        FunctionDeclaration *declaration,
        DeclarationParent parent
    ) :
    declaration { declaration },
    parent { parent }
    {}
};

struct IntegerConstant : ConstantValue {
    uint64_t value;

    IntegerConstant() {}

    IntegerConstant(
        uint64_t value
    ) :
    value { value }
    {}
};

struct FloatConstant : ConstantValue {
    double value;

    FloatConstant() {}

    FloatConstant(
        double value
    ) :
    value { value }
    {}
};

struct BooleanConstant : ConstantValue {
    bool value;

    BooleanConstant() {}

    BooleanConstant(
        bool value
    ) :
    value { value }
    {}
};

struct VoidConstant : ConstantValue {
    VoidConstant() {}
};

struct PointerConstant : ConstantValue {
    uint64_t value;

    PointerConstant() {}

    PointerConstant(
        uint64_t value
    ) :
    value { value }
    {}
};

struct ArrayConstant : ConstantValue {
    uint64_t length;

    uint64_t pointer;

    ArrayConstant() {}

    ArrayConstant(
        uint64_t length,
        uint64_t pointer
    ) :
    length { length },
    pointer { pointer }
    {}
};

struct StaticArrayConstant : ConstantValue {
    ConstantValue **elements;

    StaticArrayConstant() {}

    StaticArrayConstant(
        ConstantValue **elements
    ) :
    elements { elements }
    {}
};

struct StructConstant : ConstantValue {
    ConstantValue **members;

    StructConstant() {}

    StructConstant(
        ConstantValue **members
    ) :
    members { members }
    {}
};

struct FileModuleConstant : ConstantValue {
    const char *path;

    Array<Statement*> statements;

    FileModuleConstant() {}

    FileModuleConstant(
        const char *path,
        Array<Statement*> statements
    ) :
    path { path },
    statements { statements }
    {}
};

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
    FileRange type_range;

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

    DeterminedDeclaration declaration;
};

struct ParsedFile {
    const char *path;

    Array<Statement*> statements;
};

struct GenerationContext {
    RegisterSize address_integer_size;
    RegisterSize default_integer_size;

    Array<GlobalConstant> global_constants;

    bool is_top_level;

    DeclarationParent parent;

    Array<ConstantParameter> constant_parameters;

    Type *return_type;
    size_t return_parameter_register;

    List<List<Variable>> variable_context_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<RuntimeStatic*> statics;

    List<ParsedFile> parsed_files;
};

static void error(GenerationContext context, FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    const char *file_path;
    if(context.is_top_level) {
        file_path = context.parent.file_path;
    } else {
        auto current_declaration = context.parent.parent;

        while(current_declaration->declaration->parent) {
            current_declaration = current_declaration->parent.parent;
        }

        file_path = current_declaration->parent.file_path;
    }

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

static bool match_public_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(statement)) {
        declaration_name = function_declaration->name.text;
    } else if(auto constant_definition = dynamic_cast<ConstantDefinition*>(statement)) {
        declaration_name = constant_definition->name.text;
    } else if(auto struct_definition = dynamic_cast<StructDefinition*>(statement)) {
        declaration_name = struct_definition->name.text;
    } else {
        return false;
    }

    return strcmp(declaration_name, name) == 0;
}

static bool match_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(statement)) {
        declaration_name = function_declaration->name.text;
    } else if(auto constant_definition = dynamic_cast<ConstantDefinition*>(statement)) {
        declaration_name = constant_definition->name.text;
    } else if(auto struct_definition = dynamic_cast<StructDefinition*>(statement)) {
        declaration_name = struct_definition->name.text;
    } else if(auto import = dynamic_cast<Import*>(statement)) {
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

static uint64_t get_type_alignment(GenerationContext context, Type *type);

static uint64_t get_struct_alignment(GenerationContext context, StructType type) {
    size_t current_alignment = 1;

    for(auto member : type.members) {
        auto alignment = get_type_alignment(context, member.type);

        if(alignment > current_alignment) {
            current_alignment = alignment;
        }
    }

    return current_alignment;
}

static uint64_t get_type_alignment(GenerationContext context, Type *type) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        return register_size_to_byte_size(integer->size);
    } else if(dynamic_cast<Boolean*>(type)) {
        return register_size_to_byte_size(context.default_integer_size);
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
        return register_size_to_byte_size(float_type->size);
    } else if(dynamic_cast<Pointer*>(type)) {
        return register_size_to_byte_size(context.address_integer_size);
    } else if(dynamic_cast<ArrayTypeType*>(type)) {
        return register_size_to_byte_size(context.address_integer_size);
    } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
        return get_type_alignment(context, static_array->element_type);
    } else if(auto struct_type = dynamic_cast<StructType*>(type)) {
        return get_struct_alignment(context, *struct_type);
    } else {
        abort();
    }
}

static uint64_t get_type_size(GenerationContext context, Type *type);

static uint64_t get_struct_size(GenerationContext context, StructType type) {
    uint64_t current_size = 0;

    for(auto member : type.members) {
        if(type.definition->is_union) {
            auto size = get_type_size(context, member.type);

            if(size > current_size) {
                current_size = size;
            }
        } else {
            auto alignment = get_type_alignment(context, member.type);

            auto alignment_difference = current_size % alignment;

            uint64_t offset;
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

static uint64_t get_type_size(GenerationContext context, Type *type) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        return register_size_to_byte_size(integer->size);
    } else if(dynamic_cast<Boolean*>(type)) {
        return register_size_to_byte_size(context.default_integer_size);
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
        return register_size_to_byte_size(float_type->size);
    } else if(dynamic_cast<Pointer*>(type)) {
        return register_size_to_byte_size(context.address_integer_size);
    } else if(dynamic_cast<ArrayTypeType*>(type)) {
        return 2 * register_size_to_byte_size(context.address_integer_size);
    } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
        return static_array->length * get_type_alignment(context, static_array->element_type);
    } else if(auto struct_type = dynamic_cast<StructType*>(type)) {
        return get_struct_size(context, *struct_type);
    } else {
        abort();
    }
}

static uint64_t get_struct_member_offset(GenerationContext context, StructType type, size_t member_index) {
    if(type.definition->is_union) {
        return 0;
    }

    uint64_t current_offset = 0;

    for(auto i = 0; i < member_index; i += 1) {
        auto alignment = get_type_alignment(context, type.members[i].type);

        auto alignment_difference = current_offset % alignment;

        uint64_t offset;
        if(alignment_difference != 0) {
            offset = alignment - alignment_difference;
        } else {
            offset = 0;
        }

        auto size = get_type_size(context, type.members[i].type);

        current_offset += offset + size;
    }
    
    auto alignment = get_type_alignment(context, type.members[member_index].type);

    auto alignment_difference = current_offset % alignment;

    uint64_t offset;
    if(alignment_difference != 0) {
        offset = alignment - alignment_difference;
    } else {
        offset = 0;
    }

    return current_offset + offset;
}

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Expression *expression);

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Statement *declaration);

static Result<TypedConstantValue> resolve_constant_named_reference(GenerationContext *context, Identifier name) {
    for (auto constant_parameter : context->constant_parameters) {
        if (strcmp(constant_parameter.name, name.text) == 0) {
            return {
                true,
                {
                    constant_parameter.type,
                    constant_parameter.value
                }
            };
        }
    }

    auto old_is_top_level = context->is_top_level;
    auto old_constant_parameters = context->constant_parameters;
    auto old_parent = context->parent;

    while(!context->is_top_level) {
        auto parent_declaration = context->parent.parent;

        if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(parent_declaration->declaration)) {
            for(auto statement : function_declaration->statements) {
                if(match_declaration(statement, name.text)) {
                    expect(value, resolve_declaration(context, statement));

                    context->is_top_level = old_is_top_level;
                    context->constant_parameters = old_constant_parameters;
                    context->parent = old_parent;

                    return {
                        true,
                        value
                    };
                } else if(auto using_statement = dynamic_cast<UsingStatement*>(statement)) {
                    expect(expression_value, evaluate_constant_expression(context, using_statement->module));

                    if(!dynamic_cast<FileModule*>(expression_value.type)) {
                        error(*context, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                        return { false };
                    }

                    auto file_module = dynamic_cast<FileModuleConstant*>(expression_value.value);
                    assert(file_module);

                    auto sub_old_is_top_level = context->is_top_level;
                    auto sub_old_constant_parameters = context->constant_parameters;
                    auto sub_old_parent = context->parent;

                    context->is_top_level = true;
                    context->parent.top_level_statements = file_module->statements;
                    context->parent.file_path = file_module->path;
                    context->constant_parameters = {};

                    for(auto statement : file_module->statements) {
                        if(match_public_declaration(statement, name.text)) {
                            expect(value, resolve_declaration(context, statement));

                            context->is_top_level = old_is_top_level;
                            context->constant_parameters = old_constant_parameters;
                            context->parent = old_parent;

                            return {
                                true,
                                value
                            };
                        }
                    }

                    context->is_top_level = sub_old_is_top_level;
                    context->constant_parameters = sub_old_constant_parameters;
                    context->parent = sub_old_parent;
                }
            }
        }

        for(auto constant_parameter : parent_declaration->constant_parameters) {
            if(strcmp(constant_parameter.name, name.text) == 0) {
                context->is_top_level = old_is_top_level;
                context->constant_parameters = old_constant_parameters;
                context->parent = old_parent;

                return {
                    true,
                    {
                        constant_parameter.type,
                        constant_parameter.value
                    }
                };
            }
        }

        context->is_top_level = parent_declaration->declaration->parent == nullptr;
        context->parent = parent_declaration->parent;
        context->constant_parameters = {};
    }

    for(auto statement : context->parent.top_level_statements) {
        if(match_declaration(statement, name.text)) {
            expect(value, resolve_declaration(context, statement));

            context->is_top_level = old_is_top_level;
            context->constant_parameters = old_constant_parameters;
            context->parent = old_parent;

            return {
                true,
                value
            };
        } else if(auto using_statement = dynamic_cast<UsingStatement*>(statement)) {
            expect(expression_value, evaluate_constant_expression(context, using_statement->module));

            if(!dynamic_cast<FileModule*>(expression_value.type)) {
                error(*context, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                return { false };
            }

            auto file_module = dynamic_cast<FileModuleConstant*>(expression_value.value);
            assert(file_module != nullptr);

            auto sub_old_is_top_level = context->is_top_level;
            auto sub_old_constant_parameters = context->constant_parameters;
            auto sub_old_parent = context->parent;

            context->is_top_level = true;
            context->parent.top_level_statements = file_module->statements;
            context->parent.file_path = file_module->path;
            context->constant_parameters = {};

            for(auto statement : file_module->statements) {
                if(match_public_declaration(statement, name.text)) {
                    expect(value, resolve_declaration(context, statement));

                    context->is_top_level = old_is_top_level;
                    context->constant_parameters = old_constant_parameters;
                    context->parent = old_parent;

                    return {
                        true,
                        value
                    };
                }
            }

            context->is_top_level = sub_old_is_top_level;
            context->constant_parameters = sub_old_constant_parameters;
            context->parent = sub_old_parent;
        }
    }

    context->is_top_level = old_is_top_level;
    context->constant_parameters = old_constant_parameters;
    context->parent = old_parent;

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

    error(*context, name.range, "Cannot find named reference %s", name.text);

    return { false };
}

static Result<IntegerConstant*> coerce_constant_to_integer_type(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Integer target_type,
    bool probing
) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        if(integer->size != target_type.size || integer->size != target_type.size) {
            if(!probing) {
                error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(integer), type_description(&target_type));
            }

            return { false };
        }

        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        return {
            true,
            integer_value
        };
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        return {
            true,
            integer_value
        };
    } else {
        if(!probing) {
            error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
        }

        return { false };
    }
}

static Result<IntegerConstant*> coerce_constant_to_undetermined_integer(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    bool probing
) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

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
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        return {
            true,
            integer_value
        };
    } else {
        if(!probing) {
            error(context, range, "Cannot implicitly convert '%s' to '{integer}'", type_description(type));
        }

        return { false };
    }
}

static Result<ConstantValue*> coerce_constant_to_type(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Type *target_type,
    bool probing
);

static Result<PointerConstant*> coerce_constant_to_pointer_type(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Pointer target_type,
    bool probing
) {
    if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        return {
            true,
            new PointerConstant {
                integer_value->value
            }
        };
    } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
        if(types_equal(pointer->type, target_type.type)) {
            auto pointer_value = dynamic_cast<PointerConstant*>(value);
            assert(pointer_value);

            return {
                true,
                pointer_value
            };
        }
    }

    if(!probing) {
        error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<ConstantValue*> coerce_constant_to_type(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Type *target_type,
    bool probing
) {
    if(auto integer = dynamic_cast<Integer*>(target_type)) {
        expect(integer_value, coerce_constant_to_integer_type(context, range, type, value, *integer, probing));

        return {
            true,
            integer_value
        };
    } else if(dynamic_cast<UndeterminedInteger*>(target_type)) {
        expect(integer_value, coerce_constant_to_undetermined_integer(context, range, type, value, probing));

        return {
            true,
            integer_value
        };
    } else if(auto target_float_type = dynamic_cast<FloatType*>(target_type)) {
        if(dynamic_cast<UndeterminedInteger*>(type)) {
            auto integer_value = dynamic_cast<IntegerConstant*>(value);
            assert(integer_value);

            return {
                true,
                new FloatConstant {
                    (double)integer_value->value
                }
            };
        } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
            if(target_float_type->size == float_type->size) {
                return {
                    true,
                    value
                };
            }
        } else if(dynamic_cast<UndeterminedFloat*>(type)) {
            return {
                true,
                value
            };
        }
    } else if(dynamic_cast<UndeterminedFloat*>(target_type)) {
        if(auto float_type = dynamic_cast<FloatType*>(type)) {
            auto float_value = dynamic_cast<FloatConstant*>(value);
            assert(float_value);

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
        } else if(dynamic_cast<UndeterminedFloat*>(type)) {
            return {
                true,
                value
            };
        }
    } else if(auto target_pointer = dynamic_cast<Pointer*>(target_type)) {
        expect(pointer_value, coerce_constant_to_pointer_type(context, range, type, value, *target_pointer, probing));

        return {
            true,
            pointer_value
        };
    } else if(auto target_array_type = dynamic_cast<ArrayTypeType*>(target_type)) {
        if(auto array_type = dynamic_cast<ArrayTypeType*>(type)) {
            if(types_equal(target_array_type->element_type, array_type->element_type)) {
                return {
                    true,
                    value
                };
            }
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = dynamic_cast<StructConstant*>(value);
                assert(undetermined_struct_value);

                auto pointer_result = coerce_constant_to_pointer_type(
                    context,
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
                        context,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        {
                            context.address_integer_size,
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
        error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return { false };
}

static Result<TypedConstantValue> evaluate_constant_index(
    GenerationContext context,
    Type *type,
    ConstantValue *value,
    FileRange range,
    Type *index_type,
    ConstantValue *index_value,
    FileRange index_range
) {
    expect(index, coerce_constant_to_integer_type(
        context,
        index_range,
        index_type,
        index_value,
        {
            context.address_integer_size,
            false
        },
        false
    ));

    if(auto static_array = dynamic_cast<StaticArray*>(type)) {
        if(index->value >= static_array->length) {
            error(context, index_range, "Array index %zu out of bounds", index);

            return { false };
        }

        auto static_array_value = dynamic_cast<StaticArrayConstant*>(value);
        assert(static_array_value);

        return {
            true,
            {
                static_array->element_type,
                static_array_value->elements[index->value]
            }
        };
    } else {
        error(context, range, "Cannot index %s", type_description(type));

        return { false };
    }
}

static Result<Type*> determine_binary_operation_type(GenerationContext context, FileRange range, Type *left, Type *right) {
    auto left_integer = dynamic_cast<Integer*>(left);
    auto right_integer = dynamic_cast<Integer*>(right);

    auto left_float = dynamic_cast<FloatType*>(left);
    auto right_float = dynamic_cast<FloatType*>(right);

    if(dynamic_cast<Boolean*>(left) || dynamic_cast<Boolean*>(right)) {
        return {
            true,
            new Boolean
        };
    } else if(auto left_pointer = dynamic_cast<Pointer*>(left)) {
        return {
            true,
            left_pointer
        };
    } else if(auto right_pointer = dynamic_cast<Pointer*>(right)) {
        return {
            true,
            right_pointer
        };
    } else if(left_integer && right_integer) {
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
    } else if(left_float && right_float) {
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
    } else if(left_float) {
        return {
            true,
            left_float
        };
    } else if(right_float) {
        return {
            true,
            right_float
        };
    } else if(dynamic_cast<UndeterminedFloat*>(left) || dynamic_cast<UndeterminedFloat*>(right)) {
        return {
            true,
            new UndeterminedFloat
        };
    } else if(left_integer) {
        return {
            true,
            left_integer
        };
    } else if(right_integer) {
        return {
            true,
            right_integer
        };
    } else if(dynamic_cast<UndeterminedInteger*>(left) || dynamic_cast<UndeterminedInteger*>(right)) {
        return {
            true,
            new UndeterminedInteger
        };
    } else {
        error(context, range, "Mismatched types '%s' and '%s'", type_description(left), type_description(right));

        return { false };
    }
}

static Result<TypedConstantValue> evaluate_constant_binary_operation(
    GenerationContext context,
    FileRange range,
    BinaryOperation::Operator binary_operator,
    FileRange left_range,
    Type *left_type,
    ConstantValue *left_value,
    FileRange right_range,
    Type *right_type,
    ConstantValue *right_value
) {
    expect(type, determine_binary_operation_type(context, range, left_type, right_type));

    expect(coerced_left_value, coerce_constant_to_type(context, left_range, left_type, left_value, type, false));

    expect(coerced_right_value, coerce_constant_to_type(context, right_range, right_type, right_value, type, false));

    if(auto integer = dynamic_cast<Integer*>(type)) {
        auto left = dynamic_cast<IntegerConstant*>(coerced_left_value);
        assert(left);

        auto right = dynamic_cast<IntegerConstant*>(coerced_right_value);
        assert(right);

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
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
                        new BooleanConstant {
                            result
                        }
                    }
                };
            } break;

            default: {
                error(context, range, "Cannot perform that operation on integers");

                return { false };
            } break;
        }
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto left = dynamic_cast<IntegerConstant*>(coerced_left_value);
        assert(left);

        auto right = dynamic_cast<IntegerConstant*>(coerced_right_value);
        assert(right);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return {
                    true,
                    {
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new UndeterminedInteger,
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
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
                        new BooleanConstant {
                            (int64_t)left->value > (int64_t)right->value
                        }
                    }
                };
            } break;

            default: {
                error(context, range, "Cannot perform that operation on integers");

                return { false };
            } break;
        }
    } else if(dynamic_cast<Boolean*>(type)) {
        auto left = dynamic_cast<BooleanConstant*>(coerced_left_value);
        assert(left);

        auto right = dynamic_cast<BooleanConstant*>(coerced_right_value);
        assert(right);

        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                return {
                    true,
                    {
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
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
                        new Boolean,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(context, range, "Cannot perform that operation on booleans");

                return { false };
            } break;
        }
    } else if(dynamic_cast<FloatType*>(type) || dynamic_cast<UndeterminedFloat*>(type)) {
        auto left = dynamic_cast<FloatConstant*>(coerced_left_value);
        assert(left);

        auto right = dynamic_cast<FloatConstant*>(coerced_right_value);
        assert(right);

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
                        new Boolean,
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
                        new Boolean,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(context, range, "Cannot perform that operation on pointers");

                return { false };
            } break;
        }
    } else if(dynamic_cast<Pointer*>(type)) {
        auto left = dynamic_cast<PointerConstant*>(coerced_left_value);
        assert(left);

        auto right = dynamic_cast<PointerConstant*>(coerced_right_value);
        assert(right);

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {
                return {
                    true,
                    {
                        new Boolean,
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
                        new Boolean,
                        new BooleanConstant {
                            left->value != right->value
                        }
                    }
                };
            } break;

            default: {
                error(context, range, "Cannot perform that operation on pointers");

                return { false };
            } break;
        }
    } else {
        abort();
    }
}

static Result<ConstantValue*> evaluate_constant_cast(
    GenerationContext context,
    Type *type,
    ConstantValue *value,
    FileRange value_range,
    Type *target_type,
    FileRange target_range,
    bool probing
) {
    auto coerce_result = coerce_constant_to_type(
        context,
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

    if(auto target_integer = dynamic_cast<Integer*>(target_type)) {
        uint64_t result;

        if(auto integer = dynamic_cast<Integer*>(type)) {
            auto integer_value = dynamic_cast<IntegerConstant*>(value);
            assert(integer_value);

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
        } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
            auto float_value = dynamic_cast<FloatConstant*>(value);
            assert(float_value);

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
        } else if(dynamic_cast<UndeterminedFloat*>(type)) {
            auto float_value = dynamic_cast<FloatConstant*>(value);
            assert(float_value);

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
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            if(target_integer->size == context.address_integer_size && !target_integer->is_signed) {
                auto pointer_value = dynamic_cast<PointerConstant*>(value);
                assert(pointer_value);

                result = pointer_value->value;
            } else {
                if(!probing) {
                    error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(pointer), type_description(target_integer));
                }

                return { false };
            }
        } else {
            if(!probing) {
                error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_integer));
            }

            return { false };
        }

        return {
            true,
            new IntegerConstant {
                result
            }
        };
    } else if(auto target_float_type = dynamic_cast<FloatType*>(target_type)) {
        double result;
        if(auto integer = dynamic_cast<Integer*>(type)) {
            auto integer_value = dynamic_cast<IntegerConstant*>(value);
            assert(integer_value);

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
        } else if(dynamic_cast<UndeterminedInteger*>(type)) {
            auto integer_value = dynamic_cast<IntegerConstant*>(value);
            assert(integer_value);

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
        } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
            auto float_value = dynamic_cast<FloatConstant*>(value);
            assert(float_value);

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
        } else if(dynamic_cast<UndeterminedFloat*>(type)) {
            auto float_value = dynamic_cast<FloatConstant*>(value);
            assert(float_value);

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
                error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_float_type));
            }

            return { false };
        }

        return {
            true,
            new FloatConstant {
                result
            }
        };
    } else if(auto target_pointer = dynamic_cast<Pointer*>(target_type)) {
        uint64_t result;

        if(auto integer = dynamic_cast<Integer*>(type)) {
            if(integer->size == context.address_integer_size && !integer->is_signed) {
                auto integer_value = dynamic_cast<IntegerConstant*>(value);
                assert(integer_value);

                result = integer_value->value;
            } else {
                if(!probing) {
                    error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(integer), type_description(target_pointer));
                }

                return { false };
            }
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            auto pointer_value = dynamic_cast<PointerConstant*>(value);
            assert(pointer_value);

            result = pointer_value->value;
        } else {
            if(!probing) {
                error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_pointer));
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
            error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_type));
        }

        return { false };
    }
}

struct RegisterRepresentation {
    bool is_in_register;

    RegisterSize value_size;
    bool is_float;
};

static RegisterRepresentation get_type_representation(GenerationContext context, Type *type) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        return {
            true,
            integer->size,
            false
        };
    } else if(dynamic_cast<Boolean*>(type)) {
        return {
            true,
            context.default_integer_size,
            false
        };
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
        return {
            true,
            float_type->size,
            true
        };
    } else if(dynamic_cast<Pointer*>(type)) {
        return {
            true,
            context.address_integer_size,
            false
        };
    } else if(
        dynamic_cast<ArrayTypeType*>(type) ||
        dynamic_cast<StaticArray*>(type) ||
        dynamic_cast<StructType*>(type)
    ) {
        return {
            false
        };
    } else {
        abort();
    }
}

static Result<Type*> coerce_to_default_type(GenerationContext context, FileRange range, Type *type) {
    if(dynamic_cast<UndeterminedInteger*>(type)) {
        return {
            true,
            new Integer {
                context.default_integer_size,
                true
            }
        };
    } else if(dynamic_cast<UndeterminedFloat*>(type)) {
        return {
            true,
            new FloatType {
                context.default_integer_size
            }
        };
    } else if(dynamic_cast<UndeterminedStruct*>(type)) {
        error(context, range, "Undetermined struct types cannot exist at runtime");

        return { false };
    } else {
        return {
            true,
            type
        };
    }
}

static const char* generate_function_mangled_name(GenerationContext context, FunctionDeclaration declaration) {
    if(declaration.is_external) {
        return declaration.name.text;
    } else {
        char *buffer{};

        string_buffer_append(&buffer, declaration.name.text);

        if(context.is_top_level) {
            string_buffer_append(&buffer, "_");
            string_buffer_append(&buffer, path_get_file_component(context.parent.file_path));
        } else {
            auto current = context.parent.parent;

            while(current->declaration->parent) {
                const char *name;
                if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(current->declaration)) {
                    return function_declaration->name.text;
                } else if(auto constant_definition = dynamic_cast<ConstantDefinition*>(current->declaration)) {
                    return constant_definition->name.text;
                } else if(auto struct_definition = dynamic_cast<StructDefinition*>(current->declaration)) {
                    return struct_definition->name.text;
                } else {
                    abort();
                }

                string_buffer_append(&buffer, "_");
                string_buffer_append(&buffer, name);

                current = current->parent.parent;
            }

            string_buffer_append(&buffer, "_");
            string_buffer_append(&buffer, path_get_file_component(current->parent.file_path));
        }

        return buffer;
    }
}

static Result<Type*> evaluate_type_expression(GenerationContext *context, Expression *expression);

static Result<TypedConstantValue> evaluate_function_declaration(GenerationContext *context, FunctionDeclaration *declaration) {
    for(auto parameter : declaration->parameters) {
        if(parameter.is_polymorphic_determiner || parameter.is_constant) {
            return {
                true,
                {
                    new PolymorphicFunction,
                    new PolymorphicFunctionConstant {
                        declaration,
                        context->parent
                    }
                }
            };
        }
    }

    auto parameter_count = declaration->parameters.count;

    auto parameter_types = allocate<Type*>(parameter_count);
    for(size_t i = 0; i < parameter_count; i += 1) {
        expect(type, evaluate_type_expression(context, declaration->parameters[i].type));

        if(!is_runtime_type(type)) {
            error(*context, declaration->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

            return { false };
        }

        parameter_types[i] = type;
    }

    Type *return_type;
    if(declaration->return_type) {
        expect(return_type_value, evaluate_type_expression(context, declaration->return_type));

        if(!is_runtime_type(return_type_value)) {
            error(*context, declaration->return_type->range, "Function parameters cannot be of type '%s'", type_description(return_type_value));

            return { false };
        }

        return_type = return_type_value;
    } else {
        return_type = new Void;
    }

    auto mangled_name = generate_function_mangled_name(*context, *declaration);

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
                mangled_name,
                declaration,
                context->parent
            }
        }
    };
}

static Result<TypedConstantValue> evaluate_struct_definition(GenerationContext *context, StructDefinition *definition) {
    auto parameter_count = definition->parameters.count;

    if(parameter_count == 0) {
        auto member_count = definition->members.count;

        auto members = allocate<StructType::Member>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < member_count; j += 1) {
                if(j != i && strcmp(definition->members[i].name.text, definition->members[j].name.text) == 0) {
                    error(*context, definition->members[i].name.range, "Duplicate struct member name %s", definition->members[i].name.text);

                    return { false };
                }
            }

            expect(type, evaluate_type_expression(context, definition->members[i].type));

            if(!is_runtime_type(type)) {
                error(*context, definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(type));

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
                new TypeType,
                new StructType {
                    definition,
                    {
                        member_count,
                        members
                    }
                }
            }
        };
    } else {
        auto parameter_types = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect(type, evaluate_type_expression(context, definition->parameters[i].type));

            parameter_types[i] = type;
        }

        return {
            true,
            {
                new TypeType,
                new PolymorphicStruct {
                    definition,
                    parameter_types,
                    context->parent
                }
            }
        };
    }
}

static Result<Type*> evaluate_type_expression(GenerationContext *context, Expression *expression);

static Result<TypedConstantValue> evaluate_constant_expression(GenerationContext *context, Expression *expression) {
    if(auto named_reference = dynamic_cast<NamedReference*>(expression)) {
        return resolve_constant_named_reference(context, named_reference->name);
    } else if(auto member_reference = dynamic_cast<MemberReference*>(expression)) {
        expect(expression_value, evaluate_constant_expression(context, member_reference->expression));

        if(auto array_type = dynamic_cast<ArrayTypeType*>(expression_value.type)) {
            auto array_value = dynamic_cast<ArrayConstant*>(expression_value.value);
            assert(array_value);

            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    new Integer {
                        context->address_integer_size,
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
                error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return { false };
            }
        } else if(auto static_array = dynamic_cast<StaticArray*>(expression_value.type)) {
            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    new Integer {
                        context->address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        static_array->length
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                error(*context, member_reference->name.range, "Cannot take pointer to static array in constant context", member_reference->name.text);

                return { false };
            } else {
                error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return { false };
            }
        } else if(auto struct_type = dynamic_cast<StructType*>(expression_value.type)) {
            auto struct_value = dynamic_cast<StructConstant*>(expression_value.value);
            assert(struct_value);

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

            error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(expression_value.type)) {
            auto undetermined_struct_value = dynamic_cast<StructConstant*>(expression_value.value);
            assert(undetermined_struct_value);

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

            error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else if(auto file_module = dynamic_cast<FileModule*>(expression_value.type)) {
            auto file_module_value = dynamic_cast<FileModuleConstant*>(expression_value.value);
            assert(file_module_value);

            for(auto statement : file_module_value->statements) {
                if(match_public_declaration(statement, member_reference->name.text)) {
                    auto old_is_top_level = context->is_top_level;
                    auto old_parent = context->parent;
                    auto old_constant_parameters = context->constant_parameters;

                    context->is_top_level = true;
                    context->parent.file_path = file_module_value->path;
                    context->parent.top_level_statements = file_module_value->statements;
                    context->constant_parameters = {};

                    expect(value, resolve_declaration(context, statement));

                    context->is_top_level = old_is_top_level;
                    context->parent = old_parent;
                    context->constant_parameters = old_constant_parameters;

                    return {
                        true,
                        value
                    };
                }
            }

            error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else {
            error(*context, member_reference->expression->range, "Type '%s' has no members", type_description(expression_value.type));

            return { false };
        }
    } else if(auto index_reference = dynamic_cast<IndexReference*>(expression)) {
        expect(expression_value, evaluate_constant_expression(context, index_reference->expression));

        expect(index, evaluate_constant_expression(context, index_reference->index));

        return evaluate_constant_index(
            *context,
            expression_value.type,
            expression_value.value,
            index_reference->expression->range,
            index.type,
            index.value,
            index_reference->index->range
        );
    } else if(auto integer_literal = dynamic_cast<IntegerLiteral*>(expression)) {
        return {
            true,
            {
                new UndeterminedInteger,
                new IntegerConstant {
                    integer_literal->value
                }
            }
        };
    } else if(auto float_literal = dynamic_cast<FloatLiteral*>(expression)) {
        return {
            true,
            {
                new UndeterminedFloat,
                new FloatConstant {
                    float_literal->value
                }
            }
        };
    } else if(auto string_literal = dynamic_cast<StringLiteral*>(expression)) {
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
    } else if(auto array_literal = dynamic_cast<ArrayLiteral*>(expression)) {
        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(*context, array_literal->range, "Empty array literal");

            return { false };
        }

        expect(first_element, evaluate_constant_expression(context, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(*context, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(*context, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return { false };
        }

        auto elements = allocate<ConstantValue*>(element_count);
        elements[0] = first_element.value;

        for(size_t i = 1; i < element_count; i += 1) {
            expect(element, evaluate_constant_expression(context, array_literal->elements[i]));

            expect(element_value, coerce_constant_to_type(
                *context,
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
    } else if(auto struct_literal = dynamic_cast<StructLiteral*>(expression)) {
        auto member_count = struct_literal->members.count;

        if(member_count == 0) {
            error(*context, struct_literal->range, "Empty struct literal");

            return { false };
        }

        auto members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<ConstantValue*>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            auto member_name = struct_literal->members[i].name;

            for(size_t j = 0; j < member_count; j += 1) {
                if(j != i && strcmp(member_name.text, struct_literal->members[j].name.text) == 0) {
                    error(*context, member_name.range, "Duplicate struct member %s", member_name.text);

                    return { false };
                }
            }

            expect(member, evaluate_constant_expression(context, struct_literal->members[i].value));

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
    } else if(auto function_call = dynamic_cast<FunctionCall*>(expression)) {
        expect(expression_value, evaluate_constant_expression(context, function_call->expression));

        if(auto function = dynamic_cast<FunctionTypeType*>(expression_value.type)) {
            error(*context, function_call->range, "Function calls not allowed in global context");

            return { false };
        } else if(dynamic_cast<BuiltinFunction*>(expression_value.type)) {
            auto builtin_function_value = dynamic_cast<BuiltinFunctionConstant*>(expression_value.value);
            assert(builtin_function_value);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(*context, function_call->range, "Incorrect parameter count. Expected '%zu' got '%zu'", 1, function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, evaluate_constant_expression(context, function_call->parameters[0]));

                Type *type;
                if(dynamic_cast<TypeType*>(parameter_value.type)) {
                    type = dynamic_cast<Type*>(parameter_value.value);
                    assert(type);
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(*context, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return { false };
                }

                auto size = get_type_size(*context, type);

                return {
                    true,
                    {
                        new Integer {
                            context->address_integer_size,
                            false
                        },
                        new IntegerConstant {
                            size
                        }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(*context, function_call->range, "Incorrect parameter count. Expected '%zu' got '%zu'", 1, function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, evaluate_constant_expression(context, function_call->parameters[0]));

                return {
                    true,
                    {
                        new TypeType,
                        parameter_value.type
                    }
                };
            } else {
                abort();
            }
        } else if(dynamic_cast<TypeType*>(expression_value.type)) {
            auto type = dynamic_cast<Type*>(expression_value.value);
            assert(type);

            if(auto polymorphic_struct = dynamic_cast<PolymorphicStruct*>(type)) {
                auto definition = polymorphic_struct->definition;

                auto parameter_count = definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(*context, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.count);

                    return { false };
                }

                auto parameters = allocate<ConstantParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect(parameter, evaluate_constant_expression(context, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        *context,
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

                auto old_is_top_level = context->is_top_level;
                auto old_parent = context->parent;
                auto old_constant_parameters = context->constant_parameters;

                context->is_top_level = false;
                context->parent = polymorphic_struct->parent;
                context->constant_parameters = {
                    parameter_count,
                    parameters
                };

                auto member_count = definition->members.count;

                auto members = allocate<StructType::Member>(member_count);

                for(size_t i = 0; i < member_count; i += 1) {
                    for(size_t j = 0; j < member_count; j += 1) {
                        if(j != i && strcmp(definition->members[i].name.text, definition->members[j].name.text) == 0) {
                            error(*context, definition->members[i].name.range, "Duplicate struct member name %s", definition->members[i].name.text);

                            return { false };
                        }
                    }

                    expect(type, evaluate_type_expression(context, definition->members[i].type));

                    if(!is_runtime_type(type)) {
                        error(*context, definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(type));

                        return { false };
                    }

                    members[i] = {
                        definition->members[i].name.text,
                        type
                    };
                }

                context->is_top_level = old_is_top_level;
                context->parent = old_parent;
                context->constant_parameters = old_constant_parameters;

                return {
                    true,
                    {
                        new TypeType,
                        new StructType {
                            definition,
                            {
                                member_count,
                                members
                            }
                        }
                    }
                };
            } else {
                error(*context, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return { false };
            }
        } else {
            error(*context, function_call->expression->range, "Cannot call non-function '%s'", type_description(expression_value.type));

            return { false };
        }
    } else if(auto binary_operation = dynamic_cast<BinaryOperation*>(expression)) {
        expect(left, evaluate_constant_expression(context, binary_operation->left));

        expect(right, evaluate_constant_expression(context, binary_operation->right));

        expect(value, evaluate_constant_binary_operation(
            *context,
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
    } else if(auto unary_operation = dynamic_cast<UnaryOperation*>(expression)) {
        expect(expression_value, evaluate_constant_expression(context, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                if(dynamic_cast<TypeType*>(expression_value.type)) {
                    auto type = dynamic_cast<Type*>(expression_value.value);
                    assert(type);

                    if(
                        !is_runtime_type(type) &&
                        !dynamic_cast<Void*>(type) &&
                        !dynamic_cast<FunctionTypeType*>(type)
                    ) {
                        error(*context, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                        return { false };
                    }

                    return {
                        true,
                        {
                            new TypeType,
                            new Pointer {
                                type
                            }
                        }
                    };
                } else {
                    error(*context, unary_operation->range, "Cannot take pointers at constant time");

                    return { false };
                }
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(dynamic_cast<Boolean*>(expression_value.type)) {
                    auto boolean_value = dynamic_cast<BooleanConstant*>(expression_value.value);
                    assert(boolean_value);

                    return {
                        true,
                        {
                            new Boolean,
                            new BooleanConstant {
                                !boolean_value->value
                            }
                        }
                    };
                } else {
                    error(*context, unary_operation->expression->range, "Expected a boolean, got '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            case UnaryOperation::Operator::Negation: {
                if(dynamic_cast<Integer*>(expression_value.type) || dynamic_cast<UndeterminedInteger*>(expression_value.type)) {
                    auto integer_value = dynamic_cast<IntegerConstant*>(expression_value.value);
                    assert(integer_value);

                    return {
                        true,
                        {
                            expression_value.type,
                            new IntegerConstant {
                                -integer_value->value
                            }
                        }
                    };
                } else if(dynamic_cast<FloatType*>(expression_value.type) || dynamic_cast<UndeterminedFloat*>(expression_value.type)) {
                    auto float_value = dynamic_cast<FloatConstant*>(expression_value.value);
                    assert(float_value);

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
                    error(*context, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(auto cast = dynamic_cast<Cast*>(expression)) {
        expect(expression_value, evaluate_constant_expression(context, cast->expression));

        expect(type, evaluate_type_expression(context, cast->type));

        expect(value, evaluate_constant_cast(
            *context,
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
    } else if(auto array_type = dynamic_cast<ArrayType*>(expression)) {
        expect(type, evaluate_type_expression(context, array_type->expression));

        if(!is_runtime_type(type)) {
            error(*context, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return { false };
        }

        if(array_type->index != nullptr) {
            expect(index_value, evaluate_constant_expression(context, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                *context,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    context->address_integer_size,
                    false
                },
                false
            ));

            return {
                true,
                {
                    new TypeType,
                    new StaticArray {
                        length->value,
                        type
                    }
                }
            };
        } else {
            return {
                true,
                {
                    new TypeType,
                    new ArrayTypeType {
                        type
                    }
                }
            };
        }
    } else if(auto function_type = dynamic_cast<FunctionType*>(expression)) {
        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(*context, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return { false };
            }

            expect(type, evaluate_type_expression(context, parameter.type));

            if(!is_runtime_type(type)) {
                error(*context, parameter.type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return { false };
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = new Void;
        } else {
            expect(return_type_value, evaluate_type_expression(context, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(*context, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return { false };
            }

            return_type = return_type_value;
        }

        return {
            true,
            {
                new TypeType,
                new FunctionTypeType {
                    {
                        parameter_count,
                        parameters
                    },
                    return_type
                }
            }
        };
    } else {
        abort();
    }
}

static Result<Type*> evaluate_type_expression(GenerationContext *context, Expression *expression) {
    expect(expression_value, evaluate_constant_expression(context, expression));

    if(dynamic_cast<TypeType*>(expression_value.type)) {
        auto type = dynamic_cast<Type*>(expression_value.value);
        assert(type);

        return {
            true,
            type
        };
    } else {
        error(*context, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return { false };
    }
}

static Result<TypedConstantValue> resolve_declaration(GenerationContext *context, Statement *declaration) {
    if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(declaration)) {
        expect(value, evaluate_function_declaration(context, function_declaration));

        return {
            true,
            value
        };
    } else if(auto constant_definition = dynamic_cast<ConstantDefinition*>(declaration)) {
        expect(value, evaluate_constant_expression(context, constant_definition->expression));

        return {
            true,
            value
        };
    } else if(auto struct_definition = dynamic_cast<StructDefinition*>(declaration)) {
        expect(value, evaluate_struct_definition(context, struct_definition));

        return {
            true,
            value
        };
    } else if(auto import = dynamic_cast<Import*>(declaration)) {
        const char *file_path;
        if(context->is_top_level) {
            file_path = context->parent.file_path;
        } else {
            auto current_declaration = context->parent.parent;

            while(!current_declaration->declaration->parent) {
                current_declaration = current_declaration->parent.parent;
            }

            file_path = current_declaration->parent.file_path;
        }

        auto source_file_directory = path_get_directory_component(file_path);

        char *import_file_path{};

        string_buffer_append(&import_file_path, source_file_directory);
        string_buffer_append(&import_file_path, import->path);

        expect(import_file_path_absolute, path_relative_to_absolute(import_file_path));

        auto already_parsed = false;
        Array<Statement*> statements;
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

        return {
            true,
            {
                new FileModule,
                new FileModuleConstant {
                    import_file_path,
                    statements
                }
            }
        };
    } else {
        abort();
    }
}

static bool add_new_variable(GenerationContext *context, Identifier name, size_t address_register, Type *type, FileRange type_range) {
    auto variable_context = &(context->variable_context_stack[context->variable_context_stack.count - 1]);

    for(auto variable : *variable_context) {
        if(strcmp(variable.name.text, name.text) == 0) {
            error(*context, name.range, "Duplicate variable name %s", name.text);
            error(*context, variable.name.range, "Original declared here");

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

static bool does_runtime_static_exist(GenerationContext context, const char *name) {
    for(auto runtime_static : context.statics) {
        if(strcmp(runtime_static->name, name) == 0) {
            return true;
        }
    }

    return false;
}

struct RegisterValue : Value {
    size_t register_index;

    RegisterValue(size_t register_index) : register_index { register_index } {}
};

struct AddressValue : Value {
    size_t address_register;

    AddressValue(size_t address_register) : address_register { address_register } {}
};

struct UndeterminedStructValue : Value {
    Value **members;

    UndeterminedStructValue(Value **members) : members { members } {}
};

struct TypedValue {
    Type *type;

    Value *value;
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

static void write_value(GenerationContext context, uint8_t *data, size_t offset, Type *type, ConstantValue *value);

static void write_struct(GenerationContext context, uint8_t *data, size_t offset, StructType struct_type, ConstantValue **member_values) {
    for(size_t i = 0; i < struct_type.members.count; i += 1) {
        write_value(
            context,
            data,
            offset + get_struct_member_offset(context, struct_type, i),
            struct_type.members[i].type,
            member_values[i]
        );
    }
}

static void write_static_array(GenerationContext context, uint8_t *data, size_t offset, Type *element_type, Array<ConstantValue*> elements) {
    auto element_size = get_type_size(context, element_type);

    for(size_t i = 0; i < elements.count; i += 1) {
        write_value(
            context,
            data,
            offset + i * element_size,
            element_type,
            elements[i]
        );
    }
}

static void write_value(GenerationContext context, uint8_t *data, size_t offset, Type *type, ConstantValue *value) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        write_integer(data, offset, integer->size, integer_value->value);
    } else if(dynamic_cast<Boolean*>(type)) {
        auto boolean_value = dynamic_cast<BooleanConstant*>(value);
        assert(boolean_value);

        write_integer(data, offset, context.default_integer_size, boolean_value->value);
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
        auto float_value = dynamic_cast<FloatConstant*>(value);
        assert(float_value);

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
    } else if(dynamic_cast<Pointer*>(type)) {
        auto pointer_value = dynamic_cast<PointerConstant*>(value);
        assert(pointer_value);

        write_integer(data, offset, context.address_integer_size, pointer_value->value);
    } else if(dynamic_cast<ArrayTypeType*>(type)) {
        auto array_value = dynamic_cast<ArrayConstant*>(value);
        assert(array_value);

        write_integer(
            data,
            offset,
            context.address_integer_size,
            array_value->pointer
        );

        write_integer(
            data,
            offset + register_size_to_byte_size(context.address_integer_size),
            context.address_integer_size,
            array_value->length
        );
    } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
        auto static_array_value = dynamic_cast<StaticArrayConstant*>(value);
        assert(static_array_value);

        write_static_array(
            context,
            data,
            offset,
            static_array->element_type,
            {
                static_array->length,
                static_array_value->elements
            }
        );
    } else if(auto struct_type = dynamic_cast<StructType*>(type)) {
        auto struct_value = dynamic_cast<StructConstant*>(value);
        assert(struct_value);

        write_struct(context, data, offset, *struct_type, struct_value->members);
    } else {
        abort();
    }
}

static const char *register_static_array_constant(GenerationContext *context, Type* element_type, Array<ConstantValue*> elements) {
    auto data_length = get_type_size(*context, element_type) * elements.count;
    auto data = allocate<uint8_t>(data_length);

    write_static_array(*context, data, 0, element_type, elements);

    auto number = context->statics.count;

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, number);

    while(does_runtime_static_exist(*context, name_buffer)) {
        number += 1;

        string_buffer_append(&name_buffer, "constant_");
        string_buffer_append(&name_buffer, number);
    }

    auto constant = new StaticConstant;
    constant->name = name_buffer;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_type_alignment(*context, element_type);

    append(&context->statics, (RuntimeStatic*)constant);

    return name_buffer;
}

static const char *register_struct_constant(GenerationContext *context, StructType struct_type, ConstantValue **members) {
    auto data_length = get_struct_size(*context, struct_type);
    auto data = allocate<uint8_t>(data_length);

    write_struct(*context, data, 0, struct_type, members);

    auto number = context->statics.count;

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, number);

    while(does_runtime_static_exist(*context, name_buffer)) {
        number += 1;

        string_buffer_append(&name_buffer, "constant_");
        string_buffer_append(&name_buffer, number);
    }

    auto constant = new StaticConstant;
    constant->name = name_buffer;
    constant->data = {
        data_length,
        data
    };
    constant->alignment = get_struct_alignment(*context, struct_type);

    append(&context->statics, (RuntimeStatic*)constant);

    return name_buffer;
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
    size_t destination_address_register
) {
    auto copy_memory = new CopyMemory;
    copy_memory->line = line;
    copy_memory->length_register = length_register;
    copy_memory->source_address_register = source_address_register;
    copy_memory->destination_address_register = destination_address_register;

    append(instructions, (Instruction*)copy_memory);
}

static void generate_constant_size_copy(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    size_t length,
    size_t source_address_register,
    size_t destination_address_register
) {
    auto length_register = append_integer_constant(context, instructions, range.first_line, context->address_integer_size, length);

    append_copy_memory(context, instructions, range.first_line, length_register, source_address_register, destination_address_register);
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

static size_t generate_address_offset(GenerationContext *context, List<Instruction*> *instructions, FileRange range, size_t address_register, size_t offset) {
    auto offset_register = append_integer_constant(
        context,
        instructions,
        range.first_line,
        context->address_integer_size,
        offset
    );

    auto final_address_register = append_integer_arithmetic_operation(
        context,
        instructions,
        range.first_line,
        IntegerArithmeticOperation::Operation::Add,
        context->address_integer_size,
        address_register,
        offset_register
    );

    return final_address_register;
}

static size_t generate_boolean_invert(GenerationContext *context, List<Instruction*> *instructions, FileRange range, size_t value_register) {
    auto local_register = append_allocate_local(
        context,
        instructions,
        range.first_line,
        register_size_to_byte_size(context->default_integer_size),
        register_size_to_byte_size(context->default_integer_size)
    );

    append_branch(context, instructions, range.first_line, value_register, instructions->count + 4);

    auto true_register = append_integer_constant(context, instructions, range.first_line, context->default_integer_size, 1);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, true_register, local_register);

    append_jump(context, instructions, range.first_line, instructions->count + 3);

    auto false_register = append_integer_constant(context, instructions, range.first_line, context->default_integer_size, 0);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range.first_line, context->default_integer_size, local_register);

    return result_register;
}

static size_t generate_in_register_integer_value(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Integer type,
    Value *value
) {
    if(auto integer_value = dynamic_cast<IntegerConstant*>(value)) {
        return append_integer_constant(context, instructions, range.first_line, type.size, integer_value->value);
    } else if(auto regsiter_value = dynamic_cast<RegisterValue*>(value)) {
        return regsiter_value->register_index;
    } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
        return append_load_integer(context, instructions, range.first_line, type.size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_boolean_value(GenerationContext *context, List<Instruction*> *instructions, FileRange range, Value *value) {
    if(auto boolean_value = dynamic_cast<BooleanConstant*>(value)) {
        return append_integer_constant(context, instructions, range.first_line, context->default_integer_size, boolean_value->value);
    } else if(auto regsiter_value = dynamic_cast<RegisterValue*>(value)) {
        return regsiter_value->register_index;
    } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
        return append_load_integer(context, instructions, range.first_line, context->default_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static size_t generate_in_register_pointer_value(GenerationContext *context, List<Instruction*> *instructions, FileRange range, Value *value) {
    if(auto pointer_value = dynamic_cast<PointerConstant*>(value)) {
        return append_integer_constant(context, instructions, range.first_line, context->address_integer_size, pointer_value->value);
    } else if(auto regsiter_value = dynamic_cast<RegisterValue*>(value)) {
        return regsiter_value->register_index;
    } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
        return append_load_integer(context, instructions, range.first_line, context->address_integer_size, address_value->address_register);
    } else {
        abort();
    }
}

static Result<size_t> coerce_to_integer_register_value(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    Integer target_type,
    bool probing
) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        if(integer->size == target_type.size && integer->is_signed == target_type.is_signed) {
            auto register_index = generate_in_register_integer_value(context, instructions, range, target_type, value);

            return {
                true,
                register_index
            };
        }
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto constant_value = dynamic_cast<IntegerConstant*>(value);
        assert(constant_value);

        auto regsiter_index = append_integer_constant(context, instructions, range.first_line, target_type.size, constant_value->value);

        return {
            true,
            regsiter_index
        };
    }

    if(!probing) {
        error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<size_t> coerce_to_float_register_value(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    FloatType target_type,
    bool probing
) {
    if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        auto register_index = append_float_constant(context, instructions, range.first_line, target_type.size, (double)integer_value->value);

        return {
            true,
            register_index
        };
    } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
        if(target_type.size == float_type->size) {
            size_t register_index;
            if(auto float_value = dynamic_cast<FloatConstant*>(value)) {
                register_index = append_float_constant(context, instructions, range.first_line, target_type.size, float_value->value);
            } else if(auto regsiter_value = dynamic_cast<RegisterValue*>(value)) {
                register_index = regsiter_value->register_index;
            } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                register_index = append_load_float(context, instructions, range.first_line, float_type->size, address_value->address_register);
            } else {
                abort();
            }

            return {
                true,
                register_index
            };
        }
    } else if(dynamic_cast<UndeterminedFloat*>(type)) {
        auto float_value = dynamic_cast<FloatConstant*>(value);
        assert(float_value);

        auto register_index = append_float_constant(context, instructions, range.first_line, target_type.size, float_value->value);

        return {
            true,
            register_index
        };
    }

    if(!probing) {
        error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static Result<size_t> coerce_to_pointer_register_value(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    Pointer target_type,
    bool probing
) {
    if(dynamic_cast<UndeterminedInteger*>(type)) {
        auto integer_value = dynamic_cast<IntegerConstant*>(value);
        assert(integer_value);

        auto register_index = append_integer_constant(
            context,
            instructions,
            range.first_line,
            context->address_integer_size,
            integer_value->value
        );

        return {
            true,
            register_index
        };
    } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
        if(types_equal(pointer->type, target_type.type)) {
            auto register_index = generate_in_register_pointer_value(context, instructions, range, value);

            return {
                true,
                register_index
            };
        }
    }

    if (!probing) {
        error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return { false };
}

static bool coerce_to_type_write(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    Type *target_type,
    size_t address_register
);

static Result<size_t> coerce_to_type_register(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    Type *target_type,
    bool probing
) {
    if(auto integer = dynamic_cast<Integer*>(target_type)) {
        expect(register_index, coerce_to_integer_register_value(
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
    } else if(dynamic_cast<Boolean*>(target_type)) {
        if(dynamic_cast<Boolean*>(type)) {
            auto register_index = generate_in_register_boolean_value(context, instructions, range, value);

            return {
                true,
                register_index
            };
        }
    } else if(auto float_type = dynamic_cast<FloatType*>(target_type)) {
        expect(register_index, coerce_to_float_register_value(
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
    } else if(auto pointer = dynamic_cast<Pointer*>(target_type)) {
        expect(register_index, coerce_to_pointer_register_value(
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
    } else if(auto target_array = dynamic_cast<ArrayTypeType*>(target_type)) {
        if(auto array_type = dynamic_cast<ArrayTypeType*>(type)) {
            if(types_equal(target_array->element_type, array_type->element_type)) {
                size_t register_index;
                if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    register_index = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                    register_index = address_value->address_register;
                } else {
                    abort();
                }

                return {
                    true,
                    register_index
                };
            }
        } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
            if(types_equal(target_array->element_type, static_array->element_type)) {
                size_t pointer_register;
                if(auto constant_value = dynamic_cast<StaticArrayConstant*>(value)) {
                    auto constant_name = register_static_array_constant(
                        context,
                        static_array->element_type,
                        { static_array->length, constant_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range.first_line, constant_name);
                } else if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    pointer_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                    pointer_register = address_value->address_register;
                } else {
                    abort();
                }

                auto address_register = append_allocate_local(
                    context,
                    instructions,
                    range.first_line,
                    2 * register_size_to_byte_size(context->address_integer_size),
                    register_size_to_byte_size(context->address_integer_size)
                );

                append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(context->address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range.first_line, context->address_integer_size, static_array->length);

                append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);

                return {
                    true,
                    address_register
                };
            }
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(value);
                assert(undetermined_struct_value);

                auto pointer_result = coerce_to_pointer_register_value(
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
                        context,
                        instructions,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        {
                            context->address_integer_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        auto address_register = append_allocate_local(
                            context,
                            instructions,
                            range.first_line,
                            2 * register_size_to_byte_size(context->address_integer_size),
                            register_size_to_byte_size(context->address_integer_size)
                        );

                        append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(context->address_integer_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range.first_line,
                            context->address_integer_size,
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
    } else if(auto target_static_array = dynamic_cast<StaticArray*>(target_type)) {
        if(auto static_array = dynamic_cast<StaticArray*>(type)) {
            if(types_equal(target_static_array->element_type, static_array->element_type) && target_static_array->length == static_array->length) {
                size_t register_index;
                if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    register_index = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
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
    } else if(auto target_struct_type = dynamic_cast<StructType*>(target_type)) {
        if(auto struct_type = dynamic_cast<StructType*>(type)) {
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
                    if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                        register_index = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
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
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
            auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(value);
            assert(undetermined_struct_value);

            if(target_struct_type->definition->is_union) {
                if(undetermined_struct->members.count == 1) {
                    for(size_t i = 0; i < target_struct_type->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[0].name) == 0) {
                            auto address_register = append_allocate_local(
                                context,
                                instructions,
                                range.first_line,
                                get_struct_size(*context, *target_struct_type),
                                get_struct_alignment(*context, *target_struct_type)
                            );

                            if(coerce_to_type_write(
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
                            get_struct_size(*context, *target_struct_type),
                            get_struct_alignment(*context, *target_struct_type)
                        );

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                            auto member_address_register = generate_address_offset(
                                context,
                                instructions,
                                range,
                                address_register,
                                get_struct_member_offset(*context, *struct_type, i)
                            );

                            if(!coerce_to_type_write(
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
        error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return { false };
}

static bool coerce_to_type_write(
    GenerationContext *context,
    List<Instruction*> *instructions,
    FileRange range,
    Type *type,
    Value *value,
    Type *target_type,
    size_t address_register
) {
    if(auto integer_type = dynamic_cast<Integer*>(target_type)) {
        expect(register_index, coerce_to_integer_register_value(context, instructions, range, type, value, *integer_type, false));

        append_store_integer(context, instructions, range.first_line, integer_type->size, register_index, address_register);

        return true;
    } else if(dynamic_cast<Boolean*>(target_type)) {
        if(dynamic_cast<Boolean*>(type)) {
            size_t register_index = generate_in_register_boolean_value(context, instructions, range, value);

            append_store_integer(context, instructions, range.first_line, context->default_integer_size, register_index, address_register);

            return true;
        }
    } else if(auto float_type = dynamic_cast<FloatType*>(target_type)) {
        expect(register_index, coerce_to_float_register_value(
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
    } else if(auto target_pointer = dynamic_cast<Pointer*>(target_type)) {
        if(dynamic_cast<UndeterminedInteger*>(type)) {
            auto integer_value = dynamic_cast<IntegerConstant*>(value);
            assert(integer_value);

            auto register_index = append_integer_constant(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                integer_value->value
            );

            append_store_integer(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                register_index,
                address_register
            );

            return true;
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            if(types_equal(target_pointer->type, pointer->type)) {
                size_t register_index = generate_in_register_pointer_value(context, instructions, range, value);

                append_store_integer(context, instructions, range.first_line, context->address_integer_size, register_index, address_register);

                return true;
            }
        }
    } else if(auto target_array = dynamic_cast<ArrayTypeType*>(target_type)) {
        if(auto array_type = dynamic_cast<ArrayTypeType*>(type)) {
            if(types_equal(target_array->element_type, array_type->element_type)) {
                size_t source_address_register;
                if(auto constant_value = dynamic_cast<ArrayConstant*>(value)) {
                    auto pointer_register = append_integer_constant(
                        context,
                        instructions,
                        range.first_line,
                        context->address_integer_size,
                        constant_value->pointer
                    );

                    append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

                    auto length_register = append_integer_constant(
                        context,
                        instructions,
                        range.first_line,
                        context->address_integer_size,
                        constant_value->length
                    );

                    auto length_address_register = generate_address_offset(
                        context,
                        instructions,
                        range,
                        address_register,
                        register_size_to_byte_size(context->address_integer_size)
                    );

                    append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);

                    return true;
                } else if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    source_address_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                    source_address_register = address_value->address_register;
                } else {
                    abort();
                }

                generate_constant_size_copy(
                    context,
                    instructions,
                    range,
                    2 * get_type_size(*context, array_type->element_type),
                    source_address_register,
                    address_register
                );

                return true;
            }
        } else if(auto static_array = dynamic_cast<StaticArray*>(type)) {
            if(types_equal(target_array->element_type, static_array->element_type)) {
                size_t pointer_register;
                if(auto constant_value = dynamic_cast<StaticArrayConstant*>(value)) {
                    auto constant_name = register_static_array_constant(
                        context,
                        static_array->element_type,
                        { static_array->length, constant_value->elements }
                    );

                    pointer_register = append_reference_static(context, instructions, range.first_line, constant_name);
                } else if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    pointer_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                    pointer_register = address_value->address_register;
                } else {
                    abort();
                }

                append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

                auto length_address_register = generate_address_offset(
                    context,
                    instructions,
                    range,
                    address_register,
                    register_size_to_byte_size(context->address_integer_size)
                );

                auto length_register = append_integer_constant(context, instructions, range.first_line, context->address_integer_size, static_array->length);

                append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, length_address_register);

                return true;
            }
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(value);
                assert(undetermined_struct_value);

                auto pointer_result = coerce_to_pointer_register_value(
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
                        context,
                        instructions,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        {
                            context->address_integer_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_result.value, address_register);

                        auto length_address_register = generate_address_offset(
                            context,
                            instructions,
                            range,
                            address_register,
                            register_size_to_byte_size(context->address_integer_size)
                        );

                        append_store_integer(
                            context,
                            instructions,
                            range.first_line,
                            context->address_integer_size,
                            length_result.value,
                            length_address_register
                        );

                        return true;
                    }
                }
            }
        }
    } else if(auto target_static_array = dynamic_cast<StaticArray*>(target_type)) {
        if(auto static_array = dynamic_cast<StaticArray*>(type)) {
            if(types_equal(target_static_array->element_type, static_array->element_type) && target_static_array->length == static_array->length) {
                size_t source_address_register;
                if(auto constant_value = dynamic_cast<StaticArrayConstant*>(value)) {
                    auto constant_name = register_static_array_constant(
                        context,
                        static_array->element_type,
                        { static_array->length, constant_value->elements }
                    );

                    source_address_register = append_reference_static(context, instructions, range.first_line, constant_name);
                } else if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                    source_address_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                    source_address_register = address_value->address_register;
                } else {
                    abort();
                }

                generate_constant_size_copy(
                    context,
                    instructions,
                    range,
                    static_array->length * get_type_size(*context, static_array->element_type),
                    source_address_register,
                    address_register
                );

                return true;
            }
        }
    } else if(auto target_struct_type = dynamic_cast<StructType*>(target_type)) {
        if(auto struct_type = dynamic_cast<StructType*>(type)) {
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
                    if(auto constant_value = dynamic_cast<StructConstant*>(value)) {
                        auto constant_name = register_struct_constant(
                            context,
                            *struct_type,
                            constant_value->members
                        );

                        source_address_register = append_reference_static(context, instructions, range.first_line, constant_name);
                    } else if(auto register_value = dynamic_cast<RegisterValue*>(value)) {
                        source_address_register = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(value)) {
                        source_address_register = address_value->address_register;
                    } else {
                        abort();
                    }

                    generate_constant_size_copy(
                        context,
                        instructions,
                        range,
                        get_struct_size(*context, *struct_type),
                        source_address_register,
                        address_register
                    );

                    return true;
                }
            }
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
            if(target_struct_type->definition->is_union) {
                if(undetermined_struct->members.count == 1) {
                    for(size_t i = 0; i < target_struct_type->members.count; i += 1) {
                        if(strcmp(target_struct_type->members[i].name, undetermined_struct->members[0].name) == 0) {
                            Value *variant_value;
                            if(auto constant_value = dynamic_cast<StructConstant*>(value)) {
                                variant_value = constant_value->members[0];
                            } else if(auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(value)) {
                                variant_value = undetermined_struct_value->members[0];
                            } else {
                                abort();
                            }

                            if(coerce_to_type_write(
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
                            Value *member_value;
                            if(auto constant_value = dynamic_cast<StructConstant*>(value)) {
                                member_value = constant_value->members[i];
                            } else if(auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(value)) {
                                member_value = undetermined_struct_value->members[i];
                            } else {
                                abort();
                            }

                            auto member_address_register = generate_address_offset(
                                context,
                                instructions,
                                range,
                                address_register,
                                get_struct_member_offset(*context, *target_struct_type, i)
                            );

                            if(!coerce_to_type_write(
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

    error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));

    return { false };
}

static Result<TypedValue> generate_expression(GenerationContext *context, List<Instruction*> *instructions, Expression *expression);

static Result<Type*> evaluate_type_expression_runtime(GenerationContext *context, List<Instruction*> *instructions, Expression *expression) {
    expect(expression_value, generate_expression(context, instructions, expression));

    if(dynamic_cast<TypeType*>(expression_value.type)) {
        auto type = dynamic_cast<Type*>(expression_value.value);
        assert(type);

        return {
            true,
            type
        };
    } else {
        error(*context, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return { false };
    }
}

static Result<TypedValue> generate_expression(GenerationContext *context, List<Instruction*> *instructions, Expression *expression) {
    if(auto named_reference = dynamic_cast<NamedReference*>(expression)) {
        for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
            for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
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
        }

        expect(constant, resolve_constant_named_reference(context, named_reference->name));

        return {
            true,
            {
                constant.type,
                constant.value
            }
        };
    } else if(auto index_reference = dynamic_cast<IndexReference*>(expression)) {
        expect(expression_value, generate_expression(context, instructions, index_reference->expression));

        expect(index, generate_expression(context, instructions, index_reference->index));

        if(dynamic_cast<ConstantValue*>(expression_value.value) && dynamic_cast<ConstantValue*>(index.value)) {
            auto expression_constant = dynamic_cast<ConstantValue*>(expression_value.value);
            auto index_constant = dynamic_cast<ConstantValue*>(index.value);

            expect(constant, evaluate_constant_index(
                *context,
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
                    constant.value
                }
            };
        }

        expect(index_register, coerce_to_integer_register_value(
            context,
            instructions,
            index_reference->index->range,
            index.type,
            index.value,
            {
                context->address_integer_size,
                false
            },
            false
        ));

        size_t base_address_register;
        Type *element_type;
        if(auto array_type = dynamic_cast<ArrayTypeType*>(expression_value.type)) {
            element_type = array_type->element_type;

            if(auto constant_value = dynamic_cast<ArrayConstant*>(expression_value.value)) {
                base_address_register = append_integer_constant(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    context->address_integer_size,
                    constant_value->pointer
                );
            } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    context->address_integer_size,
                    register_value->register_index
                );
            } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                base_address_register = append_load_integer(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    context->address_integer_size,
                    address_value->address_register
                );
            } else {
                abort();
            }
        } else if(auto static_array = dynamic_cast<StaticArray*>(expression_value.type)) {
            element_type = static_array->element_type;

            if(auto constant_value = dynamic_cast<StaticArrayConstant*>(expression_value.value)) {
                auto constant_name = register_static_array_constant(
                    context,
                    static_array->element_type,
                    { static_array->length, constant_value->elements }
                );

                base_address_register = append_reference_static(
                    context,
                    instructions,
                    index_reference->expression->range.first_line,
                    constant_name
                );
            } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                base_address_register = register_value->register_index;
            } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                base_address_register = address_value->address_register;
            } else {
                abort();
            }
        }

        auto element_size_register = append_integer_constant(
            context,
            instructions,
            index_reference->range.first_line,
            context->address_integer_size,
            get_type_size(*context, element_type)
        );

        auto offset = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range.first_line,
            IntegerArithmeticOperation::Operation::UnsignedMultiply,
            context->address_integer_size,
            element_size_register,
            index_register
        );

        auto address_register = append_integer_arithmetic_operation(
            context,
            instructions,
            index_reference->range.first_line,
            IntegerArithmeticOperation::Operation::Add,
            context->address_integer_size,
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
    } else if(auto member_reference = dynamic_cast<MemberReference*>(expression)) {
        expect(expression_value, generate_expression(context, instructions, member_reference->expression));

        Type *actual_type;
        Value *actual_value;
        if(auto pointer = dynamic_cast<Pointer*>(expression_value.type)) {
            actual_type = pointer->type;

            size_t address_register;
            if(auto constant_value = dynamic_cast<PointerConstant*>(expression_value.value)) {
                address_register = append_integer_constant(
                    context,
                    instructions,
                    member_reference->expression->range.first_line,
                    context->address_integer_size,
                    constant_value->value
                );
            } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                address_register = register_value->register_index;
            } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                address_register = append_load_integer(
                    context,
                    instructions,
                    member_reference->expression->range.first_line,
                    context->address_integer_size,
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

        if(auto array_type = dynamic_cast<ArrayTypeType*>(actual_type)) {
            if(strcmp(member_reference->name.text, "length") == 0) {
                auto type = new Integer {
                    context->address_integer_size,
                    false
                };

                Value *value;
                if(auto constant_value = dynamic_cast<ArrayConstant*>(actual_value)) {
                    value = new IntegerConstant {
                        constant_value->length
                    };
                } else if(auto register_value = dynamic_cast<RegisterValue*>(actual_value)) {
                    auto address_register = generate_address_offset(
                        context,
                        instructions,
                        member_reference->range,
                        register_value->register_index,
                        register_size_to_byte_size(context->address_integer_size)
                    );

                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range.first_line,
                        context->address_integer_size,
                        address_register
                    );

                    value = new RegisterValue {
                        length_register
                    };
                } else if(auto address_value = dynamic_cast<AddressValue*>(actual_value)) {
                    auto address_register = generate_address_offset(
                        context,
                        instructions,
                        member_reference->range,
                        address_value->address_register,
                        register_size_to_byte_size(context->address_integer_size)
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
                            context->address_integer_size,
                            false
                        },
                        value
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                Value *value;
                if(auto constant_value = dynamic_cast<ArrayConstant*>(actual_value)) {
                    value = new PointerConstant {
                        constant_value->pointer
                    };
                } else if(auto register_value = dynamic_cast<RegisterValue*>(actual_value)) {
                    auto length_register = append_load_integer(
                        context,
                        instructions,
                        member_reference->range.first_line,
                        context->address_integer_size,
                        register_value->register_index
                    );

                    value = new RegisterValue {
                        length_register
                    };
                } else if(auto address_value = dynamic_cast<AddressValue*>(actual_value)) {
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
                error(*context, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return { false };
            }
        } else if(auto static_array = dynamic_cast<StaticArray*>(actual_type)) {
            if(strcmp(member_reference->name.text, "length") == 0) {
                return {
                    true,
                    {
                        new Integer {
                            context->address_integer_size,
                            false
                        },
                        new IntegerConstant {
                            static_array->length
                        }
                    }
                };
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                size_t address_regsiter;
                if(auto constant_value = dynamic_cast<StaticArrayConstant*>(actual_value)) {
                    auto constant_name = register_static_array_constant(
                        context,
                        static_array->element_type,
                        { static_array->length, constant_value->elements }
                    );

                    address_regsiter = append_reference_static(context, instructions, member_reference->range.first_line, constant_name);
                } else if(auto register_value = dynamic_cast<RegisterValue*>(actual_value)) {
                    address_regsiter = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(actual_value)) {
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
                error(*context, member_reference->name.range, "No member with name %s", member_reference->name.text);

                return { false };
            }
        } else if(auto struct_type = dynamic_cast<StructType*>(actual_type)) {
            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(strcmp(struct_type->members[i].name, member_reference->name.text) == 0) {
                    auto member_type = struct_type->members[i].type;

                    if(auto constant_value = dynamic_cast<StructConstant*>(actual_value)) {
                        assert(!struct_type->definition->is_union);

                        return {
                            true,
                            {
                                member_type,
                                constant_value->members[i]
                            }
                        };
                    } else if(auto register_value = dynamic_cast<RegisterValue*>(actual_value)) {
                        auto address_register = generate_address_offset(
                            context,
                            instructions,
                            member_reference->range,
                            register_value->register_index,
                            get_struct_member_offset(*context, *struct_type, i)
                        );

                        auto member_representation = get_type_representation(*context, member_type);

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
                    } else if(auto address_value = dynamic_cast<AddressValue*>(actual_value)) {
                        auto address_register = generate_address_offset(
                            context,
                            instructions,
                            member_reference->range,
                            address_value->address_register,
                            get_struct_member_offset(*context, *struct_type, i)
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

            error(*context, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return { false };
        } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(actual_type)) {
            auto undetermined_struct_value = dynamic_cast<UndeterminedStructValue*>(actual_value);
            assert(undetermined_struct_value);

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
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

            error(*context, member_reference->name.range, "No member with name %s", member_reference->name.text);

            return { false };
        } else if(auto file_module = dynamic_cast<FileModule*>(actual_type)) {
            auto file_module_value = dynamic_cast<FileModuleConstant*>(actual_value);
            assert(file_module_value);

            for(auto statement : file_module_value->statements) {
                if(match_public_declaration(statement, member_reference->name.text)) {
                    auto old_is_top_level = context->is_top_level;
                    auto old_parent = context->parent;
                    auto old_constant_parameters = context->constant_parameters;

                    context->is_top_level = true;
                    context->parent.file_path = file_module_value->path;
                    context->parent.top_level_statements = file_module_value->statements;
                    context->constant_parameters = {};

                    expect(value, resolve_declaration(context, statement));

                    context->is_top_level = old_is_top_level;
                    context->parent = old_parent;
                    context->constant_parameters = old_constant_parameters;

                    return {
                        true,
                        {
                            value.type,
                            value.value
                        }
                    };
                }
            }

            error(*context, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return { false };
        } else {
            error(*context, member_reference->expression->range, "Type %s has no members", type_description(actual_type));

            return { false };
        }
    } else if(auto integer_literal = dynamic_cast<IntegerLiteral*>(expression)) {
        return {
            true,
            {
                new UndeterminedInteger,
                new IntegerConstant {
                    integer_literal->value
                }
            }
        };
    } else if(auto float_literal = dynamic_cast<FloatLiteral*>(expression)) {
        return {
            true,
            {
                new UndeterminedFloat,
                new FloatConstant {
                    float_literal->value
                }
            }
        };
    } else if(auto string_literal = dynamic_cast<StringLiteral*>(expression)) {
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
    } else if(auto array_literal = dynamic_cast<ArrayLiteral*>(expression)) {
        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(*context, array_literal->range, "Empty array literal");

            return { false };
        }

        expect(first_element, generate_expression(context, instructions, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(*context, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(*context, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return { false };
        }

        auto elements = allocate<TypedValue>(element_count);
        elements[0] = first_element;

        auto all_constant = true;
        for(size_t i = 1; i < element_count; i += 1) {
            expect(element, generate_expression(context, instructions, array_literal->elements[i]));

            elements[i] = element;

            if(!dynamic_cast<ConstantValue*>(element.value)) {
                all_constant = false;
            }
        }

        Value *value;
        if(all_constant) {
            auto element_values = allocate<ConstantValue*>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                expect(constant_value, coerce_constant_to_type(
                    *context,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    dynamic_cast<ConstantValue*>(elements[i].value),
                    determined_element_type,
                    false
                ));

                element_values[i] = constant_value;
            }

            value = new StaticArrayConstant {
                element_values
            };
        } else {
            auto element_size = get_type_size(*context, determined_element_type);

            auto address_register = append_allocate_local(
                context,
                instructions,
                array_literal->range.first_line,
                array_literal->elements.count * element_size,
                get_type_alignment(*context, determined_element_type)
            );

            auto element_size_register = append_integer_constant(
                context,
                instructions,
                array_literal->range.first_line,
                context->address_integer_size,
                element_size
            );

            auto element_address_register = address_register;
            for(size_t i = 0; i < element_count; i += 1) {
                if(!coerce_to_type_write(
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
                        context->address_integer_size,
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
    } else if(auto struct_literal = dynamic_cast<StructLiteral*>(expression)) {
        if(struct_literal->members.count == 0) {
            error(*context, struct_literal->range, "Empty struct literal");

            return { false };
        }

        auto member_count = struct_literal->members.count;

        auto type_members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<Value*>(member_count);
        auto all_constant = true;

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(strcmp(struct_literal->members[i].name.text, type_members[j].name) == 0) {
                    error(*context, struct_literal->members[i].name.range, "Duplicate struct member %s", struct_literal->members[i].name.text);

                    return { false };
                }
            }

            expect(member, generate_expression(context, instructions, struct_literal->members[i].value));

            type_members[i] = {
                struct_literal->members[i].name.text,
                member.type
            };

            member_values[i] = member.value;

            if(!dynamic_cast<ConstantValue*>(member.value)) {
                all_constant = false;
            }
        }

        Value *value;
        if(all_constant) {
            auto constant_member_values = allocate<ConstantValue*>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                auto constant_value = dynamic_cast<ConstantValue*>(member_values[i]);
                assert(constant_value);

                constant_member_values[i] = constant_value;
            }

            value = new StructConstant {
                constant_member_values
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
    } else if(auto function_call = dynamic_cast<FunctionCall*>(expression)) {
        expect(expression_value, generate_expression(context, instructions, function_call->expression));

        if(auto function = dynamic_cast<FunctionTypeType*>(expression_value.type)) {
            auto parameter_count = function->parameters.count;

            if(function_call->parameters.count != parameter_count) {
                error(
                    *context,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    parameter_count,
                    function_call->parameters.count
                );

                return { false };
            }

            auto has_return = !dynamic_cast<Void*>(function->return_type);

            RegisterRepresentation return_type_representation;
            if(has_return) {
                return_type_representation = get_type_representation(*context, function->return_type);
            }

            auto runtime_parameter_count = parameter_count;
            if(has_return && !return_type_representation.is_in_register) {
                runtime_parameter_count += 1;
            }

            auto parameter_registers = allocate<size_t>(runtime_parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect(parameter_value, generate_expression(context, instructions, function_call->parameters[i]));

                expect(parameter_register, coerce_to_type_register(
                    context,
                    instructions,
                    function_call->parameters[i]->range,
                    parameter_value.type,
                    parameter_value.value,
                    function->parameters[i],
                    false
                ));

                parameter_registers[i] = parameter_register;
            }

            if(has_return && !return_type_representation.is_in_register) {
                parameter_registers[runtime_parameter_count - 1] = append_allocate_local(
                    context,
                    instructions,
                    function_call->range.first_line,
                    get_type_size(*context, function->return_type),
                    get_type_alignment(*context, function->return_type)
                );
            }

            auto function_value = dynamic_cast<FunctionConstant*>(expression_value.value);
            assert(function_value);

            auto is_registered = false;
            for(auto runtime_function : context->runtime_functions) {
                if(strcmp(runtime_function.mangled_name, function_value->mangled_name) == 0) {
                    is_registered = true;

                    break;
                }
            }

            if(!is_registered) {
                auto runtime_parameters = allocate<RuntimeFunctionParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    runtime_parameters[i] = {
                        function_value->declaration->parameters[i].name,
                        function->parameters[i],
                        function_value->declaration->parameters[i].type->range
                    };
                }

                append(&context->runtime_functions, {
                    function_value->mangled_name,
                    { parameter_count, runtime_parameters },
                    function->return_type,
                    {
                        function_value->declaration,
                        {},
                        function_value->parent
                    }
                });
            }

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->line = function_call->range.first_line;
            function_call_instruction->function_name = function_value->mangled_name;
            function_call_instruction->parameter_registers = { parameter_count, parameter_registers };
            function_call_instruction->has_return = has_return && return_type_representation.is_in_register;

            Value *value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_register = return_register;

                    value = new RegisterValue {
                        return_register
                    };
                } else {
                    value = new RegisterValue {
                        parameter_registers[runtime_parameter_count - 1]
                    };
                }
            } else {
                value = new VoidConstant;
            }

            append(instructions, (Instruction*)function_call_instruction);

            return {
                true,
                {
                    function->return_type,
                    value
                }
            };
        } else if(auto polymorphic_function = dynamic_cast<PolymorphicFunction*>(expression_value.type)) {
            auto polymorphic_function_value = dynamic_cast<PolymorphicFunctionConstant*>(expression_value.value);
            assert(polymorphic_function_value);

            auto original_parameter_count = polymorphic_function_value->declaration->parameters.count;

            if(function_call->parameters.count != original_parameter_count) {
                error(
                    *context,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    original_parameter_count,
                    function_call->parameters.count
                );

                return { false };
            }

            auto parameter_types = allocate<Type*>(original_parameter_count);
            List<TypedValue> polymorphic_runtime_parameter_values {};

            List<ConstantParameter> polymorphic_determiners {};

            for(size_t i = 0; i < original_parameter_count; i += 1) {
                auto declaration_parameter = polymorphic_function_value->declaration->parameters[i];

                if(declaration_parameter.is_polymorphic_determiner) {
                    expect(parameter_value, generate_expression(context, instructions, function_call->parameters[i]));

                    expect(determined_type, coerce_to_default_type(*context, function_call->parameters[i]->range, parameter_value.type));

                    if(!declaration_parameter.is_constant) {
                        append(&polymorphic_runtime_parameter_values, parameter_value);
                    }

                    parameter_types[i] = determined_type;

                    append(&polymorphic_determiners, {
                        polymorphic_function_value->declaration->parameters[i].polymorphic_determiner.text,
                        new TypeType,
                        determined_type
                    });
                }
            }

            auto old_is_top_level = context->is_top_level;
            auto old_parent = context->parent;
            auto old_constant_parameters = context->constant_parameters;

            context->is_top_level = polymorphic_function_value->declaration->parent == nullptr;
            context->parent = polymorphic_function_value->parent;
            context->constant_parameters = to_array(polymorphic_determiners);

            List<ConstantParameter> constant_parameters {};

            for(size_t i = 0; i < polymorphic_determiners.count; i += 1) {
                append(&constant_parameters, polymorphic_determiners[i]);
            }

            for(size_t i = 0; i < original_parameter_count; i += 1) {
                auto declaration_parameter = polymorphic_function_value->declaration->parameters[i];
                auto call_parameter = function_call->parameters[i];

                if(declaration_parameter.is_constant) {
                    if(!declaration_parameter.is_polymorphic_determiner) {
                        expect(parameter_type, evaluate_type_expression(context, declaration_parameter.type));

                        parameter_types[i] = parameter_type;
                    }

                    expect(parameter_value, generate_expression(context, instructions, call_parameter));

                    auto constant_value = dynamic_cast<ConstantValue*>(parameter_value.value);
                    if(!constant_value) {
                        error(*context, call_parameter->range, "Expected a constant value");

                        return { false };
                    }

                    expect(coerced_constant_value, coerce_constant_to_type(
                        *context,
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
                auto declaration_parameter = polymorphic_function_value->declaration->parameters[i];
                auto call_parameter = function_call->parameters[i];

                if(!declaration_parameter.is_constant) {
                    if(!declaration_parameter.is_polymorphic_determiner) {
                        expect(parameter_type, evaluate_type_expression(context, declaration_parameter.type));

                        if(!is_runtime_type(parameter_type)) {
                            error(*context, call_parameter->range, "Non-constant function parameters cannot be of type '%s'", type_description(parameter_type));

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
            if(polymorphic_function_value->declaration->return_type) {
                has_return = true;

                expect(return_type_value, evaluate_type_expression(context, polymorphic_function_value->declaration->return_type));

                if(!is_runtime_type(return_type_value)) {
                    error(
                        *context,
                        polymorphic_function_value->declaration->return_type->range,
                        "Function returns cannot be of type '%s'",
                        type_description(return_type_value)
                    );

                    return { false };
                }

                return_type_representation = get_type_representation(*context, return_type_value);

                return_type = return_type_value;
            } else {
                has_return = false;

                return_type = new Void;
            }

            context->is_top_level = old_is_top_level;
            context->parent = old_parent;
            context->constant_parameters = old_constant_parameters;

            auto parameter_register_count = runtime_parameter_count;
            if(has_return && !return_type_representation.is_in_register) {
                parameter_register_count += 1;
            }

            auto parameter_registers = allocate<size_t>(parameter_register_count);

            {
                size_t runtime_parameter_index = 0;
                size_t polymorphic_parameter_index = 0;

                for(size_t i = 0; i < original_parameter_count; i += 1) {
                    auto declaration_parameter = polymorphic_function_value->declaration->parameters[i];

                    if(!declaration_parameter.is_constant) {
                        Type *type;
                        Value *value;
                        if(declaration_parameter.is_polymorphic_determiner) {
                            type = polymorphic_runtime_parameter_values[polymorphic_parameter_index].type;
                            value = polymorphic_runtime_parameter_values[polymorphic_parameter_index].value;

                            polymorphic_parameter_index += 1;
                        } else {
                            expect(parameter_value, generate_expression(context, instructions, function_call->parameters[i]));

                            type = parameter_value.type;
                            value = parameter_value.value;
                        }

                        expect(parameter_register, coerce_to_type_register(
                            context,
                            instructions,
                            function_call->parameters[i]->range,
                            type,
                            value,
                            parameter_types[i],
                            false
                        ));

                        parameter_registers[runtime_parameter_index] = parameter_register;

                        runtime_parameter_index += 1;
                    }
                    
                }
            }

            if(has_return && !return_type_representation.is_in_register) {
                parameter_registers[parameter_register_count - 1] = append_allocate_local(
                    context,
                    instructions,
                    function_call->range.first_line,
                    get_type_size(*context, return_type),
                    get_type_alignment(*context, return_type)
                );
            }

            const char *mangled_name;
            if(polymorphic_function_value->declaration->is_external) {
                mangled_name = polymorphic_function_value->declaration->name.text;
            } else {
                char *mangled_name_buffer{};

                string_buffer_append(&mangled_name_buffer, "function_");

                char buffer[32];
                sprintf(buffer, "%zu", context->runtime_functions.count);
                string_buffer_append(&mangled_name_buffer, buffer);

                mangled_name = mangled_name_buffer;
            }

            auto runtime_parameters = allocate<RuntimeFunctionParameter>(runtime_parameter_count);

            {
                size_t runtime_parameter_index = 0;

                for(size_t i = 0; i < original_parameter_count; i += 1) {
                    auto declaration_parameter = polymorphic_function_value->declaration->parameters[i];

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
                {
                    polymorphic_function_value->declaration,
                    to_array(constant_parameters),
                    polymorphic_function_value->parent
                }
            });

            auto function_call_instruction = new FunctionCallInstruction;
            function_call_instruction->function_name = mangled_name;
            function_call_instruction->parameter_registers = { parameter_register_count, parameter_registers };
            function_call_instruction->has_return = has_return && return_type_representation.is_in_register;

            Value *value;
            if(has_return) {
                if(return_type_representation.is_in_register) {
                    auto return_register = allocate_register(context);

                    function_call_instruction->return_register = return_register;

                    value = new RegisterValue {
                        return_register
                    };
                } else {
                    value = new RegisterValue {
                        parameter_registers[parameter_register_count - 1]
                    };
                }
            } else {
                value = new VoidConstant;
            }

            append(instructions, (Instruction*)function_call_instruction);

            return {
                true,
                {
                    return_type,
                    value
                }
            };
        } else if(dynamic_cast<BuiltinFunction*>(expression_value.type)) {
            auto builtin_function_value = dynamic_cast<BuiltinFunctionConstant*>(expression_value.value);
            assert(builtin_function_value);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(*context, function_call->range, "Incorrect parameter count. Expected '%zu' got '%zu'", 1, function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, generate_expression(context, instructions, function_call->parameters[0]));

                Type *type;
                if(dynamic_cast<TypeType*>(parameter_value.type)) {
                    type = dynamic_cast<Type*>(parameter_value.value);
                    assert(type);
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(*context, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return { false };
                }

                auto size = get_type_size(*context, type);

                return {
                    true,
                    {
                        new Integer {
                            context->address_integer_size,
                            false
                        },
                        new IntegerConstant {
                            size
                        }
                    }
                };
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(*context, function_call->range, "Incorrect parameter count. Expected '%zu' got '%zu'", 1, function_call->parameters.count);

                    return { false };
                }

                expect(parameter_value, generate_expression(context, instructions, function_call->parameters[0]));

                return {
                    true,
                    {
                        new TypeType,
                        parameter_value.type
                    }
                };
            } else {
                abort();
            }
        } else if(dynamic_cast<TypeType*>(expression_value.type)) {
            auto type = dynamic_cast<Type*>(expression_value.value);
            assert(type);

            if(auto polymorphic_struct = dynamic_cast<PolymorphicStruct*>(type)) {
                auto parameter_count = polymorphic_struct->definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(
                        *context,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        parameter_count,
                        function_call->parameters.count
                    );

                    return { false };
                }

                auto parameters = allocate<ConstantParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect(parameter_value, evaluate_constant_expression(context, function_call->parameters[i]));

                    expect(coerced_value, coerce_constant_to_type(
                        *context,
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

                auto old_is_top_level = context->is_top_level;
                auto old_parent = context->parent;
                auto old_constant_parameters = context->constant_parameters;

                context->is_top_level = polymorphic_struct->definition->parent == nullptr;
                context->parent = polymorphic_struct->parent;
                context->constant_parameters = { parameter_count, parameters };

                auto member_count = polymorphic_struct->definition->members.count;

                auto members = allocate<StructType::Member>(member_count);

                for(size_t i = 0; i < member_count; i += 1) {
                    expect(member_type, evaluate_type_expression(context, polymorphic_struct->definition->members[i].type));

                    if(!is_runtime_type(member_type)) {
                        error(*context, polymorphic_struct->definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(member_type));

                        return { false };
                    }

                    members[i] = {
                        polymorphic_struct->definition->members[i].name.text,
                        member_type
                    };
                }

                context->is_top_level = old_is_top_level;
                context->parent = old_parent;
                context->constant_parameters = old_constant_parameters;

                return {
                    true,
                    {
                        new TypeType,
                        new StructType {
                            polymorphic_struct->definition,
                            {
                                member_count,
                                members
                            }
                        }
                    }
                };
            } else {
                error(*context, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return { false };
            }
        } else {
            error(*context, function_call->expression->range, "Cannot call '%s'", type_description(expression_value.type));

            return { false };
        }
    } else if(auto binary_operation = dynamic_cast<BinaryOperation*>(expression)) {
        expect(left, generate_expression(context, instructions, binary_operation->left));

        expect(right, generate_expression(context, instructions, binary_operation->right));

        if(dynamic_cast<ConstantValue*>(left.value) && dynamic_cast<ConstantValue*>(right.value)) {
            expect(constant, evaluate_constant_binary_operation(
                *context,
                binary_operation->range,
                binary_operation->binary_operator,
                binary_operation->left->range,
                left.type,
                dynamic_cast<ConstantValue*>(left.value),
                binary_operation->right->range,
                right.type,
                dynamic_cast<ConstantValue*>(right.value)
            ));

            return {
                true,
                {
                    constant.type,
                    constant.value
                }
            };
        }

        expect(type, determine_binary_operation_type(*context, binary_operation->range, left.type, right.type));

        if(auto integer = dynamic_cast<Integer*>(type)) {
            expect(left_register, coerce_to_integer_register_value(
                context,
                instructions,
                binary_operation->left->range,
                left.type,
                left.value,
                *integer,
                false
            ));

            expect(right_register, coerce_to_integer_register_value(
                context,
                instructions,
                binary_operation->right->range,
                right.type,
                right.value,
                *integer,
                false
            ));

            auto is_arithmetic = true;
            IntegerArithmeticOperation::Operation arithmetic_operation;
            switch(binary_operation->binary_operator) {
                case BinaryOperation::Operator::Addition: {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::Add;
                } break;

                case BinaryOperation::Operator::Subtraction: {
                    arithmetic_operation = IntegerArithmeticOperation::Operation::Subtract;
                } break;

                case BinaryOperation::Operator::Multiplication: {
                    if(integer->is_signed) {
                        arithmetic_operation = IntegerArithmeticOperation::Operation::SignedMultiply;
                    } else {
                        arithmetic_operation = IntegerArithmeticOperation::Operation::UnsignedMultiply;
                    }
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
                    binary_operation->range.first_line,
                    arithmetic_operation,
                    integer->size,
                    left_register,
                    right_register
                );

                result_type = integer;
            } else {
                IntegerComparisonOperation::Operation comparison_operation;
                auto invert = false;
                switch(binary_operation->binary_operator) {
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
                        error(*context, binary_operation->range, "Cannot perform that operation on integers");

                        return { false };
                    } break;
                }

                result_register = append_integer_comparison_operation(
                    context,
                    instructions,
                    binary_operation->range.first_line,
                    comparison_operation,
                    integer->size,
                    left_register,
                    right_register
                );

                if(invert) {
                    result_register = generate_boolean_invert(context, instructions, binary_operation->range, result_register);
                }

                result_type = new Boolean;
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
        } else if(dynamic_cast<Boolean*>(type)) {
            if(!dynamic_cast<Boolean*>(left.type)) {
                error(*context, binary_operation->left->range, "Expected 'bool', got '%s'", type_description(left.type));

                return { false };
            }

            auto left_register = generate_in_register_boolean_value(context, instructions, binary_operation->left->range, left.value);

            if(!dynamic_cast<Boolean*>(right.type)) {
                error(*context, binary_operation->right->range, "Expected 'bool', got '%s'", type_description(right.type));

                return { false };
            }

            auto right_register = generate_in_register_boolean_value(context, instructions, binary_operation->right->range, right.value);

            auto is_arithmetic = true;
            IntegerArithmeticOperation::Operation arithmetic_operation;
            switch(binary_operation->binary_operator) {
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
                    binary_operation->range.first_line,
                    arithmetic_operation,
                    context->default_integer_size,
                    left_register,
                    right_register
                );
            } else {
                IntegerComparisonOperation::Operation comparison_operation;
                auto invert = false;
                switch(binary_operation->binary_operator) {
                    case BinaryOperation::Operator::Equal: {
                        comparison_operation = IntegerComparisonOperation::Operation::Equal;
                    } break;

                    case BinaryOperation::Operator::NotEqual: {
                        comparison_operation = IntegerComparisonOperation::Operation::Equal;
                        invert = true;
                    } break;

                    default: {
                        error(*context, binary_operation->range, "Cannot perform that operation on 'bool'");

                        return { false };
                    } break;
                }

                result_register = append_integer_comparison_operation(
                    context,
                    instructions,
                    binary_operation->range.first_line,
                    comparison_operation,
                    context->default_integer_size,
                    left_register,
                    right_register
                );

                if(invert) {
                    result_register = generate_boolean_invert(context, instructions, binary_operation->range, result_register);
                }
            }

            return {
                true,
                {
                    new Boolean,
                    new RegisterValue {
                        result_register
                    }
                }
            };
        } else if(auto float_type = dynamic_cast<FloatType*>(type)) {
            expect(left_register, coerce_to_float_register_value(
                context,
                instructions,
                binary_operation->left->range,
                left.type,
                left.value,
                *float_type,
                false
            ));

            expect(right_register, coerce_to_float_register_value(
                context,
                instructions,
                binary_operation->right->range,
                right.type,
                right.value,
                *float_type,
                false
            ));

            auto is_arithmetic = true;
            FloatArithmeticOperation::Operation arithmetic_operation;
            switch(binary_operation->binary_operator) {
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
                    binary_operation->range.first_line,
                    arithmetic_operation,
                    float_type->size,
                    left_register,
                    right_register
                );

                result_type = float_type;
            } else {
                FloatComparisonOperation::Operation comparison_operation;
                auto invert = false;
                switch(binary_operation->binary_operator) {
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
                        error(*context, binary_operation->range, "Cannot perform that operation on floats");

                        return { false };
                    } break;
                }

                result_register = append_float_comparison_operation(
                    context,
                    instructions,
                    binary_operation->range.first_line,
                    comparison_operation,
                    float_type->size,
                    left_register,
                    right_register
                );

                if(invert) {
                    result_register = generate_boolean_invert(context, instructions, binary_operation->range, result_register);
                }

                result_type = new Boolean;
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
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            expect(left_register, coerce_to_pointer_register_value(
                context,
                instructions,
                binary_operation->left->range,
                left.type,
                left.value,
                *pointer,
                false
            ));

            expect(right_register, coerce_to_pointer_register_value(
                context,
                instructions,
                binary_operation->right->range,
                right.type,
                right.value,
                *pointer,
                false
            ));

            IntegerComparisonOperation::Operation comparison_operation;
            auto invert = false;
            switch(binary_operation->binary_operator) {
                case BinaryOperation::Operator::Equal: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                } break;

                case BinaryOperation::Operator::NotEqual: {
                    comparison_operation = IntegerComparisonOperation::Operation::Equal;
                    invert = true;
                } break;

                default: {
                    error(*context, binary_operation->range, "Cannot perform that operation on '%s'", type_description(pointer));

                    return { false };
                } break;
            }

            auto result_register = append_integer_comparison_operation(
                context,
                instructions,
                binary_operation->range.first_line,
                comparison_operation,
                context->address_integer_size,
                left_register,
                right_register
            );

            if(invert) {
                result_register = generate_boolean_invert(context, instructions, binary_operation->range, result_register);
            }

            return {
                true,
                {
                    new Boolean,
                    new RegisterValue {
                        result_register
                    }
                }
            };
        } else {
            abort();
        }
    } else if(auto unary_operation = dynamic_cast<UnaryOperation*>(expression)) {
        expect(expression_value, generate_expression(context, instructions, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                size_t address_register;
                if(auto constant_value = dynamic_cast<ConstantValue*>(expression_value.value)) {
                    if(auto function = dynamic_cast<FunctionTypeType*>(expression_value.type)) {
                        auto function_value = dynamic_cast<FunctionConstant*>(expression_value.value);
                        assert(function_value);

                        auto is_registered = false;
                        for(auto runtime_function : context->runtime_functions) {
                            if(strcmp(runtime_function.mangled_name, function_value->mangled_name) == 0) {
                                is_registered = true;

                                break;
                            }
                        }

                        if(!is_registered) {
                            if(!is_registered) {
                                auto parameter_count = function->parameters.count;

                                auto runtime_parameters = allocate<RuntimeFunctionParameter>(parameter_count);

                                for(size_t i = 0; i < parameter_count; i += 1) {
                                    runtime_parameters[i] = {
                                        function_value->declaration->parameters[i].name,
                                        function->parameters[i],
                                        function_value->declaration->parameters[i].type->range
                                    };
                                }

                                append(&context->runtime_functions, {
                                    function_value->mangled_name,
                                    { parameter_count, runtime_parameters },
                                    function->return_type,
                                    {
                                        function_value->declaration,
                                        {},
                                        function_value->parent
                                    }
                                });
                            }
                        }

                        address_register = append_reference_static(
                            context,
                            instructions,
                            unary_operation->range.first_line,
                            function_value->mangled_name
                        );
                    } else if(dynamic_cast<TypeType*>(expression_value.type)) {
                        auto type = dynamic_cast<Type*>(expression_value.value);
                        assert(type);

                        if(
                            !is_runtime_type(type) &&
                            !dynamic_cast<Void*>(type) &&
                            !dynamic_cast<FunctionTypeType*>(type)
                        ) {
                            error(*context, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                            return { false };
                        }

                        return {
                            true,
                            {
                                new TypeType,
                                new Pointer {
                                    type
                                }
                            }
                        };
                    } else {
                        error(*context, unary_operation->expression->range, "Cannot take pointers to constants of type '%s'", type_description(expression_value.type));

                        return { false };
                    }
                } else if(dynamic_cast<RegisterValue*>(expression_value.value) || dynamic_cast<UndeterminedStructValue*>(expression_value.value)) {
                    error(*context, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return { false };
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
                if(!dynamic_cast<Boolean*>(expression_value.type)) {
                    error(*context, unary_operation->expression->range, "Expected bool, got '%s'", type_description(expression_value.type));

                    return { false };
                }

                size_t register_index;
                if(auto boolean_value = dynamic_cast<BooleanConstant*>(expression_value.value)) {
                    return {
                        true,
                        {
                            new Boolean,
                            new BooleanConstant {
                                !boolean_value->value
                            }
                        }
                    };
                } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    register_index = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                    register_index = append_load_integer(
                        context,
                        instructions,
                        unary_operation->expression->range.first_line,
                        context->default_integer_size,
                        address_value->address_register
                    );
                }

                auto result_register = generate_boolean_invert(context, instructions, unary_operation->expression->range, register_index);

                return {
                    true,
                    {
                        new Boolean,
                        new RegisterValue {
                            result_register
                        }
                    }
                };
            } break;

            case UnaryOperation::Operator::Negation: {
                if(dynamic_cast<UndeterminedInteger*>(expression_value.type)) {
                    auto integer_value = dynamic_cast<IntegerConstant*>(expression_value.value);
                    assert(integer_value);

                    return {
                        true,
                        {
                            new UndeterminedInteger,
                            new IntegerConstant {
                                -integer_value->value
                            }
                        }
                    };
                } else if(auto integer = dynamic_cast<Integer*>(expression_value.type)) {
                    size_t register_index;
                    if(auto integer_value = dynamic_cast<IntegerConstant*>(expression_value.value)) {
                        return {
                            true,
                            {
                                integer,
                                new IntegerConstant {
                                    -integer_value->value
                                }
                            }
                        };
                    } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                        register_index = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
                } else if(auto float_type = dynamic_cast<FloatType*>(expression_value.type)) {
                    size_t register_index;
                    if(auto float_value = dynamic_cast<FloatConstant*>(expression_value.value)) {
                        return {
                            true,
                            {
                                float_type,
                                new FloatConstant {
                                    -float_value->value
                                }
                            }
                        };
                    } else if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                        register_index = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
                } else if(dynamic_cast<UndeterminedFloat*>(expression_value.type)) {
                    auto float_value = dynamic_cast<FloatConstant*>(expression_value.value);
                    assert(float_value);

                    return {
                        true,
                        {
                            new UndeterminedFloat,
                            new FloatConstant {
                                -float_value->value
                            }
                        }
                    };
                } else {
                    error(*context, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return { false };
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(auto cast = dynamic_cast<Cast*>(expression)) {
        expect(expression_value, generate_expression(context, instructions, cast->expression));

        expect(target_type, evaluate_type_expression_runtime(context, instructions, cast->type));

        if(auto constant_value = dynamic_cast<ConstantValue*>(expression_value.value)) {
            auto constant_cast_result = evaluate_constant_cast(
                *context,
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
                        constant_cast_result.value
                    }
                };
            }
        }

        auto coercion_result = coerce_to_type_register(
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
        } else if(auto target_integer = dynamic_cast<Integer*>(target_type)) {
            if(auto integer = dynamic_cast<Integer*>(expression_value.type)) {
                size_t value_register;
                if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    value_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
                register_index = append_integer_upcast(
                    context,
                    instructions,
                    cast->range.first_line,
                    integer->is_signed,
                    integer->size,
                    target_integer->size,
                    value_register
                );
            } else if(auto float_type = dynamic_cast<FloatType*>(expression_value.type)) {
                size_t value_register;
                if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    value_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
            } else if(auto pointer = dynamic_cast<Pointer*>(expression_value.type)) {
                if(target_integer->size == context->address_integer_size && !target_integer->is_signed) {
                    has_cast = true;

                    if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                        register_index = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range.first_line,
                            context->address_integer_size,
                            address_value->address_register
                        );
                    } else {
                        abort();
                    }
                }
            }
        } else if(auto target_float_type = dynamic_cast<FloatType*>(target_type)) {
            if(auto integer = dynamic_cast<Integer*>(expression_value.type)) {
                size_t value_register;
                if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    value_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
            } else if(auto float_type = dynamic_cast<FloatType*>(expression_value.type)) {
                size_t value_register;
                if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    value_register = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
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
        } else if(auto target_pointer = dynamic_cast<Pointer*>(target_type)) {
            if(auto integer = dynamic_cast<Integer*>(expression_value.type)) {
                if(integer->size == context->address_integer_size && !integer->is_signed) {
                    has_cast = true;

                    if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                        register_index = register_value->register_index;
                    } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                        register_index = append_load_integer(
                            context,
                            instructions,
                            cast->expression->range.first_line,
                            context->address_integer_size,
                            address_value->address_register
                        );
                    } else {
                        abort();
                    }
                }
            } else if(auto pointer = dynamic_cast<Pointer*>(expression_value.type)) {
                has_cast = true;

                if(auto register_value = dynamic_cast<RegisterValue*>(expression_value.value)) {
                    register_index = register_value->register_index;
                } else if(auto address_value = dynamic_cast<AddressValue*>(expression_value.value)) {
                    register_index = append_load_integer(
                        context,
                        instructions,
                        cast->expression->range.first_line,
                        context->address_integer_size,
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
            error(*context, cast->range, "Cannot cast from '%s' to '%s'", type_description(expression_value.type), type_description(target_type));

            return { false };
        }
    } else if(auto array_type = dynamic_cast<ArrayType*>(expression)) {
        expect(type, evaluate_type_expression_runtime(context, instructions, array_type->expression));

        if(!is_runtime_type(type)) {
            error(*context, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return { false };
        }

        if(array_type->index != nullptr) {
            expect(index_value, evaluate_constant_expression(context, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                *context,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    context->address_integer_size,
                    false
                },
                false
            ));

            return {
                true,
                {
                    new TypeType,
                    new StaticArray {
                        length->value,
                        type
                    }
                }
            };
        } else {
            return {
                true,
                {
                    new TypeType,
                    new ArrayTypeType {
                        type
                    }
                }
            };
        }
    } else if(auto function_type = dynamic_cast<FunctionType*>(expression)) {
        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(*context, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return { false };
            }

            expect(type, evaluate_type_expression_runtime(context, instructions, parameter.type));

            if(!is_runtime_type(type)) {
                error(*context, function_type->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return { false };
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = new Void;
        } else {
            expect(return_type_value, evaluate_type_expression_runtime(context, instructions, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(*context, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return { false };
            }

            return_type = return_type_value;
        }

        return {
            true,
            {
                new TypeType,
                new FunctionTypeType {
                    { parameter_count, parameters },
                    return_type
                }
            }
        };
    } else {
        abort();
    }
}

static bool generate_statement(GenerationContext *context, List<Instruction*> *instructions, Statement *statement) {
    if(auto expression_statement = dynamic_cast<ExpressionStatement*>(statement)) {
        if(!generate_expression(context, instructions, expression_statement->expression).status) {
            return false;
        }

        return true;
    } else if(auto variable_declaration = dynamic_cast<VariableDeclaration*>(statement)) {
        Type *type;
        FileRange type_range;
        size_t address_register;

        if(variable_declaration->type != nullptr && variable_declaration->initializer != nullptr) {
            expect(type_value, evaluate_type_expression_runtime(context, instructions, variable_declaration->type));
            
            if(!is_runtime_type(type_value)) {
                error(*context, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return false;
            }

            type = type_value;
            type_range = variable_declaration->type->range;

            expect(initializer_value, generate_expression(context, instructions, variable_declaration->initializer));

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
                get_type_size(*context, type),
                get_type_alignment(*context, type)
            );

            if(!coerce_to_type_write(
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
            expect(type_value, evaluate_type_expression_runtime(context, instructions, variable_declaration->type));
            
            if(!is_runtime_type(type_value)) {
                error(*context, variable_declaration->type->range, "Cannot create variables of type '%s'", type_description(type_value));

                return false;
            }

            type = type_value;
            type_range = variable_declaration->type->range;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
                get_type_size(*context, type),
                get_type_alignment(*context, type)
            );
        } else if(variable_declaration->initializer != nullptr) {
            expect(initializer_value, generate_expression(context, instructions, variable_declaration->initializer));

            expect(actual_type, coerce_to_default_type(*context, variable_declaration->initializer->range, initializer_value.type));
            
            if(!is_runtime_type(actual_type)) {
                error(*context, variable_declaration->initializer->range, "Cannot create variables of type '%s'", type_description(actual_type));

                return false;
            }

            type = actual_type;
            type_range = variable_declaration->initializer->range;

            address_register = append_allocate_local(
                context,
                instructions,
                variable_declaration->range.first_line,
                get_type_size(*context, type),
                get_type_alignment(*context, type)
            );

            if(!coerce_to_type_write(
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
            type,
            type_range
        )) {
            return false;
        }

        return true;
    } else if(auto assignment = dynamic_cast<Assignment*>(statement)) {
        expect(target, generate_expression(context, instructions, assignment->target));

        size_t address_register;
        if(auto address_value = dynamic_cast<AddressValue*>(target.value)){
            address_register = address_value->address_register;
        } else {
            error(*context, assignment->target->range, "Value is not assignable");

            return false;
        }

        expect(value, generate_expression(context, instructions, assignment->value));

        if(!coerce_to_type_write(
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
    } else if(auto if_statement = dynamic_cast<IfStatement*>(statement)) {
        auto end_jump_count = 1 + if_statement->else_ifs.count;
        auto end_jumps = allocate<Jump*>(end_jump_count);

        expect(condition, generate_expression(context, instructions, if_statement->condition));

        if(!dynamic_cast<Boolean*>(condition.type)) {
            error(*context, if_statement->condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

            return false;
        }

        auto condition_register = generate_in_register_boolean_value(context, instructions, if_statement->condition->range, condition.value);

        append_branch(context, instructions, if_statement->condition->range.first_line, condition_register, instructions->count + 2);

        auto first_jump = new Jump;
        first_jump->line = if_statement->range.first_line;

        append(instructions, (Instruction*)first_jump);

        append(&context->variable_context_stack, List<Variable>{});

        for(auto child_statement : if_statement->statements) {
            if(!generate_statement(context, instructions, child_statement)) {
                return false;
            }
        }

        context->variable_context_stack.count -= 1;

        auto first_end_jump = new Jump;
        first_end_jump->line = if_statement->range.first_line;

        append(instructions, (Instruction*)first_end_jump);

        end_jumps[0] = first_end_jump;

        first_jump->destination_instruction = instructions->count;

        for(size_t i = 0; i < if_statement->else_ifs.count; i += 1) {
            expect(condition, generate_expression(context, instructions, if_statement->else_ifs[i].condition));

            if(!dynamic_cast<Boolean*>(condition.type)) {
                error(*context, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_in_register_boolean_value(
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

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : if_statement->else_ifs[i].statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            auto end_jump = new Jump;
            end_jump->line = if_statement->range.first_line;

            append(instructions, (Instruction*)end_jump);

            end_jumps[i + 1] = end_jump;

            jump->destination_instruction = instructions->count;
        }

        append(&context->variable_context_stack, List<Variable>{});

        for(auto child_statement : if_statement->else_statements) {
            if(!generate_statement(context, instructions, child_statement)) {
                return false;
            }
        }

        context->variable_context_stack.count -= 1;

        for(size_t i = 0; i < end_jump_count; i += 1) {
            end_jumps[i]->destination_instruction = instructions->count;
        }

        return true;
    } else if(auto while_loop = dynamic_cast<WhileLoop*>(statement)) {
        auto condition_index = instructions->count;

        expect(condition, generate_expression(context, instructions, while_loop->condition));

        if(!dynamic_cast<Boolean*>(condition.type)) {
            error(*context, while_loop->condition->range, "Non-boolean while loop condition. Got %s", type_description(condition.type));

            return false;
        }

        auto condition_register = generate_in_register_boolean_value(
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

        append(&context->variable_context_stack, List<Variable>{});

        for(auto child_statement : while_loop->statements) {
            if(!generate_statement(context, instructions, child_statement)) {
                return false;
            }
        }

        context->variable_context_stack.count -= 1;

        append_jump(
            context,
            instructions,
            while_loop->range.first_line,
            condition_index
        );

        jump_out->destination_instruction = instructions->count;

        return true;
    } else if(auto return_statement = dynamic_cast<ReturnStatement*>(statement)) {
        auto return_instruction = new ReturnInstruction;
        return_instruction->line = return_statement->range.first_line;

        if(return_statement->value != nullptr) {
            if(dynamic_cast<Void*>(context->return_type)) {
                error(*context, return_statement->range, "Erroneous return value");

                return { false };
            } else {
                expect(value, generate_expression(context, instructions, return_statement->value));

                auto representation = get_type_representation(*context, context->return_type);

                if(representation.is_in_register) {
                    expect(register_index, coerce_to_type_register(
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
        } else if(!dynamic_cast<Void*>(context->return_type)) {
            error(*context, return_statement->range, "Missing return value");

            return { false };
        }

        append(instructions, (Instruction*)return_instruction);

        return true;
    } else {
        abort();
    }
}

inline void append_base_integer_type(List<GlobalConstant> *global_constants, const char *name, RegisterSize size, bool is_signed) {
    append(global_constants, {
        name,
        new TypeType,
        new Integer {
            size,
            is_signed
        }
    });
}

inline void append_builtin(List<GlobalConstant> *global_constants, const char *name) {
    append(global_constants, {
        name,
        new BuiltinFunction,
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

    append(&global_constants, {
        "bool",
        new TypeType,
        new Boolean
    });

    append(&global_constants, {
        "void",
        new TypeType,
        new Void
    });

    append(&global_constants, {
        "f32",
        new TypeType,
        new FloatType {
            RegisterSize::Size32
        }
    });

    append(&global_constants, {
        "f64",
        new TypeType,
        new FloatType {
            RegisterSize::Size64
        }
    });

    append(&global_constants, GlobalConstant {
        "true",
        new Boolean,
        new BooleanConstant { true }
    });

    append(&global_constants, GlobalConstant {
        "false",
        new Boolean,
        new BooleanConstant { false }
    });

    append(&global_constants, {
        "type", new TypeType, new TypeType
    });

    append_builtin(&global_constants, "size_of");
    append_builtin(&global_constants, "type_of");

    GenerationContext context {
        address_size,
        default_size,
        to_array(global_constants)
    };

    append(&context.parsed_files, {
        main_file_path,
        main_file_statements
    });

    context.is_top_level = true;
    context.parent.file_path = main_file_path;
    context.parent.top_level_statements = main_file_statements;
    context.constant_parameters = {};

    auto main_found = false;
    for(auto statement : main_file_statements) {
        if(match_declaration(statement, "main")) {
            expect(value, resolve_declaration(&context, statement));

            if(auto function = dynamic_cast<FunctionTypeType*>(value.type)) {
                auto function_value = dynamic_cast<FunctionConstant*>(value.value);
                assert(function_value);

                auto parameter_count = function->parameters.count;

                auto runtime_parameters = allocate<RuntimeFunctionParameter>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    runtime_parameters[i] = {
                        function_value->declaration->parameters[i].name,
                        function->parameters[i]
                    };
                }

                auto mangled_name = "main";

                RuntimeFunction runtime_function;
                runtime_function.mangled_name = mangled_name;
                runtime_function.parameters = {
                    parameter_count,
                    runtime_parameters
                };
                runtime_function.return_type = function->return_type;
                runtime_function.declaration.declaration = function_value->declaration;
                runtime_function.declaration.constant_parameters = {};
                runtime_function.declaration.parent.file_path = main_file_path;
                runtime_function.declaration.parent.top_level_statements = main_file_statements;

                if(does_runtime_static_exist(context, mangled_name)) {
                    error(context, function_value->declaration->name.range, "Duplicate global name '%s'", mangled_name);

                    return { false };
                }

                append(&context.runtime_functions, runtime_function);

                main_found = true;

                break;
            } else if(dynamic_cast<PolymorphicFunction*>(value.type)) {
                error(context, statement->range, "'main' cannot be polymorphic");

                return { false };
            } else {
                error(context, statement->range, "'main' must be a function");

                return { false };
            }
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
            context.is_top_level = false;
            context.parent.parent = heapify(function.declaration);
            context.constant_parameters = {};

            auto function_declaration = dynamic_cast<FunctionDeclaration*>(function.declaration.declaration);
            assert(function_declaration);

            if(does_runtime_static_exist(context, function.mangled_name)) {
                error(context, function_declaration->name.range, "Duplicate runtime name '%s'", function.mangled_name);

                return { false };
            }

            auto total_parameter_count = function.parameters.count;

            bool has_return;
            RegisterRepresentation return_representation;                
            if(dynamic_cast<Void*>(function.return_type)) {
                has_return = false;
            } else {
                has_return = true;

                return_representation = get_type_representation(context, function.return_type);

                if(!return_representation.is_in_register) {
                    total_parameter_count += 1;
                }
            }

            auto ir_parameters = allocate<Function::Parameter>(total_parameter_count);

            for(size_t i = 0; i < function.parameters.count; i += 1) {
                auto representation = get_type_representation(context, function.parameters[i].type);

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

            const char *file_path;
            {
                auto current_declaration = function.declaration;

                while(current_declaration.declaration->parent != nullptr) {
                    current_declaration = *current_declaration.parent.parent;
                }

                file_path = current_declaration.parent.file_path;
            }

            auto ir_function = new Function;
            ir_function->name = function.mangled_name;
            ir_function->is_external = function_declaration->is_external;
            ir_function->parameters = {
                total_parameter_count,
                ir_parameters
            };
            ir_function->has_return = has_return && return_representation.is_in_register;
            ir_function->file = file_path;
            ir_function->line = function_declaration->range.first_line;

            if(has_return && return_representation.is_in_register) {
                ir_function->return_size = return_representation.value_size;
                ir_function->is_return_float = return_representation.is_float;
            }

            context.next_register = total_parameter_count;

            if(function_declaration->is_external) {
                if(function_declaration->is_external) {
                    for(auto library : function_declaration->external_libraries) {
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
                }
            } else {
                append(&context.variable_context_stack, List<Variable>{});

                List<Instruction*> instructions{};

                auto parameters = allocate<Variable>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    parameters[i] = {
                        parameter.name,
                        parameter.type,
                        parameter.type_range,
                        i
                    };

                    auto size = get_type_size(context, parameter.type);

                    auto address_register = append_allocate_local(
                        &context,
                        &instructions,
                        function.declaration.declaration->range.first_line,
                        size,
                        get_type_alignment(context, parameter.type)
                    );

                    auto representation = get_type_representation(context, parameter.type);

                    if(representation.is_in_register) {
                        if(representation.is_float) {
                            append_store_float(
                                &context,
                                &instructions,
                                function.declaration.declaration->range.first_line,
                                representation.value_size,
                                i,
                                address_register
                            );
                        } else {
                            append_store_integer(
                                &context,
                                &instructions,
                                function.declaration.declaration->range.first_line,
                                representation.value_size,
                                i,
                                address_register
                            );
                        }
                    } else {
                        generate_constant_size_copy(
                            &context,
                            &instructions,
                            function.declaration.declaration->range,
                            size,
                            i,
                            address_register
                        );
                    }

                    add_new_variable(
                        &context,
                        parameter.name,
                        address_register,
                        parameter.type,
                        parameter.type_range
                    );
                }

                context.return_type = function.return_type;

                if(has_return && !return_representation.is_in_register) {
                    context.return_parameter_register = total_parameter_count - 1;
                }

                for(auto statement : function_declaration->statements) {
                    if(
                        dynamic_cast<ExpressionStatement*>(statement) ||
                        dynamic_cast<VariableDeclaration*>(statement) ||
                        dynamic_cast<Assignment*>(statement) ||
                        dynamic_cast<IfStatement*>(statement) ||
                        dynamic_cast<WhileLoop*>(statement) ||
                        dynamic_cast<ReturnStatement*>(statement)
                    ) {
                        if(!generate_statement(&context, &instructions, statement)) {
                            return { false };
                        }
                    }
                }

                bool has_return_at_end;
                if(function_declaration->statements.count > 0) {
                    has_return_at_end = dynamic_cast<ReturnStatement*>(function_declaration->statements[function_declaration->statements.count - 1]);
                } else {
                    has_return_at_end = false;
                }

                if(!has_return_at_end) {
                    if(has_return) {
                        error(context, function_declaration->range, "Function '%s' must end with a return", function_declaration->name.text);

                        return { false };
                    } else {
                        auto return_instruction = new ReturnInstruction;

                        append(&instructions, (Instruction*)return_instruction);
                    }
                }

                context.variable_context_stack.count -= 1;
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