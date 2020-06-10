#include "llvm_backend.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include "util.h"
#include "list.h"
#include "platform.h"
#include "profiler.h"

LLVMTypeRef get_llvm_integer_type(RegisterSize size) {
    switch(size) {
        case RegisterSize::Size8: {
            return LLVMInt8Type();
        } break;

        case RegisterSize::Size16: {
            return LLVMInt16Type();
        } break;

        case RegisterSize::Size32: {
            return LLVMInt32Type();
        } break;

        case RegisterSize::Size64: {
            return LLVMInt64Type();
        } break;

        default: {
            abort();
        } break;
    }
}

LLVMTypeRef get_llvm_float_type(RegisterSize size) {
    switch(size) {
        case RegisterSize::Size32: {
            return LLVMFloatType();
        } break;

        case RegisterSize::Size64: {
            return LLVMDoubleType();
        } break;

        default: {
            abort();
        } break;
    }
}

LLVMTypeRef get_llvm_type(RegisterSize size, bool is_float) {
    if(is_float) {
        return get_llvm_float_type(size);
    } else {
        return get_llvm_integer_type(size);
    }
}

struct Register {
    size_t index;

    LLVMValueRef value;
};

LLVMValueRef get_register_value(Function function, LLVMValueRef function_value, List<Register> registers, size_t register_index) {
    if(register_index < function.parameters.count) {
        auto parameter = LLVMGetParam(function_value, register_index);
        assert(parameter);

        return parameter;
    }

    for(auto register_ : registers) {
        if(register_.index == register_index) {
            return register_.value;
        }
    }

    abort();
}

struct InstructionBlock {
    Instruction *instruction;

    LLVMBasicBlockRef block;
};

static void register_instruction_block(List<InstructionBlock> *blocks, LLVMValueRef function, Instruction *instruction) {
    StringBuffer block_name {};
    string_buffer_append(&block_name, "block_");
    string_buffer_append(&block_name, blocks->count);

    append(blocks, {
        instruction,
        LLVMAppendBasicBlock(function, block_name.data)
    });
}

static void maybe_register_instruction_block(List<InstructionBlock> *blocks, LLVMValueRef function, Instruction *instruction) {
    for(auto block : *blocks) {
        if(block.instruction == instruction) {
            return;
        }
    }

    register_instruction_block(blocks, function, instruction);
}

static LLVMBasicBlockRef get_instruction_block(List<InstructionBlock> blocks, Instruction *instruction) {
    for(auto block : blocks) {
        if(block.instruction == instruction) {
            return block.block;
        }
    }

    abort();
}

profiled_function(Result<Array<NameMapping>>, generate_llvm_object, (
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *object_file_path
), (
    statics,
    architecture,
    os,
    config,
    object_file_path
)) {
    List<NameMapping> name_mappings {};

    for(auto runtime_static : statics) {
        if(runtime_static->is_no_mangle) {
            for(auto name_mapping : name_mappings) {
                if(strcmp(name_mapping.name, runtime_static->name) == 0) {
                    error(runtime_static->scope, runtime_static->range, "Conflicting no_mangle name '%s'", name_mapping.name);
                    error(name_mapping.runtime_static->scope, name_mapping.runtime_static->range, "Conflicing declaration here");

                    return err;
                }
            }

            append(&name_mappings, {
                runtime_static,
                runtime_static->name
            });
        }
    }

    for(auto runtime_static : statics) {
        if(!runtime_static->is_no_mangle) {
            StringBuffer name_buffer {};

            size_t number = 0;
            while(true) {
                string_buffer_append(&name_buffer, runtime_static->name);
                if(number != 0) {
                    string_buffer_append(&name_buffer, "_");
                    string_buffer_append(&name_buffer, number);
                }

                auto name_taken = false;
                for(auto name_mapping : name_mappings) {
                    if(strcmp(name_mapping.name, name_buffer.data) == 0) {
                        name_taken = true;
                        break;
                    }
                }

                if(name_taken) {
                    name_buffer.length = 0;
                    number += 1;
                } else {
                    append(&name_mappings, {
                        runtime_static,
                        name_buffer.data
                    });

                    break;
                }
            }
        }
    }

    assert(name_mappings.count == statics.count);

    auto register_sizes = get_register_sizes(architecture);

    auto builder = LLVMCreateBuilder();

    auto module = LLVMModuleCreateWithName("module");

    auto global_values = allocate<LLVMValueRef>(statics.count);

    for(size_t i = 0; i < statics.count; i += 1) {
        auto runtime_static = statics[i];

        LLVMValueRef global_value;
        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto parameter_count = function->parameters.count;
            auto parameter_types = allocate<LLVMTypeRef>(parameter_count);
            for(size_t i = 0; i < parameter_count; i += 1) {
                auto parameter = function->parameters[i];

                parameter_types[i] = get_llvm_type(parameter.size, parameter.is_float);
            }

            LLVMTypeRef return_type;
            if(function->has_return) {
                return_type = get_llvm_type(function->return_size, function->is_return_float);
            } else {
                return_type = LLVMVoidType();
            }

            auto function_type = LLVMFunctionType(return_type, parameter_types, (unsigned int)parameter_count, false);

            global_value = LLVMAddFunction(module, function->name, function_type);

            if(function->is_external) {
                LLVMSetLinkage(global_value, LLVMLinkage::LLVMExternalLinkage);
            }
        } else if(runtime_static->kind == RuntimeStaticKind::StaticConstant) {
            auto constant = (StaticConstant*)runtime_static;

            auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)constant->data.count);

            global_value = LLVMAddGlobal(module, byte_array_type, constant->name);
            LLVMSetAlignment(global_value, (unsigned int)constant->alignment);
            LLVMSetGlobalConstant(global_value, true);

            auto element_values = allocate<LLVMValueRef>(constant->data.count);

            for(size_t i = 0; i < constant->data.count; i += 1) {
                element_values[i] = LLVMConstInt(LLVMInt8Type(), constant->data[i], false);
            }

            auto array_constant = LLVMConstArray(LLVMInt8Type(), element_values, (unsigned int)constant->data.count);

            LLVMSetInitializer(global_value, array_constant);
        } else if(runtime_static->kind == RuntimeStaticKind::StaticVariable) {
            auto variable = (StaticVariable*)runtime_static;

            auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)variable->size);

            global_value = LLVMAddGlobal(module, byte_array_type, variable->name);
            LLVMSetAlignment(global_value, (unsigned int)variable->alignment);

            if(variable->is_external) {
                LLVMSetLinkage(global_value, LLVMLinkage::LLVMExternalLinkage);
            } else if(variable->has_initial_data) {
                auto element_values = allocate<LLVMValueRef>(variable->size);

                for(size_t i = 0; i < variable->size; i += 1) {
                    element_values[i] = LLVMConstInt(LLVMInt8Type(), variable->initial_data[i], false);
                }

                auto array_constant = LLVMConstArray(LLVMInt8Type(), element_values, (unsigned int)variable->size);

                LLVMSetInitializer(global_value, array_constant);
            }
        } else {
            abort();
        }

        global_values[i] = global_value;
    }

    for(auto runtime_static : statics) {
        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto function_value = LLVMGetNamedFunction(module, function->name);
            assert(function_value);

            if(!function->is_external) {
                List<InstructionBlock> blocks {};

                register_instruction_block(&blocks, function_value, 0);

                for(size_t i = 1; i < function->instructions.count; i += 1) {
                    auto instruction = function->instructions[i];

                    if(instruction->kind == InstructionKind::Jump) {
                        auto jump = (Jump*)instruction;

                        maybe_register_instruction_block(&blocks, function_value, function->instructions[jump->destination_instruction]);
                    } else if(instruction->kind == InstructionKind::Branch) {
                        auto branch = (Branch*)instruction;

                        maybe_register_instruction_block(&blocks, function_value, function->instructions[branch->destination_instruction]);

                        maybe_register_instruction_block(&blocks, function_value, function->instructions[i + 1]);
                    }
                }

                List<Register> registers {};

                LLVMPositionBuilderAtEnd(builder, blocks[0].block);

                struct Local {
                    AllocateLocal *allocate_local;

                    LLVMValueRef pointer_value;
                };

                List<Local> locals {};

                for(auto instruction : function->instructions) {
                    if(instruction->kind == InstructionKind::AllocateLocal) {
                        auto allocate_local = (AllocateLocal*)instruction;

                        auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)allocate_local->size);

                        auto pointer_value = LLVMBuildAlloca(builder, byte_array_type, "allocate_local");

                        LLVMSetAlignment(pointer_value, (unsigned int)allocate_local->alignment);

                        append(&locals, {
                            allocate_local,
                            pointer_value
                        });
                    }
                }

                for(size_t i = 0 ; i < function->instructions.count; i += 1) {
                    auto instruction = function->instructions[i];

                    for(auto block : blocks) {
                        if(block.instruction == instruction) {
                            LLVMPositionBuilderAtEnd(builder, block.block);
                        }
                    }

                    auto might_need_terminator = true;
                    if(instruction->kind == InstructionKind::IntegerArithmeticOperation) {
                        auto integer_arithmetic_operation = (IntegerArithmeticOperation*)instruction;

                        auto type = get_llvm_integer_type(integer_arithmetic_operation->size);

                        auto value_a = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_arithmetic_operation->source_register_a),
                            type,
                            "value_a"
                        );

                        auto value_b = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_arithmetic_operation->source_register_b),
                            type,
                            "value_b"
                        );

                        LLVMValueRef value;
                        switch(integer_arithmetic_operation->operation) {
                            case IntegerArithmeticOperation::Operation::Add: {
                                value = LLVMBuildAdd(builder, value_a, value_b, "add");
                            } break;

                            case IntegerArithmeticOperation::Operation::Subtract: {
                                value = LLVMBuildSub(builder, value_a, value_b, "subtract");
                            } break;

                            case IntegerArithmeticOperation::Operation::Multiply: {
                                value = LLVMBuildMul(builder, value_a, value_b, "multiply");
                            } break;

                            case IntegerArithmeticOperation::Operation::SignedDivide: {
                                value = LLVMBuildSDiv(builder, value_a, value_b, "divide");
                            } break;

                            case IntegerArithmeticOperation::Operation::UnsignedDivide: {
                                value = LLVMBuildUDiv(builder, value_a, value_b, "divide");
                            } break;

                            case IntegerArithmeticOperation::Operation::SignedModulus: {
                                value = LLVMBuildSRem(builder, value_a, value_b, "modulus");
                            } break;

                            case IntegerArithmeticOperation::Operation::UnsignedModulus: {
                                value = LLVMBuildURem(builder, value_a, value_b, "modulus");
                            } break;

                            case IntegerArithmeticOperation::Operation::BitwiseAnd: {
                                value = LLVMBuildAnd(builder, value_a, value_b, "and");
                            } break;

                            case IntegerArithmeticOperation::Operation::BitwiseOr: {
                                value = LLVMBuildOr(builder, value_a, value_b, "or");
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&registers, {
                            integer_arithmetic_operation->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::IntegerComparisonOperation) {
                        auto integer_comparison_operation = (IntegerComparisonOperation*)instruction;

                        auto type = get_llvm_integer_type(integer_comparison_operation->size);

                        auto value_a = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_comparison_operation->source_register_a),
                            type,
                            "value_a"
                        );

                        auto value_b = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_comparison_operation->source_register_b),
                            type,
                            "value_b"
                        );

                        LLVMIntPredicate predicate;
                        const char *name;
                        switch(integer_comparison_operation->operation) {
                            case IntegerComparisonOperation::Operation::Equal: {
                                predicate = LLVMIntPredicate::LLVMIntEQ;
                                name = "add";
                            } break;

                            case IntegerComparisonOperation::Operation::SignedLessThan: {
                                predicate = LLVMIntPredicate::LLVMIntSLT;
                                name = "less_than";
                            } break;

                            case IntegerComparisonOperation::Operation::UnsignedLessThan: {
                                predicate = LLVMIntPredicate::LLVMIntULT;
                                name = "less_than";
                            } break;

                            case IntegerComparisonOperation::Operation::SignedGreaterThan: {
                                predicate = LLVMIntPredicate::LLVMIntSGT;
                                name = "greater_than";
                            } break;

                            case IntegerComparisonOperation::Operation::UnsignedGreaterThan: {
                                predicate = LLVMIntPredicate::LLVMIntUGT;
                                name = "greater_than";
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        auto value = LLVMBuildICmp(builder, predicate, value_a, value_b, name);

                        auto extended_value = LLVMBuildZExt(builder, value, get_llvm_integer_type(register_sizes.default_size), "extend");

                        append(&registers, {
                            integer_comparison_operation->destination_register,
                            extended_value
                        });
                    } else if(instruction->kind == InstructionKind::IntegerUpcast) {
                        auto integer_upcast = (IntegerUpcast*)instruction;

                        auto source_value = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_upcast->source_register),
                            get_llvm_integer_type(integer_upcast->source_size),
                            "source"
                        );

                        LLVMValueRef value;
                        if(integer_upcast->is_signed) {
                            value = LLVMBuildSExt(builder, source_value, get_llvm_integer_type(integer_upcast->destination_size), "extend");
                        } else {
                            value = LLVMBuildZExt(builder, source_value, get_llvm_integer_type(integer_upcast->destination_size), "extend");
                        }

                        append(&registers, {
                            integer_upcast->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::IntegerConstantInstruction) {
                        auto integer_constant = (IntegerConstantInstruction*)instruction;

                        auto value = LLVMConstInt(get_llvm_integer_type(integer_constant->size), integer_constant->value, false);

                        append(&registers, {
                            integer_constant->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::FloatArithmeticOperation) {
                        auto float_arithmetic_operation = (FloatArithmeticOperation*)instruction;

                        auto value_a = get_register_value(*function, function_value, registers, float_arithmetic_operation->source_register_a);
                        auto value_b = get_register_value(*function, function_value, registers, float_arithmetic_operation->source_register_b);

                        LLVMValueRef value;
                        switch(float_arithmetic_operation->operation) {
                            case FloatArithmeticOperation::Operation::Add: {
                                value = LLVMBuildFAdd(builder, value_a, value_b, "add");
                            } break;

                            case FloatArithmeticOperation::Operation::Subtract: {
                                value = LLVMBuildFSub(builder, value_a, value_b, "subtract");
                            } break;

                            case FloatArithmeticOperation::Operation::Multiply: {
                                value = LLVMBuildFMul(builder, value_a, value_b, "multiply");
                            } break;

                            case FloatArithmeticOperation::Operation::Divide: {
                                value = LLVMBuildFDiv(builder, value_a, value_b, "divide");
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&registers, {
                            float_arithmetic_operation->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::FloatComparisonOperation) {
                        auto float_comparison_operation = (FloatComparisonOperation*)instruction;

                        auto value_a = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_a);
                        auto value_b = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_b);

                        LLVMRealPredicate predicate;
                        const char *name;
                        switch(float_comparison_operation->operation) {
                            case FloatComparisonOperation::Operation::Equal: {
                                predicate = LLVMRealPredicate::LLVMRealOEQ;
                                name = "add";
                            } break;

                            case FloatComparisonOperation::Operation::LessThan: {
                                predicate = LLVMRealPredicate::LLVMRealOLT;
                                name = "greater_than";
                            } break;

                            case FloatComparisonOperation::Operation::GreaterThan: {
                                predicate = LLVMRealPredicate::LLVMRealOGT;
                                name = "less_than";
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        auto value = LLVMBuildFCmp(builder, predicate, value_a, value_b, name);

                        auto extended_value = LLVMBuildZExt(builder, value, get_llvm_integer_type(register_sizes.default_size), "extend");

                        append(&registers, {
                            float_comparison_operation->destination_register,
                            extended_value
                        });
                    } else if(instruction->kind == InstructionKind::FloatConversion) {
                        auto float_conversion = (FloatConversion*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_conversion->source_register);

                        auto value = LLVMBuildFPCast(builder, source_value, get_llvm_float_type(float_conversion->destination_size), "float_conversion");

                        append(&registers, {
                            float_conversion->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::FloatTruncation) {
                        auto float_truncation = (FloatTruncation*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_truncation->source_register);

                        auto value = LLVMBuildFPToSI(builder, source_value, get_llvm_integer_type(float_truncation->destination_size), "float_truncation");

                        append(&registers, {
                            float_truncation->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::FloatFromInteger) {
                        auto float_from_integer = (FloatFromInteger*)instruction;

                        auto source_value = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, float_from_integer->source_register),
                            get_llvm_integer_type(float_from_integer->source_size),
                            "source"
                        );

                        auto value = LLVMBuildSIToFP(builder, source_value, get_llvm_float_type(float_from_integer->destination_size), "float_from_integer");

                        append(&registers, {
                            float_from_integer->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::FloatConstantInstruction) {
                        auto float_constant = (FloatConstantInstruction*)instruction;

                        auto value = LLVMConstReal(get_llvm_float_type(float_constant->size), float_constant->value);

                        append(&registers, {
                            float_constant->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::Jump) {
                        auto jump = (Jump*)instruction;

                        auto destination = get_instruction_block(blocks, function->instructions[jump->destination_instruction]);

                        LLVMBuildBr(builder, destination);

                        might_need_terminator = false;
                    } else if(instruction->kind == InstructionKind::Branch) {
                        auto branch = (Branch*)instruction;

                        auto condition_value = get_register_value(*function, function_value, registers, branch->condition_register);

                        auto truncated_condition_value = LLVMBuildTrunc(builder, condition_value, LLVMInt1Type(), "truncate");

                        auto destination = get_instruction_block(blocks, function->instructions[branch->destination_instruction]);

                        auto next = get_instruction_block(blocks, function->instructions[i + 1]);

                        LLVMBuildCondBr(builder, truncated_condition_value, destination, next);

                        might_need_terminator = false;
                    } else if(instruction->kind == InstructionKind::FunctionCallInstruction) {
                        auto function_call = (FunctionCallInstruction*)instruction;

                        auto parameter_count = function_call->parameters.count;

                        auto parameter_types = allocate<LLVMTypeRef>(parameter_count);
                        auto parameter_values = allocate<LLVMValueRef>(parameter_count);
                        for(size_t i = 0; i < parameter_count; i += 1) {
                            auto parameter = function_call->parameters[i];

                            auto parameter_value = get_register_value(*function, function_value, registers, parameter.register_index);

                            if(parameter.is_float) {
                                parameter_types[i] = get_llvm_float_type(parameter.size);
                                parameter_values[i] = parameter_value;
                            } else {
                                parameter_types[i] = get_llvm_integer_type(parameter.size);

                                parameter_values[i] = LLVMBuildTrunc(
                                    builder,
                                    parameter_value,
                                    get_llvm_integer_type(parameter.size),
                                    "parameter"
                                );
                            }
                        }

                        LLVMTypeRef return_type;
                        if(function_call->has_return) {
                            return_type = get_llvm_type(function_call->return_size, function_call->is_return_float);
                        } else {
                            return_type = LLVMVoidType();
                        }

                        auto function_type = LLVMFunctionType(return_type, parameter_types, (unsigned int)parameter_count, false);

                        auto function_pointer_type = LLVMPointerType(function_type, 0);

                        auto address_value = get_register_value(*function, function_value, registers, function_call->address_register);

                        auto function_pointer_value = LLVMBuildIntToPtr(builder, address_value, function_pointer_type, "pointer");

                        const char *name;
                        if(function_call->has_return) {
                            name = "call";
                        } else {
                            name = "";
                        }

                        auto value = LLVMBuildCall(builder, function_pointer_value, parameter_values, (unsigned int)parameter_count, name);

                        if(function_call->has_return) {
                            append(&registers, {
                                function_call->return_register,
                                value
                            });
                        }
                    } else if(instruction->kind == InstructionKind::ReturnInstruction) {
                        auto return_instruction = (ReturnInstruction*)instruction;

                        if(function->has_return) {
                            auto return_value = get_register_value(*function, function_value, registers, return_instruction->value_register);

                            LLVMBuildRet(builder, return_value);
                        } else {
                            LLVMBuildRetVoid(builder);
                        }

                        might_need_terminator = false;
                    } else if(instruction->kind == InstructionKind::AllocateLocal) {
                        auto allocate_local = (AllocateLocal*)instruction;

                        auto found = false;
                        LLVMValueRef pointer_value;
                        for(auto local : locals) {
                            if(local.allocate_local == allocate_local) {
                                pointer_value = local.pointer_value;
                                found = true;

                                break;
                            }
                        }
                        assert(found);

                        auto address_value = LLVMBuildPtrToInt(builder, pointer_value, get_llvm_integer_type(register_sizes.address_size), "local_address");

                        append(&registers, {
                            allocate_local->destination_register,
                            address_value
                        });
                    } else if(instruction->kind == InstructionKind::LoadInteger) {
                        auto load_integer = (LoadInteger*)instruction;

                        auto address_value = get_register_value(*function, function_value, registers, load_integer->address_register);

                        auto pointer_type = LLVMPointerType(get_llvm_integer_type(load_integer->size), 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        auto value = LLVMBuildLoad(builder, pointer_value, "load_integer");

                        append(&registers, {
                            load_integer->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::StoreInteger) {
                        auto store_integer = (StoreInteger*)instruction;

                        auto source_value = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, store_integer->source_register),
                            get_llvm_integer_type(store_integer->size),
                            "source"
                        );

                        auto address_value = get_register_value(*function, function_value, registers, store_integer->address_register);

                        auto pointer_type = LLVMPointerType(get_llvm_integer_type(store_integer->size), 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        LLVMBuildStore(builder, source_value, pointer_value);
                    } else if(instruction->kind == InstructionKind::LoadFloat) {
                        auto load_float = (LoadFloat*)instruction;

                        auto address_value = get_register_value(*function, function_value, registers, load_float->address_register);

                        auto pointer_type = LLVMPointerType(get_llvm_float_type(load_float->size), 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        auto value = LLVMBuildLoad(builder, pointer_value, "load_float");

                        append(&registers, {
                            load_float->destination_register,
                            value
                        });
                    } else if(instruction->kind == InstructionKind::StoreFloat) {
                        auto store_float = (StoreFloat*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, store_float->source_register);

                        auto address_value = get_register_value(*function, function_value, registers, store_float->address_register);

                        auto pointer_type = LLVMPointerType(get_llvm_float_type(store_float->size), 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        LLVMBuildStore(builder, source_value, pointer_value);
                    } else if(instruction->kind == InstructionKind::ReferenceStatic) {
                        auto reference_static = (ReferenceStatic*)instruction;

                        auto found = false;
                        LLVMValueRef global_value;
                        for(size_t i = 0; i < statics.count; i += 1) {
                            if(statics[i] == reference_static->runtime_static) {
                                global_value = global_values[i];
                                found = true;

                                break;
                            }
                        }
                        assert(found);

                        auto address_value = LLVMBuildPtrToInt(builder, global_value, get_llvm_integer_type(register_sizes.address_size), "static_address");

                        append(&registers, {
                            reference_static->destination_register,
                            address_value
                        });
                    } else if(instruction->kind == InstructionKind::CopyMemory) {
                        auto copy_memory = (CopyMemory*)instruction;

                        auto pointer_type = LLVMPointerType(LLVMInt8Type(), 0);

                        auto source_address_value = get_register_value(*function, function_value, registers, copy_memory->source_address_register);

                        auto source_pointer_value = LLVMBuildIntToPtr(builder, source_address_value, pointer_type, "source");

                        auto destination_address_value = get_register_value(*function, function_value, registers, copy_memory->destination_address_register);

                        auto destination_pointer_value = LLVMBuildIntToPtr(builder, destination_address_value, pointer_type, "destination");

                        auto length_value = LLVMConstInt(get_llvm_integer_type(register_sizes.address_size), copy_memory->length, false);

                        LLVMBuildMemCpyInline(
                            builder,
                            destination_pointer_value,
                            (unsigned int)copy_memory->alignment,
                            source_pointer_value,
                            (unsigned int)copy_memory->alignment,
                            length_value
                        );
                    } else {
                        abort();
                    }

                    if(might_need_terminator) {
                        for(auto block : blocks) {
                            if(block.instruction == function->instructions[i + 1]) {
                                LLVMBuildBr(builder, block.block);
                            }
                        }
                    }
                }
            }
        }
    }

    assert(LLVMVerifyModule(module, LLVMVerifierFailureAction::LLVMAbortProcessAction, nullptr) == 0);

    auto triple = get_llvm_triple(architecture, os);

    LLVMTargetRef target;
    if(strcmp(architecture, "x64") == 0) {
        LLVMInitializeX86TargetInfo();
        LLVMInitializeX86Target();
        LLVMInitializeX86TargetMC();
        LLVMInitializeX86AsmPrinter();

        auto status = LLVMGetTargetFromTriple(triple, &target, nullptr);
        assert(status == 0);
    } else {
        abort();
    }

    LLVMCodeGenOptLevel optimization_level;
    if(strcmp(config, "debug") == 0) {
        optimization_level = LLVMCodeGenOptLevel::LLVMCodeGenLevelNone;
    } else if(strcmp(config, "release") == 0) {
        optimization_level = LLVMCodeGenOptLevel::LLVMCodeGenLevelDefault;
    } else {
        abort();
    }

    auto target_machine = LLVMCreateTargetMachine(
        target,
        triple,
        "",
        "",
        optimization_level,
        LLVMRelocMode::LLVMRelocDefault,
        LLVMCodeModel::LLVMCodeModelDefault
    );
    assert(target_machine);

    char *error_message;
    if(LLVMTargetMachineEmitToFile(target_machine, module, (char*)object_file_path, LLVMCodeGenFileType::LLVMObjectFile, &error_message) != 0) {
        fprintf(stderr, "Error: Unable to emit object file '%s' (%s)\n", object_file_path, error_message);

        return err;
    }

    return ok(to_array(name_mappings));
}