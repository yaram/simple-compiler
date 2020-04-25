#pragma once

#include <stdint.h>
#include "array.h"
#include "register_size.h"

enum struct InstructionKind {
    IntegerArithmeticOperation,
    IntegerComparisonOperation,
    IntegerUpcast,
    IntegerConstantInstruction,
    FloatArithmeticOperation,
    FloatComparisonOperation,
    FloatConversion,
    FloatTruncation,
    FloatFromInteger,
    FloatConstantInstruction,
    Jump,
    Branch,
    FunctionCallInstruction,
    ReturnInstruction,
    AllocateLocal,
    LoadInteger,
    StoreInteger,
    LoadFloat,
    StoreFloat,
    CopyMemory,
    ReferenceStatic
};

struct Instruction {
    InstructionKind kind;

    unsigned int line;
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
        BitwiseOr
    };

    Operation operation;

    RegisterSize size;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    IntegerArithmeticOperation() : Instruction { InstructionKind::IntegerArithmeticOperation } {}
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

    RegisterSize size;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    IntegerComparisonOperation() : Instruction { InstructionKind::IntegerComparisonOperation } {}
};

struct IntegerUpcast : Instruction {
    bool is_signed;

    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    IntegerUpcast() : Instruction { InstructionKind::IntegerUpcast } {}
};

struct IntegerConstantInstruction : Instruction {
    RegisterSize size;

    size_t destination_register;

    uint64_t value;

    IntegerConstantInstruction() : Instruction { InstructionKind::IntegerConstantInstruction } {}
};

struct FloatArithmeticOperation : Instruction {
    enum struct Operation {
        Add,
        Subtract,
        Multiply,
        Divide
    };

    Operation operation;

    RegisterSize size;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    FloatArithmeticOperation() : Instruction { InstructionKind::FloatArithmeticOperation } {}
};

struct FloatComparisonOperation : Instruction {
    enum struct Operation {
        Equal,
        LessThan,
        GreaterThan
    };

    Operation operation;

    RegisterSize size;

    size_t source_register_a;
    size_t source_register_b;

    size_t destination_register;

    FloatComparisonOperation() : Instruction { InstructionKind::FloatComparisonOperation } {}
};

struct FloatConversion : Instruction {
    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    FloatConversion() : Instruction { InstructionKind::FloatConversion } {}
};

struct FloatTruncation : Instruction {
    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    FloatTruncation() : Instruction { InstructionKind::FloatTruncation } {}
};

struct FloatFromInteger : Instruction {
    bool is_signed;

    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    FloatFromInteger() : Instruction { InstructionKind::FloatFromInteger } {}
};

struct FloatConstantInstruction : Instruction {
    RegisterSize size;

    size_t destination_register;

    double value;

    FloatConstantInstruction() : Instruction { InstructionKind::FloatConstantInstruction } {}
};

struct Jump : Instruction {
    size_t destination_instruction;

    Jump() : Instruction { InstructionKind::Jump } {}
};

struct Branch : Instruction {
    size_t condition_register;

    size_t destination_instruction;

    Branch() : Instruction { InstructionKind::Branch } {}
};

struct FunctionCallInstruction : Instruction {
    struct Parameter {
        RegisterSize size;

        bool is_float;

        size_t register_index;
    };

    size_t address_register;

    Array<Parameter> parameters;

    bool has_return;
    RegisterSize return_size;
    bool is_return_float;
    size_t return_register;

    FunctionCallInstruction() : Instruction { InstructionKind::FunctionCallInstruction } {}
};

struct ReturnInstruction : Instruction {
    size_t value_register;

    ReturnInstruction() : Instruction { InstructionKind::ReturnInstruction } {}
};

struct AllocateLocal : Instruction {
    size_t size;

    size_t alignment;

    size_t destination_register;

    AllocateLocal() : Instruction { InstructionKind::AllocateLocal } {}
};

struct LoadInteger : Instruction {
    RegisterSize size;

    size_t address_register;

    size_t destination_register;

    LoadInteger() : Instruction { InstructionKind::LoadInteger } {}
};

struct StoreInteger : Instruction {
    RegisterSize size;

    size_t source_register;

    size_t address_register;

    StoreInteger() : Instruction { InstructionKind::StoreInteger } {}
};

struct LoadFloat : Instruction {
    RegisterSize size;

    size_t address_register;

    size_t destination_register;

    LoadFloat() : Instruction { InstructionKind::LoadFloat } {}
};

struct StoreFloat : Instruction {
    RegisterSize size;

    size_t source_register;

    size_t address_register;

    StoreFloat() : Instruction { InstructionKind::StoreFloat } {}
};

struct CopyMemory : Instruction {
    size_t length_register;

    size_t source_address_register;

    size_t destination_address_register;

    size_t alignment;

    CopyMemory() : Instruction { InstructionKind::CopyMemory } {}
};

struct ReferenceStatic : Instruction {
    const char *name;

    size_t destination_register;

    ReferenceStatic() : Instruction { InstructionKind::ReferenceStatic } {}
};

void print_instruction(Instruction *instruction, bool has_return);

enum struct RuntimeStaticKind {
    Function,
    StaticConstant,
    StaticVariable
};

struct RuntimeStatic {
    RuntimeStaticKind kind;

    const char *name;
};

struct Function : RuntimeStatic {
    struct Parameter {
        RegisterSize size;

        bool is_float;
    };

    Array<Parameter> parameters;

    bool has_return;
    RegisterSize return_size;
    bool is_return_float;

    bool is_external;

    Array<Instruction*> instructions;

    const char *file;
    unsigned int line;

    Function() : RuntimeStatic { RuntimeStaticKind::Function } {}
};

struct StaticConstant : RuntimeStatic {
    size_t alignment;

    Array<uint8_t> data;

    StaticConstant() : RuntimeStatic { RuntimeStaticKind::StaticConstant } {}
};

struct StaticVariable : RuntimeStatic {
    size_t size;

    size_t alignment;

    bool is_external;

    bool has_initial_data;
    uint8_t *initial_data;

    StaticVariable() : RuntimeStatic { RuntimeStaticKind::StaticVariable } {}
};

void print_static(RuntimeStatic *runtime_static);