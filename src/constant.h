#pragma once

#include <assert.h>
#include "result.h"
#include "ast.h"
#include "types.h"
#include "platform.h"
#include "list.h"

struct AnyJob;
struct ConstantScope;
struct AnyConstantValue;

struct FunctionConstant {
    inline FunctionConstant() = default;
    explicit inline FunctionConstant(
        FunctionDeclaration* declaration,
        bool is_external,
        Array<String> external_libraries,
        ConstantScope* body_scope,
        Array<ConstantScope*> child_scopes
    ) :
        declaration(declaration),
        is_external(is_external),
        external_libraries(external_libraries),
        body_scope(body_scope),
        child_scopes(child_scopes)
    {}

    FunctionDeclaration* declaration;

    bool is_external;
    Array<String> external_libraries;

    ConstantScope* body_scope;
    Array<ConstantScope*> child_scopes;

    bool is_no_mangle;
};

struct PolymorphicFunctionConstant {
    inline PolymorphicFunctionConstant() = default;
    explicit inline PolymorphicFunctionConstant(FunctionDeclaration* declaration, ConstantScope* scope) : declaration(declaration), scope(scope) {}

    FunctionDeclaration* declaration;
    ConstantScope* scope;
};

struct BuiltinFunctionConstant {
    inline BuiltinFunctionConstant() = default;
    explicit inline BuiltinFunctionConstant(String name) : name(name) {}

    String name;
};

struct ArrayConstant {
    inline ArrayConstant() = default;
    explicit inline ArrayConstant(AnyConstantValue* length, AnyConstantValue* pointer) : length(length), pointer(pointer) {}

    AnyConstantValue* length;

    AnyConstantValue* pointer;
};

struct StaticArrayConstant {
    inline StaticArrayConstant() = default;
    explicit inline StaticArrayConstant(Array<AnyConstantValue> elements) : elements(elements) {}

    Array<AnyConstantValue> elements;
};

struct StructConstant {
    inline StructConstant() = default;
    explicit inline StructConstant(Array<AnyConstantValue> members) : members(members) {}

    Array<AnyConstantValue> members;
};

struct FileModuleConstant {
    inline FileModuleConstant() = default;
    explicit inline FileModuleConstant(ConstantScope* scope) : scope(scope) {}

    ConstantScope* scope;
};

// These do not map 1-to-1 to types / type kinds!!
enum struct ConstantValueKind {
    FunctionConstant,
    BuiltinFunctionConstant,
    PolymorphicFunctionConstant,
    IntegerConstant,
    FloatConstant,
    BooleanConstant,
    VoidConstant,
    ArrayConstant,
    StaticArrayConstant,
    StructConstant,
    FileModuleConstant,
    TypeConstant,
    UndefConstant
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
        ArrayConstant array;
        StaticArrayConstant static_array;
        StructConstant struct_;
        FileModuleConstant file_module;
        AnyType type;
    };

    inline AnyConstantValue() = default;
    explicit inline AnyConstantValue(FunctionConstant value) : kind(ConstantValueKind::FunctionConstant), function(value) {}
    explicit inline AnyConstantValue(PolymorphicFunctionConstant value) : kind(ConstantValueKind::PolymorphicFunctionConstant), polymorphic_function(value) {}
    explicit inline AnyConstantValue(BuiltinFunctionConstant value) : kind(ConstantValueKind::BuiltinFunctionConstant), builtin_function(value) {}
    explicit inline AnyConstantValue(uint64_t value) : kind(ConstantValueKind::IntegerConstant), integer(value) {}
    explicit inline AnyConstantValue(double value) : kind(ConstantValueKind::FloatConstant), float_(value) {}
    explicit inline AnyConstantValue(bool value) : kind(ConstantValueKind::BooleanConstant), boolean(value) {}
    explicit inline AnyConstantValue(ArrayConstant value) : kind(ConstantValueKind::ArrayConstant), array(value) {}
    explicit inline AnyConstantValue(StaticArrayConstant value) : kind(ConstantValueKind::StaticArrayConstant), static_array(value) {}
    explicit inline AnyConstantValue(StructConstant value) : kind(ConstantValueKind::StructConstant), struct_(value) {}
    explicit inline AnyConstantValue(FileModuleConstant value) : kind(ConstantValueKind::FileModuleConstant), file_module(value) {}
    explicit inline AnyConstantValue(AnyType value) : kind(ConstantValueKind::TypeConstant), type(value) {}

    static inline AnyConstantValue create_void() {
        AnyConstantValue result {};
        result.kind = ConstantValueKind::VoidConstant;

        return result;
    }

    static inline AnyConstantValue create_undef() {
        AnyConstantValue result {};
        result.kind = ConstantValueKind::UndefConstant;

        return result;
    }

    inline FunctionConstant unwrap_function() {
        assert(kind == ConstantValueKind::FunctionConstant);

        return function;
    }

    inline PolymorphicFunctionConstant unwrap_polymorphic_function() {
        assert(kind == ConstantValueKind::PolymorphicFunctionConstant);

        return polymorphic_function;
    }

    inline BuiltinFunctionConstant unwrap_builtin_function() {
        assert(kind == ConstantValueKind::BuiltinFunctionConstant);

        return builtin_function;
    }

    inline uint64_t unwrap_integer() {
        assert(kind == ConstantValueKind::IntegerConstant);

        return integer;
    }

    inline bool unwrap_boolean() {
        assert(kind == ConstantValueKind::BooleanConstant);

        return boolean;
    }

    inline double unwrap_float() {
        assert(kind == ConstantValueKind::FloatConstant);

        return float_;
    }

    inline ArrayConstant unwrap_array() {
        assert(kind == ConstantValueKind::ArrayConstant);

        return array;
    }

    inline StaticArrayConstant unwrap_static_array() {
        assert(kind == ConstantValueKind::StaticArrayConstant);

        return static_array;
    }

    inline StructConstant unwrap_struct() {
        assert(kind == ConstantValueKind::StructConstant);

        return struct_;
    }

    inline FileModuleConstant unwrap_file_module() {
        assert(kind == ConstantValueKind::FileModuleConstant);

        return file_module;
    }

    inline AnyType unwrap_type() {
        assert(kind == ConstantValueKind::TypeConstant);

        return type;
    }

    String get_description();
};

const size_t DECLARATION_HASH_TABLE_SIZE = 32;

struct DeclarationHashTable {
    List<Statement*> buckets[DECLARATION_HASH_TABLE_SIZE];
};

uint32_t calculate_string_hash(String string);
DeclarationHashTable create_declaration_hash_table(Array<Statement*> statements);
Statement* search_in_declaration_hash_table(DeclarationHashTable declaration_hash_table, String name);
Statement* search_in_declaration_hash_table(DeclarationHashTable declaration_hash_table, uint32_t hash, String name);

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

    ConstantScope* parent;

    String file_path;
};

String get_scope_file_path(ConstantScope scope);

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
    inline TypedConstantValue() {}
    explicit inline TypedConstantValue(AnyType type, AnyConstantValue value) : type(type), value(value) {}

    AnyType type;

    AnyConstantValue value;
};

template <typename T>
struct DelayedResult {
    inline DelayedResult() {}

    inline DelayedResult(Result<T> result) {
        has_value = true;
        status = result.status;
        value = result.value;
    }

    inline DelayedResult(ResultErrorHelper helper) {
        has_value = true;
        status = false;
    }

    bool has_value;

    union {
        struct {
            bool status;
            T value;
        };

        size_t waiting_for;
    };
};

template <>
struct DelayedResult<void> {
    inline DelayedResult() {}

    inline DelayedResult(Result<void> result) {
        has_value = true;
        status = result.status;
    }

    inline DelayedResult(ResultErrorHelper helper) {
        has_value = true;
        status = false;
    }

    bool has_value;

    union {
        bool status;

        size_t waiting_for;
    };
};

struct DelayedResultWaitHelper {
    size_t waiting_for;

    template <typename T>
    inline operator DelayedResult<T>() {
        DelayedResult<T> result;
        result.has_value = false;
        result.waiting_for = waiting_for;
        return result;
    }
};

inline DelayedResultWaitHelper wait(size_t job) {
    DelayedResultWaitHelper helper {};
    helper.waiting_for = job;

    return helper;
}

#define expect_delayed(name, expression) auto __##name##_result=(expression);if(__##name##_result.has_value){if(!__##name##_result.status){return err();}}else return(wait(__##name##_result.waiting_for));auto name = __##name##_result.value
#define expect_delayed_void(expression) auto __void_result=(expression);if(__void_result.has_value){if(!__void_result.status){return err();}}else return(wait(__void_result.waiting_for))

void error(ConstantScope* scope, FileRange range, const char* format, ...);

Result<String> array_to_string(ConstantScope* scope, FileRange range, AnyType type, AnyConstantValue value);

Result<void> check_undetermined_integer_to_integer_coercion(ConstantScope* scope, FileRange range, Integer target_type, uint64_t value, bool probing);
Result<AnyConstantValue> coerce_constant_to_integer_type(
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    Integer target_type,
    bool probing
);
Result<AnyConstantValue> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    AnyType target_type,
    bool probing
);
Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope* scope,
    AnyType type,
    AnyConstantValue value,
    FileRange range,
    AnyType index_type,
    AnyConstantValue index_value,
    FileRange index_range
);
Result<AnyType> determine_binary_operation_type(ConstantScope* scope, FileRange range, AnyType left, AnyType right);
Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope* scope,
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
    ConstantScope* scope,
    AnyType type,
    AnyConstantValue value,
    FileRange value_range,
    AnyType target_type,
    FileRange target_range,
    bool probing
);
DelayedResult<AnyType> evaluate_type_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* ignore_statement,
    Expression* expression
);
Result<AnyType> coerce_to_default_type(GlobalInfo info, ConstantScope* scope, FileRange range, AnyType type);
bool is_declaration_public(Statement* declaration);
bool does_or_could_have_public_name(Statement* statement, String name);
bool does_or_could_have_name(Statement* statement, String name);
DelayedResult<TypedConstantValue> get_simple_resolved_declaration(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* declaration
);
bool constant_values_equal(AnyConstantValue a, AnyConstantValue b);

struct NameSearchResult {
    bool found;

    AnyType type;
    AnyConstantValue value;
};

DelayedResult<NameSearchResult> search_for_name(
    GlobalInfo info,
    List<AnyJob>* jobs,
    String name,
    uint32_t name_hash,
    ConstantScope* scope,
    Array<Statement*> statements,
    DeclarationHashTable declarations,
    bool external,
    Statement* ignore
);

DelayedResult<TypedConstantValue> evaluate_constant_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* ignore_statement,
    Expression* expression
);

struct StaticIfResolutionResult {
    bool condition;

    DeclarationHashTable declarations;
};

DelayedResult<StaticIfResolutionResult> do_resolve_static_if(GlobalInfo info, List<AnyJob>* jobs, StaticIf* static_if, ConstantScope* scope);

DelayedResult<TypedConstantValue> do_resolve_function_declaration(
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionDeclaration* declaration,
    ConstantScope* scope
);

struct FunctionResolutionResult {
    FunctionTypeType type;

    FunctionConstant value;
};

DelayedResult<FunctionResolutionResult> do_resolve_polymorphic_function(
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionDeclaration* declaration,
    TypedConstantValue* parameters,
    ConstantScope* scope,
    ConstantScope* call_scope,
    FileRange* call_parameter_ranges
);

DelayedResult<AnyType> do_resolve_struct_definition(
    GlobalInfo info,
    List<AnyJob>* jobs,
    StructDefinition* struct_definition,
    ConstantScope* scope
);

DelayedResult<AnyType> do_resolve_polymorphic_struct(
    GlobalInfo info,
    List<AnyJob>* jobs,
    StructDefinition* struct_definition,
    AnyConstantValue* parameters,
    ConstantScope* scope
);

DelayedResult<AnyType> do_resolve_union_definition(
    GlobalInfo info,
    List<AnyJob>* jobs,
    UnionDefinition* union_definition,
    ConstantScope* scope
);

DelayedResult<AnyType> do_resolve_polymorphic_union(
    GlobalInfo info,
    List<AnyJob>* jobs,
    UnionDefinition* union_definition,
    AnyConstantValue* parameters,
    ConstantScope* scope
);

DelayedResult<Enum> do_resolve_enum_definition(
    GlobalInfo info,
    List<AnyJob>* jobs,
    EnumDefinition* enum_definition,
    ConstantScope* scope
);

Result<void> process_scope(List<AnyJob>* jobs, ConstantScope* scope, Array<Statement*> statements, List<ConstantScope*>* child_scopes, bool is_top_level);