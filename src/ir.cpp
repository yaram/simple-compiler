#include "ir.h"
#include <stdio.h>
#include <string.h>

static const char *register_size_names[] = { "8", "16", "32", "64" };

void print_instruction(Instruction instruction) {
    switch(instruction.type) {
        case InstructionType::BinaryOperation: {
            const char *operation_text;

            switch(instruction.binary_operation.type) {
                case BinaryOperationType::Add: {
                    printf("ADD ");
                } break;

                case BinaryOperationType::Subtract: {
                    printf("SUB ");
                } break;

                case BinaryOperationType::SignedMultiply: {
                    printf("MUL ");
                } break;

                case BinaryOperationType::UnsignedMultiply: {
                    printf("UMUL ");
                } break;

                case BinaryOperationType::SignedDivide: {
                    printf("DIV ");
                } break;

                case BinaryOperationType::UnsignedDivide: {
                    printf("UDIV ");
                } break;

                case BinaryOperationType::SignedModulus: {
                    printf("MOD ");
                } break;

                case BinaryOperationType::UnsignedModulus: {
                    printf("UMOD ");
                } break;

                case BinaryOperationType::Equality: {
                    printf("EQ ");
                } break;
            }

            printf(
                " %s %zu, %zu, %zu",
                register_size_names[(int)instruction.binary_operation.size],
                instruction.binary_operation.source_register_a,
                instruction.binary_operation.source_register_b,
                instruction.binary_operation.destination_register
            );
        } break;

        case InstructionType::SignExtension: {
            printf(
                "SEXT %s %zu, %s %zu",
                register_size_names[(int)instruction.sign_extension.source_size],
                instruction.sign_extension.source_register,
                register_size_names[(int)instruction.sign_extension.destination_size],
                instruction.sign_extension.destination_register
            );
        } break;

        case InstructionType::Constant: {
            printf("CONST %s ", register_size_names[(int)instruction.constant.size]);

            switch(instruction.constant.size) {
                case RegisterSize::Size8: {
                    printf("%hhu", instruction.constant.size_8);
                } break;

                case RegisterSize::Size16: {
                    printf("%hu", instruction.constant.size_16);
                } break;

                case RegisterSize::Size32: {
                    printf("%u", instruction.constant.size_32);
                } break;

                case RegisterSize::Size64: {
                    printf("%llu", instruction.constant.size_64);
                } break;

            }
        } break;

        case InstructionType::Jump: {
            printf("JMP %zu", instruction.jump.destination_instruction);
        } break;

        case InstructionType::Branch: {
            printf(
                "BR %zu, %zu",
                instruction.branch.condition_register,
                instruction.branch.destination_instruction
            );
        } break;

        case InstructionType::FunctionCall: {
            printf("CALL %s (", instruction.function_call.function_name);

            for(size_t i = 0; i < instruction.function_call.parameter_registers.count; i += 1) {
                printf("%zu", instruction.function_call.parameter_registers[i]);

                if(i != instruction.function_call.parameter_registers.count - 1) {
                    printf(", ");
                }
            }

            printf(")");

            if(instruction.function_call.has_return) {
                printf(" %zu", instruction.function_call.return_register);
            }
        } break;

        case InstructionType::Return: {
            printf("RET %zu", instruction.return_.value_register);
        } break;

        case InstructionType::AllocateLocal: {
            printf(
                "LOCAL %s %zu",
                register_size_names[(int)instruction.allocate_local.size],
                instruction.allocate_local.destination_register
            );
        } break;

        case InstructionType::LoadInteger: {
            printf(
                "LOAD %s %zu, %zu",
                register_size_names[(int)instruction.load_integer.size],
                instruction.load_integer.address_register,
                instruction.load_integer.destination_register
            );
        } break;

        case InstructionType::StoreInteger: {
            printf(
                "STORE %s %zu, %zu",
                register_size_names[(int)instruction.store_integer.size],
                instruction.store_integer.source_register,
                instruction.load_integer.address_register
            );
        } break;
    }
}

void print_function(Function function) {
    printf("%s (");

    for(size_t i = 0; i < function.parameter_sizes.count; i += 1) {
        printf(
            "%zu: %s",
            i,
            register_size_names[(int)function.parameter_sizes[i]]
        );

        if(i != function.parameter_sizes.count - 1) {
            printf(", ");
        }
    }

    printf(")");

    if(function.has_return) {
        printf(" %s", register_size_names[(int)function.return_size]);
    }

    printf("\n");

    char buffer[20];
    sprintf(buffer, "%zu", function.instructions.count - 1);
    size_t max_index_digits = strlen(buffer);

    for(size_t i = 0; i < function.instructions.count; i += 1) {
        auto index_digits = printf("%zu", i);

        for(size_t j = 0; j < max_index_digits - index_digits; j += 1) {
            printf(" ");
        }

        printf(" : ");

        print_instruction(function.instructions[i]);

        if(i != function.instructions.count - 1) {
            printf("\n");
        }
    }
}