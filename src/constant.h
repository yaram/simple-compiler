#pragma once

#include <assert.h>
#include "result.h"
#include "ast.h"
#include "register_size.h"

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

extern Type polymorphic_function_singleton;
extern Type builtin_function_singleton;
extern Type undetermined_integer_singleton;
extern Type boolean_singleton;
extern Type undetermined_float_singleton;
extern Type type_type_singleton;
extern Type void_singleton;
extern Type file_module_singleton;

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

struct GlobalConstant {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct GlobalInfo {
    Array<GlobalConstant> global_constants;

    RegisterSize address_integer_size;
    RegisterSize default_integer_size;

    bool print_ast;
};

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

extern ConstantValue void_constant_singleton;

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
T *extract_constant_value_internal(ConstantValue *value, ConstantValueKind kind) {
    assert(value->kind == kind);

    auto extracted_value = (T*)value;

    return extracted_value;
}

struct LoadedFile {
    const char *path;

    Array<Statement*> statements;
};

struct ConstantContext {
    Array<ConstantParameter> constant_parameters;

    Array<LoadedFile> loaded_files;
};

struct TypedConstantValue {
    Type *type;

    ConstantValue *value;
};

struct RegisterRepresentation {
    bool is_in_register;

    RegisterSize value_size;
    bool is_float;
};

bool types_equal(Type *a, Type *b);
const char *type_description(Type *type);
bool is_runtime_type(Type *type);
uint64_t get_struct_alignment(GlobalInfo info, StructType type);
uint64_t get_type_alignment(GlobalInfo info, Type *type);
uint64_t get_struct_size(GlobalInfo info, StructType type);
uint64_t get_type_size(GlobalInfo info, Type *type);
uint64_t get_struct_member_offset(GlobalInfo info, StructType type, size_t member_index);

void error(ConstantScope scope, FileRange range, const char *format, ...);
bool check_undetermined_integer_to_integer_coercion(ConstantScope scope, FileRange range, Integer target_type, int64_t value, bool probing);
Result<IntegerConstant*> coerce_constant_to_integer_type(
    ConstantScope scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Integer target_type,
    bool probing
);
Result<ConstantValue*> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope scope,
    FileRange range,
    Type* type,
    ConstantValue* value,
    Type* target_type,
    bool probing
);
Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope scope,
    Type *type,
    ConstantValue *value,
    FileRange range,
    Type *index_type,
    ConstantValue *index_value,
    FileRange index_range
);
Result<Type*> determine_binary_operation_type(ConstantScope scope, FileRange range, Type *left, Type *right);
Result<TypedConstantValue> evaluate_constant_binary_operation(
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
);
Result<ConstantValue*> evaluate_constant_cast(
    GlobalInfo info,
    ConstantScope scope,
    Type *type,
    ConstantValue *value,
    FileRange value_range,
    Type *target_type,
    FileRange target_range,
    bool probing
);
Result<Type*> coerce_to_default_type(GlobalInfo info, ConstantScope scope, FileRange range, Type *type);
bool match_public_declaration(Statement *statement, const char *name);
bool match_declaration(Statement *statement, const char *name);
Result<TypedConstantValue> evaluate_constant_expression(GlobalInfo info, ConstantScope scope, ConstantContext context, Expression *expression);
Result<Type*> evaluate_type_expression(GlobalInfo info, ConstantScope scope, ConstantContext context, Expression *expression);
Result<TypedConstantValue> resolve_declaration(GlobalInfo info, ConstantScope scope, ConstantContext context, Statement *declaration);