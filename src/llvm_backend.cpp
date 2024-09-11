#include "llvm_backend.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
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

Result<LLVMCallConv> get_llvm_calling_convention(
    String path,
    FileRange range,
    String os,
    String architecture,
    CallingConvention calling_convention
) {
    if(architecture == "x86"_S) {
        if(os == "linux"_S) {
            if(calling_convention == CallingConvention::Default) {
                return ok(LLVMCallConv::LLVMCCallConv);
            }
        } else if(os == "windows"_S) {
            switch(calling_convention) {
                case CallingConvention::Default: {
                    return ok(LLVMCallConv::LLVMCCallConv);
                } break;

                case CallingConvention::StdCall: {
                    return ok(LLVMCallConv::LLVMX86StdcallCallConv);
                } break;

                default: abort();
            }
        } else {
            abort();
        }
    } else if(architecture == "x64"_S) {
        if(calling_convention == CallingConvention::Default) {
            if(os == "linux"_S) {
                return ok(LLVMCallConv::LLVMX8664SysVCallConv);
            } else if(os == "windows"_S) {
                return ok(LLVMCallConv::LLVMWin64CallConv);
            } else {
                abort();
            }
        }
    } else if(architecture == "wasm32"_S) {
        if(calling_convention == CallingConvention::Default) {
            return ok(LLVMCallConv::LLVMCCallConv);
        }
    } else {
        abort();
    }

    error(
        path,
        range,
        "Cannot use '%.*s' calling convention with %.*s %.*s",
        STRING_PRINTF_ARGUMENTS(calling_convention_name(calling_convention)),
        STRING_PRINTF_ARGUMENTS(os),
        STRING_PRINTF_ARGUMENTS(architecture)
    );

    return err();
}

struct Register {
    inline Register(size_t index, LLVMValueRef value) : index(index), value(value) {}

    size_t index;

    LLVMValueRef value;
};

LLVMValueRef get_register_value(Function function, LLVMValueRef function_value, List<Register> registers, size_t register_index) {
    if(register_index < function.parameters.length) {
        auto parameter = LLVMGetParam(function_value, (unsigned int)register_index);
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
    inline InstructionBlock(Instruction* instruction, LLVMBasicBlockRef block) : instruction(instruction), block(block) {}

    Instruction* instruction;

    LLVMBasicBlockRef block;
};

inline void register_instruction_block(List<InstructionBlock>* blocks, LLVMValueRef function, Instruction* instruction) {
    StringBuffer block_name {};
    block_name.append("block_"_S);
    block_name.append_integer(blocks->length);

    blocks->append(InstructionBlock(
        instruction,
        LLVMAppendBasicBlock(function, block_name.to_c_string())
    ));
}

inline void maybe_register_instruction_block(List<InstructionBlock>* blocks, LLVMValueRef function, Instruction* instruction) {
    for(auto block : *blocks) {
        if(block.instruction == instruction) {
            return;
        }
    }

    register_instruction_block(blocks, function, instruction);
}

inline LLVMBasicBlockRef get_instruction_block(List<InstructionBlock> blocks, Instruction* instruction) {
    for(auto block : blocks) {
        if(block.instruction == instruction) {
            return block.block;
        }
    }

    abort();
}

profiled_function(Result<Array<NameMapping>>, generate_llvm_object, (
    Array<RuntimeStatic*> statics,
    String architecture,
    String os,
    String toolchain,
    String config,
    String object_file_path,
    Array<String> reserved_names
), (
    statics,
    architecture,
    os,
    config,
    object_file_path,
    reserved_names
)) {
    List<NameMapping> name_mappings {};

    for(auto runtime_static : statics) {
        if(runtime_static->is_no_mangle) {
            for(auto name_mapping : name_mappings) {
                if(name_mapping.name == runtime_static->name) {
                    error(runtime_static->path, runtime_static->range, "Conflicting no_mangle name '%.*s'", STRING_PRINTF_ARGUMENTS(name_mapping.name));
                    error(name_mapping.runtime_static->path, name_mapping.runtime_static->range, "Conflicing declaration here");

                    return err();
                }
            }

            for(auto reserved_name : reserved_names) {
                if(reserved_name == runtime_static->name) {
                    error(runtime_static->path, runtime_static->range, "Runtime name '%.*s' is reserved", STRING_PRINTF_ARGUMENTS(reserved_name));

                    return err();
                }
            }

            NameMapping mapping {};
            mapping.runtime_static = runtime_static;
            mapping.name = runtime_static->name;

            name_mappings.append(mapping);
        }
    }

    for(auto runtime_static : statics) {
        if(!runtime_static->is_no_mangle) {
            StringBuffer name_buffer {};

            size_t number = 0;
            while(true) {
                name_buffer.append(runtime_static->name);
                if(number != 0) {
                    name_buffer.append("_"_S);
                    name_buffer.append_integer(number);
                }

                auto name_taken = false;

                for(auto name_mapping : name_mappings) {
                    if(name_mapping.name == name_buffer) {
                        name_taken = true;
                        break;
                    }
                }

                for(auto reserved_name : reserved_names) {
                    if(reserved_name == name_buffer) {
                        name_taken = true;
                        break;
                    }
                }

                if(name_taken) {
                    name_buffer.length = 0;
                    number += 1;
                } else {
                    NameMapping mapping {};
                    mapping.runtime_static = runtime_static;
                    mapping.name = name_buffer;

                    name_mappings.append(mapping);

                    break;
                }
            }
        }
    }

    assert(name_mappings.length == statics.length);

    auto architecture_sizes = get_architecture_sizes(architecture);

    auto builder = LLVMCreateBuilder();

    auto module = LLVMModuleCreateWithName("module");

    auto global_values = allocate<LLVMValueRef>(statics.length);

    for(size_t i = 0; i < statics.length; i += 1) {
        auto runtime_static = statics[i];

        String name;
        auto found = false;
        for(auto name_mapping : name_mappings) {
            if(name_mapping.runtime_static == runtime_static) {
                name = name_mapping.name;
                found = true;

                break;
            }
        }
        assert(found);

        LLVMValueRef global_value;
        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto parameter_count = function->parameters.length;
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

            global_value = LLVMAddFunction(module, name.to_c_string(), function_type);

            if(function->is_external) {
                LLVMSetLinkage(global_value, LLVMLinkage::LLVMExternalLinkage);
            }

            expect(calling_convention, get_llvm_calling_convention(function->path, function->range, os, architecture, function->calling_convention));

            LLVMSetFunctionCallConv(global_value, calling_convention);
        } else if(runtime_static->kind == RuntimeStaticKind::StaticConstant) {
            auto constant = (StaticConstant*)runtime_static;

            auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)constant->data.length);

            global_value = LLVMAddGlobal(module, byte_array_type, name.to_c_string());
            LLVMSetAlignment(global_value, (unsigned int)constant->alignment);
            LLVMSetGlobalConstant(global_value, true);

            auto element_values = allocate<LLVMValueRef>(constant->data.length);

            for(size_t i = 0; i < constant->data.length; i += 1) {
                element_values[i] = LLVMConstInt(LLVMInt8Type(), constant->data[i], false);
            }

            auto array_constant = LLVMConstArray(LLVMInt8Type(), element_values, (unsigned int)constant->data.length);

            LLVMSetInitializer(global_value, array_constant);
        } else if(runtime_static->kind == RuntimeStaticKind::StaticVariable) {
            auto variable = (StaticVariable*)runtime_static;

            auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)variable->size);

            global_value = LLVMAddGlobal(module, byte_array_type, name.to_c_string());
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

    for(size_t i = 0; i < statics.length; i += 1) {
        auto runtime_static = statics[i];

        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto function_value = global_values[i];

            if(!function->is_external) {
                List<InstructionBlock> blocks {};

                register_instruction_block(&blocks, function_value, 0);

                for(size_t i = 1; i < function->instructions.length; i += 1) {
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
                    AllocateLocal* allocate_local;

                    LLVMValueRef pointer_value;
                };

                List<Local> locals {};

                for(auto instruction : function->instructions) {
                    if(instruction->kind == InstructionKind::AllocateLocal) {
                        auto allocate_local = (AllocateLocal*)instruction;

                        auto byte_array_type = LLVMArrayType(LLVMInt8Type(), (unsigned int)allocate_local->size);

                        auto pointer_value = LLVMBuildAlloca(builder, byte_array_type, "allocate_local");

                        LLVMSetAlignment(pointer_value, (unsigned int)allocate_local->alignment);

                        Local local {};
                        local.allocate_local = allocate_local;
                        local.pointer_value = pointer_value;

                        locals.append(local);
                    }
                }

                for(size_t i = 0 ; i < function->instructions.length; i += 1) {
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

                            case IntegerArithmeticOperation::Operation::LeftShift: {
                                value = LLVMBuildShl(builder, value_a, value_b, "left_shift");
                            } break;

                            case IntegerArithmeticOperation::Operation::RightShift: {
                                value = LLVMBuildLShr(builder, value_a, value_b, "right_shift");
                            } break;

                            case IntegerArithmeticOperation::Operation::RightArithmeticShift: {
                                value = LLVMBuildAShr(builder, value_a, value_b, "right_arithmetic_shift");
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        registers.append(Register(
                            integer_arithmetic_operation->destination_register,
                            value
                        ));
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
                        const char* name;
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

                        auto extended_value = LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend");

                        registers.append(Register(
                            integer_comparison_operation->destination_register,
                            extended_value
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerExtension) {
                        auto integer_extension = (IntegerExtension*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, integer_extension->source_register);

                        LLVMValueRef value;
                        if(integer_extension->is_signed) {
                            value = LLVMBuildSExt(builder, source_value, get_llvm_integer_type(integer_extension->destination_size), "extend");
                        } else {
                            value = LLVMBuildZExt(builder, source_value, get_llvm_integer_type(integer_extension->destination_size), "extend");
                        }

                        registers.append(Register(
                            integer_extension->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerTruncation) {
                        auto integer_truncation = (IntegerTruncation*)instruction;

                        auto value = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, integer_truncation->source_register),
                            get_llvm_integer_type(integer_truncation->destination_size),
                            "truncate"
                        );

                        registers.append(Register(
                            integer_truncation->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerConstantInstruction) {
                        auto integer_constant = (IntegerConstantInstruction*)instruction;

                        auto value = LLVMConstInt(get_llvm_integer_type(integer_constant->size), integer_constant->value, false);

                        registers.append(Register(
                            integer_constant->destination_register,
                            value
                        ));
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

                        registers.append(Register(
                            float_arithmetic_operation->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::FloatComparisonOperation) {
                        auto float_comparison_operation = (FloatComparisonOperation*)instruction;

                        auto value_a = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_a);
                        auto value_b = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_b);

                        LLVMRealPredicate predicate;
                        const char* name;
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

                        auto extended_value = LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend");

                        registers.append(Register(
                            float_comparison_operation->destination_register,
                            extended_value
                        ));
                    } else if(instruction->kind == InstructionKind::FloatConversion) {
                        auto float_conversion = (FloatConversion*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_conversion->source_register);

                        auto value = LLVMBuildFPCast(builder, source_value, get_llvm_float_type(float_conversion->destination_size), "float_conversion");

                        registers.append(Register(
                            float_conversion->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::FloatTruncation) {
                        auto float_truncation = (FloatTruncation*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_truncation->source_register);

                        auto value = LLVMBuildFPToSI(builder, source_value, get_llvm_integer_type(float_truncation->destination_size), "float_truncation");

                        registers.append(Register(
                            float_truncation->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::FloatFromInteger) {
                        auto float_from_integer = (FloatFromInteger*)instruction;

                        auto source_value = LLVMBuildTrunc(
                            builder,
                            get_register_value(*function, function_value, registers, float_from_integer->source_register),
                            get_llvm_integer_type(float_from_integer->source_size),
                            "source"
                        );

                        auto value = LLVMBuildSIToFP(builder, source_value, get_llvm_float_type(float_from_integer->destination_size), "float_from_integer");

                        registers.append(Register(
                            float_from_integer->destination_register,
                            value
                        ));
                    } else if(instruction->kind == InstructionKind::FloatConstantInstruction) {
                        auto float_constant = (FloatConstantInstruction*)instruction;

                        auto value = LLVMConstReal(get_llvm_float_type(float_constant->size), float_constant->value);

                        registers.append(Register(
                            float_constant->destination_register,
                            value
                        ));
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

                        auto parameter_count = function_call->parameters.length;

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

                        const char* name;
                        if(function_call->has_return) {
                            name = "call";
                        } else {
                            name = "";
                        }

                        auto value = LLVMBuildCall2(builder, function_type, function_pointer_value, parameter_values, (unsigned int)parameter_count, name);

                        expect(calling_convention, get_llvm_calling_convention(
                            function->path,
                            function_call->range,
                            os,
                            architecture,
                            function_call->calling_convention
                        ));

                        LLVMSetInstructionCallConv(value, calling_convention);

                        if(function_call->has_return) {
                            registers.append(Register(
                                function_call->return_register,
                                value
                            ));
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

                        auto address_value = LLVMBuildPtrToInt(builder, pointer_value, get_llvm_integer_type(architecture_sizes.address_size), "local_address");

                        registers.append(Register(
                            allocate_local->destination_register,
                            address_value
                        ));
                    } else if(instruction->kind == InstructionKind::LoadInteger) {
                        auto load_integer = (LoadInteger*)instruction;

                        auto address_value = get_register_value(*function, function_value, registers, load_integer->address_register);

                        auto integer_type = get_llvm_integer_type(load_integer->size);

                        auto pointer_type = LLVMPointerType(integer_type, 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        auto value = LLVMBuildLoad2(builder, integer_type, pointer_value, "load_integer");

                        registers.append(Register(
                            load_integer->destination_register,
                            value
                        ));
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

                        auto float_type = get_llvm_float_type(load_float->size);

                        auto pointer_type = LLVMPointerType(float_type, 0);

                        auto pointer_value = LLVMBuildIntToPtr(builder, address_value, pointer_type, "pointer");

                        auto value = LLVMBuildLoad2(builder, float_type, pointer_value, "load_float");

                        registers.append(Register(
                            load_float->destination_register,
                            value
                        ));
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
                        for(size_t i = 0; i < statics.length; i += 1) {
                            if(statics[i] == reference_static->runtime_static) {
                                global_value = global_values[i];
                                found = true;

                                break;
                            }
                        }
                        assert(found);

                        auto address_value = LLVMBuildPtrToInt(builder, global_value, get_llvm_integer_type(architecture_sizes.address_size), "static_address");

                        registers.append(Register(
                            reference_static->destination_register,
                            address_value
                        ));
                    } else if(instruction->kind == InstructionKind::CopyMemory) {
                        auto copy_memory = (CopyMemory*)instruction;

                        auto pointer_type = LLVMPointerType(LLVMInt8Type(), 0);

                        auto source_address_value = get_register_value(*function, function_value, registers, copy_memory->source_address_register);

                        auto source_pointer_value = LLVMBuildIntToPtr(builder, source_address_value, pointer_type, "source");

                        auto destination_address_value = get_register_value(*function, function_value, registers, copy_memory->destination_address_register);

                        auto destination_pointer_value = LLVMBuildIntToPtr(builder, destination_address_value, pointer_type, "destination");

                        auto length_value = LLVMConstInt(get_llvm_integer_type(architecture_sizes.address_size), copy_memory->length, false);

                        LLVMBuildMemCpy(
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

    auto triple = get_llvm_triple(architecture, os, toolchain);

    LLVMTargetRef target;
    if(architecture == "x86"_S || architecture == "x64"_S) {
        LLVMInitializeX86TargetInfo();
        LLVMInitializeX86Target();
        LLVMInitializeX86TargetMC();
        LLVMInitializeX86AsmPrinter();

        auto status = LLVMGetTargetFromTriple(triple.to_c_string(), &target, nullptr);
        assert(status == 0);
    } else if(architecture == "wasm32"_S) {
        LLVMInitializeWebAssemblyTargetInfo();
        LLVMInitializeWebAssemblyTarget();
        LLVMInitializeWebAssemblyTargetMC();
        LLVMInitializeWebAssemblyAsmPrinter();

        auto status = LLVMGetTargetFromTriple(triple.to_c_string(), &target, nullptr);
        assert(status == 0);
    } else {
        abort();
    }

    LLVMCodeGenOptLevel optimization_level;
    if(config == "debug"_S) {
        optimization_level = LLVMCodeGenOptLevel::LLVMCodeGenLevelNone;
    } else if(config == "release"_S) {
        optimization_level = LLVMCodeGenOptLevel::LLVMCodeGenLevelDefault;
    } else {
        abort();
    }

    auto target_machine = LLVMCreateTargetMachine(
        target,
        triple.to_c_string(),
        "",
        "",
        optimization_level,
        LLVMRelocMode::LLVMRelocPIC,
        LLVMCodeModel::LLVMCodeModelDefault
    );
    assert(target_machine);

    char* error_message;
    if(LLVMTargetMachineEmitToFile(target_machine, module, object_file_path.to_c_string(), LLVMCodeGenFileType::LLVMObjectFile, &error_message) != 0) {
        fprintf(stderr, "Error: Unable to emit object file '%.*s' (%s)\n", STRING_PRINTF_ARGUMENTS(object_file_path), error_message);

        return err();
    }

    return ok((Array<NameMapping>)name_mappings);
}