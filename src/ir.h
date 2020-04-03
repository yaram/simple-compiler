#pragma once

#include <stdint.h>
#include "array.h"
#include "register_size.h"

struct Instruction {
    unsigned int line;

    virtual ~Instruction() {}
};

struct IntegerArithmeticOperation : Instruction {
    enum struct Operation {
        Add,
        Subtract,

        SignedMultiply,
        UnsignedMultiply,
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

    IntegerArithmeticOperation() {}
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

    IntegerComparisonOperation() {}
};

struct IntegerUpcast : Instruction {
    bool is_signed;

    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;

    IntegerUpcast() {}
};

struct IntegerConstantInstruction : Instruction {
    RegisterSize size;

    size_t destination_register;

    uint64_t value;

    IntegerConstantInstruction() {}
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

    FloatArithmeticOperation() {}
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

    FloatComparisonOperation() {}
};

struct FloatConstantInstruction : Instruction {
    RegisterSize size;

    size_t destination_register;

    double value;

    FloatConstantInstruction() {}
};

struct Jump : Instruction {
    size_t destination_instruction;

    Jump() {}
};

struct Branch : Instruction {
    size_t condition_register;

    size_t destination_instruction;

    Branch() {}
};

struct FunctionCallInstruction : Instruction {
    const char *function_name;

    Array<size_t> parameter_registers;

    bool has_return;
    size_t return_register;

    FunctionCallInstruction() {}
};

struct ReturnInstruction : Instruction {
    size_t value_register;

    ReturnInstruction() {}
};

struct AllocateLocal : Instruction {
    size_t size;

    size_t alignment;

    size_t destination_register;

    AllocateLocal() {}
};

struct LoadInteger : Instruction {
    RegisterSize size;

    size_t address_register;

    size_t destination_register;

    LoadInteger() {}
};

struct StoreInteger : Instruction {
    RegisterSize size;

    size_t source_register;

    size_t address_register;

    StoreInteger() {}
};

struct LoadFloat : Instruction {
    RegisterSize size;

    size_t address_register;

    size_t destination_register;

    LoadFloat() {}
};

struct StoreFloat : Instruction {
    RegisterSize size;

    size_t source_register;

    size_t address_register;

    StoreFloat() {}
};

struct CopyMemory : Instruction {
    size_t length_register;

    size_t source_address_register;

    size_t destination_address_register;

    CopyMemory() {}
};

struct ReferenceStatic : Instruction {
    const char *name;

    size_t destination_register;

    ReferenceStatic() {}
};

void print_instruction(Instruction *instruction, bool has_return);

struct RuntimeStatic {
    const char *name;

    virtual ~RuntimeStatic() {}
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

    Function() {}
};

struct StaticConstant : RuntimeStatic {
    size_t alignment;

    Array<uint8_t> data;

    StaticConstant() {}
};

void print_static(RuntimeStatic *runtime_static);