#pragma once

#include <assert.h>
#include "result.h"
#include "ast.h"
#include "register_size.h"
#include "list.h"

struct Job;
struct Type;
struct FunctionTypeType;
struct Integer;
struct ConstantValue;
struct FunctionConstant;

struct ScopeConstant {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct ConstantScope {
    Array<Statement*> statements;

    Array<ScopeConstant> scope_constants;

    bool is_top_level;

    ConstantScope *parent;

    const char *file_path;
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
};

enum struct ConstantValueKind {
    FunctionConstant,
    BuiltinFunctionConstant,
    PolymorphicFunctionConstant,
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
    ConstantScope *body_scope;
    Array<ConstantScope*> child_scopes;

    FunctionConstant(
        FunctionDeclaration *declaration,
        ConstantScope *body_scope,
        Array<ConstantScope*> child_scopes
    ) :
    ConstantValue { ConstantValueKind::FunctionConstant },
    declaration { declaration },
    body_scope { body_scope },
    child_scopes { child_scopes }
    {}
};

struct PolymorphicFunctionConstant : ConstantValue {
    FunctionDeclaration *declaration;
    ConstantScope *scope;

    PolymorphicFunctionConstant(
        FunctionDeclaration *declaration,
        ConstantScope *scope
    ) :
    ConstantValue { ConstantValueKind::PolymorphicFunctionConstant },
    declaration { declaration },
    scope { scope }
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
    ConstantScope *scope;

    FileModuleConstant(
        ConstantScope *scope
    ) :
        ConstantValue { ConstantValueKind::FileModuleConstant },
        scope { scope }
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

struct TypedConstantValue {
    Type *type;

    ConstantValue *value;
};

template <typename T>
struct DelayedValue {
    bool has_value;

    T value;
    Job *waiting_for;
};

template <>
struct DelayedValue<void> {
    bool has_value;

    Job *waiting_for;
};

#define expect_delayed(name, expression) expect(__##name##_delayed, expression);if(!__##name##_delayed.has_value)return{true,{false,{},__##name##_delayed.waiting_for}};auto name=__##name##_delayed.value
#define expect_delayed_void_ret(name, expression) expect(__##name##_delayed, expression);if(!__##name##_delayed.has_value)return{true,{false,__##name##_delayed.waiting_for}};auto name=__##name##_delayed.value
#define expect_delayed_void_val(expression) expect(__##name##_delayed, expression);if(!__##name##_delayed.has_value)return{true,{false,{},__##name##_delayed.waiting_for}}
#define expect_delayed_void_both(expression) expect(__##name##_delayed, expression);if(!__##name##_delayed.has_value)return{true,{false,__##name##_delayed.waiting_for}}

void error(ConstantScope *scope, FileRange range, const char *format, ...);

bool check_undetermined_integer_to_integer_coercion(ConstantScope *scope, FileRange range, Integer *target_type, int64_t value, bool probing);
Result<IntegerConstant*> coerce_constant_to_integer_type(
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Integer *target_type,
    bool probing
);
Result<ConstantValue*> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Type *target_type,
    bool probing
);
Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope *scope,
    Type *type,
    ConstantValue *value,
    FileRange range,
    Type *index_type,
    ConstantValue *index_value,
    FileRange index_range
);
Result<Type*> determine_binary_operation_type(ConstantScope *scope, FileRange range, Type *left, Type *right);
Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope *scope,
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
    ConstantScope *scope,
    Type *type,
    ConstantValue *value,
    FileRange value_range,
    Type *target_type,
    FileRange target_range,
    bool probing
);
Result<DelayedValue<Type*>> evaluate_type_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Expression *expression
);
Result<Type*> coerce_to_default_type(GlobalInfo info, ConstantScope *scope, FileRange range, Type *type);
bool match_public_declaration(Statement *statement, const char *name);
bool match_declaration(Statement *statement, const char *name);
Result<DelayedValue<TypedConstantValue>> get_simple_resolved_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *declaration
);
bool constant_values_equal(Type *type, ConstantValue *a, ConstantValue *b);

bool static_if_may_have_declaration(const char *name, bool external, StaticIf *static_if);

Result<DelayedValue<TypedConstantValue>> evaluate_constant_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Expression *expression
);

Result<DelayedValue<bool>> do_resolve_static_if(GlobalInfo info, List<Job*> *jobs, StaticIf *static_if, ConstantScope *scope);

struct FunctionResolutionValue {
    FunctionTypeType *type;
    FunctionConstant *value;
};

Result<DelayedValue<FunctionResolutionValue>> do_resolve_function_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    ConstantScope *scope
);

Result<DelayedValue<FunctionResolutionValue>> do_resolve_polymorphic_function(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    TypedConstantValue *parameters,
    ConstantScope *scope,
    ConstantScope *call_scope,
    FileRange *call_parameter_ranges
);

Result<DelayedValue<Type*>> do_resolve_struct_definition(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantScope *scope
);

Result<DelayedValue<Type*>> do_resolve_polymorphic_struct(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantValue **parameters,
    ConstantScope *scope
);

bool process_scope(List<Job*> *jobs, ConstantScope *scope, List<ConstantScope*> *child_scopes, bool is_top_level);