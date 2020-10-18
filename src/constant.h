#pragma once

#include <assert.h>
#include "result.h"
#include "ast.h"
#include "types.h"
#include "register_size.h"
#include "platform.h"
#include "list.h"

struct Job;
struct ConstantScope;
struct AnyConstantValue;

struct FunctionConstant {
    FunctionDeclaration *declaration;

    bool is_external;
    Array<const char*> external_libraries;

    ConstantScope *body_scope;
    Array<ConstantScope*> child_scopes;

    bool is_no_mangle;
};

struct PolymorphicFunctionConstant {
    FunctionDeclaration *declaration;
    ConstantScope *scope;
};

struct BuiltinFunctionConstant {
    String name;
};

struct ArrayConstant {
    uint64_t length;

    uint64_t pointer;
};

struct StaticArrayConstant {
    AnyConstantValue *elements;
};

struct StructConstant {
    AnyConstantValue *members;
};

struct FileModuleConstant {
    ConstantScope *scope;
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

struct AnyConstantValue {
    ConstantValueKind kind;

    union {
        FunctionConstant function;
        BuiltinFunctionConstant builtin_function;
        PolymorphicFunctionConstant polymorphic_function;
        uint64_t integer;
        double float_;
        bool boolean;
        uint64_t pointer;
        ArrayConstant array;
        StaticArrayConstant static_array;
        StructConstant struct_;
        FileModuleConstant file_module;
        AnyType type;
    };
};

inline AnyConstantValue wrap_function_constant(FunctionConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::FunctionConstant;
    result.function = value;

    return result;
}

inline FunctionConstant unwrap_function_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::FunctionConstant);

    return value.function;
}

inline AnyConstantValue wrap_polymorphic_function_constant(PolymorphicFunctionConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::PolymorphicFunctionConstant;
    result.polymorphic_function = value;

    return result;
}

inline PolymorphicFunctionConstant unwrap_polymorphic_function_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::PolymorphicFunctionConstant);

    return value.polymorphic_function;
}

inline AnyConstantValue wrap_builtin_function_constant(BuiltinFunctionConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::BuiltinFunctionConstant;
    result.builtin_function = value;

    return result;
}

inline BuiltinFunctionConstant unwrap_builtin_function_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::BuiltinFunctionConstant);

    return value.builtin_function;
}

inline AnyConstantValue wrap_integer_constant(uint64_t value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::IntegerConstant;
    result.integer = value;

    return result;
}

inline uint64_t unwrap_integer_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::IntegerConstant);

    return value.integer;
}

inline AnyConstantValue wrap_float_constant(double value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::FloatConstant;
    result.float_ = value;

    return result;
}

inline AnyConstantValue wrap_boolean_constant(bool value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::BooleanConstant;
    result.boolean = value;

    return result;
}

inline bool unwrap_boolean_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::BooleanConstant);

    return value.boolean;
}

inline AnyConstantValue create_void_constant() {
    AnyConstantValue result;
    result.kind = ConstantValueKind::VoidConstant;

    return result;
}

inline double unwrap_float_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::FloatConstant);

    return value.float_;
}

inline AnyConstantValue wrap_pointer_constant(uint64_t value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::PointerConstant;
    result.pointer = value;

    return result;
}

inline uint64_t unwrap_pointer_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::PointerConstant);

    return value.pointer;
}

inline AnyConstantValue wrap_array_constant(ArrayConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::ArrayConstant;
    result.array = value;

    return result;
}

inline ArrayConstant unwrap_array_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::ArrayConstant);

    return value.array;
}

inline AnyConstantValue wrap_static_array_constant(StaticArrayConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::StaticArrayConstant;
    result.static_array = value;

    return result;
}

inline StaticArrayConstant unwrap_static_array_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::StaticArrayConstant);

    return value.static_array;
}

inline AnyConstantValue wrap_struct_constant(StructConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::StructConstant;
    result.struct_ = value;

    return result;
}

inline StructConstant unwrap_struct_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::StructConstant);

    return value.struct_;
}

inline AnyConstantValue wrap_file_module_constant(FileModuleConstant value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::FileModuleConstant;
    result.file_module = value;

    return result;
}

inline FileModuleConstant unwrap_file_module_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::FileModuleConstant);

    return value.file_module;
}

inline AnyConstantValue wrap_type_constant(AnyType value) {
    AnyConstantValue result;
    result.kind = ConstantValueKind::TypeConstant;
    result.type = value;

    return result;
}

inline AnyType unwrap_type_constant(AnyConstantValue value) {
    assert(value.kind == ConstantValueKind::TypeConstant);

    return value.type;
}

const size_t DECLARATION_HASH_TABLE_SIZE = 32;

struct DeclarationHashTable {
    List<Statement*> buckets[DECLARATION_HASH_TABLE_SIZE];
};

uint32_t calculate_string_hash(String string);
DeclarationHashTable construct_declaration_hash_table(Array<Statement*> statements);
Statement *search_in_declaration_hash_table(DeclarationHashTable declaration_hash_table, String name);
Statement *search_in_declaration_hash_table(DeclarationHashTable declaration_hash_table, uint32_t hash, String name);

struct ScopeConstant {
    String name;

    AnyType type;

    AnyConstantValue value;
};

struct ConstantScope {
    Array<Statement*> statements;
    DeclarationHashTable declarations;

    Array<ScopeConstant> scope_constants;

    bool is_top_level;

    ConstantScope *parent;

    const char *file_path;
};

const char *get_scope_file_path(ConstantScope scope);

struct GlobalConstant {
    String name;

    AnyType type;

    AnyConstantValue value;
};

struct GlobalInfo {
    Array<GlobalConstant> global_constants;

    ArchitectureSizes architecture_sizes;
};

struct TypedConstantValue {
    AnyType type;

    AnyConstantValue value;
};

template <typename T>
struct DelayedResult {
    bool status;

    bool has_value;

    T value;

    Job *waiting_for;
};

template <>
struct DelayedResult<void> {
    bool status;

    bool has_value;

    Job *waiting_for;
};

#define wait(job) {true,false,{},job}
#define has(...) {true,true,##__VA_ARGS__}

#define expect_delayed(name, expression) auto __##name##_result = expression;if(!__##name##_result.status)return{false};if(!__##name##_result.has_value)return{true,false,{},__##name##_result.waiting_for};auto name=__##name##_result.value
#define expect_delayed_void_ret(name, expression) auto __##name##_result = expression;if(!__##name##_result.status)return{false};if(!__##name##_result.has_value)return{true,false,__##name##_result.waiting_for};auto name=__##name##_result.value
#define expect_delayed_void_val(expression) auto __##name##_result = expression;if(!__##name##_result.status)return{false};if(!__##name##_result.has_value)return{true,false,{},__##name##_result.waiting_for}
#define expect_delayed_void_both(expression) auto __##name##_result = expression;if(!__##name##_result.status)return{false};if(!__##name##_result.has_value)return{true,false,__##name##_result.waiting_for}
void error(ConstantScope *scope, FileRange range, const char *format, ...);

Result<const char *> static_array_to_c_string(ConstantScope *scope, FileRange range, AnyType type, AnyConstantValue value);

bool check_undetermined_integer_to_integer_coercion(ConstantScope *scope, FileRange range, Integer target_type, int64_t value, bool probing);
Result<uint64_t> coerce_constant_to_integer_type(
    ConstantScope *scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    Integer target_type,
    bool probing
);
Result<AnyConstantValue> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope *scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    AnyType target_type,
    bool probing
);
Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope *scope,
    AnyType type,
    AnyConstantValue value,
    FileRange range,
    AnyType index_type,
    AnyConstantValue index_value,
    FileRange index_range
);
Result<AnyType> determine_binary_operation_type(ConstantScope *scope, FileRange range, AnyType left, AnyType right);
Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope *scope,
    FileRange range,
    BinaryOperation::Operator binary_operator,
    FileRange left_range,
    AnyType left_type,
    AnyConstantValue left_value,
    FileRange right_range,
    AnyType right_type,
    AnyConstantValue right_value
);
Result<AnyConstantValue> evaluate_constant_cast(
    GlobalInfo info,
    ConstantScope *scope,
    AnyType type,
    AnyConstantValue value,
    FileRange value_range,
    AnyType target_type,
    FileRange target_range,
    bool probing
);
DelayedResult<AnyType> evaluate_type_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *ignore_statement,
    Expression *expression
);
Result<AnyType> coerce_to_default_type(GlobalInfo info, ConstantScope *scope, FileRange range, AnyType type);
bool is_declaration_public(Statement *declaration);
bool match_public_declaration(Statement *statement, String name);
bool match_declaration(Statement *statement, String name);
DelayedResult<TypedConstantValue> get_simple_resolved_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *declaration
);
bool constant_values_equal(AnyType type, AnyConstantValue a, AnyConstantValue b);

struct DeclarationSearchValue {
    bool found;

    AnyType type;
    AnyConstantValue value;
};

DelayedResult<DeclarationSearchValue> search_for_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    String name,
    uint32_t name_hash,
    ConstantScope *scope,
    Array<Statement*> statements,
    DeclarationHashTable declarations,
    bool external,
    Statement *ignore
);

bool static_if_may_have_declaration(const char *name, bool external, StaticIf *static_if);

DelayedResult<TypedConstantValue> evaluate_constant_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *ignore_statement,
    Expression *expression
);

struct StaticIfResolutionValue {
    bool condition;

    DeclarationHashTable declarations;
};

DelayedResult<StaticIfResolutionValue> do_resolve_static_if(GlobalInfo info, List<Job*> *jobs, StaticIf *static_if, ConstantScope *scope);

DelayedResult<TypedConstantValue> do_resolve_function_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    ConstantScope *scope
);

struct FunctionResolutionValue {
    FunctionTypeType type;

    FunctionConstant value;
};

DelayedResult<FunctionResolutionValue> do_resolve_polymorphic_function(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    TypedConstantValue *parameters,
    ConstantScope *scope,
    ConstantScope *call_scope,
    FileRange *call_parameter_ranges
);

DelayedResult<AnyType> do_resolve_struct_definition(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantScope *scope
);

DelayedResult<AnyType> do_resolve_polymorphic_struct(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    AnyConstantValue *parameters,
    ConstantScope *scope
);

bool process_scope(List<Job*> *jobs, ConstantScope *scope, Array<Statement*> statements, List<ConstantScope*> *child_scopes, bool is_top_level);