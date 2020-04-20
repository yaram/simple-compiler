#include "c_backend.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "platform.h"
#include "path.h"
#include "util.h"

static void generate_integer_type(char **source, RegisterSize size, bool is_signed) {
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

static void generate_float_type(char **source, RegisterSize size) {
    switch(size) {
        case RegisterSize::Size32: {
            string_buffer_append(source, "float");
        } break;

        case RegisterSize::Size64: {
            string_buffer_append(source, "double");
        } break;

        default: {
            abort();
        } break;
    }
}

static bool generate_function_signature(char **source, Function function) {
    if(function.has_return) {
        if(function.is_return_float) {
            generate_float_type(source, function.return_size);
        } else {
            generate_integer_type(source, function.return_size, false);
        }
    } else {
        string_buffer_append(source, "void");
    }

    string_buffer_append(source, " ");

    string_buffer_append(source, function.name);

    string_buffer_append(source, "(");
    
    for(size_t i = 0; i < function.parameters.count; i += 1) {
        auto parameter = function.parameters[i];

        if(parameter.is_float) {
            generate_float_type(source, parameter.size);
        } else {
            generate_integer_type(source, parameter.size, false);
        }

        string_buffer_append(source, " reg_");
        string_buffer_append(source, i);

        if(i != function.parameters.count - 1) {
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

                generate_function_signature(&implementation_source, *function);

                string_buffer_append(&implementation_source, "{\n");

                for(size_t i = 0 ; i < function->instructions.count; i += 1) {
                    auto instruction = function->instructions[i];

                    string_buffer_append(&implementation_source, "#line ");
                    string_buffer_append(&implementation_source, instruction->line);
                    string_buffer_append(&implementation_source, "\n");

                    string_buffer_append(&implementation_source, function->name);
                    string_buffer_append(&implementation_source, "_");
                    string_buffer_append(&implementation_source, i);
                    string_buffer_append(&implementation_source, ":;");

                    if(auto integer_arithmetic_operation = dynamic_cast<IntegerArithmeticOperation*>(instruction)) {
                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, integer_arithmetic_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
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

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_arithmetic_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_arithmetic_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_arithmetic_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto integer_comparison_operation = dynamic_cast<IntegerComparisonOperation*>(instruction)) {
                        generate_integer_type(&implementation_source, register_sizes.default_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, integer_comparison_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;
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

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_comparison_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_comparison_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_comparison_operation->size, is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_comparison_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto integer_upcast = dynamic_cast<IntegerUpcast*>(instruction)) {
                        generate_integer_type(&implementation_source, integer_upcast->destination_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, integer_upcast->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, integer_upcast->destination_size, integer_upcast->is_signed);
                        string_buffer_append(&implementation_source, ")(");
                        generate_integer_type(&implementation_source, integer_upcast->source_size, integer_upcast->is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, integer_upcast->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto integer_constant = dynamic_cast<IntegerConstantInstruction*>(instruction)) {
                        generate_integer_type(&implementation_source, integer_constant->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, integer_constant->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, integer_constant->value);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_arithmetic_operation = dynamic_cast<FloatArithmeticOperation*>(instruction)) {
                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_arithmetic_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;

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

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_arithmetic_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_arithmetic_operation->size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_arithmetic_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_comparison_operation = dynamic_cast<FloatComparisonOperation*>(instruction)) {
                        generate_float_type(&implementation_source, register_sizes.default_size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_comparison_operation->destination_register);

                        string_buffer_append(&implementation_source, "=");

                        char *operator_;

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

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_comparison_operation->size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_comparison_operation->source_register_a);

                        string_buffer_append(&implementation_source, operator_);

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_comparison_operation->size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_comparison_operation->source_register_b);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_conversion = dynamic_cast<FloatConversion*>(instruction)) {
                        generate_float_type(&implementation_source, float_conversion->destination_size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_conversion->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_conversion->destination_size);
                        string_buffer_append(&implementation_source, ")(");
                        generate_float_type(&implementation_source, float_conversion->source_size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_conversion->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_truncation = dynamic_cast<FloatTruncation*>(instruction)) {
                        generate_integer_type(&implementation_source, float_truncation->destination_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_truncation->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, float_truncation->destination_size, false);
                        string_buffer_append(&implementation_source, ")(");
                        generate_float_type(&implementation_source, float_truncation->source_size);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_truncation->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_from_integer = dynamic_cast<FloatFromInteger*>(instruction)) {
                        generate_float_type(&implementation_source, float_from_integer->destination_size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_from_integer->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, float_from_integer->destination_size);
                        string_buffer_append(&implementation_source, ")(");
                        generate_integer_type(&implementation_source, float_from_integer->source_size, float_from_integer->is_signed);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, float_from_integer->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto float_constant = dynamic_cast<FloatConstantInstruction*>(instruction)) {
                        generate_float_type(&implementation_source, float_constant->size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, float_constant->destination_register);
                        string_buffer_append(&implementation_source, "=");

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

                        string_buffer_append(&implementation_source, buffer);

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
                        generate_integer_type(&implementation_source, register_sizes.default_size, false);
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
                        if(function_call->has_return) {
                            if(function_call->is_return_float) {
                                generate_float_type(&implementation_source, function_call->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->return_size, false);
                            }

                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, function_call->return_register);
                            string_buffer_append(&implementation_source, "=");
                        }

                        string_buffer_append(&implementation_source, "(");

                        string_buffer_append(&implementation_source, "(");
                        if(function_call->has_return) {
                            if(function_call->is_return_float) {
                                generate_float_type(&implementation_source, function_call->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->return_size, false);
                            }
                        } else {
                            string_buffer_append(&implementation_source, "void");
                        }
                        string_buffer_append(&implementation_source, "(*)");
                        string_buffer_append(&implementation_source, "(");
                        for(size_t i = 0; i < function_call->parameters.count; i += 1) {
                            if(function_call->parameters[i].is_float) {
                                generate_float_type(&implementation_source, function_call->parameters[i].size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->parameters[i].size, false);
                            }

                            if(i != function_call->parameters.count - 1) {
                                string_buffer_append(&implementation_source, ",");
                            }
                        }
                        string_buffer_append(&implementation_source, ")");
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, function_call->address_register);

                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "(");

                        for(size_t i = 0; i < function_call->parameters.count; i += 1) {
                            string_buffer_append(&implementation_source, "(");
                            if(function_call->parameters[i].is_float) {
                                generate_float_type(&implementation_source, function_call->parameters[i].size);
                            } else {
                                generate_integer_type(&implementation_source, function_call->parameters[i].size, false);
                            }
                            string_buffer_append(&implementation_source, ")");

                            string_buffer_append(&implementation_source, " reg_");
                            string_buffer_append(&implementation_source, function_call->parameters[i].register_index);

                            if(i != function_call->parameters.count - 1) {
                                string_buffer_append(&implementation_source, ",");
                            }
                        }

                        string_buffer_append(&implementation_source, ");");
                    } else if(auto return_instuction = dynamic_cast<ReturnInstruction*>(instruction)) {
                        string_buffer_append(&implementation_source, "return");

                        if(function->has_return) {
                            string_buffer_append(&implementation_source, "(");
                            if(function->is_return_float) {
                                generate_float_type(&implementation_source, function->return_size);
                            } else {
                                generate_integer_type(&implementation_source, function->return_size, false);
                            }
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

                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, allocate_local->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, ")");

                        string_buffer_append(&implementation_source, "&local_");
                        string_buffer_append(&implementation_source, allocate_local->destination_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto load_integer = dynamic_cast<LoadInteger*>(instruction)) {
                        generate_integer_type(&implementation_source, load_integer->size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, load_integer->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "*");

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, load_integer->size, false);
                        string_buffer_append(&implementation_source, "*)");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, load_integer->address_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto store_integer = dynamic_cast<StoreInteger*>(instruction)) {
                        string_buffer_append(&implementation_source, "*(");
                        generate_integer_type(&implementation_source, store_integer->size, false);
                        string_buffer_append(&implementation_source, "*)reg_");
                        string_buffer_append(&implementation_source, store_integer->address_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_integer_type(&implementation_source, store_integer->size, false);
                        string_buffer_append(&implementation_source, ")");
                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, store_integer->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto load_float = dynamic_cast<LoadFloat*>(instruction)) {
                        generate_float_type(&implementation_source, load_float->size);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, load_float->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "*");

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, load_float->size);
                        string_buffer_append(&implementation_source, "*)");

                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, load_float->address_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto store_float = dynamic_cast<StoreFloat*>(instruction)) {
                        string_buffer_append(&implementation_source, "*(");
                        generate_float_type(&implementation_source, store_float->size);
                        string_buffer_append(&implementation_source, "*)reg_");
                        string_buffer_append(&implementation_source, store_float->address_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "(");
                        generate_float_type(&implementation_source, store_float->size);
                        string_buffer_append(&implementation_source, ")");
                        string_buffer_append(&implementation_source, "reg_");
                        string_buffer_append(&implementation_source, store_float->source_register);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto reference_static = dynamic_cast<ReferenceStatic*>(instruction)) {
                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
                        string_buffer_append(&implementation_source, " reg_");
                        string_buffer_append(&implementation_source, reference_static->destination_register);
                        string_buffer_append(&implementation_source, "=");

                        string_buffer_append(&implementation_source, "&");
                        string_buffer_append(&implementation_source, reference_static->name);

                        string_buffer_append(&implementation_source, ";");
                    } else if(auto copy_memory = dynamic_cast<CopyMemory*>(instruction)) {
                        string_buffer_append(&implementation_source, "for(");
                        generate_integer_type(&implementation_source, register_sizes.address_size, false);
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

                    string_buffer_append(&implementation_source, "\n");
                }

                string_buffer_append(&implementation_source, "}\n");
            }
        } else if(auto constant = dynamic_cast<StaticConstant*>(runtime_static)) {
            string_buffer_append(&forward_declaration_source, "const ");
            generate_integer_type(&forward_declaration_source, RegisterSize::Size8, false);
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
        } else if(auto variable = dynamic_cast<StaticVariable*>(runtime_static)) {
            if(variable->is_external) {
                string_buffer_append(&forward_declaration_source, "extern ");
            }

            generate_integer_type(&forward_declaration_source, RegisterSize::Size8, false);
            string_buffer_append(&forward_declaration_source, " __attribute__((aligned(");
            string_buffer_append(&forward_declaration_source, variable->alignment);
            string_buffer_append(&forward_declaration_source, ")))");
            string_buffer_append(&forward_declaration_source, variable->name);

            string_buffer_append(&forward_declaration_source, ";\n");
        } else {
            abort();
        }
    }

    char *source{};

    string_buffer_append(&source, forward_declaration_source);
    string_buffer_append(&source, implementation_source);

    if(source == nullptr) {
        string_buffer_append(&source, "");
    }

    if(strcmp(os, "windows") == 0) {
        string_buffer_append(&source, "int _fltused;");
        string_buffer_append(&source, "int __fltused;");
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