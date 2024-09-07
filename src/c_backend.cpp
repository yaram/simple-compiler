#include "c_backend.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "platform.h"
#include "profiler.h"
#include "path.h"
#include "util.h"
#include "list.h"

static void generate_integer_type(StringBuffer* source, RegisterSize size, bool is_signed) {
    if(is_signed) {
        source->append("signed "_S);
    } else {
        source->append("unsigned "_S);
    }

    switch(size) {
        case RegisterSize::Size8: {
            source->append("char"_S);
        } break;

        case RegisterSize::Size16: {
            source->append("short"_S);
        } break;

        case RegisterSize::Size32: {
            source->append("int"_S);
        } break;

        case RegisterSize::Size64: {
            source->append("long long"_S);
        } break;

        default: {
            abort();
        } break;
    }
}

static void generate_float_type(StringBuffer* source, RegisterSize size) {
    switch(size) {
        case RegisterSize::Size32: {
            source->append("float"_S);
        } break;

        case RegisterSize::Size64: {
            source->append("double"_S);
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_function_signature(StringBuffer* source, String name, Function function) {
    if(function.has_return) {
        if(function.is_return_float) {
            generate_float_type(source, function.return_size);
        } else {
            generate_integer_type(source, function.return_size, false);
        }
    } else {
        source->append("void"_S);
    }

    source->append(" "_S);

    source->append(name);

    source->append("("_S);
    
    for(size_t i = 0; i < function.parameters.length; i += 1) {
        auto parameter = function.parameters[i];

        if(parameter.is_float) {
            generate_float_type(source, parameter.size);
        } else {
            generate_integer_type(source, parameter.size, false);
        }

        source->append(" reg_"_S);
        source->append(i);

        if(i != function.parameters.length - 1) {
            source->append(","_S);
        }
    }

    source->append(")"_S);

    return true;
}

profiled_function(Result<Array<NameMapping>>, generate_c_object,(
    Array<RuntimeStatic*> statics,
    String architecture,
    String os,
    String config,
    String object_file_path
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
                    error(runtime_static->scope, runtime_static->range, "Conflicting no_mangle name '%.*s'", name_mapping.name);
                    error(name_mapping.runtime_static->scope, name_mapping.runtime_static->range, "Conflicing declaration here");

                    return err();
                }
            }

            NameMapping name_mapping {};
            name_mapping.runtime_static = runtime_static;
            name_mapping.name = runtime_static->name;

            name_mappings.append(name_mapping);
        }
    }

    for(auto runtime_static : statics) {
        if(!runtime_static->is_no_mangle) {
            StringBuffer name_buffer {};

            size_t number = 0;
            while(true) {
                append(&name_buffer, runtime_static->name);
                if(number != 0) {
                    append(&name_buffer, "_");
                    append(&name_buffer, number);
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

    assert(name_mappings.length == statics.length);

    StringBuffer forward_declaration_source {};
    StringBuffer implementation_source {};

    auto register_sizes = get_register_sizes(architecture);

    for(auto runtime_static : statics) {
        String file_path;
        {
            auto current_scope = runtime_static->scope;
            while(!current_scope->is_top_level) {
                current_scope = current_scope->parent;
            }
            file_path = current_scope->file_path;
        }

        append(&forward_declaration_source, "#line ");
        append(&forward_declaration_source, runtime_static->range.first_line);
        append(&forward_declaration_source, " \"");
        for(size_t i = 0; i < strlen(file_path); i += 1) {
            auto character = file_path[i];

            if(character == '\\') {
                append(&forward_declaration_source, "\\\\");
            } else {
                append_character(&forward_declaration_source, character);
            }
        }
        append(&forward_declaration_source, "\"\n");

        append(&implementation_source, "#line ");
        append(&implementation_source, runtime_static->range.first_line);
        append(&implementation_source, " \"");
        for(size_t i = 0; i < strlen(file_path); i += 1) {
            auto character = file_path[i];

            if(character == '\\') {
                append(&implementation_source, "\\\\");
            } else {
                append_character(&implementation_source, character);
            }
        }
        append(&implementation_source, "\"\n");

        auto found = false;
        String name;
        for(auto name_mapping : name_mappings) {
            if(name_mapping.runtime_static == runtime_static) {
                found = true;
                name = name_mapping.name;
                break;
            }
        }

        assert(found);

        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            generate_function_signature(&forward_declaration_source, name,* function);
            append(&forward_declaration_source, ";\n");

            if(!function->is_external) {
                generate_function_signature(&implementation_source, name,* function);

                append(&implementation_source, "{\n");

                for(size_t i = 0 ; i < function->instructions.length; i += 1) {
                    auto instruction = function->instructions[i];

                    append(&implementation_source, "#line ");
                    append(&implementation_source, instruction->range.first_line);
                    append(&implementation_source, "\n");

                    append(&implementation_source, "__goto_");
                    append(&implementation_source, name);
                    append(&implementation_source, "_");
                    append(&implementation_source, i);
                    append(&implementation_source, ":;");

                    if(instruction->kind == InstructionKind::IntegerArithmeticOperation) {
                        auto integer_arithmetic_operation = (IntegerArithmeticOperation*)instruction;

                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, integer_arithmetic_operation->destination_register);

                        append(&implementation_source, "=");

                        String operator_;
                        bool is_signed;

                        switch(integer_arithmetic_operation->operation) {
                            case IntegerArithmeticOperation::Operation::Add: {
                                operator_ = "+";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::Subtract: {
                                operator_ = "-";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::Multiply: {
                                operator_ = "*";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::SignedDivide: {
                                operator_ = "/";
                                is_signed = true;
                            } break;

                            case IntegerArithmeticOperation::Operation::UnsignedDivide: {
                                operator_ = "/";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::SignedModulus: {
                                operator_ = "%";
                                is_signed = true;
                            } break;

                            case IntegerArithmeticOperation::Operation::UnsignedModulus: {
                                operator_ = "%";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::BitwiseAnd: {
                                operator_ = "&";
                                is_signed = false;
                            } break;

                            case IntegerArithmeticOperation::Operation::BitwiseOr: {
                                operator_ = "|";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, integer_arithmetic_operation->source_register_a);

                        append(&implementation_source, operator_);

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, integer_arithmetic_operation->source_register_b);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::IntegerComparisonOperation) {
                        auto integer_comparison_operation = (IntegerComparisonOperation*)instruction;

                        generate_integer_type(&implementation_source, register_sizes.default_size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, integer_comparison_operation->destination_register);

                        append(&implementation_source, "=");

                        String operator_;
                        bool is_signed;

                        switch(integer_comparison_operation->operation) {
                            case IntegerComparisonOperation::Operation::Equal: {
                                operator_ = "==";
                                is_signed = false;
                            } break;

                            case IntegerComparisonOperation::Operation::SignedLessThan: {
                                operator_ = "<";
                                is_signed = true;
                            } break;

                            case IntegerComparisonOperation::Operation::UnsignedLessThan: {
                                operator_ = "<";
                                is_signed = false;
                            } break;

                            case IntegerComparisonOperation::Operation::SignedGreaterThan: {
                                operator_ = ">";
                                is_signed = true;
                            } break;

                            case IntegerComparisonOperation::Operation::UnsignedGreaterThan: {
                                operator_ = ">";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_comparison_operation->size, is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, integer_comparison_operation->source_register_a);

                        append(&implementation_source, operator_);

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_comparison_operation->size, is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, integer_comparison_operation->source_register_b);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::IntegerUpcast) {
                        auto integer_upcast = (IntegerUpcast*)instruction;

                        generate_integer_type(&implementation_source, integer_upcast->destination_size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, integer_upcast->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_upcast->destination_size, integer_upcast->is_signed);
                        append(&implementation_source, ")(");
                        generate_integer_type(&implementation_source, integer_upcast->source_size, integer_upcast->is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, integer_upcast->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::IntegerConstantInstruction) {
                        auto integer_constant = (IntegerConstantInstruction*)instruction;

                        generate_integer_type(&implementation_source, integer_constant->size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, integer_constant->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, integer_constant->value);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatArithmeticOperation) {
                        auto float_arithmetic_operation = (FloatArithmeticOperation*)instruction;

                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_arithmetic_operation->destination_register);

                        append(&implementation_source, "=");

                        String operator_;

                        switch(float_arithmetic_operation->operation) {
                            case FloatArithmeticOperation::Operation::Add: {
                                operator_ = "+";
                            } break;

                            case FloatArithmeticOperation::Operation::Subtract: {
                                operator_ = "-";
                            } break;

                            case FloatArithmeticOperation::Operation::Multiply: {
                                operator_ = "*";
                            } break;

                            case FloatArithmeticOperation::Operation::Divide: {
                                operator_ = "/";
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_arithmetic_operation->source_register_a);

                        append(&implementation_source, operator_);

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_arithmetic_operation->source_register_b);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatComparisonOperation) {
                        auto float_comparison_operation = (FloatComparisonOperation*)instruction;

                        generate_float_type(&implementation_source, register_sizes.default_size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_comparison_operation->destination_register);

                        append(&implementation_source, "=");

                        String operator_;

                        switch(float_comparison_operation->operation) {
                            case FloatComparisonOperation::Operation::Equal: {
                                operator_ = "==";
                            } break;

                            case FloatComparisonOperation::Operation::LessThan: {
                                operator_ = "<";
                            } break;

                            case FloatComparisonOperation::Operation::GreaterThan: {
                                operator_ = ">";
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_comparison_operation->size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_comparison_operation->source_register_a);

                        append(&implementation_source, operator_);

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_comparison_operation->size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_comparison_operation->source_register_b);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatConversion) {
                        auto float_conversion = (FloatConversion*)instruction;

                        generate_float_type(&implementation_source, float_conversion->destination_size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_conversion->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_conversion->destination_size);
                        append(&implementation_source, ")(");
                        generate_float_type(&implementation_source, float_conversion->source_size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_conversion->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatTruncation) {
                        auto float_truncation = (FloatTruncation*)instruction;

                        generate_integer_type(&implementation_source, float_truncation->destination_size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_truncation->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, float_truncation->destination_size, false);
                        append(&implementation_source, ")(");
                        generate_float_type(&implementation_source, float_truncation->source_size);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_truncation->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatFromInteger) {
                        auto float_from_integer = (FloatFromInteger*)instruction;

                        generate_float_type(&implementation_source, float_from_integer->destination_size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_from_integer->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_from_integer->destination_size);
                        append(&implementation_source, ")(");
                        generate_integer_type(&implementation_source, float_from_integer->source_size, float_from_integer->is_signed);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, float_from_integer->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::FloatConstantInstruction) {
                        auto float_constant = (FloatConstantInstruction*)instruction;

                        generate_float_type(&implementation_source, float_constant->size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, float_constant->destination_register);
                        append(&implementation_source, "=");

                        double value;
                        switch(float_constant->size) {
                            case RegisterSize::Size32: {
                                value = (float)float_constant->value;
                            } break;

                            case RegisterSize::Size64: {
                                value = float_constant->value;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        const size_t buffer_size = 128;
                        char buffer[buffer_size];
                        snprintf(buffer, buffer_size, "%f", value);

                        append(&implementation_source, buffer);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::Jump) {
                        auto jump = (Jump*)instruction;

                        append(&implementation_source, "goto __goto_");
                        append(&implementation_source, name);
                        append(&implementation_source, "_");
                        append(&implementation_source, jump->destination_instruction);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::Branch) {
                        auto branch = (Branch*)instruction;

                        append(&implementation_source, "if(");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, register_sizes.default_size, false);
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, branch->condition_register);

                        append(&implementation_source, ")");

                        append(&implementation_source, "{");

                        append(&implementation_source, "goto __goto_");
                        append(&implementation_source, name);
                        append(&implementation_source, "_");
                        append(&implementation_source, branch->destination_instruction);
                        append(&implementation_source, ";");

                        append(&implementation_source, "}");
                    } else if(instruction->kind == InstructionKind::FunctionCallInstruction) {
                        auto function_call = (FunctionCallInstruction*)instruction;

                        if(function_call->has_return) {
                            if(function_call->is_return_float) {
                                generate_float_type(&implementation_source, function_call->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->return_size, false);
                            }

                            append(&implementation_source, " reg_");
                            append(&implementation_source, function_call->return_register);
                            append(&implementation_source, "=");
                        }

                        append(&implementation_source, "(");

                        append(&implementation_source, "(");
                        if(function_call->has_return) {
                            if(function_call->is_return_float) {
                                generate_float_type(&implementation_source, function_call->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->return_size, false);
                            }
                        } else {
                            append(&implementation_source, "void");
                        }
                        append(&implementation_source, "(*)");
                        append(&implementation_source, "(");
                        for(size_t i = 0; i < function_call->parameters.length; i += 1) {
                            if(function_call->parameters[i].is_float) {
                                generate_float_type(&implementation_source, function_call->parameters[i].size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->parameters[i].size, false);
                            }

                            if(i != function_call->parameters.length - 1) {
                                append(&implementation_source, ",");
                            }
                        }
                        append(&implementation_source, ")");
                        append(&implementation_source, ")");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, function_call->address_register);

                        append(&implementation_source, ")");

                        append(&implementation_source, "(");

                        for(size_t i = 0; i < function_call->parameters.length; i += 1) {
                            append(&implementation_source, "(");
                            if(function_call->parameters[i].is_float) {
                                generate_float_type(&implementation_source, function_call->parameters[i].size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->parameters[i].size, false);
                            }
                            append(&implementation_source, ")");

                            append(&implementation_source, " reg_");
                            append(&implementation_source, function_call->parameters[i].register_index);

                            if(i != function_call->parameters.length - 1) {
                                append(&implementation_source, ",");
                            }
                        }

                        append(&implementation_source, ");");
                    } else if(instruction->kind == InstructionKind::ReturnInstruction) {
                        auto return_instuction = (ReturnInstruction*)instruction;

                        append(&implementation_source, "return");

                        if(function->has_return) {
                            append(&implementation_source, "(");
                            if(function->is_return_float) {
                                generate_float_type(&implementation_source, function->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function->return_size, false);
                            }
                            append(&implementation_source, ")");

                            append(&implementation_source, "reg_");
                            append(&implementation_source, return_instuction->value_register);
                        }

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::AllocateLocal) {
                        auto allocate_local = (AllocateLocal*)instruction;

                        append(&implementation_source, "char __attribute__((aligned(");
                        append(&implementation_source, allocate_local->alignment);
                        append(&implementation_source, "))) local_");
                        append(&implementation_source, allocate_local->destination_register);
                        append(&implementation_source, "[");
                        append(&implementation_source, allocate_local->size);
                        append(&implementation_source, "];");

                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, allocate_local->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        append(&implementation_source, ")");

                        append(&implementation_source, "&local_");
                        append(&implementation_source, allocate_local->destination_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::LoadInteger) {
                        auto load_integer = (LoadInteger*)instruction;

                        generate_integer_type(&implementation_source, load_integer->size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, load_integer->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "*");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, load_integer->size, false);
                        append(&implementation_source, "*)");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, load_integer->address_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::StoreInteger) {
                        auto store_integer = (StoreInteger*)instruction;

                        append(&implementation_source, "*(");
                        generate_integer_type(&implementation_source, store_integer->size, false);
                        append(&implementation_source, "*)reg_");
                        append(&implementation_source, store_integer->address_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, store_integer->size, false);
                        append(&implementation_source, ")");
                        append(&implementation_source, "reg_");
                        append(&implementation_source, store_integer->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::LoadFloat) {
                        auto load_float = (LoadFloat*)instruction;

                        generate_float_type(&implementation_source, load_float->size);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, load_float->destination_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "*");

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, load_float->size);
                        append(&implementation_source, "*)");

                        append(&implementation_source, "reg_");
                        append(&implementation_source, load_float->address_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::StoreFloat) {
                        auto store_float = (StoreFloat*)instruction;

                        append(&implementation_source, "*(");
                        generate_float_type(&implementation_source, store_float->size);
                        append(&implementation_source, "*)reg_");
                        append(&implementation_source, store_float->address_register);
                        append(&implementation_source, "=");

                        append(&implementation_source, "(");
                        generate_float_type(&implementation_source, store_float->size);
                        append(&implementation_source, ")");
                        append(&implementation_source, "reg_");
                        append(&implementation_source, store_float->source_register);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::ReferenceStatic) {
                        auto reference_static = (ReferenceStatic*)instruction;

                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        append(&implementation_source, " reg_");
                        append(&implementation_source, reference_static->destination_register);
                        append(&implementation_source, "=");

                        auto found = false;
                        String name;
                        for(auto name_mapping : name_mappings) {
                            if(name_mapping.runtime_static == reference_static->runtime_static) {
                                found = true;
                                name = name_mapping.name;
                                break;
                            }
                        }

                        assert(found);

                        append(&implementation_source, "&");
                        append(&implementation_source, name);

                        append(&implementation_source, ";");
                    } else if(instruction->kind == InstructionKind::CopyMemory) {
                        auto copy_memory = (CopyMemory*)instruction;

                        append(&implementation_source, "for(");
                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        append(&implementation_source, " i=0;i<");
                        append(&implementation_source, copy_memory->length);
                        append(&implementation_source, ";i++)");

                        append(&implementation_source, "{");

                        append(&implementation_source, "((char*)reg_");
                        append(&implementation_source, copy_memory->destination_address_register);
                        append(&implementation_source, ")[i]");

                        append(&implementation_source, "=");

                        append(&implementation_source, "((char*)reg_");
                        append(&implementation_source, copy_memory->source_address_register);
                        append(&implementation_source, ")[i]");

                        append(&implementation_source, ";");

                        append(&implementation_source, "}");
                    } else {
                        abort();
                    }

                    append(&implementation_source, "\n");
                }

                append(&implementation_source, "}\n");
            }
        } else if(runtime_static->kind == RuntimeStaticKind::StaticConstant) {
            auto constant = (StaticConstant*)runtime_static;

            append(&forward_declaration_source, "const ");
            generate_integer_type(&forward_declaration_source, RegisterSize::Size8, false);
            append(&forward_declaration_source, " __attribute__((aligned(");
            append(&forward_declaration_source, constant->alignment);
            append(&forward_declaration_source, ")))");
            append(&forward_declaration_source, name);
            append(&forward_declaration_source, "[]");

            append(&forward_declaration_source, "=");

            append(&forward_declaration_source, "{");

            for(size_t i = 0; i < constant->data.length; i += 1) {
                append(&forward_declaration_source, constant->data.elements[i]);

                if(i != constant->data.length - 1) {
                    append(&forward_declaration_source, ",");
                }
            }

            append(&forward_declaration_source, "};\n");
        } else if(runtime_static->kind == RuntimeStaticKind::StaticVariable) {
            auto variable = (StaticVariable*)runtime_static;

            if(variable->is_external) {
                append(&forward_declaration_source, "extern ");
            }

            generate_integer_type(&forward_declaration_source, RegisterSize::Size8, false);
            append(&forward_declaration_source, " __attribute__((aligned(");
            append(&forward_declaration_source, variable->alignment);
            append(&forward_declaration_source, ")))");
            append(&forward_declaration_source, name);
            append(&forward_declaration_source, "[");
            append(&forward_declaration_source, variable->size);
            append(&forward_declaration_source, "]");

            if(!variable->is_external && variable->has_initial_data) {
                append(&forward_declaration_source, "=");

                append(&forward_declaration_source, "{");

                for(size_t i = 0; i < variable->size; i += 1) {
                    append(&forward_declaration_source, variable->initial_data[i]);

                    if(i != variable->size - 1) {
                        append(&forward_declaration_source, ",");
                    }
                }

                append(&forward_declaration_source, "}");
            }

            append(&forward_declaration_source, ";\n");
        } else {
            abort();
        }
    }

    StringBuffer source_file_path_buffer {};

    auto object_file_directory = path_get_directory_component(object_file_path);

    String source_file_name;
    {
        auto full_name = path_get_file_component(object_file_path);

        auto dot_pointer = strchr(full_name, '.');

        if(dot_pointer == nullptr) {
            source_file_name = full_name;
        } else {
            auto length = (size_t)dot_pointer - (size_t)full_name;

            if(length == 0) {
                source_file_name = "out.c";
            } else {
                auto buffer = allocate<char>(length + 1);

                memcpy(buffer, full_name, length);
                buffer[length] = 0;

                source_file_name = buffer;
            }
        }
    }

    append(&source_file_path_buffer, object_file_directory);
    append(&source_file_path_buffer, source_file_name);
    append(&source_file_path_buffer, ".c");

    enter_region("write c source file");
    auto source_file = fopen(source_file_path_buffer.data, "w");

    if(source_file == nullptr) {
        fprintf(stderr, "Unable to create C output file\n");

        return err();
    }

    fputs(forward_declaration_source.data, source_file);
    fputs(implementation_source.data, source_file);

    fclose(source_file);
    leave_region();

    StringBuffer command_buffer {};

    auto triple = get_llvm_triple(architecture, os);

    append(&command_buffer, "clang -std=gnu99 -ffreestanding -w -nostdinc -c -target ");

    append(&command_buffer, triple);

    if(strcmp(config, "debug") == 0) {
        append(&command_buffer, " -g");
    } else if(strcmp(config, "release") == 0) {
        append(&command_buffer, " -O2");
    } else {
        abort();
    }

    append(&command_buffer, " -o ");
    append(&command_buffer, object_file_path);

    append(&command_buffer, " ");
    append(&command_buffer, source_file_path_buffer.data);

    enter_region("clang");

    if(system(command_buffer.data) != 0) {
        fprintf(stderr, "Error: 'clang' returned non-zero\n");

        return err();
    }

    leave_region();

    return ok(name_mappings);
}