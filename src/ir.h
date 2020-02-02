#pragma once

#include <stdint.h>
#include "array.h"

enum struct RegisterSize {
    Size8,
    Size16,
    Size32,
    Size64
};

enum struct BinaryOperationType {
    Add,
    Subtract,
    SignedMultiply,
    UnsignedMultiply,
    SignedDivide,
    UnsignedDivide,
    SignedModulus,
    UnsignedModulus,
    Equality
};

enum struct InstructionType {
    BinaryOperation,

    IntegerUpcast,

    Constant,

    Jump,
    Branch,

    FunctionCall,
    Return,

    AllocateLocal,

    LoadInteger,
    StoreInteger
};

struct Instruction {
    InstructionType type;

    union {
        struct {
            BinaryOperationType type;

            RegisterSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } binary_operation;

        struct {
            bool is_signed;

            RegisterSize source_size;
            size_t source_register;

            RegisterSize destination_size;
            size_t destination_register;
        } integer_upcast;

        struct {
            RegisterSize size;

            size_t destination_register;

            uint64_t value;
        } constant;

        struct {
            size_t destination_instruction;
        } jump;

        struct {
            size_t condition_register;

            size_t destination_instruction;
        } branch;

        struct {
            const char *function_name;

            Array<size_t> parameter_registers;

            bool has_return;
            size_t return_register;
        } function_call;

        struct {
            size_t value_register;
        } return_;

        struct {
            size_t size;

            size_t destination_register;
        } allocate_local;

        struct {
            RegisterSize size;

            size_t address_register;

            size_t destination_register;
        } load_integer;

        struct {
            RegisterSize size;

            size_t source_register;

            size_t address_register;
        } store_integer;
    };
};

void print_instruction(Instruction instruction);

struct Function {
    const char *name;

    Array<RegisterSize> parameter_sizes;

    bool has_return;
    RegisterSize return_size;

    Array<Instruction> instructions;
};

void print_function(Function function);