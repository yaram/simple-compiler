#include "c_backend.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "path.h"
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

bool generate_c_object(
    Array<Function> functions,
    Array<StaticConstant> constants,
    const char *architecture,
    const char *os,
    const char *config,
    const char *output_directory,
    const char *output_name
) {
    char *forward_declaration_source{};
    char *implementation_source{};

    auto register_sizes = get_register_sizes(architecture);

    for(auto constant : constants) {
        generate_type(&forward_declaration_source, RegisterSize::Size8, false);
        string_buffer_append(&forward_declaration_source, " __attribute__((aligned(");
        string_buffer_append(&forward_declaration_source, constant.alignment);
        string_buffer_append(&forward_declaration_source, ")))");
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

        string_buffer_append(&forward_declaration_source, "};\n");
    }

    for(auto function : functions) {
        generate_function_signature(&forward_declaration_source, function);
        string_buffer_append(&forward_declaration_source, ";\n");

        if(!function.is_external) {
            string_buffer_append(&implementation_source, "#line ");
            string_buffer_append(&implementation_source, function.line);
            string_buffer_append(&implementation_source, " \"");
            for(size_t i = 0; i < strlen(function.file); i += 1) {
                auto character = function.file[i];

                if(character == '\\') {
                    string_buffer_append(&implementation_source, "\\\\");
                } else {
                    string_buffer_append_character(&implementation_source, character);
                }
            }
            string_buffer_append(&implementation_source, "\"\n");

            auto last_line = function.line;

            generate_function_signature(&implementation_source, function);

            string_buffer_append(&implementation_source, "{\n");

            for(size_t i = 0 ; i < function.instructions.count; i += 1) {
                auto instruction = function.instructions[i];

                if(instruction.line > last_line) {
                    string_buffer_append(&implementation_source, "#line ");
                    string_buffer_append(&implementation_source, instruction.line);
                    string_buffer_append(&implementation_source, "\n");

                    last_line = instruction.line;
                }

                string_buffer_append(&implementation_source, function.name);
                string_buffer_append(&implementation_source, "_");
                string_buffer_append(&implementation_source, i);
                string_buffer_append(&implementation_source, ":;");

                switch(instruction.type) {
                    case InstructionType::ArithmeticOperation: {
                        generate_type(&implementation_source, instruction.arithmetic_operation.size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.arithmetic_operation.destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
                        bool is_signed;

                        switch(instruction.arithmetic_operation.type) {
                            case ArithmeticOperationType::Add: {
                                operator_ = "+";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::Subtract: {
                                operator_ = "-";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::SignedMultiply: {
                                operator_ = "*";
                                is_signed = true;
                            } break;

                            case ArithmeticOperationType::UnsignedMultiply: {
                                operator_ = "*";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::SignedDivide: {
                                operator_ = "/";
                                is_signed = true;
                            } break;

                            case ArithmeticOperationType::UnsignedDivide: {
                                operator_ = "/";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::SignedModulus: {
                                operator_ = "%";
                                is_signed = true;
                            } break;

                            case ArithmeticOperationType::UnsignedModulus: {
                                operator_ = "%";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::BitwiseAnd: {
                                operator_ = "&";
                                is_signed = false;
                            } break;

                            case ArithmeticOperationType::BitwiseOr: {
                                operator_ = "|";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.arithmetic_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.arithmetic_operation.source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.arithmetic_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.arithmetic_operation.source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::ComparisonOperation: {
                        generate_type(&implementation_source, register_sizes.default_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.comparison_operation.destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
                        bool is_signed;

                        switch(instruction.comparison_operation.type) {
                            case ComparisonOperationType::Equal: {
                                operator_ = "==";
                                is_signed = false;
                            } break;

                            case ComparisonOperationType::SignedLessThan: {
                                operator_ = "<";
                                is_signed = true;
                            } break;

                            case ComparisonOperationType::UnsignedLessThan: {
                                operator_ = "<";
                                is_signed = false;
                            } break;

                            case ComparisonOperationType::SignedGreaterThan: {
                                operator_ = ">";
                                is_signed = true;
                            } break;

                            case ComparisonOperationType::UnsignedGreaterThan: {
                                operator_ = ">";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.comparison_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.comparison_operation.source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, instruction.comparison_operation.size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, instruction.comparison_operation.source_register_b);

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
                        generate_type(&implementation_source, register_sizes.default_size, false);
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
                        string_buffer_append(&implementation_source, "char __attribute__((aligned(");
                        string_buffer_append(&implementation_source, instruction.allocate_local.alignment);
                        string_buffer_append(&implementation_source, "))) local_");
                        string_buffer_append(&implementation_source, instruction.allocate_local.destination_register);
                        string_buffer_append(&implementation_source, "[");
                        string_buffer_append(&implementation_source, instruction.allocate_local.size);
                        string_buffer_append(&implementation_source, "];");

                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.allocate_local.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, register_sizes.address_size, false);
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
                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, instruction.reference_static.destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "&");
                        string_buffer_append(&implementation_source, instruction.reference_static.name);

                        string_buffer_append(&implementation_source, ";");
                    } break;

                    case InstructionType::CopyMemory: {
                        string_buffer_append(&implementation_source, "for(");
                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " i=0;i<reg_");
                        string_buffer_append(&implementation_source, instruction.copy_memory.length_register);
                        string_buffer_append(&implementation_source, ";i++)");

                        string_buffer_append(&implementation_source, "{");

                        string_buffer_append(&implementation_source, "((char*)reg_");
                        string_buffer_append(&implementation_source, instruction.copy_memory.destination_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "((char*)reg_");
                        string_buffer_append(&implementation_source, instruction.copy_memory.source_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, ";");

                        string_buffer_append(&implementation_source, "}");
                    } break;

                    default: {
                        abort();
                    } break;
                }

                string_buffer_append(&implementation_source, "\n");
            }

            string_buffer_append(&implementation_source, "}\n");
        }
    }

    char *source{};

    string_buffer_append(&source, forward_declaration_source);
    string_buffer_append(&source, implementation_source);

    if(source == nullptr) {
        return false;
    }

    char *source_file_path_buffer{};

    string_buffer_append(&source_file_path_buffer, output_directory);
    string_buffer_append(&source_file_path_buffer, output_name);
    string_buffer_append(&source_file_path_buffer, ".c");

    auto source_file = fopen(source_file_path_buffer, "w");

    if(source_file == nullptr) {
        fprintf(stderr, "Unable to create C output file\n");

        return false;
    }

    fprintf(source_file, "%s", source);

    fclose(source_file);

    char *command_buffer{};

    auto triple = get_llvm_triple(architecture, os);

    string_buffer_append(&command_buffer, "clang -std=gnu99 -ffreestanding -w -nostdinc -c -target ");

    string_buffer_append(&command_buffer, triple);

    if(strcmp(config, "debug") == 0) {
        string_buffer_append(&command_buffer, " -g");
    } else if(strcmp(config, "release") == 0) {
        string_buffer_append(&command_buffer, " -O2");
    } else {
        abort();
    }

    string_buffer_append(&command_buffer, " -o ");
    string_buffer_append(&command_buffer, output_directory);
    string_buffer_append(&command_buffer, output_name);
    string_buffer_append(&command_buffer, ".o ");

    string_buffer_append(&command_buffer, source_file_path_buffer);

    if(system(command_buffer) != 0) {
        return false;
    }

    return true;
}