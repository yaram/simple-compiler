#include "hlir.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool IRType::operator==(IRType other) {
    if(other.kind != kind) {
        return false;
    }

    if(kind == IRTypeKind::Boolean) {
        return true;
    } else if(kind == IRTypeKind::Integer) {
        return integer.size == other.integer.size;
    } else if(kind == IRTypeKind::Float) {
        return float_.size == other.float_.size;
    } else if(kind == IRTypeKind::Pointer) {
        return true;
    } else if(kind == IRTypeKind::StaticArray) {
        return
            static_array.length == other.static_array.length &&
            *static_array.element_type == *other.static_array.element_type
        ;
    } else if(kind == IRTypeKind::Struct) {
        if(struct_.members.length != other.struct_.members.length) {
            return false;
        }

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            if(struct_.members[i] != other.struct_.members[i]) {
                return false;
            }
        }

        return true;
    } else {
        abort();
    }
}

bool IRType::operator!=(IRType other) {
    return !(*this == other);
}

inline String register_size_name(RegisterSize size){
    switch(size) {
        case RegisterSize::Size8: {
            return u8"8"_S;
        } break;

        case RegisterSize::Size16: {
            return u8"16"_S;
        } break;

        case RegisterSize::Size32: {
            return u8"32"_S;
        } break;

        case RegisterSize::Size64: {
            return u8"64"_S;
        } break;

        default: {
            abort();
        } break;
    }
}

void IRType::print() {
    if(kind == IRTypeKind::Boolean) {
        printf("bool");
    } else if(kind == IRTypeKind::Integer) {
        printf("i%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(integer.size)));
    } else if(kind == IRTypeKind::Float) {
        printf("f%.*s", STRING_PRINTF_ARGUMENTS(register_size_name(float_.size)));
    } else if(kind == IRTypeKind::Pointer) {
        printf("*");
    } else if(kind == IRTypeKind::StaticArray) {
        printf("[%" PRIu64 "]", static_array.length);

        static_array.element_type->print();
    } else if(kind == IRTypeKind::Struct) {
        printf("{ ");

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            struct_.members[i].print();

            if(i != struct_.members.length - 1) {
                printf(", ");
            }
        }

        printf(" }");
    } else {
        abort();
    }
}

void IRConstantValue::print() {
    if(kind == IRConstantValueKind::FunctionConstant) {
        printf("func");
    } else if(kind == IRConstantValueKind::IntegerConstant) {
        printf("%" PRIu64, integer);
    } else if(kind == IRConstantValueKind::FloatConstant) {
        printf("%f", float_);
    } else if(kind == IRConstantValueKind::BooleanConstant) {
        if(boolean) {
            printf("true");
        } else {
            printf("false");
        }
    } else if(kind == IRConstantValueKind::StaticArrayConstant) {
        printf("[ ");

        for(size_t i = 0; i < static_array.elements.length; i += 1) {
            static_array.elements[i].print();

            if(i != static_array.elements.length - 1) {
                printf(", ");
            }
        }

        printf(" ]");
    } else if(kind == IRConstantValueKind::StructConstant) {
        printf("{ ");

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            struct_.members[i].print();

            if(i != struct_.members.length - 1) {
                printf(", ");
            }
        }

        printf(" }");
    } else if(kind == IRConstantValueKind::UndefConstant) {
        printf("undef");
    } else {
        abort();
    }
}

void Instruction::print(Array<Block*> blocks, bool has_return) {
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
            " r%zu, r%zu, r%zu",
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
            " r%zu, r%zu, r%zu",
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
            " r%zu, i%.*s r%zu",
            integer_extension->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_extension->destination_size)),
            integer_extension->destination_register
        );
    } else if(kind == InstructionKind::IntegerTruncation) {
        auto integer_truncation = (IntegerTruncation*)this;

        printf(
            "TRUNC r%zu, i%.*s r%zu",
            integer_truncation->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_truncation->destination_size)),
            integer_truncation->destination_register
        );
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

            case FloatArithmeticOperation::Operation::Modulus: {
                printf("FMOD ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " r%zu, r%zu, r%zu",
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
            " r%zu, r%zu, r%zu",
            float_comparison_operation->source_register_a,
            float_comparison_operation->source_register_b,
            float_comparison_operation->destination_register
        );
    } else if(kind == InstructionKind::FloatConversion) {
        auto float_conversion = (FloatConversion*)this;

        printf(
            "FCAST r%zu, f%.*s r%zu",
            float_conversion->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(float_conversion->destination_size)),
            float_conversion->destination_register
        );
    } else if(kind == InstructionKind::IntegerFromFloat) {
        auto integer_from_float = (IntegerFromFloat*)this;

        printf(
            "FTOI r%zu, i%.*s r%zu",
            integer_from_float->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_from_float->destination_size)),
            integer_from_float->destination_register
        );
    } else if(kind == InstructionKind::FloatFromInteger) {
        auto float_from_integer = (FloatFromInteger*)this;

        if(float_from_integer->is_signed) {
            printf("SITOF");
        } else {
            printf("UITOF");
        }

        printf(
            " r%zu, f%.*s r%zu",
            float_from_integer->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(float_from_integer->destination_size)),
            float_from_integer->destination_register
        );
    } else if(kind == InstructionKind::PointerEquality) {
        auto pointer_equality = (PointerEquality*)this;

        printf(
            "PTREQ r%zu, r%zu, r%zu",
            pointer_equality->source_register_a,
            pointer_equality->source_register_b,
            pointer_equality->destination_register
        );
    } else if(kind == InstructionKind::IntegerFromPointer) {
        auto integer_from_pointer = (IntegerFromPointer*)this;

        printf(
            "PTRTOI r%zu, i%.*s r%zu",
            integer_from_pointer->source_register,
            STRING_PRINTF_ARGUMENTS(register_size_name(integer_from_pointer->destination_size)),
            integer_from_pointer->destination_register
        );
    } else if(kind == InstructionKind::PointerFromInteger) {
        auto pointer_from_integer = (PointerFromInteger*)this;

        printf(
            "ITOPTR r%zu, r%zu",
            pointer_from_integer->source_register,
            pointer_from_integer->destination_register
        );
    } else if(kind == InstructionKind::BooleanArithmeticOperation) {
        auto boolean_arithmetic_operation = (BooleanArithmeticOperation*)this;

        switch(boolean_arithmetic_operation->operation) {
            case BooleanArithmeticOperation::Operation::BooleanAnd: {
                printf("BAND ");
            } break;

            case BooleanArithmeticOperation::Operation::BooleanOr: {
                printf("BOR ");
            } break;

            default: {
                abort();
            } break;
        }

        printf(
            " r%zu, r%zu, r%zu",
            boolean_arithmetic_operation->source_register_a,
            boolean_arithmetic_operation->source_register_b,
            boolean_arithmetic_operation->destination_register
        );
    } else if(kind == InstructionKind::BooleanEquality) {
        auto boolean_equalty = (BooleanEquality*)this;

        printf(
            "BEQ r%zu, r%zu, r%zu",
            boolean_equalty->source_register_a,
            boolean_equalty->source_register_b,
            boolean_equalty->destination_register
        );
    } else if(kind == InstructionKind::BooleanInversion) {
        auto boolean_inversion = (BooleanInversion*)this;

        printf(
            "BNOT r%zu, r%zu",
            boolean_inversion->source_register,
            boolean_inversion->destination_register
        );
    } else if(kind == InstructionKind::AssembleStaticArray) {
        auto assemble_static_array = (AssembleStaticArray*)this;

        printf("MKARRAY [ ");

        for(size_t i = 0; i < assemble_static_array->element_registers.length; i += 1) {
            printf("r%zu", assemble_static_array->element_registers[i]);

            if(i != assemble_static_array->element_registers.length - 1) {
                printf(", ");
            }
        }

        printf(" ], r%zu", assemble_static_array->destination_register);
    } else if(kind == InstructionKind::ReadStaticArrayElement) {
        auto read_static_array_element = (ReadStaticArrayElement*)this;

        printf(
            "RDARRAY %zu, r%zu, r%zu",
            read_static_array_element->element_index,
            read_static_array_element->source_register,
            read_static_array_element->destination_register
        );
    } else if(kind == InstructionKind::AssembleStruct) {
        auto assemble_struct = (AssembleStruct*)this;

        printf("MKSTRUCT [ ");

        for(size_t i = 0; i < assemble_struct->member_registers.length; i += 1) {
            printf("r%zu", assemble_struct->member_registers[i]);

            if(i != assemble_struct->member_registers.length - 1) {
                printf(", ");
            }
        }

        printf(" ], r%zu", assemble_struct->destination_register);
    } else if(kind == InstructionKind::ReadStructMember) {
        auto read_struct_member = (ReadStructMember*)this;

        printf(
            "RDSTRUCT %zu, r%zu, r%zu",
            read_struct_member->member_index,
            read_struct_member->source_register,
            read_struct_member->destination_register
        );
    } else if(kind == InstructionKind::Literal) {
        auto literal = (Literal*)this;

        printf("LITERAL ");

        literal->type.print();

        printf(" ");

        literal->value.print();

        printf(", r%zu", literal->destination_register);
    } else if(kind == InstructionKind::Jump) {
        auto jump = (Jump*)this;

        auto found = false;
        size_t index;
        for(size_t i = 0; i < blocks.length; i += 1) {
            if(blocks[i] == jump->destination_block) {
                index = i;
                found = true;

                break;
            }
        }

        assert(found);

        printf("JMP block %zu", index);
    } else if(kind == InstructionKind::Branch) {
        auto branch = (Branch*)this;

        auto true_found = false;
        size_t true_index;
        for(size_t i = 0; i < blocks.length; i += 1) {
            if(blocks[i] == branch->true_destination_block) {
                true_index = i;
                true_found = true;

                break;
            }
        }

        assert(true_found);

        auto false_found = false;
        size_t false_index;
        for(size_t i = 0; i < blocks.length; i += 1) {
            if(blocks[i] == branch->false_destination_block) {
                false_index = i;
                false_found = true;

                break;
            }
        }

        assert(false_found);

        printf(
            "BR r%zu, block %zu, block %zu",
            branch->condition_register,
            true_index,
            false_index
        );
    } else if(kind == InstructionKind::FunctionCallInstruction) {
        auto function_call = (FunctionCallInstruction*)this;

        printf("CALL r%zu (", function_call->pointer_register);

        for(size_t i = 0; i < function_call->parameters.length; i += 1) {
            function_call->parameters[i].type.print();

            printf(" r%zu", function_call->parameters[i].register_index);

            if(i != function_call->parameters.length - 1) {
                printf(", ");
            }
        }

        if(function_call->has_return) {
            printf(") -> ");

            function_call->return_type.print();

            printf(" r%zu", function_call->return_register);
        } else {
            printf(")");
        }

        switch(function_call->calling_convention) {
            case CallingConvention::Default: break;

            case CallingConvention::StdCall: {
                printf(" stdcall");
            } break;

            default: abort();
        }
    } else if(kind == InstructionKind::IntrinsicCallInstruction) {
        auto intrinsic_call = (IntrinsicCallInstruction*)this;

        printf("INTRIN ");

        if(intrinsic_call->intrinsic == IntrinsicCallInstruction::Intrinsic::Sqrt) {
            printf("sqrt");
        } else {
            abort();
        }

        printf(" (");

        for(size_t i = 0; i < intrinsic_call->parameters.length; i += 1) {
            intrinsic_call->parameters[i].type.print();

            printf(" r%zu", intrinsic_call->parameters[i].register_index);

            if(i != intrinsic_call->parameters.length - 1) {
                printf(", ");
            }
        }

        if(intrinsic_call->has_return) {
            printf(") -> ");

            intrinsic_call->return_type.print();

            printf(" r%zu", intrinsic_call->return_register);
        } else {
            printf(")");
        }
    } else if(kind == InstructionKind::ReturnInstruction) {
        auto return_instruction = (ReturnInstruction*)this;

        printf("RET");

        if(has_return) {
            printf(" r%zu", return_instruction->value_register);
        }
    } else if(kind == InstructionKind::AllocateLocal) {
        auto allocate_local = (AllocateLocal*)this;

        printf("LOCAL ");

        allocate_local->type.print();

        printf(", r%zu", allocate_local->destination_register);
    } else if(kind == InstructionKind::Load) {
        auto load = (Load*)this;

        printf(
            "LOAD *"
        );

        load->destination_type.print();

        printf(
            " r%zu, r%zu",
            load->pointer_register,
            load->destination_register
        );
    } else if(kind == InstructionKind::Store) {
        auto store = (Store*)this;

        printf(
            "STORE r%zu, r%zu",
            store->source_register,
            store->pointer_register
        );
    } else if(kind == InstructionKind::StructMemberPointer) {
        auto struct_member_pointer = (StructMemberPointer*)this;

        printf(
            "STRUCTPTR %zu, *",
            struct_member_pointer->member_index
        );

        auto struct_type = IRType::create_struct(struct_member_pointer->members);
        struct_type.print();

        printf(
            " r%zu, r%zu",
            struct_member_pointer->pointer_register,
            struct_member_pointer->destination_register
        );
    } else if(kind == InstructionKind::PointerIndex) {
        auto pointer_index = (PointerIndex*)this;

        printf(
            "PTRINDEX r%zu, *",
            pointer_index->index_register
        );

        pointer_index->pointed_to_type.print();

        printf(
            " r%zu, r%zu",
            pointer_index->pointer_register,
            pointer_index->destination_register
        );
    } else if(kind == InstructionKind::AssemblyInstruction) {
        auto assembly_instruction = (AssemblyInstruction*)this;

        printf("ASM \"%.*s\"", STRING_PRINTF_ARGUMENTS(assembly_instruction->assembly));

        for(auto binding : assembly_instruction->bindings) {
            assert(binding.constraint.length > 0);

            printf(" \"%.*s\"", STRING_PRINTF_ARGUMENTS(binding.constraint));

            if(binding.constraint[0] == '=') {
                printf(" *");
                binding.pointed_to_type.print();
            }

            printf(" r%zu", binding.register_index);
        }
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
            function->parameters[i].print();

            printf(" r%zu", i);

            if(i != function->parameters.length - 1) {
                printf(", ");
            }
        }

        if(function->has_return) {
            printf(") -> ");

            function->return_type.print();
        } else {
            printf(")");
        }

        if(function->is_external) {
            printf(" extern");
        } else {
            printf("\n");

            for(size_t k = 0; k < function->blocks.length; k += 1) {
                auto block = function->blocks[k];

                printf("block %zu\n", k);

                char buffer[20];
                snprintf(buffer, 20, "%zu", block->instructions.length - 1);
                size_t max_index_digits = strlen(buffer);

                for(size_t i = 0; i < block->instructions.length; i += 1) {
                    auto index_digits = printf("%zu", i);

                    for(size_t j = 0; j < max_index_digits - index_digits; j += 1) {
                        printf(" ");
                    }

                    printf(" : ");

                    block->instructions[i]->print(function->blocks, function->has_return);

                    if(i != block->instructions.length - 1) {
                        printf("\n");
                    }
                }

                if(k != function->blocks.length - 1) {
                    printf("\n");
                }
            }
        }
    } else if(kind == RuntimeStaticKind::StaticConstant) {
        auto constant = (StaticConstant*)this;

        constant->type.print();
        printf(" ");
        constant->value.print();
    } else if(kind == RuntimeStaticKind::StaticVariable) {
        auto variable = (StaticVariable*)this;

        variable->type.print();

        if(variable->is_external) {
            printf(" extern");
        } else if(variable->has_initial_value) {
            printf(" ");

            variable->initial_value.print();
        }
    } else {
        abort();
    }
}