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
    if(instruction->kind == InstructionKind::IntegerArithmeticOperation) {
        auto integer_arithmetic_operation = (IntegerArithmeticOperation*)instruction;

        switch(integer_arithmetic_operation->operation) {
            case IntegerArithmeticOperation::Operation::Add: {
                printf("ADD ");
            } break;

            case IntegerArithmeticOperation::Operation::Subtract: {
                printf("SUB ");
            } break;

            case IntegerArithmeticOperation::Operation::Multiply: {
                printf("MUL ");
            } break;

            case IntegerArithmeticOperation::Operation::SignedDivide: {
                printf("SDIV ");
            } break;

            case IntegerArithmeticOperation::Operation::UnsignedDivide: {
                printf("UDIV ");
            } break;

            case IntegerArithmeticOperation::Operation::SignedModulus: {
                printf("SMOD ");
            } break;

            case IntegerArithmeticOperation::Operation::UnsignedModulus: {
                printf("UMOD ");
            } break;

            case IntegerArithmeticOperation::Operation::BitwiseAnd: {
                printf("AND ");
            } break;

            case IntegerArithmeticOperation::Operation::BitwiseOr: {
                printf("OR ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " %s r%zu, r%zu, r%zu",
            register_size_name(integer_arithmetic_operation->size),
            integer_arithmetic_operation->source_register_a,
            integer_arithmetic_operation->source_register_b,
            integer_arithmetic_operation->destination_register
        );
    } else if(instruction->kind == InstructionKind::IntegerComparisonOperation) {
        auto integer_comparison_operation = (IntegerComparisonOperation*)instruction;

        switch(integer_comparison_operation->operation) {
            case IntegerComparisonOperation::Operation::Equal: {
                printf("EQ ");
            } break;

            case IntegerComparisonOperation::Operation::SignedLessThan: {
                printf("SLT ");
            } break;

            case IntegerComparisonOperation::Operation::UnsignedLessThan: {
                printf("ULT ");
            } break;

            case IntegerComparisonOperation::Operation::SignedGreaterThan: {
                printf("SGT ");
            } break;

            case IntegerComparisonOperation::Operation::UnsignedGreaterThan: {
                printf("UGT ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " %s r%zu, r%zu, r%zu",
            register_size_name(integer_comparison_operation->size),
            integer_comparison_operation->source_register_a,
            integer_comparison_operation->source_register_b,
            integer_comparison_operation->destination_register
        );
    } else if(instruction->kind == InstructionKind::IntegerUpcast) {
        auto integer_upcast = (IntegerUpcast*)instruction;

        if(integer_upcast->is_signed) {
            printf("SCAST");
        } else {
            printf("UCAST");
        }

        printf(
            " %s r%zu, %s r%zu",
            register_size_name(integer_upcast->source_size),
            integer_upcast->source_register,
            register_size_name(integer_upcast->destination_size),
            integer_upcast->destination_register
        );
    } else if(instruction->kind == InstructionKind::IntegerConstantInstruction) {
        auto integer_constant = (IntegerConstantInstruction*)instruction;

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
    } else if(instruction->kind == InstructionKind::FloatArithmeticOperation) {
        auto float_arithmetic_operation = (FloatArithmeticOperation*)instruction;

        switch(float_arithmetic_operation->operation) {
            case FloatArithmeticOperation::Operation::Add: {
                printf("FADD ");
            } break;

            case FloatArithmeticOperation::Operation::Subtract: {
                printf("FSUB ");
            } break;

            case FloatArithmeticOperation::Operation::Multiply: {
                printf("FMUL ");
            } break;

            case FloatArithmeticOperation::Operation::Divide: {
                printf("FDIV ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " f%s r%zu, r%zu, r%zu",
            register_size_name(float_arithmetic_operation->size),
            float_arithmetic_operation->source_register_a,
            float_arithmetic_operation->source_register_b,
            float_arithmetic_operation->destination_register
        );
    } else if(instruction->kind == InstructionKind::FloatComparisonOperation) {
        auto float_comparison_operation = (FloatComparisonOperation*)instruction;

        switch(float_comparison_operation->operation) {
            case FloatComparisonOperation::Operation::Equal: {
                printf("FEQ ");
            } break;

            case FloatComparisonOperation::Operation::LessThan: {
                printf("FLT ");
            } break;

            case FloatComparisonOperation::Operation::GreaterThan: {
                printf("FGT ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " f%s r%zu, r%zu, r%zu",
            register_size_name(float_comparison_operation->size),
            float_comparison_operation->source_register_a,
            float_comparison_operation->source_register_b,
            float_comparison_operation->destination_register
        );
    } else if(instruction->kind == InstructionKind::FloatConversion) {
        auto float_conversion = (FloatConversion*)instruction;

        printf(
            "FCAST f%s r%zu, f%s r%zu",
            register_size_name(float_conversion->source_size),
            float_conversion->source_register,
            register_size_name(float_conversion->destination_size),
            float_conversion->destination_register
        );
    } else if(instruction->kind == InstructionKind::FloatTruncation) {
        auto float_truncation = (FloatTruncation*)instruction;

        printf(
            "FTRUNC f%s r%zu, %s r%zu",
            register_size_name(float_truncation->source_size),
            float_truncation->source_register,
            register_size_name(float_truncation->destination_size),
            float_truncation->destination_register
        );
    } else if(instruction->kind == InstructionKind::FloatFromInteger) {
        auto float_from_integer = (FloatFromInteger*)instruction;

        if(float_from_integer->is_signed) {
            printf("FSINT");
        } else {
            printf("FUINT");
        }

        printf(
            " %s r%zu, f%s r%zu",
            register_size_name(float_from_integer->source_size),
            float_from_integer->source_register,
            register_size_name(float_from_integer->destination_size),
            float_from_integer->destination_register
        );
    } else if(instruction->kind == InstructionKind::FloatConstantInstruction) {
        auto float_constant = (FloatConstantInstruction*)instruction;

        printf("FCONST f%s ", register_size_name(float_constant->size));

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
    } else if(instruction->kind == InstructionKind::Jump) {
        auto jump = (Jump*)instruction;

        printf("JMP %zu", jump->destination_instruction);
    } else if(instruction->kind == InstructionKind::Branch) {
        auto branch = (Branch*)instruction;

        printf(
            "BR r%zu, %zu",
            branch->condition_register,
            branch->destination_instruction
        );
    } else if(instruction->kind == InstructionKind::FunctionCallInstruction) {
        auto function_call = (FunctionCallInstruction*)instruction;

        printf("CALL r%zu (", function_call->address_register);

        for(size_t i = 0; i < function_call->parameters.count; i += 1) {
            printf("r%zu: ", function_call->parameters[i].register_index);

            if(function_call->parameters[i].is_float) {
                printf("f");
            }

            printf("%s", register_size_name(function_call->parameters[i].size));

            if(i != function_call->parameters.count - 1) {
                printf(", ");
            }
        }

        printf(")");

        if(function_call->has_return) {
            printf(" r%zu: ", function_call->return_register);
            
            if(function_call->is_return_float) {
                printf("f");
            }

            printf("%s", register_size_name(function_call->return_size));
        }
    } else if(instruction->kind == InstructionKind::ReturnInstruction) {
        auto return_instruction = (ReturnInstruction*)instruction;

        printf("RET");

        if(has_return) {
            printf(" r%zu", return_instruction->value_register);
        }
    } else if(instruction->kind == InstructionKind::AllocateLocal) {
        auto allocate_local = (AllocateLocal*)instruction;

        printf(
            "LOCAL %zu(%zu), r%zu",
            allocate_local->size,
            allocate_local->alignment,
            allocate_local->destination_register
        );
    } else if(instruction->kind == InstructionKind::LoadInteger) {
        auto load_integer = (LoadInteger*)instruction;

        printf(
            "LOAD %s r%zu, r%zu",
            register_size_name(load_integer->size),
            load_integer->address_register,
            load_integer->destination_register
        );
    } else if(instruction->kind == InstructionKind::StoreInteger) {
        auto store_integer = (StoreInteger*)instruction;

        printf(
            "STORE %s r%zu, r%zu",
            register_size_name(store_integer->size),
            store_integer->source_register,
            store_integer->address_register
        );
    } else if(instruction->kind == InstructionKind::LoadFloat) {
        auto load_float = (LoadFloat*)instruction;

        printf(
            "FLOAD %s r%zu, r%zu",
            register_size_name(load_float->size),
            load_float->address_register,
            load_float->destination_register
        );
    } else if(instruction->kind == InstructionKind::StoreFloat) {
        auto store_float = (StoreFloat*)instruction;

        printf(
            "FSTORE %s r%zu, r%zu",
            register_size_name(store_float->size),
            store_float->source_register,
            store_float->address_register
        );
    } else if(instruction->kind == InstructionKind::CopyMemory) {
        auto copy_memory = (CopyMemory*)instruction;

        printf(
            "COPY r%zu (%zu), r%zu, r%zu",
            copy_memory->length_register,
            copy_memory->alignment,
            copy_memory->source_address_register,
            copy_memory->destination_address_register
        );
    } else if(instruction->kind == InstructionKind::ReferenceStatic) {
        auto reference_static = (ReferenceStatic*)instruction;

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

    if(runtime_static->kind == RuntimeStaticKind::Function) {
        auto function = (Function*)runtime_static;

        printf(" (");

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
    } else if(runtime_static->kind == RuntimeStaticKind::StaticConstant) {
        auto constant = (StaticConstant*)runtime_static;

        printf(" %zu(%zu) (const)", constant->data.count, constant->alignment);
    } else if(runtime_static->kind == RuntimeStaticKind::StaticVariable) {
        auto variable = (StaticVariable*)runtime_static;

        if(variable->is_external) {
            printf(" extern");
        } else if(variable->has_initial_data) {
            printf(" initialized");
        }
    } else {
        abort();
    }
}