#pragma once

#include <stdint.h>
#include "ast.h"
#include "array.h"
#include "register_size.h"
#include "calling_convention.h"
#include "string.h"
#include "util.h"
#include "types.h"

enum struct IRTypeKind {
    Boolean,
    Integer,
    Float,
    Pointer,
    StaticArray,
    Struct
};

struct IRType {
    IRTypeKind kind;

    union {
        struct {
            Array<IRType> parameters;

            IRType* return_type;

            CallingConvention calling_convention;
        } function;

        struct {
            RegisterSize size;
        } integer;

        struct {
            RegisterSize size;
        } float_;

        struct {
            uint64_t length;

            IRType* element_type;
        } static_array;

        struct {
            Array<IRType> members;
        } struct_;
    };

    bool operator==(IRType other);
    bool operator!=(IRType other);

    static inline IRType create_boolean() {
        IRType result {};
        result.kind = IRTypeKind::Boolean;

        return result;
    }

    static inline IRType create_integer(RegisterSize size) {
        IRType result {};
        result.kind = IRTypeKind::Integer;
        result.integer.size = size;

        return result;
    }

    static inline IRType create_float(RegisterSize size) {
        IRType result {};
        result.kind = IRTypeKind::Float;
        result.float_.size = size;

        return result;
    }

    static inline IRType create_pointer() {
        IRType result {};
        result.kind = IRTypeKind::Pointer;

        return result;
    }

    static inline IRType create_static_array(size_t length, IRType* element_type) {
        IRType result {};
        result.kind = IRTypeKind::StaticArray;
        result.static_array.length = length;
        result.static_array.element_type = element_type;

        return result;
    }

    static inline IRType create_struct(Array<IRType> members) {
        IRType result {};
        result.kind = IRTypeKind::Struct;
        result.struct_.members = members;

        return result;
    }

    void print();
};

enum struct IRConstantValueKind {
    FunctionConstant,
    IntegerConstant,
    FloatConstant,
    BooleanConstant,
    AggregateConstant,
    UndefConstant
};

struct IRConstantValue {
    IRConstantValueKind kind;

    union {
        struct {
            FunctionDeclaration* declaration;

            bool is_external;
            Array<String> external_libraries;

            bool is_no_mangle;
        } function;

        uint64_t integer;

        double float_;

        bool boolean;

        struct {
            Array<IRConstantValue> values;
        } aggregate;
    };

    static inline IRConstantValue create_function(FunctionDeclaration* declaration, bool is_external, Array<String> external_libraries, bool is_no_mangle) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::FunctionConstant;
        result.function.declaration = declaration;
        result.function.is_external = is_external;
        result.function.external_libraries = external_libraries;
        result.function.is_no_mangle = is_no_mangle;

        return result;
    }

    static inline IRConstantValue create_integer(uint64_t value) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::IntegerConstant;
        result.integer = value;

        return result;
    }

    static inline IRConstantValue create_float(double value) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::FloatConstant;
        result.float_ = value;

        return result;
    }

    static inline IRConstantValue create_boolean(bool value) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::BooleanConstant;
        result.boolean = value;

        return result;
    }

    static inline IRConstantValue create_aggregate(Array<IRConstantValue> values) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::AggregateConstant;
        result.aggregate.values = values;

        return result;
    }

    static inline IRConstantValue create_undef() {
        IRConstantValue result;
        result.kind = IRConstantValueKind::UndefConstant;

        return result;
    }

    void print();
};

struct Block;

enum struct InstructionKind {
    IntegerArithmeticOperation,
    IntegerComparisonOperation,
    IntegerExtension,
    IntegerTruncation,
    FloatArithmeticOperation,
    FloatComparisonOperation,
    FloatConversion,
    FloatFromInteger,
    IntegerFromFloat,
    PointerEquality,
    PointerFromInteger,
    IntegerFromPointer,
    BooleanArithmeticOperation,
    BooleanEquality,
    BooleanInversion,
    AssembleStaticArray,
    ReadStaticArrayElement,
    AssembleStruct,
    ReadStructMember,
    Literal,
    Jump,
    Branch,
    FunctionCallInstruction,
    IntrinsicCallInstruction,
    ReturnInstruction,
    AllocateLocal,
    Load,
    Store,
    StructMemberPointer,
    PointerIndex,
    AssemblyInstruction,
    ReferenceStatic
};

struct Instruction {
    InstructionKind kind;

    FileRange range;

    size_t debug_scope_index;

    void print(Array<Block*> blocks, bool has_return);
};

struct IntegerArithmeticOperation : Instruction {
    enum struct Operation {
        Add,
        Subtract,

        Multiply,

        SignedDivide,
        UnsignedDivide,
        SignedModulus,
        UnsignedModulus,

        BitwiseAnd,
        BitwiseOr,

        LeftShift,
        RightShift,
        RightArithmeticShift
    };

    Operation operation;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline IntegerArithmeticOperation() : Instruction { InstructionKind::IntegerArithmeticOperation } {}
};

struct IntegerComparisonOperation : Instruction {
    enum struct Operation {
        Equal,
        SignedLessThan,
        UnsignedLessThan,
        SignedGreaterThan,
        UnsignedGreaterThan
    };

    Operation operation;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline IntegerComparisonOperation() : Instruction { InstructionKind::IntegerComparisonOperation } {}
};

struct IntegerExtension : Instruction {
    bool is_signed;

    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline IntegerExtension() : Instruction { InstructionKind::IntegerExtension } {}
};

struct IntegerTruncation : Instruction {
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline IntegerTruncation() : Instruction { InstructionKind::IntegerTruncation } {}
};

struct FloatArithmeticOperation : Instruction {
    enum struct Operation {
        Add,
        Subtract,
        Multiply,
        Divide,
        Modulus
    };

    Operation operation;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline FloatArithmeticOperation() : Instruction { InstructionKind::FloatArithmeticOperation } {}
};

struct FloatComparisonOperation : Instruction {
    enum struct Operation {
        Equal,
        LessThan,
        GreaterThan
    };

    Operation operation;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline FloatComparisonOperation() : Instruction { InstructionKind::FloatComparisonOperation } {}
};

struct FloatConversion : Instruction {
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline FloatConversion() : Instruction { InstructionKind::FloatConversion } {}
};

struct FloatFromInteger : Instruction {
    bool is_signed;

    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline FloatFromInteger() : Instruction { InstructionKind::FloatFromInteger } {}
};

struct IntegerFromFloat : Instruction {
    bool is_signed;

    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline IntegerFromFloat() : Instruction { InstructionKind::IntegerFromFloat } {}
};

struct PointerEquality : Instruction {
    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline PointerEquality() : Instruction { InstructionKind::PointerEquality } {}
};

struct PointerFromInteger : Instruction {
    size_t source_register;

    size_t destination_register;

    inline PointerFromInteger() : Instruction { InstructionKind::PointerFromInteger } {}
};

struct IntegerFromPointer : Instruction {
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline IntegerFromPointer() : Instruction { InstructionKind::IntegerFromPointer } {}
};

struct BooleanArithmeticOperation : Instruction {
    enum struct Operation {
        BooleanAnd,
        BooleanOr,
    };

    Operation operation;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline BooleanArithmeticOperation() : Instruction { InstructionKind::BooleanArithmeticOperation } {}
};

struct BooleanEquality : Instruction {
    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    inline BooleanEquality() : Instruction { InstructionKind::BooleanEquality } {}
};

struct BooleanInversion : Instruction {
    size_t source_register;

    size_t destination_register;

    inline BooleanInversion() : Instruction { InstructionKind::BooleanInversion } {}
};

struct AssembleStaticArray : Instruction {
    Array<size_t> element_registers;

    size_t destination_register;

    inline AssembleStaticArray() : Instruction { InstructionKind::AssembleStaticArray } {}
};

struct ReadStaticArrayElement : Instruction {
    size_t element_index;

    size_t source_register;

    size_t destination_register;

    inline ReadStaticArrayElement() : Instruction { InstructionKind::ReadStaticArrayElement } {}
};

struct AssembleStruct : Instruction {
    Array<size_t> member_registers;

    size_t destination_register;

    inline AssembleStruct() : Instruction { InstructionKind::AssembleStruct } {}
};

struct ReadStructMember : Instruction {
    size_t member_index;

    size_t source_register;

    size_t destination_register;

    inline ReadStructMember() : Instruction { InstructionKind::ReadStructMember } {}
};

struct Literal : Instruction {
    IRType type;
    IRConstantValue value;

    size_t destination_register;

    inline Literal() : Instruction { InstructionKind::Literal } {}
};

struct Jump : Instruction {
    Block* destination_block;

    inline Jump() : Instruction { InstructionKind::Jump } {}
};

struct Branch : Instruction {
    size_t condition_register;

    Block* true_destination_block;
    Block* false_destination_block;

    inline Branch() : Instruction { InstructionKind::Branch } {}
};

struct FunctionCallInstruction : Instruction {
    struct Parameter {
        IRType type;

        size_t register_index;
    };

    size_t pointer_register;

    Array<Parameter> parameters;

    bool has_return;
    IRType return_type;
    size_t return_register;

    CallingConvention calling_convention;

    inline FunctionCallInstruction() : Instruction { InstructionKind::FunctionCallInstruction } {}
};

struct IntrinsicCallInstruction : Instruction {
    struct Parameter {
        IRType type;

        size_t register_index;
    };

    enum struct Intrinsic {
        Sqrt
    };

    Intrinsic intrinsic;

    Array<Parameter> parameters;

    bool has_return;
    IRType return_type;
    size_t return_register;

    inline IntrinsicCallInstruction() : Instruction { InstructionKind::IntrinsicCallInstruction } {}
};

struct ReturnInstruction : Instruction {
    size_t value_register;

    inline ReturnInstruction() : Instruction { InstructionKind::ReturnInstruction } {}
};

struct AllocateLocal : Instruction {
    IRType type;

    size_t destination_register;

    bool has_debug_info;
    String debug_name;
    AnyType debug_type;

    inline AllocateLocal() : Instruction { InstructionKind::AllocateLocal } {}
};

struct Load : Instruction {
    size_t pointer_register;

    IRType destination_type;
    size_t destination_register;

    inline Load() : Instruction { InstructionKind::Load } {}
};

struct Store : Instruction {
    size_t source_register;

    size_t pointer_register;

    inline Store() : Instruction { InstructionKind::Store } {}
};

struct StructMemberPointer : Instruction {
    Array<IRType> members;
    size_t member_index;

    size_t pointer_register;

    size_t destination_register;

    inline StructMemberPointer() : Instruction { InstructionKind::StructMemberPointer } {}
};

struct PointerIndex : Instruction {
    size_t index_register;

    IRType pointed_to_type;
    size_t pointer_register;

    size_t destination_register;

    inline PointerIndex() : Instruction { InstructionKind::PointerIndex } {}
};

struct AssemblyInstruction : Instruction {
    struct Binding {
        String constraint;

        IRType pointed_to_type; // Only used if output binding
        size_t register_index;
    };

    String assembly;

    Array<Binding> bindings;

    inline AssemblyInstruction() : Instruction { InstructionKind::AssemblyInstruction } {}
};

struct RuntimeStatic;

struct ReferenceStatic : Instruction {
    RuntimeStatic* runtime_static;

    size_t destination_register;

    inline ReferenceStatic() : Instruction { InstructionKind::ReferenceStatic } {}
};

struct Block {
    Array<Instruction*> instructions;
};

enum struct RuntimeStaticKind {
    Function,
    StaticConstant,
    StaticVariable
};

struct RuntimeStatic {
    RuntimeStaticKind kind;

    String name;
    bool is_no_mangle;

    String path;
    FileRange range;

    AnyType debug_type;

    void print();
};

struct DebugScope {
    bool has_parent;
    size_t parent_scope_index;

    FileRange range;
};

struct Function : RuntimeStatic {
    Array<IRType> parameters;

    bool has_return;
    IRType return_type;

    bool is_external;

    Array<Block*> blocks;

    Array<String> libraries;

    CallingConvention calling_convention;

    Array<DebugScope> debug_scopes;

    inline Function() : RuntimeStatic { RuntimeStaticKind::Function } {}
};

struct StaticConstant : RuntimeStatic {
    IRType type;

    IRConstantValue value;

    StaticConstant() : RuntimeStatic { RuntimeStaticKind::StaticConstant } {}
};

struct StaticVariable : RuntimeStatic {
    IRType type;

    bool is_external;

    union {
        Array<String> libraries;

        struct {
            bool has_initial_value;
            IRConstantValue initial_value;
        };
    };

    inline StaticVariable() : RuntimeStatic { RuntimeStaticKind::StaticVariable } {}
};