#include "c_backend.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
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
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *output_directory,
    const char *output_name
) {
    char *forward_declaration_source{};
    char *implementation_source{};

    auto register_sizes = get_register_sizes(architecture);

    for(auto runtime_static : statics) {
        if(auto function = dynamic_cast<Function*>(runtime_static)) {
            generate_function_signature(&forward_declaration_source, *function);
            string_buffer_append(&forward_declaration_source, ";\n");

            if(!function->is_external) {
                string_buffer_append(&implementation_source, "#line ");
                string_buffer_append(&implementation_source, function->line);
                string_buffer_append(&implementation_source, " \"");
                for(size_t i = 0; i < strlen(function->file); i += 1) {
                    auto character = function->file[i];

                    if(character == '\\') {
                        string_buffer_append(&implementation_source, "\\\\");
                    } else {
                        string_buffer_append_character(&implementation_source, character);
                    }
                }
                string_buffer_append(&implementation_source, "\"\n");

                auto last_line = function->line;

                generate_function_signature(&implementation_source, *function);

                string_buffer_append(&implementation_source, "{\n");

                for(size_t i = 0 ; i < function->instructions.count; i += 1) {
                    auto instruction = function->instructions[i];

                    if(instruction->line > last_line) {
                        string_buffer_append(&implementation_source, "#line ");
                        string_buffer_append(&implementation_source, instruction->line);
                        string_buffer_append(&implementation_source, "\n");

                        last_line = instruction->line;
                    }

                    string_buffer_append(&implementation_source, function->name);
                    string_buffer_append(&implementation_source, "_");
                    string_buffer_append(&implementation_source, i);
                    string_buffer_append(&implementation_source, ":;");

                    if(auto arithmetic_operation = dynamic_cast<ArithmeticOperation*>(instruction)) {
                        generate_type(&implementation_source, arithmetic_operation->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, arithmetic_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
                        bool is_signed;

                        switch(arithmetic_operation->operation) {
                            case ArithmeticOperation::Operation::Add: {
                                operator_ = "+";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::Subtract: {
                                operator_ = "-";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::SignedMultiply: {
                                operator_ = "*";
                                is_signed = true;
                            } break;

                            case ArithmeticOperation::Operation::UnsignedMultiply: {
                                operator_ = "*";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::SignedDivide: {
                                operator_ = "/";
                                is_signed = true;
                            } break;

                            case ArithmeticOperation::Operation::UnsignedDivide: {
                                operator_ = "/";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::SignedModulus: {
                                operator_ = "%";
                                is_signed = true;
                            } break;

                            case ArithmeticOperation::Operation::UnsignedModulus: {
                                operator_ = "%";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::BitwiseAnd: {
                                operator_ = "&";
                                is_signed = false;
                            } break;

                            case ArithmeticOperation::Operation::BitwiseOr: {
                                operator_ = "|";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, arithmetic_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, arithmetic_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, arithmetic_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, arithmetic_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto comparison_operation = dynamic_cast<ComparisonOperation*>(instruction)) {
                        generate_type(&implementation_source, register_sizes.default_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, comparison_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
                        bool is_signed;

                        switch(comparison_operation->operation) {
                            case ComparisonOperation::Operation::Equal: {
                                operator_ = "==";
                                is_signed = false;
                            } break;

                            case ComparisonOperation::Operation::SignedLessThan: {
                                operator_ = "<";
                                is_signed = true;
                            } break;

                            case ComparisonOperation::Operation::UnsignedLessThan: {
                                operator_ = "<";
                                is_signed = false;
                            } break;

                            case ComparisonOperation::Operation::SignedGreaterThan: {
                                operator_ = ">";
                                is_signed = true;
                            } break;

                            case ComparisonOperation::Operation::UnsignedGreaterThan: {
                                operator_ = ">";
                                is_signed = false;
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, comparison_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, comparison_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, comparison_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, comparison_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto integer_upcast = dynamic_cast<IntegerUpcast*>(instruction)) {
                        generate_type(&implementation_source, integer_upcast->destination_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, integer_upcast->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, integer_upcast->destination_size, integer_upcast->is_signed);
                        string_buffer_append(&implementation_source, ")(");
                        generate_type(&implementation_source, integer_upcast->source_size, integer_upcast->is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_upcast->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto constant = dynamic_cast<Constant*>(instruction)) {
                        generate_type(&implementation_source, constant->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, constant->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, constant->value);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto jump = dynamic_cast<Jump*>(instruction)) {
                        string_buffer_append(&implementation_source, "goto ");
                        string_buffer_append(&implementation_source, function->name);
                        string_buffer_append(&implementation_source, "_");
                        string_buffer_append(&implementation_source, jump->destination_instruction);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto branch = dynamic_cast<Branch*>(instruction)) {
                        string_buffer_append(&implementation_source, "if(");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, register_sizes.default_size, false);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, branch->condition_register);

                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "{");

                        string_buffer_append(&implementation_source, "goto ");
                        string_buffer_append(&implementation_source, function->name);
                        string_buffer_append(&implementation_source, "_");
                        string_buffer_append(&implementation_source, branch->destination_instruction);
                        string_buffer_append(&implementation_source, ";");

                        string_buffer_append(&implementation_source, "}");
                    } else if(auto function_call = dynamic_cast<FunctionCallInstruction*>(instruction)) {
                        Function callee;
                        for(auto runtime_static : statics) {
                            if(strcmp(runtime_static->name, function_call->function_name) == 0) {
                                auto function = dynamic_cast<Function*>(runtime_static);
                                assert(function);

                                callee = *function;

                                break;
                            }
                        }

                        if(function_call->has_return) {
                            generate_type(&implementation_source, callee.return_size, false);
                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, function_call->return_register);
                            string_buffer_append(&implementation_source, "=");
                        }

                        string_buffer_append(&implementation_source, callee.name);
                        string_buffer_append(&implementation_source, "(");

                        for(size_t i = 0; i < function_call->parameter_registers.count; i += 1) {
                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, function_call->parameter_registers[i]);

                            if(i != function_call->parameter_registers.count - 1) {
                                string_buffer_append(&implementation_source, ",");
                            }
                        }

                        string_buffer_append(&implementation_source, ");");
                    } else if(auto return_instuction = dynamic_cast<ReturnInstruction*>(instruction)) {
                        string_buffer_append(&implementation_source, "return");

                        if(function->has_return) {
                            string_buffer_append(&implementation_source, "(");
                            generate_type(&implementation_source, function->return_size, false);
                            string_buffer_append(&implementation_source, ")");

                            string_buffer_append(&implementation_source, "reg_");
                            string_buffer_append(&implementation_source, return_instuction->value_register);
                        }

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto allocate_local = dynamic_cast<AllocateLocal*>(instruction)) {
                        string_buffer_append(&implementation_source, "char __attribute__((aligned(");
                        string_buffer_append(&implementation_source, allocate_local->alignment);
                        string_buffer_append(&implementation_source, "))) local_");
                        string_buffer_append(&implementation_source, allocate_local->destination_register);
                        string_buffer_append(&implementation_source, "[");
                        string_buffer_append(&implementation_source, allocate_local->size);
                        string_buffer_append(&implementation_source, "];");

                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, allocate_local->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "&local_");
                        string_buffer_append(&implementation_source, allocate_local->destination_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto load_integer = dynamic_cast<LoadInteger*>(instruction)) {
                        generate_type(&implementation_source, load_integer->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, load_integer->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "*");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, load_integer->size, false);
                        string_buffer_append(&implementation_source, "*)");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, load_integer->address_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto store_integer = dynamic_cast<StoreInteger*>(instruction)) {
                        string_buffer_append(&implementation_source, "*(");
                        generate_type(&implementation_source, store_integer->size, false);
                        string_buffer_append(&implementation_source, "*)reg_");
                        string_buffer_append(&implementation_source, store_integer->address_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_type(&implementation_source, store_integer->size, false);
                        string_buffer_append(&implementation_source, ")");
                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, store_integer->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto reference_static = dynamic_cast<ReferenceStatic*>(instruction)) {
                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, reference_static->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "&");
                        string_buffer_append(&implementation_source, reference_static->name);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto copy_memory = dynamic_cast<CopyMemory*>(instruction)) {
                        string_buffer_append(&implementation_source, "for(");
                        generate_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " i=0;i<reg_");
                        string_buffer_append(&implementation_source, copy_memory->length_register);
                        string_buffer_append(&implementation_source, ";i++)");

                        string_buffer_append(&implementation_source, "{");

                        string_buffer_append(&implementation_source, "((char*)reg_");
                        string_buffer_append(&implementation_source, copy_memory->destination_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "((char*)reg_");
                        string_buffer_append(&implementation_source, copy_memory->source_address_register);
                        string_buffer_append(&implementation_source, ")[i]");

                        string_buffer_append(&implementation_source, ";");

                        string_buffer_append(&implementation_source, "}");
                    } else {
                        abort();
                    }
                }

                string_buffer_append(&implementation_source, "}\n");
            }
        } else if(auto constant = dynamic_cast<StaticConstant*>(runtime_static)) {
            generate_type(&forward_declaration_source, RegisterSize::Size8, false);
            string_buffer_append(&forward_declaration_source, " __attribute__((aligned(");
            string_buffer_append(&forward_declaration_source, constant->alignment);
            string_buffer_append(&forward_declaration_source, ")))");
            string_buffer_append(&forward_declaration_source, constant->name);
            string_buffer_append(&forward_declaration_source, "[]");

            string_buffer_append(&forward_declaration_source, "=");

            string_buffer_append(&forward_declaration_source, "{");

            for(size_t i = 0; i < constant->data.count; i += 1) {
                string_buffer_append(&forward_declaration_source, constant->data.elements[i]);

                if(i != constant->data.count - 1) {
                    string_buffer_append(&forward_declaration_source, ",");
                }
            }

            string_buffer_append(&forward_declaration_source, "};\n");
        } else {
            abort();
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