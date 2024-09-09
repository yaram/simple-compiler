#include "ir.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

String calling_convention_name(CallingConvention calling_convention) {
    switch(calling_convention) {
        case CallingConvention::Default: {
            return "cdecl"_S;
        };

        case CallingConvention::StdCall: {
            return "stdcall"_S;
        } break;

        default: abort();
    }
}

inline String register_size_name(RegisterSize size){
    switch(size) {
        case RegisterSize::Size8: {
            return "8"_S;
        } break;

        case RegisterSize::Size16: {
            return "16"_S;
        } break;

        case RegisterSize::Size32: {
            return "32"_S;
        } break;

        case RegisterSize::Size64: {
            return "64"_S;
        } break;

        default: {
            abort();
        } break;
    }
}

void Instruction::print(bool has_return) {
    if(kind == InstructionKind::IntegerArithmeticOperation) {
        auto integer_arithmetic_operation = (IntegerArithmeticOperation*)this;

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

            case IntegerArithmeticOperation::Operation::LeftShift: {
                printf("LSh ");
            } break;

            case IntegerArithmeticOperation::Operation::RightShift: {
                printf("RSH ");
            } break;

            case IntegerArithmeticOperation::Operation::RightArithmeticShift: {
                printf("RSHA ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " %.*s r%zu, r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_arithmetic_operation->size)),
            integer_arithmetic_operation->source_register_a,
            integer_arithmetic_operation->source_register_b,
            integer_arithmetic_operation->destination_register
        );
    } else if(kind == InstructionKind::IntegerComparisonOperation) {
        auto integer_comparison_operation = (IntegerComparisonOperation*)this;

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
            " %.*s r%zu, r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_comparison_operation->size)),
            integer_comparison_operation->source_register_a,
            integer_comparison_operation->source_register_b,
            integer_comparison_operation->destination_register
        );
    } else if(kind == InstructionKind::IntegerExtension) {
        auto integer_extension = (IntegerExtension*)this;

        if(integer_extension->is_signed) {
            printf("SEXTEND");
        } else {
            printf("EXTEND");
        }

        printf(
            " %.*s r%zu, %.*s r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_extension->source_size)),
            integer_extension->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_extension->destination_size)),
            integer_extension->destination_register
        );
    } else if(kind == InstructionKind::IntegerTruncation) {
        auto integer_truncation = (IntegerTruncation*)this;

        printf(
            "TRUNC %.*s r%zu, %.*s r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_truncation->source_size)),
            integer_truncation->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_truncation->destination_size)),
            integer_truncation->destination_register
        );
    } else if(kind == InstructionKind::IntegerConstantInstruction) {
        auto integer_constant = (IntegerConstantInstruction*)this;

        printf("CONST %.*s ", STRING_PRINTF_ARGUMENTS(register_size_name(integer_constant->size)));

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
                printf("%" PRIu64, integer_constant->value);
            } break;

            default: {
                abort();
            } break;
        }

        printf(", r%zu", integer_constant->destination_register);
    } else if(kind == InstructionKind::FloatArithmeticOperation) {
        auto float_arithmetic_operation = (FloatArithmeticOperation*)this;

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
            " f%.*s r%zu, r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(float_arithmetic_operation->size)),
            float_arithmetic_operation->source_register_a,
            float_arithmetic_operation->source_register_b,
            float_arithmetic_operation->destination_register
        );
    } else if(kind == InstructionKind::FloatComparisonOperation) {
        auto float_comparison_operation = (FloatComparisonOperation*)this;

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
            " f%.*s r%zu, r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(float_comparison_operation->size)),
            float_comparison_operation->source_register_a,
            float_comparison_operation->source_register_b,
            float_comparison_operation->destination_register
        );
    } else if(kind == InstructionKind::FloatConversion) {
        auto float_conversion = (FloatConversion*)this;

        printf(
            "FCAST f%.*s r%zu, f%.*s r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(float_conversion->source_size)),
            float_conversion->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(float_conversion->destination_size)),
            float_conversion->destination_register
        );
    } else if(kind == InstructionKind::FloatTruncation) {
        auto float_truncation = (FloatTruncation*)this;

        printf(
            "FTRUNC f%.*s r%zu, %.*s r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(float_truncation->source_size)),
            float_truncation->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(float_truncation->destination_size)),
            float_truncation->destination_register
        );
    } else if(kind == InstructionKind::FloatFromInteger) {
        auto float_from_integer = (FloatFromInteger*)this;

        if(float_from_integer->is_signed) {
            printf("FSINT");
        } else {
            printf("FUINT");
        }

        printf(
            " %.*s r%zu, f%.*s r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(float_from_integer->source_size)),
            float_from_integer->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(float_from_integer->destination_size)),
            float_from_integer->destination_register
        );
    } else if(kind == InstructionKind::FloatConstantInstruction) {
        auto float_constant = (FloatConstantInstruction*)this;

        printf("FCONST f%.*s ", STRING_PRINTF_ARGUMENTS(register_size_name(float_constant->size)));

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
    } else if(kind == InstructionKind::Jump) {
        auto jump = (Jump*)this;

        printf("JMP %zu", jump->destination_instruction);
    } else if(kind == InstructionKind::Branch) {
        auto branch = (Branch*)this;

        printf(
            "BR r%zu, %zu",
            branch->condition_register,
            branch->destination_instruction
        );
    } else if(kind == InstructionKind::FunctionCallInstruction) {
        auto function_call = (FunctionCallInstruction*)this;

        printf("CALL r%zu (", function_call->address_register);

        for(size_t i = 0; i < function_call->parameters.length; i += 1) {
            printf("r%zu: ", function_call->parameters[i].register_index);

            if(function_call->parameters[i].is_float) {
                printf("f");
            }

            printf("%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(function_call->parameters[i].size)));

            if(i != function_call->parameters.length - 1) {
                printf(", ");
            }
        }

        printf(")");

        if(function_call->has_return) {
            printf(" r%zu: ", function_call->return_register);
            
            if(function_call->is_return_float) {
                printf("f");
            }

            printf("%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(function_call->return_size)));
        }

        switch(function_call->calling_convention) {
            case CallingConvention::Default: break;

            case CallingConvention::StdCall: {
                printf(" __stdcall");
            } break;

            default: abort();
        }
    } else if(kind == InstructionKind::ReturnInstruction) {
        auto return_instruction = (ReturnInstruction*)this;

        printf("RET");

        if(has_return) {
            printf(" r%zu", return_instruction->value_register);
        }
    } else if(kind == InstructionKind::AllocateLocal) {
        auto allocate_local = (AllocateLocal*)this;

        printf(
            "LOCAL %zu(%zu), r%zu",
            allocate_local->size,
            allocate_local->alignment,
            allocate_local->destination_register
        );
    } else if(kind == InstructionKind::LoadInteger) {
        auto load_integer = (LoadInteger*)this;

        printf(
            "LOAD %.*s r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(load_integer->size)),
            load_integer->address_register,
            load_integer->destination_register
        );
    } else if(kind == InstructionKind::StoreInteger) {
        auto store_integer = (StoreInteger*)this;

        printf(
            "STORE %.*s r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(store_integer->size)),
            store_integer->source_register,
            store_integer->address_register
        );
    } else if(kind == InstructionKind::LoadFloat) {
        auto load_float = (LoadFloat*)this;

        printf(
            "FLOAD %.*s r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(load_float->size)),
            load_float->address_register,
            load_float->destination_register
        );
    } else if(kind == InstructionKind::StoreFloat) {
        auto store_float = (StoreFloat*)this;

        printf(
            "FSTORE %.*s r%zu, r%zu",
            STRING_PRINTF_ARGUMENTS(register_size_name(store_float->size)),
            store_float->source_register,
            store_float->address_register
        );
    } else if(kind == InstructionKind::CopyMemory) {
        auto copy_memory = (CopyMemory*)this;

        printf(
            "COPY %zu (%zu), r%zu, r%zu",
            copy_memory->length,
            copy_memory->alignment,
            copy_memory->source_address_register,
            copy_memory->destination_address_register
        );
    } else if(kind == InstructionKind::ReferenceStatic) {
        auto reference_static = (ReferenceStatic*)this;

        printf(
            "STATIC %.*s r%zu",
            STRING_PRINTF_ARGUMENTS(reference_static->runtime_static->name),
            reference_static->destination_register
        );
    } else {
        abort();
    }
}

void RuntimeStatic::print() {
    printf("%.*s", STRING_PRINTF_ARGUMENTS(name));

    if(is_no_mangle) {
        printf(" (no_mangle)");
    }

    if(kind == RuntimeStaticKind::Function) {
        auto function = (Function*)this;

        printf(" (");

        for(size_t i = 0; i < function->parameters.length; i += 1) {
            printf(
                "r%zu: ",
                i
            );

            if(function->parameters[i].is_float) {
                printf("f");
            }

            printf("%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(function->parameters[i].size)));

            if(i != function->parameters.length - 1) {
                printf(", ");
            }
        }

        printf(")");

        if(function->has_return) {
            printf(" ");

            if(function->is_return_float) {
                printf("f");
            }
            printf("%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(function->return_size)));
        }

        if(function->is_external) {
            printf(" extern");
        } else {
            printf("\n");

            char buffer[20];
            snprintf(buffer, 20, "%zu", function->instructions.length - 1);
            size_t max_index_digits = strlen(buffer);

            for(size_t i = 0; i < function->instructions.length; i += 1) {
                auto index_digits = printf("%zu", i);

                for(size_t j = 0; j < max_index_digits - index_digits; j += 1) {
                    printf(" ");
                }

                printf(" : ");

                function->instructions[i]->print(function->has_return);

                if(i != function->instructions.length - 1) {
                    printf("\n");
                }
            }
        }
    } else if(kind == RuntimeStaticKind::StaticConstant) {
        auto constant = (StaticConstant*)this;

        printf(" %zu(%zu) (const)", constant->data.length, constant->alignment);
    } else if(kind == RuntimeStaticKind::StaticVariable) {
        auto variable = (StaticVariable*)this;

        if(variable->is_external) {
            printf(" extern");
        } else if(variable->has_initial_data) {
            printf(" initialized");
        }
    } else {
        abort();
    }
}