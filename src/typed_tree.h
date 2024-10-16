#pragma once

#include "array.h"
#include "types.h"
#include "ast.h"

/*
Valid type and value combinations:

FunctionTypeType    | ConstantValue(FunctionConstant)
PolymorphicFunction | ConstantValue(PolymorphicFunctionConstant)
BuiltinFunction     | ConstantValue(BultinFunctionConstant)
Integer             | AnonymousValue, AssignableValue, ConstantValue(IntegerConstant, UndefConstant)
UndeterminedInteger | IntegerConstant
Boolean             | AnonymousValue, AssignableValue, ConstantValue(BooleanConstant, UndefConstant)
FloatType           | AnonymousValue, AssignableValue, ConstantValue(FloatConstant, UndefConstant)
UndeterminedFloat   | ConstantValue(FloatConstant)
Type                | ConstantValue(TypeConstant)
Void                | ConstantValue(VoidConstant)
Pointer             | AnonymousValue, AssignableValue, ConstantValue(IntegerConstant, UndefConstant)
ArrayTypeType       | AnonymousValue, AssignableValue, UndeterminedAggregateValue, ConstantValue(ArrayConstant, AggregateConstant, UndefConstant)
StaticArray         | AnonymousValue, AssignableValue, UndeterminedAggregateValue, ConstantValue(AggregateConstant, UndefConstant)
UndeterminedArray   | ConstantValue(AggregateConstant)
StructType          | AnonymousValue, AssignableValue, UndeterminedAggregateValue, ConstantValue(AggregateConstant, UndefConstant)
PolymorphicStruct   | 
UnionType           | AnonymousValue, AssignableValue
PolymorphicUnion    | 
UndeterminedStruct  | ConstantValue(AggregateConstant)
Enum                | AnonymousValue, AssignableValue, ConstantValue(IntegerConstant, UndefConstant)
FileModule          | ConstantValue(FileModuleConstant)
Undef               | ConstantValue(UndefConstant)
MultiReturn         | 
*/

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

struct AggregateConstant {
    inline AggregateConstant() = default;
    explicit inline AggregateConstant(Array<AnyConstantValue> values) : values(values) {}

    Array<AnyConstantValue> values;
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
    AggregateConstant,
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
        AggregateConstant aggregate;
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
    explicit inline AnyConstantValue(AggregateConstant value) : kind(ConstantValueKind::AggregateConstant), aggregate(value) {}
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

    inline AggregateConstant unwrap_aggregate() {
        assert(kind == ConstantValueKind::AggregateConstant);

        return aggregate;
    }

    inline FileModuleConstant unwrap_file_module() {
        assert(kind == ConstantValueKind::FileModuleConstant);

        return file_module;
    }

    inline AnyType unwrap_type() {
        assert(kind == ConstantValueKind::TypeConstant);

        return type;
    }

    bool operator==(AnyConstantValue other);
    inline bool operator!=(AnyConstantValue other) {
        return !(*this == other);
    }

    String get_description(Arena* arena);
};

struct ScopeConstant {
    Identifier name;

    AnyType type;
    AnyConstantValue value;
};

struct ConstantScope {
    Array<Statement*> statements;

    Array<ScopeConstant> scope_constants;

    bool is_top_level;

    ConstantScope* parent;

    String file_path;

    inline String get_file_path() {
        auto current = *this;

        while(!current.is_top_level) {
            current = *current.parent;
        }

        return current.file_path;
    }
};

struct TypedConstantValue {
    inline TypedConstantValue() {}
    explicit inline TypedConstantValue(AnyType type, AnyConstantValue value) : type(type), value(value) {}

    AnyType type;

    AnyConstantValue value;
};

struct GlobalConstant {
    String name;

    AnyType type;

    AnyConstantValue value;
};

struct GlobalInfo {
    Array<GlobalConstant> global_constants;

    ArchitectureSizes architecture_sizes;
};

struct AnyValue;

struct UndeterminedAggregateValue {
    inline UndeterminedAggregateValue() = default;
    explicit inline UndeterminedAggregateValue(Array<AnyValue> values) : values(values) {}

    Array<AnyValue> values;
};

enum struct ValueKind {
    ConstantValue,
    AnonymousValue,
    AssignableValue,
    UndeterminedAggregateValue
};

struct AnyValue {
    ValueKind kind;

    union {
        AnyConstantValue constant;
        UndeterminedAggregateValue undetermined_aggregate;
    };

    inline AnyValue() = default;
    explicit inline AnyValue(AnyConstantValue constant) : kind(ValueKind::ConstantValue), constant(constant) {}
    explicit inline AnyValue(UndeterminedAggregateValue undetermined_aggregate) : kind(ValueKind::UndeterminedAggregateValue), undetermined_aggregate(undetermined_aggregate) {}

    static inline AnyValue create_anonymous_value() {
        AnyValue result;
        result.kind = ValueKind::AnonymousValue;

        return result;
    }

    static inline AnyValue create_assignable_value() {
        AnyValue result;
        result.kind = ValueKind::AssignableValue;

        return result;
    }

    inline AnyConstantValue unwrap_constant_value() {
        assert(kind == ValueKind::ConstantValue);

        return constant;
    }

    inline UndeterminedAggregateValue unwrap_undetermined_aggregate_value() {
        assert(kind == ValueKind::UndeterminedAggregateValue);

        return undetermined_aggregate;
    }
};

struct TypedValue {
    inline TypedValue() {}
    explicit inline TypedValue(AnyType type, AnyValue value) : type(type), value(value) {}

    AnyType type;

    AnyValue value;
};

enum struct TypedExpressionKind {
    VariableReference,
    StaticVariableReference,
    ConstantLiteral,
    BinaryOperation,
    IndexReference,
    MemberReference,
    ArrayLiteral,
    StructLiteral,
    FunctionCall,
    UnaryOperation,
    Cast,
    Bake,
    ArrayType,
    FunctionType,
    Coercion
};

enum struct BinaryOperationKind {
    Addition,
    Subtraction,
    Multiplication,
    Division,
    Modulus,
    BitwiseAnd,
    BitwiseOr,
    BooleanAnd,
    BooleanOr,
    LeftShift,
    RightShift,
    Equal,
    NotEqual,
    GreaterThan,
    LessThan
};

enum struct UnaryOperationKind {
    Pointer,
    PointerDereference,
    BooleanInvert,
    Negation
};

struct TypedStructMember;
struct TypedFunctionParameter;

struct TypedExpression {
    TypedExpressionKind kind;

    FileRange range;

    AnyType type;
    AnyValue value;

    union {
        struct {
            String name;
        } variable_reference;

        struct {
            ConstantScope* scope;

            VariableDeclaration* declaration;
        } static_variable_reference;

        struct {
            BinaryOperationKind kind;

            TypedExpression* left;
            TypedExpression* right;
        } binary_operation;

        struct {
            TypedExpression* value;
            TypedExpression* index;
        } index_reference;

        struct {
            TypedExpression* value;

            Identifier name;
        } member_reference;

        struct {
            Array<TypedExpression> elements;
        } array_literal;

        struct {
            Array<TypedStructMember> members;
        } struct_literal;

        struct {
            TypedExpression* value;

            Array<TypedExpression> parameters;
        } function_call;

        struct {
            UnaryOperationKind kind;

            TypedExpression* value;
        } unary_operation;

        struct {
            TypedExpression* value;
            TypedExpression* type;
        } cast;

        struct {
            TypedExpression* value;

            Array<TypedExpression> parameters;
        } bake;

        struct {
            TypedExpression* length;

            TypedExpression* element_type;
        } array_type;

        struct {
            Array<TypedFunctionParameter> parameters;

            Array<TypedExpression> return_types;

            Array<TypedExpression> tag_parameters;
        } function_type;

        struct {
            TypedExpression* original;
        } coercion;
    };
};

struct TypedStructMember {
    Identifier name;

    TypedExpression member;
};

struct TypedEnumVariant {
    Identifier name;

    bool has_value;
    TypedExpression value;
};

struct TypedFunctionParameter {
    Identifier name;

    TypedExpression type;
};

struct TypedVariable {
    Identifier name;

    AnyType type;
};

enum struct TypedStatementKind {
    ExpressionStatement,
    VariableDeclaration,
    MultiReturnVariableDeclaration,
    Assignment,
    MultiReturnAssignment,
    BinaryOperationAssignment,
    IfStatement,
    WhileLoop,
    ForLoop,
    Return,
    Break,
    InlineAssembly
};

struct TypedName {
    Identifier name;

    AnyType type;
};

struct TypedStatement;

struct TypedElseIf {
    TypedExpression condition;

    ConstantScope* scope;
    Array<TypedStatement> statements;
};

struct TypedBinding {
    String constraint;

    TypedExpression value;
};

struct TypedStatement {
    TypedStatementKind kind;

    FileRange range;

    union {
        struct {
            TypedExpression expression;
        } expression_statement;

        struct {
            Identifier name;

            bool has_type;
            TypedExpression type;

            TypedExpression initializer;

            AnyType actual_type;
        } variable_declaration;

        struct {
            Array<TypedName> names;

            TypedExpression initializer;
        } multi_return_variable_declaration;

        struct {
            TypedExpression target;

            TypedExpression value;
        } assignment;

        struct {
            Array<TypedExpression> targets;

            TypedExpression value;
        } multi_return_assignment;

        struct {
            TypedExpression operation;
        } binary_operation_assignment;

        struct {
            TypedExpression condition;

            ConstantScope* scope;
            Array<TypedStatement> statements;

            Array<TypedElseIf> else_ifs;

            ConstantScope* else_scope;
            Array<TypedStatement> else_statements;
        } if_statement;

        struct {
            TypedExpression condition;

            ConstantScope* scope;
            Array<TypedStatement> statements;
        } while_loop;

        struct {
            TypedExpression from;
            TypedExpression to;

            bool has_index_name;
            TypedName index_name;

            ConstantScope* scope;
            Array<TypedStatement> statements;
        } for_loop;

        struct {
            Array<TypedExpression> values;
        } return_;

        struct {
            String assembly;

            Array<TypedBinding> bindings;  
        } inline_assembly;
    };
};

void error(ConstantScope* scope, FileRange range, const char* format, ...);