#include "c_generator.h"
#include <stdio.h>
#include <string.h>
#include "util.h"

static void generate_type(char **source, RegisterSize size, bool is_signed) {
    if(is_signed) {
        string_buffer_append(source, "signed ");
    } else {
        string_buffer_append(source, "unsigned ");
    }

    switch(size) {
        case RegisterSize::Size8: {
            string_buffer_append(source, "char");
        } break;

        case RegisterSize::Size16: {
            string_buffer_append(source, "short");
        } break;

        case RegisterSize::Size32: {
            string_buffer_append(source, "int");
        } break;

        case RegisterSize::Size64: {
            string_buffer_append(source, "long long");
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_function_signature(char **source, Function function) {
    if(function.has_return) {
        generate_type(source, function.return_size, false);
    } else {
        string_buffer_append(source, "void");
    }

    string_buffer_append(source, " ");

    string_buffer_append(source, function.name);

    string_buffer_append(source, "(");
    
    for(size_t i = 0; i < function.parameter_sizes.count; i += 1) {
        generate_type(source, function.parameter_sizes[i], false);

        string_buffer_append(source, " reg_");
        string_buffer_append(source, i);

        if(i != function.parameter_sizes.count - 1) {
            string_buffer_append(source, ",");
        }
    }

    string_buffer_append(source, ")");

    return true;
}

Result<const char *> generate_c_source(Array<Function> functions, Array<StaticConstant> constants, ArchitectureInfo architecture_info) {
    char *forward_declaration_source{};
    char *implementation_source{};

    for(auto constant : constants) {
        generate_type(&forward_declaration_source, RegisterSize::Size8, false);
        string_buffer_append(&forward_declaration_source, " ");
        string_buffer_append(&forward_declaration_source, constant.name);
        string_buffer_append(&forward_declaration_source, "[]");

        string_buffer_append(&forward_declaration_source, "=");

        string_buffer_append(&forward_declaration_source, "{");

        for(size_t i = 0; i < constant.data.count; i += 1) {
            string_buffer_append(&forward_declaration_source, constant.data.elements[i]);

            if(i != constant.data.count - 1) {
                string_buffer_append(&forward_declaration_source, ",");
            }
        }

        string_buffer_append(&forward_declaration_source, "};");
    }

    for(auto function : functions) {
        generate_function_signature(&forward_declaration_source, function);
        string_buffer_append(&forward_declaration_source, ";");

        if(!function.is_external) {
            generate_function_signature(&implementation_source, function);

            string_buffer_append(&implementation_source, "{");

            for(size_t i = 0 ; i < function.instructions.count; i += 1) {
                auto instruction = function.instructions[i];

                string_buffer_append(&implementation_source, function.name);
                string_buffer_append(&implementation_source, "_");
                string_buffer_append(&implementation_source, i);
                string_buffer_append(&implementation_source, ":");

                switch(instruction.type) {
                    case InstructionType::BinaryOperation: {
                        generate_type(&implementation_source, instruction.binary_operation.size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.binary_operation.destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
                        bool is_signed;

                        switch(instruction.binary_operation.type) {
                            case BinaryOperationType::Add: {
                                operator_ = "+";
                                is_signed = false;
                            } break;

                            case BinaryOperationType::Subtract: {
                                operator_ = "-";
                                is_signed = false;
                            } break;

                            case BinaryOperationType::SignedMultiply: {
                                operator_ = "*";
                                is_signed = true;
                            } break;

                            case BinaryOperationType::UnsignedMultiply: {
                                operator_ = "*";
                                is_signed = false;
                            } break;

                            case BinaryOperationType::SignedDivide: {
                                operator_ = "/";
                                is_signed = true;
                            } break;

                            case BinaryOperationType::UnsignedDivide: {
                                operator_ = "/";
                                is_signed = false;
                            } break;

                            case BinaryOperationType::SignedModulus: {
                                operator_ = "%";
                                is_signed = true;
                            } break;

                            case BinaryOperationType::UnsignedModulus: {
                                operator_ = "%";
                                is_signed = false;
                            } break;

                            case BinaryOperationType::Equality: {
                                operator_ = "==";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.binary_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.binary_operation.source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.binary_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.binary_operation.source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::IntegerUpcast: {
                        generate_type(&implementation_source, instruction.integer_upcast.destination_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.integer_upcast.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.integer_upcast.destination_size, instruction.integer_upcast.is_signed);
                        string_buffer_append(&implementation_source, ")(");
                        generate_type(&implementation_source, instruction.integer_upcast.source_size, instruction.integer_upcast.is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.integer_upcast.source_register);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::Constant: {
                        generate_type(&implementation_source, instruction.constant.size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.constant.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, instruction.constant.value);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::Jump: {
                        string_buffer_append(&implementation_source, "goto ");
                        string_buffer_append(&implementation_source, function.name);
                        string_buffer_append(&implementation_source, "_");
                        string_buffer_append(&implementation_source, instruction.jump.destination_instruction);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::Branch: {
                        string_buffer_append(&implementation_source, "if(");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, architecture_info.default_size, false);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.branch.condition_register);

                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "{");

                        string_buffer_append(&implementation_source, "goto ");
                        string_buffer_append(&implementation_source, function.name);
                        string_buffer_append(&implementation_source, "_");
                        string_buffer_append(&implementation_source, instruction.branch.destination_instruction);
                        string_buffer_append(&implementation_source, ";");

                        string_buffer_append(&implementation_source, "}");
                    } break;

                    case InstructionType::FunctionCall: {
                        Function callee;
                        for(auto function : functions) {
                            if(strcmp(function.name, instruction.function_call.function_name) == 0) {
                                callee = function;

                                break;
                            }
                        }

                        if(instruction.function_call.has_return) {
                            generate_type(&implementation_source, callee.return_size, false);
                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, instruction.function_call.return_register);
                            string_buffer_append(&implementation_source, "=");
                        }

                        string_buffer_append(&implementation_source, callee.name);
                        string_buffer_append(&implementation_source, "(");

                        for(size_t i = 0; i < instruction.function_call.parameter_registers.count; i += 1) {
                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, instruction.function_call.parameter_registers[i]);

                            if(i != instruction.function_call.parameter_registers.count - 1) {
                                string_buffer_append(&implementation_source, ",");
                            }
                        }

                        string_buffer_append(&implementation_source, ");");
                    } break;

                    case InstructionType::Return: {
                        string_buffer_append(&implementation_source, "return");

                        if(function.has_return) {
                            string_buffer_append(&implementation_source, "(");
                            generate_type(&implementation_source, function.return_size, false);
                            string_buffer_append(&implementation_source, ")");

                            string_buffer_append(&implementation_source, "reg_");
                            string_buffer_append(&implementation_source, instruction.return_.value_register);
                        }

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::AllocateLocal: {
                        string_buffer_append(&implementation_source, "char local_");
                        string_buffer_append(&implementation_source, instruction.allocate_local.destination_register);
                        string_buffer_append(&implementation_source, "[");
                        string_buffer_append(&implementation_source, instruction.allocate_local.size);
                        string_buffer_append(&implementation_source, "];");

                        generate_type(&implementation_source, architecture_info.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.allocate_local.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, architecture_info.address_size, false);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "&local_");
                        string_buffer_append(&implementation_source, instruction.allocate_local.destination_register);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::LoadInteger: {
                        generate_type(&implementation_source, instruction.load_integer.size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.load_integer.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "*");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.load_integer.size, false);
                        string_buffer_append(&implementation_source, "*)");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.load_integer.address_register);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::StoreInteger: {
                        string_buffer_append(&implementation_source, "*(");
                        generate_type(&implementation_source, instruction.store_integer.size, false);
                        string_buffer_append(&implementation_source, "*)reg_");
                        string_buffer_append(&implementation_source, instruction.store_integer.address_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.store_integer.size, false);
                        string_buffer_append(&implementation_source, ")");
                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.store_integer.source_register);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::ReferenceStatic: {
                        generate_type(&implementation_source, architecture_info.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.reference_static.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "&");
                        string_buffer_append(&implementation_source, instruction.reference_static.name);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::CopyMemory: {
                        string_buffer_append(&implementation_source, "for(");
                        generate_type(&implementation_source, architecture_info.address_size, false);
                        string_buffer_append(&implementation_source, " i=0;i<reg_");
                        string_buffer_append(&implementation_source, instruction.copy_memory.length_register);
                        string_buffer_append(&implementation_source, ";i++)");

                        string_buffer_append(&implementation_source, "{");

                        string_buffer_append(&implementation_source, "((char*)");
                        string_buffer_append(&implementation_source, instruction.copy_memory.source_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "((char*)");
                        string_buffer_append(&implementation_source, instruction.copy_memory.source_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, ";");

                        string_buffer_append(&implementation_source, "}");
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            string_buffer_append(&implementation_source, "}");
        }
    }

    char *source{};

    string_buffer_append(&source, forward_declaration_source);
    string_buffer_append(&source, implementation_source);

    if(source == nullptr) {
        return { false };
    }

    return {
        true,
        source
    };
}