#pragma once

#include "array.h"

enum struct IntegerSize {
    Size8,
    Size16,
    Size32,
    Size64
};

enum struct Opcode {
    IntegerAdd,
    IntegerSubtract,
    IntegerMultiply,
    IntegerDivide,
    IntegerModulo,

    IntegerNegation,
    IntegerConversion,

    IntegerEquality,
    BooleanEquality,

    BooleanInvert,

    Branch,

    FunctionCall,
    Return,

    LoadLocal,
    StoreLocal,

    LocalPointer,

    LoadMemory,
    StoreMemory
};

struct Instruction {
    Opcode opcode;

    union {
        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_add;

        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_subtract;

        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_multiply;

        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_divide;

        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_modulo;

        struct {
            IntegerSize source_size;
            size_t source_register;

            IntegerSize destination_size;
            size_t destination_register;
        } integer_conversion;

        struct {
            IntegerSize size;

            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } integer_equality;

        struct {
            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } boolean_equality;

        struct {
            size_t source_register_a;
            size_t source_register_b;

            size_t destination_register;
        } boolean_equality;

        struct {
            size_t source_register;

            size_t destination_register;
        } boolean_invert;

        struct {
            size_t condition_register;

            size_t destination_instruction;
        } branch;

        struct {
            const char *function_name;

            size_t *parameter_registers;
        } function_call;

        struct {
            size_t value_register;
        } return_;

        struct {
            const char *source_local;

            size_t destination_register;
        } load_local;

        struct {
            size_t source_register;

            const char *destination_local;
        } store_local;

        struct {
            const char *local;

            size_t destination_register;
        } local_pointer;

        struct {
            size_t address_register;

            size_t destination_register;
        } memory_load;

        struct {
            size_t source_register;

            size_t address_register;
        } memory_store;
    };
};

struct Function {
    const char *name;

    Array<const char*> parameter_locals;

    bool has_return;

    Array<Instruction> instructions;
};