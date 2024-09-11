#pragma once

#include <stdint.h>
#include "ast.h"
#include "array.h"
#include "register_size.h"
#include "calling_convention.h"
#include "string.h"
#include "util.h"

enum struct IRTypeKind {
    Function,
    Boolean,
    Integer,
    Float,
    Pointer,
    StaticArray,
    Struct,
    Void
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

        IRType* pointer;

        struct {
            uint64_t length;

            IRType* element_type;
        } static_array;

        struct {
            Array<IRType> members;
        } struct_;
    };

    static inline IRType create_function(Array<IRType> parameters, IRType* return_type, CallingConvention calling_convention) {
        IRType result {};
        result.kind = IRTypeKind::Function;
        result.function.parameters = parameters;
        result.function.return_type = return_type;
        result.function.calling_convention = calling_convention;

        return result;
    }

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

    static inline IRType create_pointer(IRType* pointed_to_type) {
        IRType result {};
        result.kind = IRTypeKind::Pointer;
        result.pointer = pointed_to_type;

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

    static inline IRType create_void() {
        IRType result {};
        result.kind = IRTypeKind::Void;

        return result;
    }
};

enum struct IRConstantValueKind {
    FunctionConstant,
    IntegerConstant,
    FloatConstant,
    BooleanConstant,
    StaticArrayConstant,
    StructConstant
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
            Array<IRConstantValue> elements;
        } static_array;

        struct {
            Array<IRConstantValue> members;
        } struct_;
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

    static inline IRConstantValue create_static_array(Array<IRConstantValue> elements) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::StaticArrayConstant;
        result.static_array.elements = elements;

        return result;
    }

    static inline IRConstantValue create_struct(Array<IRConstantValue> members) {
        IRConstantValue result;
        result.kind = IRConstantValueKind::StructConstant;
        result.struct_.members = members;

        return result;
    }

};

enum struct InstructionKind {
    IntegerArithmeticOperation,
    IntegerComparisonOperation,
    IntegerExtension,
    IntegerTruncation,
    FloatArithmeticOperation,
    FloatComparisonOperation,
    FloatConversion,
    FloatTruncation,
    FloatFromInteger,
    IntegerFromFloat,
    PointerEquality,
    PointerConversion,
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
    ReturnInstruction,
    AllocateLocal,
    Load,
    Store,
    StructMemberPointer,
    PointerIndex,
    ReferenceStatic
};

struct Instruction {
    InstructionKind kind;

    FileRange range;

    void print(bool has_return);
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
        Divide
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

struct FloatTruncation : Instruction {
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    inline FloatTruncation() : Instruction { InstructionKind::FloatTruncation } {}
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

struct PointerConversion : Instruction {
    size_t source_register;

    IRType destination_pointed_to_type;
    size_t destination_register;

    inline PointerConversion() : Instruction { InstructionKind::PointerConversion } {}
};

struct PointerFromInteger : Instruction {
    size_t source_register;

    IRType destination_pointed_to_type;
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
    size_t index_register;

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
    size_t destination_instruction;

    inline Jump() : Instruction { InstructionKind::Jump } {}
};

struct Branch : Instruction {
    size_t condition_register;

    size_t destination_instruction;

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

struct ReturnInstruction : Instruction {
    size_t value_register;

    inline ReturnInstruction() : Instruction { InstructionKind::ReturnInstruction } {}
};

struct AllocateLocal : Instruction {
    IRType type;

    size_t destination_register;

    inline AllocateLocal() : Instruction { InstructionKind::AllocateLocal } {}
};

struct Load : Instruction {
    size_t pointer_register;

    size_t destination_register;

    inline Load() : Instruction { InstructionKind::Load } {}
};

struct Store : Instruction {
    size_t source_register;

    size_t pointer_register;

    inline Store() : Instruction { InstructionKind::Store } {}
};

struct StructMemberPointer : Instruction {
    size_t member_index;

    size_t pointer_register;

    size_t destination_register;

    inline StructMemberPointer() : Instruction { InstructionKind::StructMemberPointer } {}
};

struct PointerIndex : Instruction {
    size_t index_register;

    size_t pointer_register;

    size_t destination_register;

    inline PointerIndex() : Instruction { InstructionKind::PointerIndex } {}
};

struct RuntimeStatic;

struct ReferenceStatic : Instruction {
    RuntimeStatic* runtime_static;

    size_t destination_register;

    inline ReferenceStatic() : Instruction { InstructionKind::ReferenceStatic } {}
};

enum struct RuntimeStaticKind {
    Function,
    StaticVariable
};

struct RuntimeStatic {
    RuntimeStaticKind kind;

    String name;
    bool is_no_mangle;

    String path;
    FileRange range;

    void print();
};

struct Function : RuntimeStatic {
    Array<IRType> parameters;

    bool has_return;
    IRType return_type;

    bool is_external;

    Array<Instruction*> instructions;

    Array<String> libraries;

    CallingConvention calling_convention;

    inline Function() : RuntimeStatic { RuntimeStaticKind::Function } {}
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