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

struct Type {
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

static const char *integer_type_description(Integer integer) {
    if(integer.is_signed) {
        switch(integer.size) {
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
        switch(integer.size) {
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
    } else if(auto integer = dynamic_cast<Integer*>(type)) {
        return integer_type_description(*integer);
    } else if(dynamic_cast<UndeterminedInteger*>(type)) {
        return "{integer}";
    } else if(dynamic_cast<Boolean*>(type)) {
        return "bool";
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

struct ConstantValue {
    virtual ~ConstantValue() {}
};

struct FunctionConstant : ConstantValue {
    const char *mangled_name;

    FunctionConstant() {}

    FunctionConstant(
        const char *mangled_name
    ) :
    mangled_name { mangled_name }
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

struct BooleanConstant : ConstantValue {
    bool value;

    BooleanConstant() {}

    BooleanConstant(
        bool value
    ) :
    value { value }
    {}
};

struct TypeConstant : ConstantValue {
    Type *type;

    TypeConstant() {}

    TypeConstant(
        Type *type
    ) :
    type { type }
    {}
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

    size_t register_index;
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

    Array<Variable> parameters;
    Type *return_type;
    size_t return_parameter_register;

    List<const char*> global_names;

    List<List<Variable>> variable_context_stack;

    size_t next_register;

    List<RuntimeFunction> runtime_functions;

    List<const char*> libraries;

    List<StaticConstant> static_constants;

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

    return strcmp(declaration_name, name);
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

    return strcmp(declaration_name, name);
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
    auto old_is_top_level = context->is_top_level;
    auto old_constant_parameters = context->constant_parameters;
    auto old_parent = context->parent;

    while(!context->is_top_level) {
        for(auto constant_parameter : context->constant_parameters) {
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

                    if(dynamic_cast<FileModule*>(expression_value.type)) {
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

    for(auto constant_parameter : context->constant_parameters) {
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

            if(dynamic_cast<FileModule*>(expression_value.type)) {
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

static Result<StructConstant*> coerce_constant_to_struct_type(
    GenerationContext context,
    FileRange range,
    Type *type,
    ConstantValue *value,
    StructType target_type,
    bool probing
) {
    assert(!target_type.definition->is_union);

    if(auto struct_type = dynamic_cast<StructType*>(type)) {
        if(struct_type->definition != target_type.definition) {
            if(!probing) {
                error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(struct_type), type_description(&target_type));
            }

            return { false };
        }

        auto struct_value = dynamic_cast<StructConstant*>(value);
        assert(struct_value);

        return {
            true,
            struct_value
        };
    } else if(auto undetermined_struct = dynamic_cast<UndeterminedStruct*>(type)) {
        if(undetermined_struct->members.count != target_type.members.count) {
            if(!probing) {
                error(context, range, "Too many struct members. Expected %zu, got %zu", target_type.members.count, undetermined_struct->members.count);
            }

            return { false };
        }

        auto member_count = target_type.members.count;

        auto struct_value = dynamic_cast<StructConstant*>(value);
        assert(struct_value);

        auto member_values = allocate<ConstantValue*>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            auto member = struct_type->members[i];
            auto target_member = target_type.members[i];

            if(strcmp(member.name, target_member.name) != 0) {
                if(!probing) {
                    error(context, range, "Incorrect struct member name. Expected '%s', got '%s", target_member.name, member.name);
                }

                return { false };
            }

            expect(coerced_member_value, coerce_constant_to_type(
                context,
                range,
                member.type,
                struct_value->members[i],
                target_member.type,
                probing
            ));

            member_values[i] = coerced_member_value;
        }

        return {
            true,
            new StructConstant {
                member_values
            }
        };
    } else {
        if(!probing) {
            error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
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
    } else if(auto target_pointer = dynamic_cast<Pointer*>(target_type)) {
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
            if(types_equal(pointer->type, target_pointer->type)) {
                return {
                    true,
                    value
                };
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
                            (int64_t)left->value * (int64_t)right->value
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
                            (int64_t)left->value / (int64_t)right->value
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
                            (int64_t)left->value % (int64_t)right->value
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
    FileRange target_range
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
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            if(target_integer->size == context.address_integer_size && !target_integer->is_signed) {
                auto pointer_value = dynamic_cast<PointerConstant*>(value);
                assert(pointer_value);

                result = pointer_value->value;
            } else {
                error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(pointer), type_description(target_integer));

                return { false };
            }
        } else {
            error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_integer));

            return { false };
        }

        return {
            true,
            new IntegerConstant {
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
                error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(integer), type_description(target_pointer));

                return { false };
            }
        } else if(auto pointer = dynamic_cast<Pointer*>(type)) {
            auto pointer_value = dynamic_cast<PointerConstant*>(value);
            assert(pointer_value);

            result = pointer_value->value;
        } else {
            error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_pointer));

            return { false };
        }

        return {
            new PointerConstant {
                result
            }
        };
    } else {
        error(context, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_type));

        return { false };
    }
}

struct RegisterRepresentation {
    bool is_in_register;

    RegisterSize value_size;
};

static RegisterRepresentation get_type_representation(GenerationContext context, Type *type) {
    if(auto integer = dynamic_cast<Integer*>(type)) {
        return {
            true,
            integer->size
        };
    } else if(dynamic_cast<Boolean*>(type)) {
        return {
            true,
            context.default_integer_size
        };
    } else if(dynamic_cast<Pointer*>(type)) {
        return {
            true,
            context.address_integer_size
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

static Result<Type*> evaluate_type_expression(GenerationContext *context, Expression *expression);

static Result<TypedConstantValue> evaluate_function_declaration(GenerationContext *context, FunctionDeclaration *declaration) {
    for(auto parameter : declaration->parameters) {
        if(parameter.is_polymorphic_determiner) {
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
                mangled_name
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
        auto parameter_types = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect(type, evaluate_type_expression(context, definition->parameters[i].type));

            parameter_types[i] = type;
        }

        return {
            true,
            {
                new TypeType,
                new TypeConstant {
                    new PolymorphicStruct {
                        definition,
                        parameter_types,
                        context->parent
                    }
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
    } else if(auto string_literal = dynamic_cast<StringLiteral*>(expression)) {
        auto character_count = string_literal->characters.count;

        auto characters = allocate<ConstantValue*>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = new IntegerConstant {
                string_literal->characters[i]
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
        } else if(dynamic_cast<TypeType*>(expression_value.type)) {
            auto type_value = dynamic_cast<TypeConstant*>(expression_value.value);
            assert(type_value);

            if(auto polymorphic_struct = dynamic_cast<PolymorphicStruct*>(type_value->type)) {
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
                error(*context, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type_value->type));

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
                    auto type_value = dynamic_cast<TypeConstant*>(expression_value.value);
                    assert(type_value);

                    if(
                        !is_runtime_type(type_value->type) &&
                        !dynamic_cast<Void*>(type_value->type) &&
                        !dynamic_cast<Function*>(type_value->type)
                    ) {
                        error(*context, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type_value->type));

                        return { false };
                    }

                    return {
                        true,
                        {
                            new TypeType,
                            new TypeConstant {
                                new Pointer {
                                    type_value->type
                                }
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
                if(auto integer = dynamic_cast<Integer*>(expression_value.type)) {
                    auto integer_value = dynamic_cast<IntegerConstant*>(expression_value.value);
                    assert(integer_value);

                    return {
                        true,
                        {
                            integer,
                            new IntegerConstant {
                                -integer_value->value
                            }
                        }
                    };
                } else if(dynamic_cast<UndeterminedInteger*>(expression_value.type)) {
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
                } else {
                    error(*context, unary_operation->expression->range, "Expected an integer, got '%s'", type_description(expression_value.type));

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
            cast->type->range
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
                    new TypeType,
                    new TypeConstant {
                        new ArrayTypeType {
                            type
                        }
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

static Result<Type*> evaluate_type_expression(GenerationContext *context, Expression *expression) {
    expect(expression_value, evaluate_constant_expression(context, expression));

    if(dynamic_cast<TypeType*>(expression_value.type)) {
        auto type_value = dynamic_cast<TypeConstant*>(expression_value.value);
        assert(type_value);

        return {
            true,
            type_value->type
        }
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

static bool add_new_variable(GenerationContext *context, Identifier name, size_t address_register, Type type, FileRange type_range) {
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

enum struct ValueCategory {
    Constant,
    Anonymous,
    Address
};

struct TypedValue;

struct Value {
    ValueCategory category;

    union {
        struct {
            size_t register_;

            Value *undetermined_struct;
        } anonymous;

        size_t address;

        ConstantValue constant;
    };
};

struct TypedValue {
    Type type;

    Value value;
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

static void write_static_array(GenerationContext context, uint8_t *data, size_t offset, Type type, Array<ConstantValue> elements);

static void write_struct(GenerationContext context, uint8_t *data, size_t offset, Type type, ConstantValue *members) {
    for(size_t i = 0; i < type._struct.concrete.count; i += 1) {
        auto member_offset = get_struct_member_offset(context, type, i);

        auto representation = get_type_representation(context, type._struct.concrete[i].type);

        if(representation.is_in_register) {
            uint64_t value;
            switch(type._struct.concrete[i].type.category) {
                case TypeCategory::Integer: {
                    value = members[i].integer;
                } break;

                case TypeCategory::Boolean: {
                    value = members[i].boolean;
                } break;

                case TypeCategory::Pointer: {
                    value = members[i].pointer;
                } break;

                default: {
                    abort();
                } break;
            }

            write_integer(data, offset + member_offset, representation.value_size, value);
        } else {
            switch(type._struct.concrete[i].type.category) {
                case TypeCategory::Array: {
                    write_integer(
                        data,
                        offset + member_offset,
                        context.address_integer_size,
                        members[i].array.pointer
                    );

                    write_integer(
                        data,
                        offset + member_offset + register_size_to_byte_size(context.address_integer_size),
                        context.address_integer_size,
                        members[i].array.length
                    );
                } break;

                case TypeCategory::StaticArray: {
                    write_static_array(
                        context,
                        data,
                        offset + member_offset,
                        *type._struct.concrete[i].type.static_array.type,
                        { type._struct.concrete[i].type.static_array.length, members[i].static_array }
                    );
                } break;

                case TypeCategory::Struct: {
                    write_struct(context, data, offset + member_offset, type, members[i].struct_);
                } break;
            }
        }
    }
}

static void write_static_array(GenerationContext context, uint8_t *data, size_t offset, Type type, Array<ConstantValue> elements) {
    auto representation = get_type_representation(context, type);

    auto element_size = get_type_size(context, type);

    for(size_t i = 0; i < elements.count; i += 1) {
        if(representation.is_in_register) {
            uint64_t value;
            switch(type.category) {
                case TypeCategory::Integer: {
                    value = elements[i].integer;
                } break;

                case TypeCategory::Boolean: {
                    value = elements[i].boolean;
                } break;

                case TypeCategory::Pointer: {
                    value = elements[i].pointer;
                } break;

                default: {
                    abort();
                } break;
            }

            write_integer(data, offset + i * element_size, representation.value_size, value);
        } else {
            switch(type.category) {
                case TypeCategory::Array: {
                    write_integer(data, offset + i * element_size, context.address_integer_size, elements[i].array.pointer);
                    write_integer(data, offset + i * element_size + register_size_to_byte_size(context.address_integer_size), context.address_integer_size, elements[i].array.length);
                } break;

                case TypeCategory::StaticArray: {
                    write_static_array(context, data, offset + i * element_size, *type.static_array.type, { type.static_array.length, elements[i].static_array });
                } break;

                case TypeCategory::Struct: {
                    write_struct(context, data, offset + i * element_size, type, elements[i].struct_);
                } break;
            }
        }
    }
}

static const char *register_static_array_constant(GenerationContext *context, Type type, Array<ConstantValue> elements) {
    auto element_size = get_type_size(*context, type);
    auto element_alignment = get_type_alignment(*context, type);

    auto data = allocate<uint8_t>(element_size * elements.count);

    write_static_array(*context, data, 0, type, elements);

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, context->static_constants.count);

    append(&context->static_constants, {
        name_buffer,
        element_alignment,
        {
            elements.count * element_size,
            data
        }
    });

    return name_buffer;
}

static const char *register_struct_constant(GenerationContext *context, Type type, ConstantValue *members) {
    auto length = get_struct_size(*context, type);
    auto alignment = get_struct_alignment(*context, type);

    auto data = allocate<uint8_t>(length);

    write_struct(*context, data, 0, type, members);

    char *name_buffer{};
    string_buffer_append(&name_buffer, "constant_");
    string_buffer_append(&name_buffer, context->static_constants.count);

    append(&context->static_constants, {
        name_buffer,
        alignment,
        {
            length,
            data
        }
    });

    return name_buffer;
}

static size_t append_arithmetic_operation(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    ArithmeticOperationType type,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    Instruction arithmetic_operation;
    arithmetic_operation.type = InstructionType::ArithmeticOperation;
    arithmetic_operation.line = line;
    arithmetic_operation.arithmetic_operation = {
        type,
        size,
        source_register_a,
        source_register_b,
        destination_register
    };

    append(instructions, arithmetic_operation);

    return destination_register;
}

static size_t append_comparison_operation(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    ComparisonOperationType type,
    RegisterSize size,
    size_t source_register_a,
    size_t source_register_b
) {
    auto destination_register = allocate_register(context);

    Instruction comparison_operation;
    comparison_operation.type = InstructionType::ComparisonOperation;
    comparison_operation.line = line;
    comparison_operation.comparison_operation = {
        type,
        size,
        source_register_a,
        source_register_b,
        destination_register
    };

    append(instructions, comparison_operation);

    return destination_register;
}

static size_t append_constant(GenerationContext *context, List<Instruction> *instructions, unsigned int line, RegisterSize size, uint64_t value) {
    auto destination_register = allocate_register(context);

    Instruction constant;
    constant.type = InstructionType::Constant;
    constant.line = line;
    constant.constant = {
        size,
        destination_register,
        value
    };

    append(instructions, constant);

    return destination_register;
}

static size_t append_integer_upcast(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    bool is_signed,
    RegisterSize source_size,
    RegisterSize destination_size,
    size_t source_register
) {
    auto destination_register = allocate_register(context);

    Instruction integer_upcast;
    integer_upcast.type = InstructionType::IntegerUpcast;
    integer_upcast.line = line;
    integer_upcast.integer_upcast = {
        is_signed,
        source_size,
        source_register,
        destination_size,
        destination_register
    };

    append(instructions, integer_upcast);

    return destination_register;
}

static size_t append_reference_static(GenerationContext *context, List<Instruction> *instructions, unsigned int line, const char *name) {
    auto destination_register = allocate_register(context);

    Instruction reference_static;
    reference_static.type = InstructionType::ReferenceStatic;
    reference_static.line = line;
    reference_static.reference_static = {
        name,
        destination_register
    };

    append(instructions, reference_static);

    return destination_register;
}

static size_t append_allocate_local(GenerationContext *context, List<Instruction> *instructions, unsigned int line, size_t size, size_t alignment) {
    auto destination_register = allocate_register(context);

    Instruction allocate_local;
    allocate_local.type = InstructionType::AllocateLocal;
    allocate_local.line = line;
    allocate_local.allocate_local = {
        size,
        alignment,
        destination_register
    };

    append(instructions, allocate_local);

    return destination_register;
}

static void append_branch(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    size_t condition_register,
    size_t instruction_index
) {
    Instruction branch;
    branch.type = InstructionType::Branch;
    branch.line = line;
    branch.branch = {
        condition_register,
        instruction_index
    };

    append(instructions, branch);
}

static void append_jump(GenerationContext *context, List<Instruction> *instructions, unsigned int line, size_t instruction_index) {
    Instruction jump;
    jump.type = InstructionType::Jump;
    jump.line = line;
    jump.branch = {
        instruction_index
    };

    append(instructions, jump);
}

static void append_copy_memory(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    size_t length_register,
    size_t source_address_register,
    size_t destination_address_register
) {
    Instruction copy_memory;
    copy_memory.type = InstructionType::CopyMemory;
    copy_memory.line = line;
    copy_memory.copy_memory = {
        length_register,
        source_address_register,
        destination_address_register
    };

    append(instructions, copy_memory);
}

static size_t append_load_integer(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t address_register
) {
    auto destination_register = allocate_register(context);

    Instruction load_integer;
    load_integer.type = InstructionType::LoadInteger;
    load_integer.line = line;
    load_integer.load_integer = {
        size,
        address_register,
        destination_register
    };

    append(instructions, load_integer);

    return destination_register;
}

static void append_store_integer(
    GenerationContext *context,
    List<Instruction> *instructions,
    unsigned int line,
    RegisterSize size,
    size_t source_register,
    size_t address_register
) {
    Instruction store_integer;
    store_integer.type = InstructionType::StoreInteger;
    store_integer.line = line;
    store_integer.store_integer = {
        size,
        source_register,
        address_register
    };

    append(instructions, store_integer);
}

static size_t generate_address_offset(GenerationContext *context, List<Instruction> *instructions, FileRange range, size_t address_register, size_t offset) {
    auto offset_register = append_constant(
        context,
        instructions,
        range.first_line,
        context->address_integer_size,
        offset
    );

    auto final_address_register = append_arithmetic_operation(
        context,
        instructions,
        range.first_line,
        ArithmeticOperationType::Add,
        context->address_integer_size,
        address_register,
        offset_register
    );

    return final_address_register;
}

static size_t generate_boolean_invert(GenerationContext *context, List<Instruction> *instructions, FileRange range, size_t value_register) {
    auto local_register = append_allocate_local(
        context,
        instructions,
        range.first_line,
        register_size_to_byte_size(context->default_integer_size),
        register_size_to_byte_size(context->default_integer_size)
    );

    append_branch(context, instructions, range.first_line, value_register, instructions->count + 4);

    auto true_register = append_constant(context, instructions, range.first_line, context->default_integer_size, 1);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, true_register, local_register);

    append_jump(context, instructions, range.first_line, instructions->count + 3);

    auto false_register = append_constant(context, instructions, range.first_line, context->default_integer_size, 0);

    append_store_integer(context, instructions, range.first_line, context->default_integer_size, false_register, local_register);

    auto result_register = append_load_integer(context, instructions, range.first_line, context->default_integer_size, local_register);

    return result_register;
}

static size_t generate_in_register_constant_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, Type type, ConstantValue value) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return append_constant(context, instructions, range.first_line, type.integer.size, value.integer);
        } break;

        case TypeCategory::Boolean: {
            return append_constant(context, instructions, range.first_line, context->default_integer_size, value.boolean);
        } break;
        
        case TypeCategory::Pointer: {
            return append_constant(context, instructions, range.first_line, context->address_integer_size, value.pointer);
        } break;

        default: {
            abort();
        } break;
    }
}

static void generate_not_in_register_constant_write(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value,
    size_t address_register
) {
    switch(type.category) {
        case TypeCategory::Array: {
            auto pointer_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.pointer);

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

            auto length_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.length);

            auto length_address_register = generate_address_offset(
                context,
                instructions,
                range,
                address_register,
                register_size_to_byte_size(context->address_integer_size)
            );

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);
        } break;

        case TypeCategory::StaticArray: {
            auto constant_name = register_static_array_constant(
                context,
                *type.static_array.type,
                Array<ConstantValue> {
                    type.static_array.length,
                    value.static_array
                }
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            auto length_register = append_constant(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                type.static_array.length * get_type_size(*context, *type.static_array.type)
            );

            append_copy_memory(context, instructions, range.first_line, length_register, constant_address_register, address_register);
        } break;

        case TypeCategory::Struct: {
            auto constant_name = register_struct_constant(
                context,
                type,
                value.struct_
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            auto length_register = append_constant(
                context,
                instructions,
                range.first_line,
                context->address_integer_size,
                get_struct_size(*context, type)
            );

            append_copy_memory(context, instructions, range.first_line, length_register, constant_address_register, address_register);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_not_in_register_constant_value(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value
) {
    switch(type.category) {
        case TypeCategory::Array: {
            auto address_register = append_allocate_local(
                context,
                instructions,
                range.first_line,
                2 * register_size_to_byte_size(context->address_integer_size),
                register_size_to_byte_size(context->address_integer_size)
            );

            auto pointer_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.pointer);

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, pointer_register, address_register);

            auto length_register = append_constant(context, instructions, range.first_line, context->address_integer_size, value.array.length);

            auto length_address_register = generate_address_offset(
                context,
                instructions,
                range,
                address_register,
                register_size_to_byte_size(context->address_integer_size)
            );

            append_store_integer(context, instructions, range.first_line, context->address_integer_size, length_register, length_address_register);

            return address_register;
        } break;

        case TypeCategory::StaticArray: {
            auto constant_name = register_static_array_constant(
                context,
                *type.static_array.type,
                Array<ConstantValue> {
                    type.static_array.length,
                    value.static_array
                }
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            return constant_address_register;
        } break;

        case TypeCategory::Struct: {
            auto constant_name = register_struct_constant(
                context,
                type,
                value.struct_
            );

            auto constant_address_register = append_reference_static(context, instructions, range.first_line, constant_name);

            return constant_address_register;
        } break;

        default: {
            abort();
        } break;
    }
}

static void generate_constant_value_write(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    Type type,
    ConstantValue value,
    size_t address_register
) {
    auto representation = get_type_representation(*context, type);

    if(representation.is_in_register) {
        auto value_register = generate_in_register_constant_value(context, instructions, range, type, value);

        append_store_integer(context, instructions, range.first_line, representation.value_size, value_register, address_register);
    } else {
        generate_not_in_register_constant_write(context, instructions, range, type, value, address_register);
    }
}

static Result<Value> coerce_to_integer_type(
    GenerationContext context,
    FileRange range,
    TypedValue value,
    RegisterSize size,
    bool is_signed,
    bool probing
) {
    switch(value.type.category) {
        case TypeCategory::Integer: {
            if(value.type.integer.is_undetermined) {
                Value result;
                result.category = ValueCategory::Constant;
                result.constant.integer = value.value.constant.integer;

                return {
                    true,
                    result
                };
            } else {
                if(value.type.integer.size != size || value.type.integer.is_signed != is_signed) {
                    if(!probing) {
                        error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), determined_integer_type_description(size, is_signed));
                    }

                    return { false };
                }

                return {
                    true,
                    value.value
                };
            }
        } break;

        default: {
            if(!probing) {
                error(context, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), determined_integer_type_description(size, is_signed));
            }

            return { false };
        } break;
    }
}

static Result<Value> coerce_to_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    Type type,
    bool probing
);

static Result<Value> coerce_to_struct_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    Type type,
    bool probing
) {
    if(value.type.category != TypeCategory::Struct) {
        if(!probing) {
            error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), type._struct.name);
        }

        return { false };
    }

    Value result_value;
    if(value.type._struct.is_undetermined) {
        if(type._struct.is_union) {
            if(value.type._struct.concrete.count != 1) {
                if(!probing) {
                    error(*context, range, "Too many union members. Expected 1, got %zu", value.type._struct.concrete.count);
                }

                return { false };
            }

            auto found = false;
            size_t member_index;
            for(size_t i = 0; i < type._struct.concrete.count; i += 1) {
                if(strcmp(value.type._struct.concrete[0].name, type._struct.concrete[i].name) == 0) {
                    found = true;
                    member_index = i;

                    break;
                }
            }

            if(!found) {
                if(!probing) {
                    error(*context, range, "Unknown union member '%s'", value.type._struct.concrete[0].name);
                }

                return { false };
            }

            Value member_value;
            switch(value.value.category) {
                case ValueCategory::Constant: {
                    member_value.category = ValueCategory::Constant;
                    member_value.constant = value.value.constant.struct_[0];
                } break;

                case ValueCategory::Anonymous: {
                    member_value = value.value.anonymous.undetermined_struct[0];
                } break;

                default: {
                    abort();
                } break;
            }

            expect(coerced_member_value, coerce_to_type(
                context,
                instructions,
                range,
                { value.type._struct.concrete[0].type, member_value },
                type._struct.concrete[member_index].type,
                probing
            ));

            switch(coerced_member_value.category) {
                case ValueCategory::Constant: {
                    result_value.category = ValueCategory::Constant;
                    result_value.constant.struct_ = heapify(coerced_member_value.constant);
                } break;

                case ValueCategory::Anonymous: {
                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        range.first_line,
                        get_struct_size(*context, type),
                        get_struct_alignment(*context, type)
                    );

                    auto member_representation = get_type_representation(*context, type._struct.concrete[member_index].type);

                    if(member_representation.is_in_register) {
                        append_store_integer(
                            context,
                            instructions,
                            range.first_line,
                            member_representation.value_size,
                            coerced_member_value.anonymous.register_,
                            address_register
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, type._struct.concrete[member_index].type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            range.first_line,
                            length_register,
                            coerced_member_value.anonymous.register_,
                            address_register
                        );
                    }

                    result_value.category = ValueCategory::Anonymous;
                    result_value.anonymous.register_ = address_register;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            if(value.type._struct.concrete.count != type._struct.concrete.count) {
                error(*context, range, "Too many struct members. Expected %zu, got %zu", type._struct.concrete.count, value.type._struct.concrete.count);

                return { false };
            }

            auto all_constant = false;
            auto coerced_member_values = allocate<Value>(type._struct.concrete.count);

            for(size_t i = 0; i < type._struct.concrete.count; i += 1) {
                if(strcmp(value.type._struct.concrete[i].name, type._struct.concrete[i].name) != 0) {
                    error(*context, range, "Incorrect struct member name. Expected '%s', got '%s", type._struct.concrete[i].name, value.type._struct.concrete[i].name);

                    return { false };
                }

                Value member_value;
                switch(value.value.category) {
                    case ValueCategory::Constant: {
                        member_value.category = ValueCategory::Constant;
                        member_value.constant = value.value.constant.struct_[i];
                    } break;

                    case ValueCategory::Anonymous: {
                        member_value = value.value.anonymous.undetermined_struct[i];
                    } break;

                    default: {
                        abort();
                    } break;
                }

                expect(coerced_member_value, coerce_to_type(
                    context,
                    instructions,
                    range,
                    { value.type._struct.concrete[i].type, member_value },
                    type._struct.concrete[i].type,
                    probing
                ));

                if(coerced_member_value.category != ValueCategory::Constant) {
                    all_constant = false;
                }

                coerced_member_values[i] = coerced_member_value;
            }

            if(all_constant) {
                auto member_values = allocate<ConstantValue>(type._struct.concrete.count);

                for(size_t i = 0; i < type._struct.concrete.count; i += 1) {
                    member_values[i] = coerced_member_values[i].constant;
                }

                result_value.category = ValueCategory::Constant;
                result_value.constant.struct_ = member_values;
            } else {
                auto address_register = append_allocate_local(
                    context,
                    instructions,
                    range.first_line,
                    get_struct_size(*context, type),
                    get_struct_alignment(*context, type)
                );

                for(size_t i = 0; i < type._struct.concrete.count; i += 1) {
                    auto offset = get_struct_member_offset(*context, type, i);

                    auto final_address_register = generate_address_offset(context, instructions, range, address_register, offset);

                    auto member_representation = get_type_representation(*context, type._struct.concrete[i].type);

                    switch(coerced_member_values[i].category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                range,
                                type._struct.concrete[i].type,
                                coerced_member_values[i].constant,
                                final_address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(member_representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    coerced_member_values[i].anonymous.register_,
                                    final_address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type._struct.concrete[i].type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    range.first_line,
                                    length_register,
                                    coerced_member_values[i].anonymous.register_,
                                    final_address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(member_representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    coerced_member_values[i].address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    range.first_line,
                                    member_representation.value_size,
                                    value_register,
                                    final_address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type._struct.concrete[i].type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    range.first_line,
                                    length_register,
                                    coerced_member_values[i].address,
                                    final_address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }

                result_value.category = ValueCategory::Anonymous;
                result_value.anonymous.register_ = address_register;
            }
        }

        return {
            true,
            result_value
        };
    } else {
        if(strcmp(value.type._struct.name, type._struct.name) != 0) {
            if(!probing) {
                error(*context, range, "Cannot implicitly convert '%s' to '%s'", value.type._struct.name, type._struct.name);
            }

            return { false };
        }

        return {
            true,
            value.value
        };
    }
}

static Result<Value> coerce_to_type(
    GenerationContext *context,
    List<Instruction> *instructions,
    FileRange range,
    TypedValue value,
    Type type,
    bool probing
) {
    switch(type.category) {
        case TypeCategory::Integer: {
            return coerce_to_integer_type(*context, range, value, type.integer.size, type.integer.is_signed, probing);
        } break;

        case TypeCategory::Pointer: {
            switch(value.type.category) {
                case TypeCategory::Integer: {
                    if(value.type.integer.is_undetermined) {
                        Value result;
                        result.category = ValueCategory::Constant;
                        result.constant.pointer = value.value.constant.integer;

                        return {
                            true,
                            result
                        };
                    }
                } break;

                case TypeCategory::Pointer: {
                    if(types_equal(*type.pointer, *value.type.pointer)) {
                        return {
                            true,
                            value.value
                        };
                    }
                } break;
            }
        } break;

        case TypeCategory::Array: {
            switch(value.type.category) {
                case TypeCategory::StaticArray: {
                    size_t pointer_register;
                    switch(value.value.category) {
                        case ValueCategory::Constant: {
                            auto constant_name = register_static_array_constant(
                                context,
                                *value.type.static_array.type,
                                { value.type.static_array.length, value.value.constant.static_array}
                            );

                            pointer_register = append_reference_static(context, instructions, range.first_line, constant_name);
                        } break;

                        case ValueCategory::Anonymous: {
                            pointer_register = value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            pointer_register = value.value.address;
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    auto length_register = append_constant(
                        context,
                        instructions,
                        range.first_line,
                        context->address_integer_size,
                        value.type.static_array.length
                    );

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

                    append_store_integer(
                        context,
                        instructions,
                        range.first_line,
                        context->address_integer_size,
                        length_register,
                        length_address_register
                    );

                    Value result;
                    result.category = ValueCategory::Anonymous;
                    result.anonymous.register_ = address_register;

                    return {
                        true,
                        result
                    };
                } break;
            }
        } break;

        case TypeCategory::Struct: {
            return coerce_to_struct_type(context, instructions, range, value, type, probing);
        } break;

        default: {
            if(types_equal(type, value.type)) {
                return {
                    true,
                    value.value
                };
            }
        } break;
    }

    if(!probing) {
        error(*context, range, "Cannot implicitly convert '%s' to '%s'", type_description(value.type), type_description(type));
    }

    return { false };
}

static size_t generate_in_register_integer_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, RegisterSize size, Value value) {
    switch(value.category) {
        case ValueCategory::Constant: {
            return append_constant(context, instructions, range.first_line, size, value.constant.integer);
        } break;

        case ValueCategory::Anonymous: {
            return value.anonymous.register_;
        } break;

        case ValueCategory::Address: {
            return append_load_integer(context, instructions, range.first_line, size, value.address);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_in_register_boolean_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, Value value) {
    switch(value.category) {
        case ValueCategory::Constant: {
            return append_constant(context, instructions, range.first_line, context->default_integer_size, value.constant.boolean);
        } break;

        case ValueCategory::Anonymous: {
            return value.anonymous.register_;
        } break;

        case ValueCategory::Address: {
            return append_load_integer(context, instructions, range.first_line, context->default_integer_size, value.address);
        } break;

        default: {
            abort();
        } break;
    }
}

static size_t generate_in_register_pointer_value(GenerationContext *context, List<Instruction> *instructions, FileRange range, Value value) {
    switch(value.category) {
        case ValueCategory::Constant: {
            return append_constant(context, instructions, range.first_line, context->address_integer_size, value.constant.pointer);
        } break;

        case ValueCategory::Anonymous: {
            return value.anonymous.register_;
        } break;

        case ValueCategory::Address: {
            return append_load_integer(context, instructions, range.first_line, context->address_integer_size, value.address);
        } break;

        default: {
            abort();
        } break;
    }
}

static Result<TypedValue> generate_expression(GenerationContext *context, List<Instruction> *instructions, Expression expression) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            for(size_t i = 0; i < context->variable_context_stack.count; i += 1) {
                for(auto variable : context->variable_context_stack[context->variable_context_stack.count - 1 - i]) {
                    if(strcmp(variable.name.text, expression.named_reference.text) == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Address;
                        value.type = variable.type;
                        value.value.address = variable.register_index;

                        return {
                            true,
                            value
                        };
                    }
                }
            }

            for(auto parameter : context->parameters) {
                if(strcmp(parameter.name.text, expression.named_reference.text) == 0) {
                    TypedValue value;
                    value.value.category = ValueCategory::Anonymous;
                    value.type = parameter.type;
                    value.value.address = parameter.register_index;

                    return {
                        true,
                        value
                    };
                }
            }

            expect(constant, resolve_constant_named_reference(context, expression.named_reference));

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type = constant.type;
            value.value.constant = constant.value;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::IndexReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.index_reference.expression));

            expect(index, generate_expression(context, instructions, *expression.index_reference.index));

            if(expression_value.value.category == ValueCategory::Constant && index.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_index(
                    *context,
                    expression_value.type,
                    expression_value.value.constant,
                    expression.index_reference.expression->range,
                    index.type,
                    index.value.constant,
                    expression.index_reference.index->range
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = constant.type;
                value.value.constant = constant.value;

                return {
                    true,
                    value
                };
            }

            expect(index_value, coerce_to_integer_type(
                *context,
                expression.index_reference.index->range,
                index,
                context->address_integer_size,
                false,
                false
            ));

            size_t index_register;
            switch(index.value.category) {
                case ValueCategory::Constant: {
                    index_register = append_constant(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        context->address_integer_size,
                        index_value.constant.integer
                    );
                } break;

                case ValueCategory::Anonymous: {
                    index_register = index_value.anonymous.register_;
                } break;

                case ValueCategory::Address: {
                    index_register = append_load_integer(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        context->address_integer_size,
                        index_value.address
                    );
                } break;

                default: {
                    abort();
                } break;
            }

            size_t base_address_register;
            Type element_type;
            bool assignable;
            switch(expression_value.value.category) {
                case ValueCategory::Constant: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_constant(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.constant.array.pointer
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            auto constant_name = register_static_array_constant(
                                context,
                                *expression_value.type.static_array.type,
                                Array<ConstantValue> {
                                    expression_value.type.static_array.length,
                                    expression_value.value.constant.static_array
                                }
                            );

                            base_address_register = append_reference_static(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                constant_name
                            );
                            element_type = *expression_value.type.static_array.type;
                            assignable = false;
                        } break;

                        default: {
                            error(*context, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ValueCategory::Anonymous: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_load_integer(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.anonymous.register_
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.value.anonymous.register_;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(*context, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                case ValueCategory::Address: {
                    switch(expression_value.type.category) {
                        case TypeCategory::Array: {
                            base_address_register = append_load_integer(
                                context,
                                instructions,
                                expression.index_reference.expression->range.first_line,
                                context->address_integer_size,
                                expression_value.value.address
                            );
                            element_type = *expression_value.type.array;
                            assignable = true;
                        } break;

                        case TypeCategory::StaticArray: {
                            base_address_register = expression_value.value.address;
                            element_type = *expression_value.type.static_array.type;
                            assignable = true;
                        } break;

                        default: {
                            error(*context, expression.index_reference.expression->range, "Cannot index %s", type_description(expression_value.type));

                            return { false };
                        } break;
                    }
                } break;

                default: {
                    abort();
                } break;
            }

            auto element_size_register = append_constant(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                context->address_integer_size,
                get_type_size(*context, element_type)
            );

            auto offset_register = append_arithmetic_operation(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                ArithmeticOperationType::UnsignedMultiply,
                context->address_integer_size,
                index_register,
                element_size_register
            );

            auto final_address_register = append_arithmetic_operation(
                context,
                instructions,
                expression.index_reference.index->range.first_line,
                ArithmeticOperationType::Add,
                context->address_integer_size,
                base_address_register,
                offset_register
            );

            TypedValue value;
            value.type = element_type;

            if(assignable) {
                value.value.category = ValueCategory::Address;
                value.value.address = final_address_register;
            } else {
                auto representation = get_type_representation(*context, element_type);

                size_t register_index;
                if(representation.is_in_register) {
                    register_index = append_load_integer(
                        context,
                        instructions,
                        expression.index_reference.index->range.first_line,
                        representation.value_size,
                        final_address_register
                    );
                } else {
                    register_index = final_address_register;
                }

                value.value.category = ValueCategory::Anonymous;
                value.value.anonymous.register_ = register_index;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::MemberReference: {
            expect(expression_value, generate_expression(context, instructions, *expression.member_reference.expression));

            TypedValue actual_expression_value;
            if(expression_value.type.category == TypeCategory::Pointer) {
                size_t address_register;
                switch(expression_value.value.category) {
                    case ValueCategory::Constant: {
                        address_register = append_constant(
                            context,
                            instructions,
                            expression.member_reference.expression->range.first_line,
                            context->address_integer_size,
                            expression_value.value.constant.pointer
                        );
                    } break;

                    case ValueCategory::Anonymous: {
                        address_register = expression_value.value.anonymous.register_;
                    } break;

                    case ValueCategory::Address: {
                        address_register = append_load_integer(
                            context,
                            instructions,
                            expression.member_reference.expression->range.first_line,
                            context->address_integer_size,
                            expression_value.value.address
                        );
                    } break;

                    default: {
                        abort();
                    } break;
                }

                actual_expression_value.value.category = ValueCategory::Address;
                actual_expression_value.type = *expression_value.type.pointer;
                actual_expression_value.value.address = address_register;
            } else {
                actual_expression_value = expression_value;
            }

            switch(actual_expression_value.type.category) {
                case TypeCategory::Array: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        TypedValue value;
                        value.value.category = actual_expression_value.value.category;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = {
                            context->address_integer_size,
                            false,
                            false
                        };

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                value.value.constant.integer = actual_expression_value.value.constant.array.length;
                            } break;

                            case ValueCategory::Anonymous: {
                                auto final_address_register = generate_address_offset(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range,
                                    expression_value.value.anonymous.register_,
                                    register_size_to_byte_size(context->address_integer_size)
                                );

                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    context->address_integer_size,
                                    final_address_register
                                );

                                value.value.anonymous.register_ = value_register;
                            } break;

                            case ValueCategory::Address: {
                                auto final_address_register = generate_address_offset(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range,
                                    expression_value.value.address,
                                    register_size_to_byte_size(context->address_integer_size)
                                );

                                value.value.address = final_address_register;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        TypedValue value;
                        value.value.category = actual_expression_value.value.category;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = actual_expression_value.type.array;

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                value.value.constant.pointer = actual_expression_value.value.constant.array.pointer;
                            } break;

                            case ValueCategory::Anonymous: {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    context->address_integer_size,
                                    actual_expression_value.value.anonymous.register_
                                );

                                value.value.anonymous.register_ = value_register;
                            } break;

                            case ValueCategory::Address: {
                                value.value.anonymous.register_ = actual_expression_value.value.address;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else {
                        error(*context, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::StaticArray: {
                    if(strcmp(expression.member_reference.name.text, "length") == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Constant;
                        value.type.category = TypeCategory::Integer;
                        value.type.integer = {
                            context->address_integer_size,
                            false,
                            false
                        };
                        value.value.constant.integer = actual_expression_value.type.static_array.length;

                        return {
                            true,
                            value
                        };
                    } else if(strcmp(expression.member_reference.name.text, "pointer") == 0) {
                        TypedValue value;
                        value.value.category = ValueCategory::Anonymous;
                        value.type.category = TypeCategory::Pointer;
                        value.type.pointer = actual_expression_value.type.static_array.type;

                        switch(actual_expression_value.value.category) {
                            case ValueCategory::Constant: {
                                auto constant_name = register_static_array_constant(
                                    context,
                                    *actual_expression_value.type.static_array.type,
                                    {
                                        actual_expression_value.type.static_array.length,
                                        actual_expression_value.value.constant.static_array
                                    }
                                );

                                auto register_index = append_reference_static(
                                    context,
                                    instructions,
                                    expression.member_reference.name.range.first_line,
                                    constant_name
                                );

                                value.value.anonymous.register_ = register_index;
                            } break;

                            case ValueCategory::Anonymous: {
                                value.value.anonymous.register_ = actual_expression_value.value.anonymous.register_;
                            } break;

                            case ValueCategory::Address: {
                                value.value.anonymous.register_ = actual_expression_value.value.address;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        return {
                            true,
                            value
                        };
                    } else {
                        error(*context, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                        return { false };
                    }
                } break;

                case TypeCategory::Struct: {
                    if(actual_expression_value.type._struct.is_undetermined) {
                        for(size_t i = 0; i < actual_expression_value.type._struct.concrete.count; i += 1) {
                            if(strcmp(actual_expression_value.type._struct.concrete[i].name, expression.member_reference.name.text) == 0) {
                                expect(member_type, coerce_to_default_type(
                                    *context,
                                    expression.member_reference.expression->range,
                                    actual_expression_value.type._struct.concrete[i].type
                                ));

                                TypedValue value;
                                value.type = member_type;

                                switch(actual_expression_value.value.category) {
                                    case ValueCategory::Constant: {
                                        value.value.category = ValueCategory::Constant;
                                        value.value.constant = actual_expression_value.value.constant.struct_[i];
                                    } break;

                                    case ValueCategory::Anonymous: {
                                        value.value.category = ValueCategory::Anonymous;

                                        auto member_value = actual_expression_value.value.anonymous.undetermined_struct[i];

                                        auto representation = get_type_representation(*context, member_type);

                                        switch(member_value.category) {
                                            case ValueCategory::Constant: {
                                                size_t result_register;
                                                if(representation.is_in_register) {
                                                    result_register = generate_in_register_constant_value(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range,
                                                        member_type,
                                                        member_value.constant
                                                    );
                                                } else {
                                                    result_register = generate_not_in_register_constant_value(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range,
                                                        member_type,
                                                        member_value.constant
                                                    );
                                                }

                                                value.value.anonymous.register_ = result_register;
                                            } break;

                                            case ValueCategory::Anonymous: {
                                                value.value.anonymous.register_ = member_value.anonymous.register_;
                                            } break;

                                            case ValueCategory::Address: {
                                                if(representation.is_in_register) {
                                                    value.value.anonymous.register_ = append_load_integer(
                                                        context,
                                                        instructions,
                                                        expression.member_reference.name.range.first_line,
                                                        representation.value_size,
                                                        member_value.address
                                                    );
                                                } else {
                                                    value.value.anonymous.register_ = member_value.address;
                                                }
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
                            }
                        }
                    } else {
                        for(size_t i = 0; i < actual_expression_value.type._struct.concrete.count; i += 1) {
                            if(strcmp(actual_expression_value.type._struct.concrete[i].name, expression.member_reference.name.text) == 0) {
                                TypedValue value;
                                value.value.category = expression_value.value.category;
                                value.type = actual_expression_value.type._struct.concrete[i].type;

                                auto offset = get_struct_member_offset(*context, actual_expression_value.type, i);

                                switch(expression_value.value.category) {
                                    case ValueCategory::Constant: {
                                        value.value.constant = expression_value.value.constant.struct_[i];
                                    } break;

                                    case ValueCategory::Anonymous: {
                                        auto final_address_register = generate_address_offset(
                                            context,
                                            instructions,
                                            expression.member_reference.name.range,
                                            expression_value.value.anonymous.register_,
                                            offset
                                        );

                                        auto representation = get_type_representation(*context, actual_expression_value.type._struct.concrete[i].type);

                                        if(representation.is_in_register) {
                                            auto value_register = append_load_integer(
                                                context,
                                                instructions,
                                                expression.member_reference.name.range.first_line,
                                                representation.value_size,
                                                final_address_register
                                            );

                                            value.value.anonymous.register_ = value_register;
                                        } else {
                                            value.value.anonymous.register_ = final_address_register;
                                        }
                                    } break;

                                    case ValueCategory::Address: {
                                        auto final_address_register = generate_address_offset(
                                            context,
                                            instructions,
                                            expression.member_reference.name.range,
                                            expression_value.value.address,
                                            offset
                                        );

                                        value.value.address = final_address_register;
                                    } break;

                                    default: {
                                        abort();
                                    } break;
                                }

                                return {
                                    true,
                                    value
                                };
                            }
                        }
                    }

                    error(*context, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                case TypeCategory::FileModule: {
                    for(auto statement : expression_value.value.constant.file_module.statements) {
                        Statement declaration;

                        if(match_declaration(statement, expression.member_reference.name.text)) {
                            declaration = statement;
                        } else if(statement.type == StatementType::Using) {
                            expect(expression_value, evaluate_constant_expression(context, statement.using_));

                            if(expression_value.type.category != TypeCategory::FileModule) {
                                error(*context, statement.using_.range, "Expected a module, got '%s'", type_description(expression_value.type));

                                return { false };
                            }

                            auto found = false;
                            for(auto statement : expression_value.value.file_module.statements) {
                                if(match_declaration(statement, expression.member_reference.name.text)) {
                                    declaration = statement;

                                    found = true;
                                }
                            }

                            if(!found) {
                                continue;
                            }
                        } else {
                            continue;
                        }

                        auto old_is_top_level = context->is_top_level;
                        auto old_constant_parameters = context->constant_parameters;

                        DeterminedDeclaration old_determined_declaration;
                        Array<Statement> old_top_level_statements;
                        const char *old_file_path;
                        if(context->is_top_level) {
                            old_top_level_statements = context->top_level_statements;
                            old_file_path = context->file_path;
                        } else {
                            old_determined_declaration = context->determined_declaration;
                        }

                        context->is_top_level = true;
                        context->top_level_statements = expression_value.value.constant.file_module.statements;
                        context->file_path = expression_value.value.constant.file_module.path;
                        context->constant_parameters = {};

                        TypedConstantValue value;
                        switch(declaration.type) {
                            case StatementType::FunctionDeclaration: {
                                expect(function_value, evaluate_function_declaration(context, declaration));

                                value = function_value;
                            } break;

                            case StatementType::ConstantDefinition: {
                                expect(expression_value, evaluate_constant_expression(context, declaration.constant_definition.expression));

                                value = expression_value;
                            } break;

                            case StatementType::Import: {
                                error(*context, expression.member_reference.expression->range, "Cannot access module member '%s'. Imports are private", expression.member_reference.name.text);

                                return { false };
                            } break;

                            case StatementType::StructDefinition: {
                                expect(struct_value, evaluate_struct_definition(context, declaration));

                                value = struct_value;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        context->is_top_level = old_is_top_level;
                        context->constant_parameters = old_constant_parameters;
                        if(context->is_top_level) {
                            context->top_level_statements = old_top_level_statements;
                            context->file_path = old_file_path;
                        } else {
                            context->determined_declaration = old_determined_declaration;
                        }

                        TypedValue result;
                        result.type = value.type;
                        result.value.category = ValueCategory::Constant;
                        result.value.constant = value.value;

                        return {
                            true,
                            result
                        };
                    }

                    error(*context, expression.member_reference.name.range, "No member with name %s", expression.member_reference.name.text);

                    return { false };
                } break;

                default: {
                    error(*context, expression.member_reference.expression->range, "Type %s has no members", type_description(actual_expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::IntegerLiteral: {
            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::Integer;
            value.type.integer.is_undetermined = true;
            value.value.constant.integer = expression.integer_literal;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::StringLiteral: {      
            Type array_type;
            array_type.category = TypeCategory::Integer;
            array_type.integer = {
                RegisterSize::Size8,
                false,
                false
            };

            auto characters = allocate<ConstantValue>(expression.string_literal.count);

            for(size_t i = 0; i < expression.string_literal.count; i += 1) {
                characters[i].integer = expression.string_literal[i];
            }

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::StaticArray;
            value.type.static_array = {
                expression.string_literal.count,
                heapify(array_type)
            };
            value.value.constant.static_array = characters;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayLiteral: {
            if(expression.array_literal.count == 0) {
                error(*context, expression.range, "Empty array literal");

                return { false };
            }

            auto element_values = allocate<TypedValue>(expression.array_literal.count);
            expect(first_element_value, generate_expression(context, instructions, expression.array_literal[0]));
            element_values[0] = first_element_value;

            auto all_constant = first_element_value.value.category == ValueCategory::Constant;
            for(size_t i = 1; i < expression.array_literal.count; i += 1) {
                expect(element_value, generate_expression(context, instructions, expression.array_literal[i]));

                if(element_value.value.category != ValueCategory::Constant) {
                    all_constant = false;
                }

                element_values[i] = element_value;
            }

            expect(determined_element_type, coerce_to_default_type(*context, expression.range, first_element_value.type));

            if(!is_runtime_type(determined_element_type)) {
                error(*context, expression.range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

                return { false };
            }

            if(all_constant) {
                auto elements = allocate<ConstantValue>(expression.array_literal.count);

                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    expect(element_value, coerce_constant_to_type(
                        *context,
                        expression.array_literal.elements[i].range,
                        { element_values[i].type, element_values[i].value.constant },
                        determined_element_type,
                        false
                    ));

                    elements[i] = element_value;
                }

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(determined_element_type)
                };
                value.value.constant.static_array = elements;

                return {
                    true,
                    value
                };
            } else {
                auto element_size = get_type_size(*context, determined_element_type);

                auto base_address_register = append_allocate_local(
                    context,
                    instructions,
                    expression.range.first_line,
                    element_size * expression.array_literal.count,
                    get_type_alignment(*context, determined_element_type)
                );

                auto element_size_register = append_constant(
                    context,
                    instructions,
                    expression.range.first_line,
                    context->address_integer_size,
                    element_size
                );

                auto representation = get_type_representation(*context, determined_element_type);

                size_t length_register;
                if(representation.is_in_register) {
                    length_register = append_constant(
                        context,
                        instructions,
                        expression.range.first_line,
                        context->address_integer_size,
                        get_type_size(*context, determined_element_type)
                    );
                }

                auto address_register = base_address_register;
                for(size_t i = 0; i < expression.array_literal.count; i += 1) {
                    expect(element_value, coerce_to_type(
                        context,
                        instructions,
                        expression.array_literal.elements[i].range,
                        element_values[i],
                        determined_element_type,
                        false
                    ));

                    switch(element_values[i].value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(context,
                            instructions,
                            expression.array_literal[i].range,
                            determined_element_type,
                            element_value.constant,
                            address_register
                        );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    element_value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                append_copy_memory(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    length_register,
                                    element_value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    element_value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                append_copy_memory(
                                    context,
                                    instructions,
                                    expression.array_literal[i].range.first_line,
                                    length_register,
                                    element_value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    if(i != expression.array_literal.count - 1) {
                        auto new_address_register = append_arithmetic_operation(
                            context,
                            instructions,
                            expression.array_literal[i + 1].range.first_line,
                            ArithmeticOperationType::Add,
                            context->address_integer_size,
                            address_register,
                            element_size_register
                        );

                        address_register = new_address_register;
                    }
                }

                TypedValue value;
                value.value.category = ValueCategory::Anonymous;
                value.type.category = TypeCategory::StaticArray;
                value.type.static_array = {
                    expression.array_literal.count,
                    heapify(determined_element_type)
                };
                value.value.anonymous.register_ = base_address_register;

                return {
                    true,
                    value
                };
            }
        } break;

        case ExpressionType::StructLiteral: {
            if(expression.struct_literal.count == 0) {
                error(*context, expression.range, "Empty struct literal");

                return { false };
            }

            auto type_members = allocate<StructTypeMember>(expression.struct_literal.count);
            auto member_values = allocate<Value>(expression.struct_literal.count);
            auto all_constant = true;

            for(size_t i = 0; i < expression.struct_literal.count; i += 1) {
                for(size_t j = 0; j < i; j += 1) {
                    if(strcmp(expression.struct_literal[i].name.text, type_members[j].name) == 0) {
                        error(*context, expression.struct_literal[i].name.range, "Duplicate struct member %s", expression.struct_literal[i].name.text);

                        return { false };
                    }
                }

                expect(member, generate_expression(context, instructions, expression.struct_literal[i].value));

                type_members[i] = {
                    expression.struct_literal[i].name.text,
                    member.type
                };

                member_values[i] = member.value;

                if(member.value.category != ValueCategory::Constant) {
                    all_constant = false;
                }
            }

            TypedValue value;
            value.type.category = TypeCategory::Struct;
            value.type._struct.is_undetermined = true;
            value.type._struct.concrete = {
                expression.struct_literal.count,
                type_members
            };

            if(all_constant) {
                auto constant_member_values = allocate<ConstantValue>(expression.struct_literal.count);

                for(size_t i = 0; i < expression.struct_literal.count; i += 1) {
                    constant_member_values[i] = member_values[i].constant;
                }

                value.value.category = ValueCategory::Constant;
                value.value.constant.struct_ = constant_member_values;
            } else {
                value.value.category = ValueCategory::Anonymous;
                value.value.anonymous.undetermined_struct = member_values;
            }

            return {
                true,
                value
            };
        } break;

        case ExpressionType::FunctionCall: {
            expect(expression_value, generate_expression(context, instructions, *expression.function_call.expression));

            switch(expression_value.type.category) {
                case TypeCategory::Function: {
                    if(expression.function_call.parameters.count != expression_value.type.function.parameter_count) {
                        error(*context, expression.range, "Incorrect number of parameters. Expected %zu, got %zu", expression_value.type.function.parameter_count, expression.function_call.parameters.count);

                        return { false };
                    }

                    auto parameter_count = expression.function_call.parameters.count;

                    auto function_declaration = expression_value.value.constant.function.declaration.function_declaration;

                    const char *function_name;
                    auto function_parameter_values = allocate<TypedValue>(parameter_count);
                    Type *function_parameter_types;
                    Type function_return_type;

                    struct PolymorphicDeterminer {
                        const char *name;

                        Type type;
                    };

                    if(expression_value.type.function.is_polymorphic) {
                        List<PolymorphicDeterminer> polymorphic_determiners{};

                        for(size_t i = 0; i < parameter_count; i += 1) {
                            auto parameter = function_declaration.parameters[i];

                            if(parameter.is_polymorphic_determiner) {
                                for(auto polymorphic_determiner : polymorphic_determiners) {
                                    if(strcmp(polymorphic_determiner.name, parameter.polymorphic_determiner.text) == 0) {
                                        error(*context, parameter.polymorphic_determiner.range, "Duplicate polymorphic parameter %s", parameter.polymorphic_determiner.text);

                                        return { false };
                                    }
                                }

                                expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                                expect(determined_type, coerce_to_default_type(*context, expression.function_call.parameters[i].range, value.type));

                                if(!is_runtime_type(determined_type)) {
                                    error(*context, expression.function_call.parameters[i].range, "Function parameters cannot be of type '%s'", type_description(determined_type));

                                    return { false };
                                }

                                append(&polymorphic_determiners, {
                                    parameter.polymorphic_determiner.text,
                                    determined_type
                                });

                                function_parameter_values[i] = value;
                            }
                        }

                        function_parameter_types = allocate<Type>(parameter_count);

                        auto old_is_top_level = context->is_top_level;
                        auto old_constant_parameters = context->constant_parameters;
                        
                        DeterminedDeclaration old_determined_declaration;
                        Array<Statement> old_top_level_statements;
                        const char *old_file_path;
                        if(context->is_top_level) {
                            old_top_level_statements = context->top_level_statements;
                            old_file_path = context->file_path;
                        } else {
                            old_determined_declaration = context->determined_declaration;
                        }

                        auto constant_parameters = allocate<ConstantParameter>(polymorphic_determiners.count);

                        for(size_t i = 0; i < polymorphic_determiners.count; i += 1) {
                            TypedConstantValue value;
                            value.type.category = TypeCategory::Type;
                            value.value.type = polymorphic_determiners[i].type;

                            constant_parameters[i] = {
                                polymorphic_determiners[i].name,
                                value
                            };
                        }

                        context->is_top_level = expression_value.value.constant.function.declaration.is_top_level;
                        context->constant_parameters = {
                            polymorphic_determiners.count,
                            constant_parameters
                        };

                        if(expression_value.value.constant.function.declaration.is_top_level) {
                            context->file_path = expression_value.value.constant.function.file_path;
                            context->top_level_statements = expression_value.value.constant.function.top_level_statements;
                        } else {
                            context->determined_declaration = expression_value.value.constant.function.parent;
                        }

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

                        context->is_top_level = old_is_top_level;
                        context->constant_parameters = old_constant_parameters;
                        if(context->is_top_level) {
                            context->top_level_statements = old_top_level_statements;
                            context->file_path = old_file_path;
                        } else {
                            context->determined_declaration = old_determined_declaration;
                        }

                        if(function_declaration.is_external) {
                            function_name = function_declaration.name.text;

                            for(auto library : function_declaration.external_libraries) {
                                auto has_library = false;
                                for(auto library : context->libraries) {
                                    if(strcmp(library, library) == 0) {
                                        has_library = true;

                                        break;
                                    }
                                }

                                append(&context->libraries, library);
                            }
                        } else {
                            char *mangled_name_buffer{};

                            string_buffer_append(&mangled_name_buffer, "function_");

                            char buffer[32];
                            sprintf(buffer, "%zu", context->runtime_functions.count);
                            string_buffer_append(&mangled_name_buffer, buffer);

                            function_name = mangled_name_buffer;
                        }

                        auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(expression.function_call.parameters.count);

                        for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                            runtime_function_parameters[i] = {
                                function_declaration.parameters[i].name,
                                function_parameter_types[i],
                                expression_value.value.constant.function.declaration.function_declaration.parameters[i].type.range
                            };
                        }

                        RuntimeFunction runtime_function;
                        runtime_function.mangled_name = function_name;
                        runtime_function.parameters = {
                            expression.function_call.parameters.count,
                            runtime_function_parameters
                        };
                        runtime_function.return_type = function_return_type;
                        runtime_function.declaration.declaration = expression_value.value.constant.function.declaration;
                        runtime_function.declaration.constant_parameters = {
                            polymorphic_determiners.count,
                            constant_parameters
                        };

                        if(expression_value.value.constant.function.declaration.is_top_level) {
                            runtime_function.declaration.file_path = expression_value.value.constant.function.file_path;
                            runtime_function.declaration.top_level_statements = expression_value.value.constant.function.top_level_statements;
                        } else {
                            runtime_function.declaration.parent = heapify(expression_value.value.constant.function.parent);
                        }

                        append(&context->runtime_functions, runtime_function);

                        if(!register_global_name(context, function_name, function_declaration.name.range)) {
                            return { false };
                        }
                    } else {
                        for(size_t i = 0; i < expression.function_call.parameters.count; i += 1) {
                            expect(value, generate_expression(context, instructions, expression.function_call.parameters[i]));

                            function_parameter_values[i] = value;
                        }

                        if(function_declaration.is_external) {
                            function_name = function_declaration.name.text;

                            for(auto library : function_declaration.external_libraries) {
                                auto has_library = false;
                                for(auto library : context->libraries) {
                                    if(strcmp(library, library) == 0) {
                                        has_library = true;

                                        break;
                                    }
                                }

                                append(&context->libraries, library);
                            }
                        } else {
                            function_name = generate_mangled_name(*context, expression_value.value.constant.function.declaration);
                        }

                        function_parameter_types = expression_value.type.function.parameters;
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
                            runtime_function.declaration.declaration = expression_value.value.constant.function.declaration;
                            runtime_function.declaration.constant_parameters = {};

                            if(expression_value.value.constant.function.declaration.is_top_level) {
                                runtime_function.declaration.file_path = expression_value.value.constant.function.file_path;
                                runtime_function.declaration.top_level_statements = expression_value.value.constant.function.top_level_statements;
                            } else {
                                runtime_function.declaration.parent = heapify(expression_value.value.constant.function.parent);
                            }

                            append(&context->runtime_functions, runtime_function);

                            if(!register_global_name(context, function_name, function_declaration.name.range)) {
                                return { false };
                            }
                        }
                    }

                    auto total_parameter_count = parameter_count;

                    bool has_return;
                    bool is_return_in_register;
                    if(function_return_type.category == TypeCategory::Void) {
                        has_return = false;
                    } else {
                        has_return = true;

                        auto representation = get_type_representation(*context, function_return_type);

                        is_return_in_register = representation.is_in_register;

                        if(!representation.is_in_register) {
                            total_parameter_count += 1;
                        }
                    }

                    auto function_parameter_registers = allocate<size_t>(total_parameter_count);

                    for(size_t i = 0; i < parameter_count; i += 1) {
                        expect(parameter_value, coerce_to_type(
                            context,
                            instructions,
                            expression.function_call.parameters[i].range,
                            function_parameter_values[i],
                            function_parameter_types[i],
                            false
                        ));

                        auto representation = get_type_representation(*context, function_parameter_types[i]);

                        switch(parameter_value.category) {
                            case ValueCategory::Constant: {
                                if(representation.is_in_register) {
                                    function_parameter_registers[i] = generate_in_register_constant_value(
                                        context,
                                        instructions,
                                        expression.function_call.parameters[i].range,
                                        function_parameter_types[i],
                                        parameter_value.constant
                                    );
                                } else {
                                    function_parameter_registers[i] = generate_not_in_register_constant_value(
                                        context,
                                        instructions,
                                        expression.function_call.parameters[i].range,
                                        function_parameter_types[i],
                                        parameter_value.constant
                                    );
                                }
                            } break;

                            case ValueCategory::Anonymous: {
                                function_parameter_registers[i] = parameter_value.anonymous.register_;
                            } break;

                            case ValueCategory::Address: {
                                if(representation.is_in_register) {
                                    auto value_register = append_load_integer(
                                        context,
                                        instructions,
                                        expression.function_call.parameters[i].range.first_line,
                                        representation.value_size,
                                        parameter_value.address
                                    );

                                    function_parameter_registers[i] = value_register;
                                } else {
                                    function_parameter_registers[i] = parameter_value.address;
                                }
                            } break;

                            default: {
                                abort();
                            } break;
                        }
                    }

                    size_t return_register;

                    if(has_return && !is_return_in_register) {
                        auto local_register = append_allocate_local(
                            context,
                            instructions,
                            expression.function_call.expression->range.first_line,
                            get_type_size(*context, function_return_type),
                            get_type_alignment(*context, function_return_type)
                        );

                        function_parameter_registers[total_parameter_count - 1] = local_register;
                        return_register = local_register;
                    }

                    Instruction call;
                    call.type = InstructionType::FunctionCall;
                    call.line = expression.range.first_line;
                    call.function_call.function_name = function_name;
                    call.function_call.parameter_registers = {
                        total_parameter_count,
                        function_parameter_registers
                    };
                    if(has_return && is_return_in_register) {
                        return_register = allocate_register(context);

                        call.function_call.has_return = true;
                        call.function_call.return_register = return_register;
                    } else {
                        call.function_call.has_return = false;
                    }

                    append(instructions, call);

                    TypedValue value;
                    value.value.category = ValueCategory::Anonymous;
                    value.type = function_return_type;
                    if(has_return) {
                        value.value.anonymous.register_ = return_register;
                    }

                    return {
                        true,
                        value
                    };
                } break;

                case TypeCategory::Type: {
                    auto type = expression_value.value.constant.type;

                    if(type.category != TypeCategory::Struct) {
                        error(*context, expression.function_call.expression->range, "Type '%s' is not polymorphic", type_description(type));

                        return { false };
                    }

                    if(!type._struct.is_polymorphic) {
                        error(*context, expression.function_call.expression->range, "Struct is not polymorphic");

                        return { false };
                    }

                    auto parameter_count = type._struct.polymorphic.declaration.struct_definition.parameters.count;

                    if(expression.function_call.parameters.count != parameter_count) {
                        error(*context, expression.range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, expression.function_call.parameters.count);

                        return { false };
                    }

                    auto parameters = allocate<ConstantParameter>(parameter_count);

                    for(size_t i = 0; i < parameter_count; i += 1) {
                        expect(parameter, evaluate_constant_expression(context, expression.function_call.parameters[i]));

                        expect(parameter_value, coerce_constant_to_type(
                            *context,
                            expression.function_call.parameters[i].range,
                            parameter,
                            type._struct.polymorphic.parameters[i].type,
                            false
                        ));

                        parameters[i] = {
                            type._struct.polymorphic.parameters[i].name,
                            {
                                type._struct.polymorphic.parameters[i].type,
                                parameter_value
                            }
                        };
                    }

                    auto old_is_top_level = context->is_top_level;
                    auto old_constant_parameters = context->constant_parameters;

                    DeterminedDeclaration old_determined_declaration;
                    Array<Statement> old_top_level_statements;
                    const char *old_file_path;
                    if(context->is_top_level) {
                        old_top_level_statements = context->top_level_statements;
                        old_file_path = context->file_path;
                    } else {
                        old_determined_declaration = context->determined_declaration;
                    }

                    context->is_top_level = false;
                    context->determined_declaration.declaration = type._struct.polymorphic.declaration;
                    if(type._struct.polymorphic.declaration.is_top_level) {
                        context->determined_declaration.file_path = type._struct.polymorphic.file_path;
                        context->determined_declaration.top_level_statements = type._struct.polymorphic.top_level_statements;
                    } else {
                        context->determined_declaration.parent = heapify(type._struct.polymorphic.parent);
                    }
                    context->constant_parameters = {
                        parameter_count,
                        parameters
                    };

                    auto members = allocate<StructTypeMember>(type._struct.polymorphic.declaration.struct_definition.members.count);

                    for(size_t i = 0; i < type._struct.polymorphic.declaration.struct_definition.members.count; i += 1) {
                        expect(member_type, evaluate_type_expression(context, type._struct.polymorphic.declaration.struct_definition.members[i].type));

                        expect(determined_type,
                        coerce_to_default_type(
                            *context,
                            type._struct.polymorphic.declaration.struct_definition.members[i].type.range,
                            member_type
                        ));

                        if(!is_runtime_type(determined_type)) {
                            error(*context, type._struct.polymorphic.declaration.struct_definition.members[i].type.range, "Struct members cannot be of type '%s'", type_description(determined_type));

                            return { false };
                        }

                        members[i] = {
                            type._struct.polymorphic.declaration.struct_definition.members[i].name.text,
                            determined_type
                        };
                    }

                    auto mangled_name = generate_mangled_name(*context, type._struct.polymorphic.declaration);

                    context->is_top_level = old_is_top_level;
                    context->constant_parameters = old_constant_parameters;
                    if(context->is_top_level) {
                        context->top_level_statements = old_top_level_statements;
                        context->file_path = old_file_path;
                    } else {
                        context->determined_declaration = old_determined_declaration;
                    }

                    TypedValue value;
                    value.type.category = TypeCategory::Type;
                    value.value.constant.type.category = TypeCategory::Struct;
                    value.value.constant.type._struct.is_polymorphic = false;
                    value.value.constant.type._struct.is_undetermined = false;
                    value.value.constant.type._struct.name = mangled_name;
                    value.value.constant.type._struct.is_union = type._struct.polymorphic.declaration.struct_definition.is_union;
                    value.value.constant.type._struct.concrete = {
                        type._struct.polymorphic.declaration.struct_definition.members.count,
                        members
                    };

                    return {
                        true,
                        value
                    };
                } break;

                default: {
                    error(*context, expression.function_call.expression->range, "Cannot call %s", type_description(expression_value.type));

                    return { false };
                } break;
            }
        } break;

        case ExpressionType::BinaryOperation: {
            expect(left, generate_expression(context, instructions, *expression.binary_operation.left));

            expect(right, generate_expression(context, instructions, *expression.binary_operation.right));

            if(left.value.category == ValueCategory::Constant && right.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_binary_operation(
                    *context,
                    expression.range,
                    expression.binary_operation.binary_operator,
                    expression.binary_operation.left->range,
                    left.type,
                    left.value.constant,
                    expression.binary_operation.right->range,
                    right.type,
                    right.value.constant
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = constant.type;
                value.value.constant = constant.value;

                return {
                    true,
                    value
                };
            } else {
                expect(type, determine_binary_operation_type(*context, expression.range, left.type, right.type));

                expect(left_value, coerce_to_type(context, instructions, expression.binary_operation.left->range, left, type, false));

                expect(right_value, coerce_to_type(context, instructions, expression.binary_operation.right->range, right, type, false));

                size_t result_register;
                Type result_type;
                switch(left.type.category) {
                    case TypeCategory::Integer: {
                        auto left_register = generate_in_register_integer_value(
                            context,
                            instructions,
                            expression.binary_operation.left->range,
                            type.integer.size,
                            left_value
                        );

                        auto right_register = generate_in_register_integer_value(
                            context,
                            instructions,
                            expression.binary_operation.right->range,
                            type.integer.size,
                            right_value
                        );

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Addition: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::Add,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Subtraction: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::Subtract,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Multiplication: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedMultiply;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedMultiply;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Division: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedDivide;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedDivide;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Modulo: {
                                ArithmeticOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ArithmeticOperationType::SignedModulus;
                                } else {
                                    operation_type = ArithmeticOperationType::UnsignedModulus;
                                }

                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::BitwiseAnd: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseAnd,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::BitwiseOr: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseOr,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type = type;
                            } break;

                            case BinaryOperator::Equal: {
                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::NotEqual: {
                                auto equal_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_register = generate_boolean_invert(context,  instructions, expression.range, equal_register);

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::LessThan: {
                                ComparisonOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ComparisonOperationType::SignedLessThan;
                                } else {
                                    operation_type = ComparisonOperationType::UnsignedLessThan;
                                }

                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            case BinaryOperator::GreaterThan: {
                                ComparisonOperationType operation_type;
                                if(type.integer.is_signed) {
                                    operation_type = ComparisonOperationType::SignedGreaterThan;
                                } else {
                                    operation_type = ComparisonOperationType::UnsignedGreaterThan;
                                }

                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    operation_type,
                                    type.integer.size,
                                    left_register,
                                    right_register
                                );

                                result_type.category = TypeCategory::Boolean;
                            } break;

                            default: {
                                error(*context, expression.range, "Cannot perform that operation on integers");

                                return { false };
                            } break;
                        }
                    } break;

                    case TypeCategory::Boolean: {
                        auto left_register = generate_in_register_boolean_value(
                            context,
                            instructions,
                            expression.binary_operation.left->range,
                            left_value
                        );

                        auto right_register = generate_in_register_boolean_value(
                            context,
                            instructions,
                            expression.binary_operation.right->range,
                            right_value
                        );

                        result_type.category = TypeCategory::Boolean;

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Equal: {
                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            case BinaryOperator::NotEqual: {
                                auto equal_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );

                                result_register = generate_boolean_invert(context, instructions, expression.range, equal_register);
                            } break;

                            case BinaryOperator::BooleanAnd: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseAnd,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            case BinaryOperator::BooleanOr: {
                                result_register = append_arithmetic_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ArithmeticOperationType::BitwiseOr,
                                    context->default_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            default: {
                                error(*context, expression.range, "Cannot perform that operation on booleans");

                                return { false };
                            } break;
                        }
                    } break;

                    case TypeCategory::Pointer: {
                        auto left_register = generate_in_register_pointer_value(
                            context,
                            instructions,
                            expression.binary_operation.left->range,
                            left_value
                        );

                        auto right_register = generate_in_register_pointer_value(
                            context,
                            instructions,
                            expression.binary_operation.right->range,
                            right_value
                        );

                        result_type.category = TypeCategory::Boolean;

                        switch(expression.binary_operation.binary_operator) {
                            case BinaryOperator::Equal: {
                                result_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->address_integer_size,
                                    left_register,
                                    right_register
                                );
                            } break;

                            case BinaryOperator::NotEqual: {
                                auto equal_register = append_comparison_operation(
                                    context,
                                    instructions,
                                    expression.range.first_line,
                                    ComparisonOperationType::Equal,
                                    context->address_integer_size,
                                    left_register,
                                    right_register
                                );

                                result_register = generate_boolean_invert(context, instructions, expression.range, equal_register);
                            } break;

                            default: {
                                error(*context, expression.range, "Cannot perform that operation on pointers");

                                return { false };
                            } break;
                        }
                    } break;
                }

                TypedValue value;
                value.value.category = ValueCategory::Anonymous;
                value.type = result_type;
                value.value.anonymous.register_ = result_register;

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
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            switch(expression_value.type.category) {
                                case TypeCategory::Function: {
                                    auto function_declaration = expression_value.value.constant.function.declaration.function_declaration;

                                    if(expression_value.type.function.is_polymorphic) {
                                        error(*context, expression.unary_operation.expression->range, "Cannot take pointers to polymorphic functions");

                                        return { false };
                                    }

                                    const char *function_name;
                                    if(function_declaration.is_external) {
                                        function_name = function_declaration.name.text;

                                        for(auto library : function_declaration.external_libraries) {
                                            auto has_library = false;
                                            for(auto library : context->libraries) {
                                                if(strcmp(library, library) == 0) {
                                                    has_library = true;

                                                    break;
                                                }
                                            }

                                            append(&context->libraries, library);
                                        }
                                    } else {
                                        function_name = generate_mangled_name(*context, expression_value.value.constant.function.declaration);
                                    }

                                    auto is_registered = false;
                                    for(auto function : context->runtime_functions) {
                                        if(strcmp(function.mangled_name, function_name) == 0) {
                                            is_registered = true;

                                            break;
                                        }
                                    }

                                    if(!is_registered) {
                                        auto runtime_function_parameters = allocate<RuntimeFunctionParameter>(function_declaration.parameters.count);

                                        for(size_t i = 0; i < function_declaration.parameters.count; i += 1) {
                                            runtime_function_parameters[i] = {
                                                function_declaration.parameters[i].name,
                                                expression_value.type.function.parameters[i]
                                            };
                                        }

                                        RuntimeFunction runtime_function;
                                        runtime_function.mangled_name = function_name;
                                        runtime_function.parameters = {
                                            function_declaration.parameters.count,
                                            runtime_function_parameters
                                        };
                                        runtime_function.return_type = *expression_value.type.function.return_type;
                                        runtime_function.declaration.declaration = expression_value.value.constant.function.declaration;
                                        runtime_function.declaration.constant_parameters = {};

                                        if(expression_value.value.constant.function.declaration.is_top_level) {
                                            runtime_function.declaration.file_path = expression_value.value.constant.function.file_path;
                                            runtime_function.declaration.top_level_statements = expression_value.value.constant.function.top_level_statements;
                                        } else {
                                            runtime_function.declaration.parent = heapify(expression_value.value.constant.function.parent);
                                        }

                                        append(&context->runtime_functions, runtime_function);

                                        if(!register_global_name(context, function_name, function_declaration.name.range)) {
                                            return { false };
                                        }
                                    }

                                    auto address_regsiter = append_reference_static(
                                        context,
                                        instructions,
                                        expression.range.first_line,
                                        function_name
                                    );

                                    TypedValue value;
                                    value.value.category = ValueCategory::Anonymous;
                                    value.type.category = TypeCategory::Pointer;
                                    value.type.pointer = heapify(expression_value.type);
                                    value.value.anonymous.register_ = address_regsiter;

                                    return {
                                        true,
                                        value
                                    };
                                } break;

                                case TypeCategory::Type: {
                                    if(
                                        !is_runtime_type(expression_value.value.constant.type) &&
                                        expression_value.value.constant.type.category != TypeCategory::Void &&
                                        !(
                                            expression_value.value.constant.type.category == TypeCategory::Function &&
                                            !expression_value.value.constant.type.function.is_polymorphic
                                        )
                                    ) {
                                        error(*context, expression.unary_operation.expression->range, "Cannot create pointers to type '%s'", type_description(expression_value.value.constant.type));

                                        return { false };
                                    }

                                    TypedValue value;
                                    value.value.category = ValueCategory::Constant;
                                    value.type.category = TypeCategory::Type;
                                    value.value.constant.type.category = TypeCategory::Pointer;
                                    value.value.constant.type.pointer = heapify(expression_value.value.constant.type);

                                    return {
                                        true,
                                        value
                                    };
                                } break;

                                default: {
                                    error(*context, expression.unary_operation.expression->range, "Cannot take pointers to constants of type %s", type_description(expression_value.type));

                                    return { false };
                                }
                            }
                        } break;

                        case ValueCategory::Anonymous: {
                            error(*context, expression.unary_operation.expression->range, "Cannot take pointers to anonymous values");

                            return { false };
                        } break;

                        case ValueCategory::Address: {
                            if(!is_runtime_type(expression_value.type)) {
                                error(*context, expression.unary_operation.expression->range, "Cannot take pointers to values of type '%s'", type_description(expression_value.type));

                                return { false };
                            }

                            TypedValue value;
                            value.value.category = ValueCategory::Anonymous;
                            value.type.category = TypeCategory::Pointer;
                            value.type.pointer = heapify(expression_value.type);
                            value.value.anonymous.register_ = expression_value.value.anonymous.register_;

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

                case UnaryOperator::BooleanInvert: {
                    if(expression_value.type.category != TypeCategory::Boolean) {
                        error(*context, expression.unary_operation.expression->range, "Expected a boolean, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    TypedValue value;
                    value.type.category = TypeCategory::Boolean;

                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            value.value.category = ValueCategory::Constant;
                            value.value.constant.boolean = !expression_value.value.constant.boolean;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.unary_operation.expression->range.first_line,
                                context->default_integer_size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    auto result_register = generate_boolean_invert(
                        context,
                        instructions,
                        expression.range,
                        value_register
                    );

                    value.value.category = ValueCategory::Anonymous;
                    value.value.anonymous.register_ = result_register;

                    return {
                        true,
                        value
                    };
                } break;

                case UnaryOperator::Negation: {
                    if(expression_value.type.category != TypeCategory::Integer) {
                        error(*context, expression.unary_operation.expression->range, "Expected an integer, got %s", type_description(expression_value.type));

                        return { false };
                    }

                    TypedValue value;
                    value.type.category = TypeCategory::Integer;
                    value.type.integer = expression_value.type.integer;

                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Constant: {
                            value.value.category = ValueCategory::Constant;
                            value.value.constant.integer = -expression_value.value.constant.integer;

                            return {
                                true,
                                value
                            };
                        } break;

                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.unary_operation.expression->range.first_line,
                                expression_value.type.integer.size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    auto zero_register = append_constant(
                        context,
                        instructions,
                        expression.range.first_line,
                        expression_value.type.integer.size,
                        0
                    );

                    auto result_register = append_arithmetic_operation(
                        context,
                        instructions,
                        expression.range.first_line,
                        ArithmeticOperationType::Subtract,
                        expression_value.type.integer.size,
                        zero_register,
                        value_register
                    );

                    value.value.category = ValueCategory::Anonymous;
                    value.value.anonymous.register_ = result_register;

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
            expect(expression_value, generate_expression(context, instructions, *expression.cast.expression));

            expect(type, evaluate_type_expression(context, *expression.cast.type));

            if(expression_value.value.category == ValueCategory::Constant) {
                expect(constant, evaluate_constant_conversion(
                    *context,
                    expression_value.value.constant,
                    expression_value.type,
                    expression.cast.expression->range,
                    type,
                    expression.cast.type->range
                ));

                TypedValue value;
                value.value.category = ValueCategory::Constant;
                value.type = type;
                value.value.constant = constant;

                return {
                    true,
                    value
                };
            }

            auto coerce_result = coerce_to_type(
                context,
                instructions,
                expression.cast.expression->range,
                expression_value,
                type,
                true
            );

            if(coerce_result.status) {
                return {
                    true,
                    {
                        type,
                        coerce_result.value
                    }
                };
            }

            size_t result_register;
            switch(expression_value.type.category) {
                case TypeCategory::Integer: {
                    switch(type.category) {
                        case TypeCategory::Integer: {
                            size_t value_register;
                            switch(expression_value.value.category) {
                                case ValueCategory::Anonymous: {
                                    value_register = expression_value.value.anonymous.register_;
                                } break;

                                case ValueCategory::Address: {
                                    value_register = append_load_integer(
                                        context,
                                        instructions,
                                        expression.cast.expression->range.first_line,
                                        expression_value.type.integer.size,
                                        expression_value.value.address
                                    );
                                } break;

                                default: {
                                    abort();
                                } break;
                            }

                            result_register = append_integer_upcast(
                                context,
                                instructions,
                                expression.range.first_line,
                                expression_value.type.integer.is_signed,
                                expression_value.type.integer.size,
                                type.integer.size,
                                value_register
                            );
                        } break;

                        case TypeCategory::Pointer: {
                            if(expression_value.type.integer.size != context->address_integer_size) {
                                error(*context, expression.cast.expression->range, "Cannot cast from %s to pointer", type_description(expression_value.type));

                                return { false };
                            }

                            size_t value_register;
                            switch(expression_value.value.category) {
                                case ValueCategory::Anonymous: {
                                    value_register = expression_value.value.anonymous.register_;
                                } break;

                                case ValueCategory::Address: {
                                    value_register = append_load_integer(
                                        context,
                                        instructions,
                                        expression.range.first_line,
                                        context->address_integer_size,
                                        expression_value.value.address
                                    );
                                } break;

                                default: {
                                    abort();
                                } break;
                            }

                            result_register = value_register;
                        } break;

                        default: {
                            error(*context, expression.cast.type->range, "Cannot cast from integer to %s", type_description(type));

                            return { false };
                        } break;
                    }
                } break;

                case TypeCategory::Pointer: {
                    size_t value_register;
                    switch(expression_value.value.category) {
                        case ValueCategory::Anonymous: {
                            value_register = expression_value.value.anonymous.register_;
                        } break;

                        case ValueCategory::Address: {
                            value_register = append_load_integer(
                                context,
                                instructions,
                                expression.range.first_line,
                                context->address_integer_size,
                                expression_value.value.address
                            );
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    switch(type.category) {
                        case TypeCategory::Integer: {
                            if(type.integer.size != context->address_integer_size) {
                                error(*context, expression.cast.expression->range, "Cannot cast from pointer to %s", type_description(type));

                                return { false };
                            }

                            result_register = value_register;
                        } break;

                        case TypeCategory::Pointer: {
                            result_register = value_register;
                        } break;

                        default: {
                            error(*context, expression.cast.type->range, "Cannot cast from pointer to %s", type_description(type));

                            return { false };
                        } break;
                    }
                } break;

                default: {
                    error(*context, expression.cast.expression->range, "Cannot cast from %s", type_description(expression_value.type));

                    return { false };
                } break;
            }

            TypedValue value;
            value.value.category = ValueCategory::Anonymous;
            value.type = type;
            value.value.anonymous.register_ = result_register;

            return {
                true,
                value
            };
        } break;

        case ExpressionType::ArrayType: {
            expect(type, evaluate_type_expression(context, *expression.array_type.expression));

            if(!is_runtime_type(type)) {
                error(*context, expression.array_type.expression->range, "Arrays cannot be of type '%s'", type_description(type));

                return { false };
            }

            TypedValue value;
            value.type.category = TypeCategory::Type;
            value.value.category = ValueCategory::Constant;

            if(expression.array_type.index != nullptr) {
                expect(index_value, evaluate_constant_expression(context, *expression.array_type.index));

                if(index_value.type.category != TypeCategory::Integer) {
                    error(*context, expression.array_type.index->range, "Expected an integer, got '%s'", type_description(index_value.type));
                }

                size_t length;
                if(index_value.type.integer.is_undetermined) {
                    if((int64_t)index_value.value.integer < 0) {
                        error(*context, expression.array_type.index->range, "Negative array length: %lld", (int64_t)index_value.value.integer);

                        return { false };
                    }

                    length = index_value.value.integer;
                } else if(index_value.type.integer.is_signed) {
                    switch(index_value.type.integer.size) {
                        case RegisterSize::Size8: {
                            if((int8_t)index_value.value.integer < 0) {
                                error(*context, expression.array_type.index->range, "Negative array length: %hhd", (int8_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint8_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            if((int16_t)index_value.value.integer < 0) {
                                error(*context, expression.array_type.index->range, "Negative array length: %hd", (int16_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint16_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            if((int32_t)index_value.value.integer < 0) {
                                error(*context, expression.array_type.index->range, "Negative array length: %d", (int32_t)index_value.value.integer);

                                return { false };
                            }

                            length = (uint32_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            if((int8_t)index_value.value.integer < 0) {
                                error(*context, expression.array_type.index->range, "Negative array length: %lld", (int64_t)index_value.value.integer);

                                return { false };
                            }

                            length = index_value.value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                } else {
                    switch(index_value.type.integer.size) {
                        case RegisterSize::Size8: {
                            length = (uint8_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size16: {
                            length = (uint16_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size32: {
                            length = (uint32_t)index_value.value.integer;
                        } break;

                        case RegisterSize::Size64: {
                            length = index_value.value.integer;
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }

                value.value.constant.type.category = TypeCategory::StaticArray;
                value.value.constant.type.static_array = {
                    length,
                    heapify(type)
                };
            } else {
                value.value.constant.type.category = TypeCategory::Array;
                value.value.constant.type.array = heapify(type);
            }

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
                    error(*context, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                    return { false };
                }

                expect(type, evaluate_type_expression(context, parameter.type));

                if(!is_runtime_type(type)) {
                    error(*context, expression.function_type.parameters[i].type.range, "Function parameters cannot be of type '%s'", type_description(type));

                    return { false };
                }

                parameters[i] = type;
            }

            Type return_type;
            if(expression.function_type.return_type == nullptr) {
                return_type.category = TypeCategory::Void;
            } else {
                expect(return_type_value, evaluate_type_expression(context, *expression.function_type.return_type));

                if(!is_runtime_type(return_type_value)) {
                    error(*context, expression.function_type.return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                    return { false };
                }

                return_type = return_type_value;
            }

            TypedValue value;
            value.value.category = ValueCategory::Constant;
            value.type.category = TypeCategory::Type;
            value.value.constant.type.category = TypeCategory::Function;
            value.value.constant.type.function = {
                false,
                expression.function_type.parameters.count,
                parameters,
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

                    if(!is_runtime_type(type)) {
                        error(*context, statement.variable_declaration.uninitialized.range, "Cannot create variables of type '%s'", type_description(type));

                        return false;
                    }

                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        statement.range.first_line,
                        get_type_size(*context, type),
                        get_type_alignment(*context, type)
                    );

                    if(!add_new_variable(
                        context,
                        statement.variable_declaration.name,
                        address_register,
                        type,
                        statement.variable_declaration.uninitialized.range
                    )) {
                        return false;
                    }

                    return true;
                } break;

                case VariableDeclarationType::TypeElided: {
                    auto address_register = allocate_register(context);

                    Instruction allocate;
                    allocate.type = InstructionType::AllocateLocal;
                    allocate.line = statement.range.first_line;
                    allocate.allocate_local.destination_register = address_register;

                    auto allocate_index = append(instructions, allocate);

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.type_elided));

                    expect(coerced_type, coerce_to_default_type(*context, statement.variable_declaration.type_elided.range, initializer_value.type));

                    if(!is_runtime_type(coerced_type)) {
                        error(*context, statement.variable_declaration.type_elided.range, "Cannot create variables of type '%s'", type_description(coerced_type));

                        return false;
                    }

                    auto representation = get_type_representation(*context, coerced_type);

                    switch(initializer_value.value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                statement.range,
                                coerced_type,
                                initializer_value.value.constant,
                                address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    initializer_value.value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, coerced_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    initializer_value.value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    initializer_value.value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, coerced_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    initializer_value.value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

                    (*instructions)[allocate_index].allocate_local.size = get_type_size(*context, coerced_type);
                    (*instructions)[allocate_index].allocate_local.alignment = get_type_alignment(*context, coerced_type);

                    if(!add_new_variable(context, statement.variable_declaration.name, address_register, coerced_type, statement.variable_declaration.type_elided.range)) {
                        return false;
                    }

                    return true;
                } break;


                case VariableDeclarationType::FullySpecified: {
                    expect(type, evaluate_type_expression(context, statement.variable_declaration.fully_specified.type));

                    if(!is_runtime_type(type)) {
                        error(*context, statement.variable_declaration.fully_specified.type.range, "Cannot create variables of type '%s'", type_description(type));

                        return false;
                    }

                    auto address_register = append_allocate_local(
                        context,
                        instructions,
                        statement.range.first_line,
                        get_type_size(*context, type),
                        get_type_alignment(*context, type)
                    );

                    expect(initializer_value, generate_expression(context, instructions, statement.variable_declaration.fully_specified.initializer));

                    expect(coerced_initializer_value, coerce_to_type(
                        context,
                        instructions,
                        statement.variable_declaration.fully_specified.initializer.range,
                        initializer_value,
                        type,
                        false
                    ));

                    auto representation = get_type_representation(*context, type);

                    switch(coerced_initializer_value.category) {
                        case ValueCategory::Constant: {
                            generate_constant_value_write(
                                context,
                                instructions,
                                statement.range,
                                type,
                                coerced_initializer_value.constant,
                                address_register
                            );
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_initializer_value.anonymous.register_,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_initializer_value.anonymous.register_,
                                    address_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_initializer_value.address
                                );

                                append_store_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    value_register,
                                    address_register
                                );
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_initializer_value.address,
                                    address_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }

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

            if(target.value.category != ValueCategory::Address) {
                error(*context, statement.assignment.target.range, "Value is not assignable");

                return false;
            }

            expect(value, generate_expression(context, instructions, statement.assignment.value));

            expect(coerced_value, coerce_to_type(
                context,
                instructions,
                statement.assignment.value.range,
                value,
                target.type,
                false
            ));

            auto representation = get_type_representation(*context, target.type);

            switch(coerced_value.category) {
                case ValueCategory::Constant: {
                    generate_constant_value_write(
                        context,
                        instructions,
                        statement.range,
                        target.type,
                        coerced_value.constant,
                        target.value.address
                    );
                } break;

                case ValueCategory::Anonymous: {
                    if(representation.is_in_register) {
                        append_store_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            coerced_value.anonymous.register_,
                            target.value.address
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            statement.range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, target.type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            statement.range.first_line,
                            length_register,
                            coerced_value.anonymous.register_,
                            target.value.address
                        );
                    }
                } break;

                case ValueCategory::Address: {
                    if(representation.is_in_register) {
                        auto value_register = append_load_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            coerced_value.address
                        );

                        append_store_integer(
                            context,
                            instructions,
                            statement.range.first_line,
                            representation.value_size,
                            value_register,
                            target.value.address
                        );
                    } else {
                        auto length_register = append_constant(
                            context,
                            instructions,
                            statement.range.first_line,
                            context->address_integer_size,
                            get_type_size(*context, target.type)
                        );

                        append_copy_memory(
                            context,
                            instructions,
                            statement.range.first_line,
                            length_register,
                            coerced_value.address,
                            target.value.address
                        );
                    }
                } break;

                default: {
                    abort();
                } break;
            }

            return true;
        } break;

        case StatementType::If: {
            auto end_jump_count = 1 + statement.if_.else_ifs.count;
            auto end_jump_indices = allocate<size_t>(end_jump_count);

            expect(condition, generate_expression(context, instructions, statement.if_.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(*context, statement.if_.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_in_register_boolean_value(context, instructions, statement.if_.condition.range, condition.value);

            append_branch(context, instructions, statement.if_.condition.range.first_line, condition_register, instructions->count + 2);

            Instruction jump;
            jump.type = InstructionType::Jump;
            jump.line = statement.if_.condition.range.first_line;

            auto jump_index = append(instructions, jump);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.if_.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            Instruction end_jump;
            end_jump.type = InstructionType::Jump;
            end_jump.line = statement.range.first_line;

            end_jump_indices[0] = append(instructions, end_jump);

            (*instructions)[jump_index].jump.destination_instruction = instructions->count;

            for(size_t i = 0; i < statement.if_.else_ifs.count; i += 1) {
                expect(condition, generate_expression(context, instructions, statement.if_.else_ifs[i].condition));

                if(condition.type.category != TypeCategory::Boolean) {
                    error(*context, statement.if_.else_ifs[i].condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                    return false;
                }

                auto condition_register = generate_in_register_boolean_value(
                    context,
                    instructions,
                    statement.if_.else_ifs[i].condition.range,
                    condition.value
                );

                append_branch(
                    context,
                    instructions,
                    statement.if_.else_ifs[i].condition.range.first_line,
                    condition_register,
                    instructions->count + 2
                );

                Instruction jump;
                jump.type = InstructionType::Jump;
                jump.line = statement.if_.else_ifs[i].condition.range.first_line;

                auto jump_index = append(instructions, jump);

                append(&context->variable_context_stack, List<Variable>{});

                for(auto child_statement : statement.if_.else_ifs[i].statements) {
                    if(!generate_statement(context, instructions, child_statement)) {
                        return false;
                    }
                }

                context->variable_context_stack.count -= 1;

                Instruction end_jump;
                end_jump.type = InstructionType::Jump;
                end_jump.line = statement.range.first_line;

                end_jump_indices[i + 1] = append(instructions, end_jump);

                (*instructions)[jump_index].jump.destination_instruction = instructions->count;
            }

            if(statement.if_.has_else) {
                append(&context->variable_context_stack, List<Variable>{});

                for(auto child_statement : statement.if_.else_statements) {
                    if(!generate_statement(context, instructions, child_statement)) {
                        return false;
                    }
                }
                context->variable_context_stack.count -= 1;
            }

            for(size_t i = 0; i < end_jump_count; i += 1) {
                (*instructions)[end_jump_indices[i]].jump.destination_instruction = instructions->count;
            }

            return true;
        } break;

        case StatementType::WhileLoop: {
            auto condition_index = instructions->count;

            expect(condition, generate_expression(context, instructions, statement.while_loop.condition));

            if(condition.type.category != TypeCategory::Boolean) {
                error(*context, statement.while_loop.condition.range, "Non-boolean if statement condition. Got %s", type_description(condition.type));

                return false;
            }

            auto condition_register = generate_in_register_boolean_value(
                context,
                instructions,
                statement.while_loop.condition.range,
                condition.value
            );

            append_branch(
                context,
                instructions,
                statement.while_loop.condition.range.first_line,
                condition_register,
                instructions->count + 2
            );

            Instruction jump_out;
            jump_out.type = InstructionType::Jump;
            jump_out.line = statement.while_loop.condition.range.first_line;

            auto jump_out_index = append(instructions, jump_out);

            append(&context->variable_context_stack, List<Variable>{});

            for(auto child_statement : statement.while_loop.statements) {
                if(!generate_statement(context, instructions, child_statement)) {
                    return false;
                }
            }

            context->variable_context_stack.count -= 1;

            append_jump(
                context,
                instructions,
                statement.range.first_line,
                condition_index
            );

            (*instructions)[jump_out_index].jump.destination_instruction = instructions->count;

            return true;
        } break;

        case StatementType::Return: {
            Instruction return_;
            return_.type = InstructionType::Return;
            return_.line = statement.range.first_line;

            if(statement._return.has_value) {
                expect(value, generate_expression(context, instructions, statement._return.value));

                expect(coerced_value, coerce_to_type(
                    context,
                    instructions,
                    statement._return.value.range,
                    value,
                    context->return_type,
                    false
                ));

                if(context->return_type.category != TypeCategory::Void) {
                    auto representation = get_type_representation(*context, context->return_type);

                    switch(coerced_value.category) {
                        case ValueCategory::Constant: {
                            if(representation.is_in_register) {
                                auto value_register = generate_in_register_constant_value(
                                    context,
                                    instructions,
                                    statement.range,
                                    context->return_type,
                                    coerced_value.constant
                                );

                                return_.return_.value_register = value_register;
                            } else {
                                generate_not_in_register_constant_write(
                                    context,
                                    instructions,
                                    statement.range,
                                    context->return_type,
                                    coerced_value.constant,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        case ValueCategory::Anonymous: {
                            if(representation.is_in_register) {
                                return_.return_.value_register = coerced_value.anonymous.register_;
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, context->return_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_value.anonymous.register_,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        case ValueCategory::Address: {
                            if(representation.is_in_register) {
                                auto value_register = append_load_integer(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    representation.value_size,
                                    coerced_value.address
                                );

                                return_.return_.value_register = value_register;
                            } else {
                                auto length_register = append_constant(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    context->address_integer_size,
                                    get_type_size(*context, context->return_type)
                                );

                                append_copy_memory(
                                    context,
                                    instructions,
                                    statement.range.first_line,
                                    length_register,
                                    coerced_value.address,
                                    context->return_parameter_register
                                );
                            }
                        } break;

                        default: {
                            abort();
                        } break;
                    }
                }
            } else if(context->return_type.category != TypeCategory::Void) {
                error(*context, statement.range, "Missing return value");

                return { false };
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

Result<IR> generate_ir(const char *main_file_path, Array<Statement> main_file_statements, RegisterSize address_size, RegisterSize default_size) {
    List<GlobalConstant> global_constants{};

    append(&global_constants, create_base_integer_type("u8", RegisterSize::Size8, false));
    append(&global_constants, create_base_integer_type("u16", RegisterSize::Size16, false));
    append(&global_constants, create_base_integer_type("u32", RegisterSize::Size32, false));
    append(&global_constants, create_base_integer_type("u64", RegisterSize::Size64, false));

    append(&global_constants, create_base_integer_type("i8", RegisterSize::Size8, true));
    append(&global_constants, create_base_integer_type("i16", RegisterSize::Size16, true));
    append(&global_constants, create_base_integer_type("i32", RegisterSize::Size32, true));
    append(&global_constants, create_base_integer_type("i64", RegisterSize::Size64, true));

    append(&global_constants, create_base_integer_type("usize", address_size, false));
    append(&global_constants, create_base_integer_type("isize", address_size, true));

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

    Type type_type;
    type_type.category = TypeCategory::Type;

    append(&global_constants, create_base_type("type", type_type));

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
    context.file_path = main_file_path;
    context.top_level_statements = main_file_statements;
    context.constant_parameters = {};

    auto main_found = false;
    for(auto statement : main_file_statements) {
        if(match_declaration(statement, "main")) {
            if(statement.type != StatementType::FunctionDeclaration) {
                error(context, statement.range, "'main' must be a function");

                return { false };
            }

            if(statement.function_declaration.is_external) {
                error(context, statement.range, "'main' must not be external");

                return { false };
            }

            expect(value, resolve_declaration(&context, statement));

            if(value.type.function.is_polymorphic) {
                error(context, statement.range, "'main' cannot be polymorphic");

                return { false };
            }

            auto runtimeParameters = allocate<RuntimeFunctionParameter>(statement.function_declaration.parameters.count);

            for(size_t i = 0; i < statement.function_declaration.parameters.count; i += 1) {
                runtimeParameters[i] = {
                    statement.function_declaration.parameters[i].name,
                    value.type.function.parameters[i]
                };
            }

            auto mangled_name = "main";

            RuntimeFunction function;
            function.mangled_name = mangled_name;
            function.parameters = {
                statement.function_declaration.parameters.count,
                runtimeParameters
            };
            function.return_type = *value.type.function.return_type;
            function.declaration.declaration = statement;
            function.declaration.file_path = main_file_path;
            function.declaration.top_level_statements = main_file_statements;
            function.declaration.constant_parameters = {};

            append(&context.runtime_functions, function);

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
        RuntimeFunction function;
        for(auto runtime_function : context.runtime_functions) {
            auto generated = false;
            for(auto generated_function : functions) {
                if(strcmp(generated_function.name, runtime_function.mangled_name) == 0) {
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
            context.determined_declaration = function.declaration;

            auto total_parameter_count = function.parameters.count;

            bool has_return;
            RegisterRepresentation return_representation;                
            if(function.return_type.category == TypeCategory::Void) {
                has_return = false;
            } else {
                has_return = true;

                return_representation = get_type_representation(context, function.return_type);

                if(!return_representation.is_in_register) {
                    total_parameter_count += 1;
                }
            }

            auto parameter_sizes = allocate<RegisterSize>(total_parameter_count);

            for(size_t i = 0; i < function.parameters.count; i += 1) {
                auto representation = get_type_representation(context, function.parameters[i].type);

                if(representation.is_in_register) {
                    parameter_sizes[i] = representation.value_size;
                } else {
                    parameter_sizes[i] = address_size;
                }
            }

            if(has_return && !return_representation.is_in_register) {
                parameter_sizes[total_parameter_count - 1] = address_size;
            }

            const char *file_path;
            {
                auto current_declaration = context.determined_declaration;

                while(!current_declaration.declaration.is_top_level) {
                    current_declaration = *current_declaration.parent;
                }

                file_path = current_declaration.file_path;
            }

            Function ir_function;
            ir_function.name = function.mangled_name;
            ir_function.is_external = function.declaration.declaration.function_declaration.is_external;
            ir_function.parameter_sizes = {
                total_parameter_count,
                parameter_sizes
            };
            ir_function.has_return = has_return && return_representation.is_in_register;
            ir_function.file = file_path;
            ir_function.line = function.declaration.declaration.range.first_line;

            if(has_return && return_representation.is_in_register) {
                ir_function.return_size = return_representation.value_size;
            }

            if(!function.declaration.declaration.function_declaration.is_external) {
                append(&context.variable_context_stack, List<Variable>{});

                auto parameters = allocate<Variable>(function.parameters.count);

                for(size_t i = 0; i < function.parameters.count; i += 1) {
                    auto parameter = function.parameters[i];

                    parameters[i] = {
                        parameter.name,
                        parameter.type,
                        parameter.type_range,
                        i
                    };
                }

                context.parameters = {
                    function.parameters.count,
                    parameters
                };
                context.return_type = function.return_type;
                context.next_register = total_parameter_count;

                if(has_return && !return_representation.is_in_register) {
                    context.return_parameter_register = total_parameter_count - 1;
                }

                List<Instruction> instructions{};

                for(auto statement : function.declaration.declaration.function_declaration.statements) {
                    switch(statement.type) {
                        case StatementType::Expression:
                        case StatementType::VariableDeclaration:
                        case StatementType::Assignment:
                        case StatementType::If:
                        case StatementType::WhileLoop:
                        case StatementType::Return: {
                            if(!generate_statement(&context, &instructions, statement)) {
                                return { false };
                            }
                        } break;

                        case StatementType::Import: {
                            error(context, statement.range, "Import directive only allowed in global scope");

                            return { false };
                        } break;
                    }
                }

                auto has_return_at_end = false;
                if(function.declaration.declaration.function_declaration.statements.count > 0) {
                    has_return_at_end = function.declaration.declaration.function_declaration.statements[
                        function.declaration.declaration.function_declaration.statements.count - 1
                    ].type == StatementType::Return;
                }

                if(!has_return_at_end) {
                    if(has_return) {
                        error(context, function.declaration.declaration.range, "Function '%s' must end with a return", function.declaration.declaration.function_declaration.name.text);

                        return { false };
                    } else {
                        Instruction return_;
                        return_.type = InstructionType::Return;

                        append(&instructions, return_);
                    }
                }

                context.variable_context_stack.count -= 1;
                context.next_register = 0;

                ir_function.instructions = to_array(instructions);
            }

            append(&functions, ir_function);
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