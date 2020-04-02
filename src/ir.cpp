#include "ir.h"
#include <stdio.h>
#include <string.h>

static const char *register_size_names[] = { "8", "16", "32", "64" };
static const char *register_size_name(RegisterSize size) {
    switch(size) {
        case RegisterSize::Size8: {
            return "8";
        } break;

        case RegisterSize::Size16: {
            return "16";
        } break;

        case RegisterSize::Size32: {
            return "32";
        } break;

        case RegisterSize::Size64: {
            return "64";
        } break;

        default: {
            abort();
        } break;
    }
}

void print_instruction(Instruction *instruction, bool has_return) {
    if(auto arithmetic_operation = dynamic_cast<ArithmeticOperation*>(instruction)) {
        switch(arithmetic_operation->operation) {
            case ArithmeticOperation::Operation::Add: {
                printf("ADD ");
            } break;

            case ArithmeticOperation::Operation::Subtract: {
                printf("SUB ");
            } break;

            case ArithmeticOperation::Operation::SignedMultiply: {
                printf("MUL ");
            } break;

            case ArithmeticOperation::Operation::UnsignedMultiply: {
                printf("UMUL ");
            } break;

            case ArithmeticOperation::Operation::SignedDivide: {
                printf("DIV ");
            } break;

            case ArithmeticOperation::Operation::UnsignedDivide: {
                printf("UDIV ");
            } break;

            case ArithmeticOperation::Operation::SignedModulus: {
                printf("MOD ");
            } break;

            case ArithmeticOperation::Operation::UnsignedModulus: {
                printf("UMOD ");
            } break;

            case ArithmeticOperation::Operation::BitwiseAnd: {
                printf("AND ");
            } break;

            case ArithmeticOperation::Operation::BitwiseOr: {
                printf("OR ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " %s r%zu, r%zu, r%zu",
            register_size_name(arithmetic_operation->size),
            arithmetic_operation->source_register_a,
            arithmetic_operation->source_register_b,
            arithmetic_operation->destination_register
        );
    } else if(auto comparison_operation = dynamic_cast<ComparisonOperation*>(instruction)) {
        switch(comparison_operation->operation) {
            case ComparisonOperation::Operation::Equal: {
                printf("EQ ");
            } break;

            case ComparisonOperation::Operation::SignedLessThan: {
                printf("SLT ");
            } break;

            case ComparisonOperation::Operation::UnsignedLessThan: {
                printf("ULT ");
            } break;

            case ComparisonOperation::Operation::SignedGreaterThan: {
                printf("SGT ");
            } break;

            case ComparisonOperation::Operation::UnsignedGreaterThan: {
                printf("UGT ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " %s r%zu, r%zu, r%zu",
            register_size_name(comparison_operation->size),
            comparison_operation->source_register_a,
            comparison_operation->source_register_b,
            comparison_operation->destination_register
        );
    } else if(auto integer_upcast = dynamic_cast<IntegerUpcast*>(instruction)) {
        if(integer_upcast->is_signed) {
            printf("SCAST");
        } else {
            printf("CAST");
        }

        printf(
            " %s r%zu, %s r%zu",
            register_size_name(integer_upcast->source_size),
            integer_upcast->source_register,
            register_size_name(integer_upcast->destination_size),
            integer_upcast->destination_register
        );
    } else if(auto integer_constant = dynamic_cast<IntegerConstantInstruction*>(instruction)) {
        printf("CONST %s ", register_size_name(integer_constant->size));

        switch(integer_constant->size) {
            case RegisterSize::Size8: {
                printf("%hhx", (uint8_t)integer_constant->value);
            } break;

            case RegisterSize::Size16: {
                printf("%hx", (uint16_t)integer_constant->value);
            } break;

            case RegisterSize::Size32: {
                printf("%x", (uint32_t)integer_constant->value);
            } break;

            case RegisterSize::Size64: {
                printf("%llx", integer_constant->value);
            } break;

            default: {
                abort();
            } break;
        }

        printf(", r%zu", integer_constant->destination_register);
    } else if(auto float_constant = dynamic_cast<FloatConstantInstruction*>(instruction)) {
        printf("FCONST %s ", register_size_name(float_constant->size));

        switch(float_constant->size) {
            case RegisterSize::Size32: {
                printf("%f", (double)(float)float_constant->value);
            } break;

            case RegisterSize::Size64: {
                printf("%f", float_constant->value);
            } break;

            default: {
                abort();
            } break;
        }

        printf(", r%zu", float_constant->destination_register);
    } else if(auto jump = dynamic_cast<Jump*>(instruction)) {
        printf("JMP %zu", jump->destination_instruction);
    } else if(auto branch = dynamic_cast<Branch*>(instruction)) {
        printf(
            "BR r%zu, %zu",
            branch->condition_register,
            branch->destination_instruction
        );
    } else if(auto function_call = dynamic_cast<FunctionCallInstruction*>(instruction)) {
        printf("CALL %s (", function_call->function_name);

        for(size_t i = 0; i < function_call->parameter_registers.count; i += 1) {
            printf("r%zu", function_call->parameter_registers[i]);

            if(i != function_call->parameter_registers.count - 1) {
                printf(", ");
            }
        }

        printf(")");

        if(function_call->has_return) {
            printf(" r%zu", function_call->return_register);
        }
    } else if(auto return_instruction = dynamic_cast<ReturnInstruction*>(instruction)) {
        printf("RET");

        if(has_return) {
            printf(" r%zu", return_instruction->value_register);
        }
    } else if(auto allocate_local = dynamic_cast<AllocateLocal*>(instruction)) {
        printf(
            "LOCAL %zu(%zu), r%zu",
            allocate_local->size,
            allocate_local->alignment,
            allocate_local->destination_register
        );
    } else if(auto load_integer = dynamic_cast<LoadInteger*>(instruction)) {
        printf(
            "LOAD %s r%zu, r%zu",
            register_size_name(load_integer->size),
            load_integer->address_register,
            load_integer->destination_register
        );
    } else if(auto store_integer = dynamic_cast<StoreInteger*>(instruction)) {
        printf(
            "STORE %s r%zu, r%zu",
            register_size_name(store_integer->size),
            store_integer->source_register,
            store_integer->address_register
        );
    } else if(auto load_float = dynamic_cast<LoadFloat*>(instruction)) {
        printf(
            "FLOAD %s r%zu, r%zu",
            register_size_name(load_float->size),
            load_float->address_register,
            load_float->destination_register
        );
    } else if(auto store_float = dynamic_cast<StoreFloat*>(instruction)) {
        printf(
            "FSTORE %s r%zu, r%zu",
            register_size_name(store_float->size),
            store_float->source_register,
            store_float->address_register
        );
    } else if(auto copy_memory = dynamic_cast<CopyMemory*>(instruction)) {
        printf(
            "COPY r%zu, r%zu, r%zu",
            copy_memory->length_register,
            copy_memory->source_address_register,
            copy_memory->destination_address_register
        );
    } else if(auto reference_static = dynamic_cast<ReferenceStatic*>(instruction)) {
        printf(
            "STATIC %s r%zu",
            reference_static->name,
            reference_static->destination_register
        );
    } else {
        abort();
    }
}

void print_static(RuntimeStatic *runtime_static) {
    printf("%s", runtime_static->name);

    if(auto function = dynamic_cast<Function*>(runtime_static)) {
        printf(" (", function->name);

        for(size_t i = 0; i < function->parameters.count; i += 1) {
            printf(
                "r%zu: ",
                i
            );

            if(function->parameters[i].is_float) {
                printf("f");
            }

            printf("%s", register_size_name(function->parameters[i].size));

            if(i != function->parameters.count - 1) {
                printf(", ");
            }
        }

        printf(")");

        if(function->has_return) {
            printf(" ");

            if(function->is_return_float) {
                printf("f");
            }
            printf("%s", register_size_name(function->return_size));
        }

        if(function->is_external) {
            printf(" extern");
        } else {
            printf("\n");

            char buffer[20];
            sprintf(buffer, "%zu", function->instructions.count - 1);
            size_t max_index_digits = strlen(buffer);

            for(size_t i = 0; i < function->instructions.count; i += 1) {
                auto index_digits = printf("%zu", i);

                for(size_t j = 0; j < max_index_digits - index_digits; j += 1) {
                    printf(" ");
                }

                printf(" : ");

                print_instruction(function->instructions[i], function->has_return);

                if(i != function->instructions.count - 1) {
                    printf("\n");
                }
            }
        }
    } else if(auto constant = dynamic_cast<StaticConstant*>(runtime_static)) {
        printf("%s %zu", constant->name, constant->data.count);
    } else {
        abort();
    }
}