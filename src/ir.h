#pragma once

#include <stdint.h>
#include "array.h"
#include "register_size.h"

enum struct ArithmeticOperationType {
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

enum struct ComparisonOperationType {
    Equal,
    SignedLessThan,
    UnsignedLessThan,
    SignedGreaterThan,
    UnsignedGreaterThan
};

enum struct InstructionType {
    ArithmeticOperation,
    ComparisonOperation,

    IntegerUpcast,

    Constant,

    Jump,
    Branch,

    FunctionCall,
    Return,

    AllocateLocal,

    LoadInteger,
    StoreInteger,

    CopyMemory,

    ReferenceStatic
};

struct Instruction {
    InstructionType type;

    union {
        struct {
            ArithmeticOperationType type;

            RegisterSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } arithmetic_operation;

        struct {
            ComparisonOperationType type;

            RegisterSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } comparison_operation;

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

            size_t alignment;

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

        struct {
            size_t length_register;

            size_t source_address_register;

            size_t destination_address_register;
        } copy_memory;

        struct {
            const char *name;

            size_t destination_register;
        } reference_static;
    };
};

void print_instruction(Instruction instruction);

struct Function {
    const char *name;

    Array<RegisterSize> parameter_sizes;

    bool has_return;
    RegisterSize return_size;

    bool is_external;

    Array<Instruction> instructions;
};

void print_function(Function function);

struct StaticConstant {
    const char *name;

    size_t alignment;

    Array<uint8_t> data;
};