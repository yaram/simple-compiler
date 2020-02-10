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
                " %s r%zu, r%zu, r%zu",
                register_size_names[(int)instruction.binary_operation.size],
                instruction.binary_operation.source_register_a,
                instruction.binary_operation.source_register_b,
                instruction.binary_operation.destination_register
            );
        } break;

        case InstructionType::IntegerUpcast: {
            char postfix;
            if(instruction.integer_upcast.is_signed) {
                postfix = 'S';
            } else {
                postfix = 'U';
            }

            printf(
                "CAST%c %s r%zu, %s r%zu",
                postfix,
                register_size_names[(int)instruction.integer_upcast.source_size],
                instruction.integer_upcast.source_register,
                register_size_names[(int)instruction.integer_upcast.destination_size],
                instruction.integer_upcast.destination_register
            );
        } break;

        case InstructionType::Constant: {
            printf("CONST %s ", register_size_names[(int)instruction.constant.size]);

            switch(instruction.constant.size) {
                case RegisterSize::Size8: {
                    printf("%hhu", (uint8_t)instruction.constant.value);
                } break;

                case RegisterSize::Size16: {
                    printf("%hu", (uint16_t)instruction.constant.value);
                } break;

                case RegisterSize::Size32: {
                    printf("%u", (uint32_t)instruction.constant.value);
                } break;

                case RegisterSize::Size64: {
                    printf("%llu", (uint64_t)instruction.constant.value);
                } break;
            }

            printf(", r%zu", instruction.constant.destination_register);
        } break;

        case InstructionType::Jump: {
            printf("JMP %zu", instruction.jump.destination_instruction);
        } break;

        case InstructionType::Branch: {
            printf(
                "BR r%zu, %zu",
                instruction.branch.condition_register,
                instruction.branch.destination_instruction
            );
        } break;

        case InstructionType::FunctionCall: {
            printf("CALL %s (", instruction.function_call.function_name);

            for(size_t i = 0; i < instruction.function_call.parameter_registers.count; i += 1) {
                printf("r%zu", instruction.function_call.parameter_registers[i]);

                if(i != instruction.function_call.parameter_registers.count - 1) {
                    printf(", ");
                }
            }

            printf(")");

            if(instruction.function_call.has_return) {
                printf(" r%zu", instruction.function_call.return_register);
            }
        } break;

        case InstructionType::Return: {
            printf("RET r%zu", instruction.return_.value_register);
        } break;

        case InstructionType::AllocateLocal: {
            printf(
                "LOCAL %zu, r%zu",
                instruction.allocate_local.size,
                instruction.allocate_local.destination_register
            );
        } break;

        case InstructionType::LoadInteger: {
            printf(
                "LOAD %s r%zu, r%zu",
                register_size_names[(int)instruction.load_integer.size],
                instruction.load_integer.address_register,
                instruction.load_integer.destination_register
            );
        } break;

        case InstructionType::StoreInteger: {
            printf(
                "STORE %s r%zu, r%zu",
                register_size_names[(int)instruction.store_integer.size],
                instruction.store_integer.source_register,
                instruction.store_integer.address_register
            );
        } break;

        case InstructionType::CopyMemory: {
            printf(
                "COPY r%zu, r%zu, r%zu",
                instruction.copy_memory.length_register,
                instruction.copy_memory.source_address_register,
                instruction.copy_memory.destination_address_register
            );
        } break;

        case InstructionType::ReferenceStatic: {
            printf(
                "STATIC %s r%zu",
                instruction.reference_static.name,
                instruction.reference_static.destination_register
            );
        } break;
    }
}

void print_function(Function function) {
    printf("%s (", function.name);

    for(size_t i = 0; i < function.parameter_sizes.count; i += 1) {
        printf(
            "r%zu: %s",
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

    if(function.is_external) {
        printf(" extern");
    } else {
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
}