#pragma once

#include <stdint.h>
#include "array.h"
#include "register_size.h"

struct Instruction {
    unsigned int line;

    virtual ~Instruction() {}
};

struct ArithmeticOperation : Instruction {
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
};

struct ComparisonOperation : Instruction {
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
};

struct IntegerUpcast : Instruction {
    bool is_signed;

    RegisterSize source_size;
    size_t source_register;

    RegisterSize destination_size;
    size_t destination_register;
};

struct Constant : Instruction {
    RegisterSize size;

    size_t destination_register;

    uint64_t value;
};

struct Jump : Instruction {
    size_t destination_instruction;
};

struct Branch : Instruction {
    size_t condition_register;

    size_t destination_instruction;
};

struct FunctionCallInstruction : Instruction {
    const char *function_name;

    Array<size_t> parameter_registers;

    bool has_return;
    size_t return_register;
};

struct ReturnInstruction : Instruction {
    size_t value_register;
};

struct AllocateLocal : Instruction {
    size_t size;

    size_t alignment;

    size_t destination_register;
};

struct LoadInteger : Instruction {
    RegisterSize size;

    size_t address_register;

    size_t destination_register;
};

struct StoreInteger : Instruction {
    RegisterSize size;

    size_t source_register;

    size_t address_register;
};

struct CopyMemory : Instruction {
    size_t length_register;

    size_t source_address_register;

    size_t destination_address_register;
};

struct ReferenceStatic : Instruction {
    const char *name;

    size_t destination_register;
};

void print_instruction(Instruction *instruction, bool has_return);

struct RuntimeStatic {
    const char *name;

    virtual ~RuntimeStatic() {}
};

struct Function : RuntimeStatic {
    Array<RegisterSize> parameter_sizes;

    bool has_return;
    RegisterSize return_size;

    bool is_external;

    Array<Instruction*> instructions;

    const char *file;
    unsigned int line;
};

struct StaticConstant : RuntimeStatic {
    size_t alignment;

    Array<uint8_t> data;
};

void print_static(RuntimeStatic *runtime_static);