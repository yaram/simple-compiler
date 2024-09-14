#include "hl_llvm_backend.h"
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "register_size.h"
#include "types.h"
#include "util.h"
#include "list.h"
#include "platform.h"
#include "profiler.h"
#include "path.h"

static LLVMTypeRef get_llvm_type(ArchitectureSizes architecture_sizes, IRType type);

inline LLVMTypeRef get_llvm_integer_type(RegisterSize size) {
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

inline LLVMTypeRef get_llvm_float_type(RegisterSize size) {
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

inline LLVMTypeRef get_llvm_pointer_type(ArchitectureSizes architecture_sizes, IRType pointed_to_type) {
    if(pointed_to_type.kind == IRTypeKind::Void) {
        return LLVMPointerTypeInContext(LLVMGetGlobalContext(), 0);
    } else {
        return LLVMPointerType(get_llvm_type(architecture_sizes, pointed_to_type), 0);
    }
}

static LLVMTypeRef get_llvm_type(ArchitectureSizes architecture_sizes, IRType type) {
    if(type.kind == IRTypeKind::Function) {
        auto function = type.function;

        auto parameters = allocate<LLVMTypeRef>(function.parameters.length);

        for(size_t i = 0; i < function.parameters.length; i += 1) {
            parameters[i] = get_llvm_type(architecture_sizes, function.parameters[i]);
        }

        auto return_llvm_type = get_llvm_type(architecture_sizes, type);

        return LLVMFunctionType(return_llvm_type, parameters, (unsigned int)function.parameters.length, false);
    } else if(type.kind == IRTypeKind::Boolean) {
        return get_llvm_integer_type(architecture_sizes.boolean_size);
    } else if(type.kind == IRTypeKind::Integer) {
        return get_llvm_integer_type(type.integer.size);
    } else if(type.kind == IRTypeKind::Float) {
        return get_llvm_float_type(type.float_.size);
    } else if(type.kind == IRTypeKind::Pointer) {
        return get_llvm_pointer_type(architecture_sizes, *type.pointer);
    } else if(type.kind == IRTypeKind::StaticArray) {
        auto static_array = type.static_array;

        auto element_llvm_type = get_llvm_type(architecture_sizes, *static_array.element_type);

        return LLVMArrayType2(element_llvm_type, static_array.length);
    } else if(type.kind == IRTypeKind::Struct) {
        auto struct_ = type.struct_;

        auto members = allocate<LLVMTypeRef>(struct_.members.length);

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            members[i] = get_llvm_type(architecture_sizes, struct_.members[i]);
        }

        return LLVMStructType(members, struct_.members.length, false);
    } else if(type.kind == IRTypeKind::Void) {
        return LLVMVoidType();
    } else {
        abort();
    }
}



struct FileDebugScope {
    String path;
    LLVMMetadataRef scope;
};

static LLVMMetadataRef get_file_debug_scope(LLVMDIBuilderRef debug_builder, List<FileDebugScope>* file_debug_scopes, String path) {
    auto found = false;
    LLVMMetadataRef result;
    for(auto entry : *file_debug_scopes) {
        if(entry.path == path) {
            result = entry.scope;

            found = true;
            break;
        }
    }

    if(!found) {
        auto directory = path_get_directory_component(path);

        result = LLVMDIBuilderCreateFile(
            debug_builder,
            path.elements,
            path.length,
            directory.elements,
            directory.length
        );

        FileDebugScope entry {};
        entry.path = path;
        entry.scope = result;

        file_debug_scopes->append(entry);
    }

    return result;
}

const LLVMDWARFTypeEncoding DW_ATE_address = 0x01;
const LLVMDWARFTypeEncoding DW_ATE_boolean = 0x02;
const LLVMDWARFTypeEncoding DW_ATE_complex_float = 0x03;
const LLVMDWARFTypeEncoding DW_ATE_float = 0x04;
const LLVMDWARFTypeEncoding DW_ATE_signed = 0x05;
const LLVMDWARFTypeEncoding DW_ATE_signed_char = 0x06;
const LLVMDWARFTypeEncoding DW_ATE_unsigned = 0x07;
const LLVMDWARFTypeEncoding DW_ATE_unsigned_char = 0x08;
const LLVMDWARFTypeEncoding DW_ATE_imaginary_float = 0x09;
const LLVMDWARFTypeEncoding DW_ATE_packed_decimal = 0x0A;
const LLVMDWARFTypeEncoding DW_ATE_numeric_string = 0x0B;
const LLVMDWARFTypeEncoding DW_ATE_edited = 0x0C;
const LLVMDWARFTypeEncoding DW_ATE_signed_fixed = 0x0D;
const LLVMDWARFTypeEncoding DW_ATE_unsigned_fixed = 0x0E;
const LLVMDWARFTypeEncoding DW_ATE_decimal_float = 0x0F;
const LLVMDWARFTypeEncoding DW_ATE_UTF = 0x10;
const LLVMDWARFTypeEncoding DW_ATE_lo_user = 0x80;
const LLVMDWARFTypeEncoding DW_ATE_hi_user = 0xFF;

static LLVMMetadataRef get_llvm_debug_type(
    LLVMDIBuilderRef debug_builder,
    List<FileDebugScope>* file_debug_scopes,
    LLVMMetadataRef file_scope,
    ArchitectureSizes architecture_sizes,
    AnyType type
) {
    if(type.kind == TypeKind::FunctionTypeType) {
        auto function = type.function;

        auto parameters = allocate<LLVMMetadataRef>(function.parameters.length);

        for(size_t i = 0; i < function.parameters.length; i += 1) {
            parameters[i] = get_llvm_debug_type(debug_builder, file_debug_scopes, file_scope, architecture_sizes, function.parameters[i]);
        }

        auto return_llvm_debug_type = get_llvm_debug_type(
            debug_builder,
            file_debug_scopes,
            file_scope,
            architecture_sizes,
            *function.return_type
        );

        return LLVMDIBuilderCreateSubroutineType(
            debug_builder,
            file_scope,
            parameters,
            function.parameters.length,
            LLVMDIFlagZero
        );
    } else if(type.kind == TypeKind::Boolean) {
        auto name = "bool"_S;
        return LLVMDIBuilderCreateBasicType(debug_builder, name.elements, name.length, 8, DW_ATE_boolean, LLVMDIFlagZero);
    } else if(type.kind == TypeKind::Integer) {
        auto name = type.get_description();

        LLVMDWARFTypeEncoding encoding;
        if(type.integer.is_signed) {
            encoding = DW_ATE_signed;
        } else {
            encoding = DW_ATE_unsigned;
        }

        return LLVMDIBuilderCreateBasicType(debug_builder, name.elements, name.length, 8, encoding, LLVMDIFlagZero);
    } else if(type.kind == TypeKind::FloatType) {
        auto name = type.get_description();

        return LLVMDIBuilderCreateBasicType(debug_builder, name.elements, name.length, 8, DW_ATE_float, LLVMDIFlagZero);
    } else if(type.kind == TypeKind::Pointer) {
        auto name = type.get_description();

        auto pointed_to_llvm_debug_type = get_llvm_debug_type(
            debug_builder,
            file_debug_scopes,
            file_scope,
            architecture_sizes,
            *type.pointer.pointed_to_type
        );

        auto size = register_size_to_byte_size(architecture_sizes.address_size);

        return LLVMDIBuilderCreatePointerType(
            debug_builder,
            pointed_to_llvm_debug_type,
            size * 8,
            0,
            0,
            name.elements,
            name.length
        );
    } else if(type.kind == TypeKind::ArrayTypeType) {
        auto array = type.array;

        auto name = type.get_description();
        auto size = type.get_size(architecture_sizes);
        auto alignment = type.get_alignment(architecture_sizes);

        auto length_debug_type = get_llvm_debug_type(
            debug_builder,
            file_debug_scopes,
            file_scope,
            architecture_sizes,
            AnyType(Integer(architecture_sizes.address_size, false))
        );

        auto pointer_debug_type = get_llvm_debug_type(
            debug_builder,
            file_debug_scopes,
            file_scope,
            architecture_sizes,
            AnyType(Pointer(array.element_type))
        );

        auto address_size_bits = register_size_to_byte_size(architecture_sizes.address_size) * 8;

        LLVMMetadataRef elements[2] {};

        auto length_name = "length"_S;
        elements[0] = LLVMDIBuilderCreateMemberType(
            debug_builder,
            file_scope,
            length_name.elements,
            length_name.length,
            file_scope,
            0,
            address_size_bits,
            address_size_bits,
            0,
            LLVMDIFlagZero,
            length_debug_type
        );

        auto pointer_name = "length"_S;
        elements[1] = LLVMDIBuilderCreateMemberType(
            debug_builder,
            file_scope,
            pointer_name.elements,
            pointer_name.length,
            file_scope,
            0,
            address_size_bits,
            address_size_bits,
            address_size_bits,
            LLVMDIFlagZero,
            pointer_debug_type
        );

        return LLVMDIBuilderCreateStructType(
            debug_builder,
            file_scope,
            name.elements,
            name.length,
            file_scope,
            0,
            size * 8,
            alignment * 8,
            LLVMDIFlagZero,
            nullptr,
            elements,
            2,
            0,
            nullptr,
            nullptr,
            0
        );
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        auto element_llvm_debug_type = get_llvm_debug_type(
            debug_builder,
            file_debug_scopes,
            file_scope,
            architecture_sizes,
            *static_array.element_type
        );
        auto element_type_size = static_array.element_type->get_size(architecture_sizes);
        auto element_type_align = static_array.element_type->get_alignment(architecture_sizes);

        auto subscript = LLVMDIBuilderGetOrCreateSubrange(debug_builder, 0, static_array.length);

        return LLVMDIBuilderCreateArrayType(
            debug_builder,
            element_type_size * static_array.length * 8,
            element_type_align * 8,
            element_llvm_debug_type,
            &subscript,
            1
        );
    } else if(type.kind == TypeKind::StructType) {
        auto struct_ = type.struct_;

        auto struct_file_scope = get_file_debug_scope(
            debug_builder,
            file_debug_scopes,
            struct_.definition_file_path
        );

        auto size = struct_.get_size(architecture_sizes);
        auto alignment = struct_.get_alignment(architecture_sizes);

        auto elements = allocate<LLVMMetadataRef>(struct_.members.length);

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            auto member_debug_type = get_llvm_debug_type(
                debug_builder,
                file_debug_scopes,
                file_scope,
                architecture_sizes,
                struct_.members[i].type
            );

            auto member_size = struct_.members[i].type.get_size(architecture_sizes);
            auto member_alignment = struct_.members[i].type.get_alignment(architecture_sizes);
            auto member_offset = struct_.get_member_offset(architecture_sizes, i);

            elements[i] = LLVMDIBuilderCreateMemberType(
                debug_builder,
                struct_file_scope,
                struct_.members[i].name.elements,
                struct_.members[i].name.length,
                struct_file_scope,
                struct_.definition->range.first_line,
                member_size * 8,
                member_alignment * 8,
                member_offset * 8,
                LLVMDIFlagZero,
                member_debug_type
            );
        }

        return LLVMDIBuilderCreateStructType(
            debug_builder,
            struct_file_scope,
            struct_.definition->name.text.elements,
            struct_.definition->name.text.length,
            struct_file_scope,
            struct_.definition->range.first_line,
            size * 8,
            alignment * 8,
            LLVMDIFlagZero,
            nullptr,
            elements,
            struct_.members.length,
            0,
            nullptr,
            nullptr,
            0
        );
    } else if(type.kind == TypeKind::UnionType) {
        auto union_ = type.union_;

        auto union_file_scope = get_file_debug_scope(
            debug_builder,
            file_debug_scopes,
            union_.definition_file_path
        );

        auto size = union_.get_size(architecture_sizes);
        auto alignment = union_.get_alignment(architecture_sizes);

        auto elements = allocate<LLVMMetadataRef>(union_.members.length);

        for(size_t i = 0; i < union_.members.length; i += 1) {
            auto member_debug_type = get_llvm_debug_type(
                debug_builder,
                file_debug_scopes,
                union_file_scope,
                architecture_sizes,
                union_.members[i].type
            );

            auto member_size = union_.members[i].type.get_size(architecture_sizes);
            auto member_alignment = union_.members[i].type.get_alignment(architecture_sizes);

            elements[i] = LLVMDIBuilderCreateMemberType(
                debug_builder,
                union_file_scope,
                union_.members[i].name.elements,
                union_.members[i].name.length,
                union_file_scope,
                union_.definition->range.first_line,
                member_size * 8,
                member_alignment * 8,
                0,
                LLVMDIFlagZero,
                member_debug_type
            );
        }

        return LLVMDIBuilderCreateUnionType(
            debug_builder,
            union_file_scope,
            union_.definition->name.text.elements,
            union_.definition->name.text.length,
            union_file_scope,
            union_.definition->range.first_line,
            size * 8,
            alignment * 8,
            LLVMDIFlagZero,
            elements,
            union_.members.length,
            0,
            nullptr,
            0
        );
    } else if(type.kind == TypeKind::Enum) {
        auto enum_ = type.enum_;

        auto enum_file_scope = get_file_debug_scope(
            debug_builder,
            file_debug_scopes,
            enum_.definition_file_path
        );

        auto size = register_size_to_byte_size(enum_.backing_type->size);

        auto elements = allocate<LLVMMetadataRef>(enum_.variant_values.length);

        for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
            elements[i] = LLVMDIBuilderCreateEnumerator(
                debug_builder,
                enum_.definition->variants[i].name.text.elements,
                enum_.definition->variants[i].name.text.length,
                (int64_t)enum_.variant_values[i],
                !enum_.backing_type->is_signed
            );
        }

        return LLVMDIBuilderCreateEnumerationType(
            debug_builder,
            enum_file_scope,
            enum_.definition->name.text.elements,
            enum_.definition->name.text.length,
            enum_file_scope,
            enum_.definition->range.first_line,
            size * 8,
            size * 8,
            elements,
            enum_.variant_values.length,
            nullptr
        );
    } else if(type.kind == TypeKind::Void) {
        auto name = "void"_S;
        return LLVMDIBuilderCreateUnspecifiedType(debug_builder, name.elements, name.length);
    } else {
        abort();
    }
}

struct GetLLVMConstantResult {
    LLVMTypeRef llvm_type;
    LLVMValueRef value;
};

static GetLLVMConstantResult get_llvm_constant(ArchitectureSizes architecture_sizes, IRType type, IRConstantValue value) {
    LLVMTypeRef result_type;
    LLVMValueRef result_value;
    if(type.kind == IRTypeKind::Boolean) {
        assert(value.kind == IRConstantValueKind::BooleanConstant);

        result_type = get_llvm_integer_type(architecture_sizes.boolean_size);

        result_value = LLVMConstInt(result_type, value.boolean, false);
    } else if(type.kind == IRTypeKind::Integer) {
        assert(value.kind == IRConstantValueKind::IntegerConstant);

        result_type = get_llvm_integer_type(type.integer.size);

        result_value = LLVMConstInt(result_type, value.integer, false);
    } else if(type.kind == IRTypeKind::Float) {
        assert(value.kind == IRConstantValueKind::FloatConstant);

        result_type = get_llvm_float_type(type.float_.size);

        result_value = LLVMConstReal(result_type, value.float_);
    } else if(type.kind == IRTypeKind::Pointer) {
        assert(value.kind == IRConstantValueKind::IntegerConstant);

        auto pointed_to_llvm_type = get_llvm_type(architecture_sizes, *type.pointer);

        result_type = get_llvm_pointer_type(architecture_sizes, *type.pointer);

        auto integer_llvm_type = get_llvm_integer_type(architecture_sizes.address_size);

        auto integer_constant = LLVMConstInt(integer_llvm_type, value.integer, false);

        result_value = LLVMConstIntToPtr(integer_constant, result_type);
    } else if(type.kind == IRTypeKind::StaticArray) {
        assert(value.kind == IRConstantValueKind::StaticArrayConstant);

        auto static_array = type.static_array;

        assert(static_array.length == value.static_array.elements.length);

        auto element_llvm_type = get_llvm_type(architecture_sizes, *static_array.element_type);

        result_type = LLVMArrayType2(element_llvm_type, static_array.length);

        auto elements = allocate<LLVMValueRef>(static_array.length);

        for(size_t i = 0; i < static_array.length; i += 1) {
            elements[i] = get_llvm_constant(architecture_sizes, *static_array.element_type, value.static_array.elements[i]).value;
        }

        result_value = LLVMConstArray2(element_llvm_type, elements, type.static_array.length);
    } else if(type.kind == IRTypeKind::Struct) {
        assert(value.kind == IRConstantValueKind::StructConstant);

        auto struct_ = type.struct_;

        assert(struct_.members.length == value.struct_.members.length);

        auto member_types = allocate<LLVMTypeRef>(struct_.members.length);
        auto member_values = allocate<LLVMValueRef>(struct_.members.length);

        for(size_t i = 0; i < struct_.members.length; i += 1) {
            auto result = get_llvm_constant(architecture_sizes, struct_.members[i], value.struct_.members[i]);

            member_types[i] = result.llvm_type;
            member_values[i] = result.value;
        }

        result_type = LLVMStructType(member_types, struct_.members.length, false);
        result_value = LLVMConstStruct(member_values, struct_.members.length, false);
    } else {
        abort();
    }

    GetLLVMConstantResult result {};
    result.llvm_type = result_type;
    result.value = result_value;

    return result;
}

static Result<LLVMCallConv> get_llvm_calling_convention(
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

struct TypedValue {
    inline TypedValue() = default;
    explicit inline TypedValue(IRType type, LLVMValueRef value) : type(type), value(value) {}

    IRType type;

    LLVMValueRef value;
};

struct Register {
    inline Register() = default;
    explicit inline Register(size_t index, TypedValue value) : index(index), value(value) {}

    size_t index;

    TypedValue value;
};

static TypedValue get_register_value(Function function, LLVMValueRef function_value, List<Register> registers, size_t register_index) {
    if(register_index < function.parameters.length) {
        auto parameter_value = LLVMGetParam(function_value, (unsigned int)register_index);
        assert(parameter_value);

        return TypedValue(function.parameters[register_index], parameter_value);
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

#define llvm_instruction(variable_name, call) auto variable_name=(call);if(LLVMIsAInstruction(variable_name))LLVMInstructionSetDebugLoc(variable_name, debug_location)
#define llvm_instruction_ignore(call) { auto value=(call);if(LLVMIsAInstruction(value))LLVMInstructionSetDebugLoc(value, debug_location); }

profiled_function(Result<Array<NameMapping>>, generate_llvm_object, (
    String top_level_source_file_path,
    Array<RuntimeStatic*> statics,
    String architecture,
    String os,
    String toolchain,
    String config,
    String object_file_path,
    Array<String> reserved_names
), (
    top_level_source_file_path,
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

    auto debug_builder = LLVMCreateDIBuilder(module);

    List<FileDebugScope> file_debug_scopes {};

    auto top_level_source_file_directory = path_get_directory_component(top_level_source_file_path);
    auto top_level_file_debug_scope = LLVMDIBuilderCreateFile(
        debug_builder,
        top_level_source_file_path.elements,
        top_level_source_file_path.length,
        top_level_source_file_directory.elements,
        top_level_source_file_directory.length
    );

    {
        FileDebugScope entry {};
        entry.path = top_level_source_file_path;
        entry.scope = top_level_file_debug_scope;

        file_debug_scopes.append(entry);
    }

    auto producer_name = "simple-compiler"_S;
    auto debug_compile_unit = LLVMDIBuilderCreateCompileUnit(
        debug_builder,
        LLVMDWARFSourceLanguageC_plus_plus,
        top_level_file_debug_scope,
        producer_name.elements,
        producer_name.length,
        false,
        nullptr,
        0,
        0,
        nullptr, 
        0,
        LLVMDWARFEmissionFull,
        0,
        false,
        false,
        nullptr,
        0,
        nullptr,
        0
    );

    auto global_values = allocate<TypedValue>(statics.length);

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

        TypedValue global_value;
        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto parameter_count = function->parameters.length;
            auto parameter_llvm_types = allocate<LLVMTypeRef>(parameter_count);
            for(size_t i = 0; i < parameter_count; i += 1) {
                auto parameter = function->parameters[i];

                parameter_llvm_types[i] = get_llvm_type(architecture_sizes, parameter);
            }

            auto return_llvm_type = get_llvm_type(architecture_sizes, function->return_type);

            auto function_llvm_type = LLVMFunctionType(return_llvm_type, parameter_llvm_types, (unsigned int)parameter_count, false);

            global_value = TypedValue(
                IRType::create_function(function->parameters, heapify(function->return_type), function->calling_convention),
                LLVMAddFunction(module, name.to_c_string(), function_llvm_type)
            );

            if(function->is_external) {
                LLVMSetLinkage(global_value.value, LLVMLinkage::LLVMExternalLinkage);
            }

            expect(calling_convention, get_llvm_calling_convention(function->path, function->range, os, architecture, function->calling_convention));

            LLVMSetFunctionCallConv(global_value.value, calling_convention);
        } else if(runtime_static->kind == RuntimeStaticKind::StaticVariable) {
            auto variable = (StaticVariable*)runtime_static;

            auto llvm_type = get_llvm_type(architecture_sizes, variable->type);

            auto llvm_value = LLVMAddGlobal(module, llvm_type, name.to_c_string());

            global_value = TypedValue(
                variable->type,
                llvm_value
            );

            if(variable->is_external) {
                LLVMSetLinkage(global_value.value, LLVMLinkage::LLVMExternalLinkage);
            } else if(variable->has_initial_value) {
                auto initial_value_llvm = get_llvm_constant(architecture_sizes, variable->type, variable->initial_value).value;

                LLVMSetInitializer(global_value.value, initial_value_llvm);
            }

            auto file_debug_scope = get_file_debug_scope(debug_builder, &file_debug_scopes, variable->path);

            auto debug_type = get_llvm_debug_type(
                debug_builder,
                &file_debug_scopes,
                file_debug_scope,
                architecture_sizes,
                variable->debug_type
            );

            auto debug_expression = LLVMDIBuilderCreateExpression(debug_builder, nullptr, 0);

            auto debug_variable_expression = LLVMDIBuilderCreateGlobalVariableExpression(
                debug_builder,
                file_debug_scope,
                variable->name.elements,
                variable->name.length,
                name.elements,
                name.length,
                file_debug_scope,
                variable->range.first_line,
                debug_type,
                !variable->is_external,
                debug_expression,
                nullptr,
                0
            );

            auto debug_variable = LLVMDIGlobalVariableExpressionGetVariable(debug_variable_expression);

            auto debug_location = LLVMDIBuilderCreateDebugLocation(
                LLVMGetGlobalContext(),
                variable->range.first_line,
                variable->range.first_column,
                file_debug_scope,
                nullptr
            );

            LLVMDIBuilderInsertDbgValueAtEnd(
                debug_builder,
                llvm_value,
                debug_variable,
                debug_expression,
                debug_location,
                nullptr
            );
        } else {
            abort();
        }

        global_values[i] = global_value;
    }

    for(size_t i = 0; i < statics.length; i += 1) {
        auto runtime_static = statics[i];

        String link_name;
        auto found = false;
        for(auto name_mapping : name_mappings) {
            if(name_mapping.runtime_static == runtime_static) {
                link_name = name_mapping.name;
                found = true;

                break;
            }
        }
        assert(found);

        if(runtime_static->kind == RuntimeStaticKind::Function) {
            auto function = (Function*)runtime_static;

            auto function_value = global_values[i].value;

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

                auto file_debug_scope = get_file_debug_scope(debug_builder, &file_debug_scopes, function->path);

                auto function_debug_type = get_llvm_debug_type(
                    debug_builder,
                    &file_debug_scopes,
                    file_debug_scope,
                    architecture_sizes,
                    function->debug_type
                );

                auto function_debug_scope = LLVMDIBuilderCreateFunction(
                    debug_builder,
                    file_debug_scope,
                    function->name.elements,
                    function->name.length,
                    link_name.elements,
                    link_name.length,
                    file_debug_scope,
                    function->range.first_line,
                    function_debug_type,
                    true,
                    true,
                    function->range.first_line,
                    LLVMDIFlagZero,
                    false
                );

                LLVMSetSubprogram(function_value, function_debug_scope);

                for(auto instruction : function->instructions) {
                    if(instruction->kind == InstructionKind::AllocateLocal) {
                        auto allocate_local = (AllocateLocal*)instruction;

                        auto debug_location = LLVMDIBuilderCreateDebugLocation(
                            LLVMGetGlobalContext(),
                            allocate_local->range.first_line,
                            allocate_local->range.first_column,
                            function_debug_scope,
                            nullptr
                        );

                        auto llvm_type = get_llvm_type(architecture_sizes, allocate_local->type);

                        auto pointer_value = LLVMBuildAlloca(builder, llvm_type, "allocate_local");
                        if(!allocate_local->has_debug_info) {
                            LLVMInstructionSetDebugLoc(pointer_value, debug_location);
                        } else {
                            auto debug_type = get_llvm_debug_type(
                                debug_builder,
                                &file_debug_scopes,
                                file_debug_scope,
                                architecture_sizes,
                                allocate_local->debug_type
                            );

                            auto debug_variable = LLVMDIBuilderCreateAutoVariable(
                                debug_builder,
                                function_debug_scope,
                                allocate_local->debug_name.elements,
                                allocate_local->debug_name.length,
                                file_debug_scope,
                                allocate_local->range.first_line,
                                debug_type,
                                false,
                                LLVMDIFlagZero,
                                0
                            );

                            auto debug_expression = LLVMDIBuilderCreateExpression(debug_builder, nullptr, 0);

                            LLVMDIBuilderInsertDeclareAtEnd(
                                debug_builder,
                                pointer_value,
                                debug_variable,
                                debug_expression,
                                debug_location,
                                blocks[0].block
                            );
                        }

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

                    auto debug_location = LLVMDIBuilderCreateDebugLocation(
                        LLVMGetGlobalContext(),
                        instruction->range.first_line,
                        instruction->range.first_column,
                        function_debug_scope,
                        nullptr
                    );

                    auto might_need_terminator = true;
                    if(instruction->kind == InstructionKind::IntegerArithmeticOperation) {
                        auto integer_arithmetic_operation = (IntegerArithmeticOperation*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, integer_arithmetic_operation->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, integer_arithmetic_operation->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Integer);
                        assert(source_value_b.type.kind == IRTypeKind::Integer);
                        assert(source_value_a.type.integer.size == source_value_b.type.integer.size);

                        auto value_a = source_value_a.value;
                        auto value_b = source_value_b.value;

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

                            if(LLVMIsAInstruction(value)) {
                                LLVMInstructionSetDebugLoc(value, debug_location);
                            }

                            default: {
                                abort();
                            } break;
                        }

                        registers.append(Register(
                            integer_arithmetic_operation->destination_register,
                            TypedValue(source_value_a.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerComparisonOperation) {
                        auto integer_comparison_operation = (IntegerComparisonOperation*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, integer_comparison_operation->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, integer_comparison_operation->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Integer);
                        assert(source_value_b.type.kind == IRTypeKind::Integer);
                        assert(source_value_a.type.integer.size == source_value_b.type.integer.size);

                        auto value_a = source_value_a.value;
                        auto value_b = source_value_b.value;

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

                        llvm_instruction(value, LLVMBuildICmp(builder, predicate, value_a, value_b, name));

                        llvm_instruction(extended_value, LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend"));

                        registers.append(Register(
                            integer_comparison_operation->destination_register,
                            TypedValue(IRType::create_boolean(), extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerExtension) {
                        auto integer_extension = (IntegerExtension*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, integer_extension->source_register);

                        auto destination_llvm_type = get_llvm_integer_type(integer_extension->destination_size);

                        LLVMValueRef value;
                        if(integer_extension->is_signed) {
                            value = LLVMBuildSExt(builder, source_value.value, destination_llvm_type , "extend");
                        } else {
                            value = LLVMBuildZExt(builder, source_value.value, destination_llvm_type, "extend");
                        }

                        if(LLVMIsAInstruction(value)) {
                            LLVMInstructionSetDebugLoc(value, debug_location);
                        }

                        registers.append(Register(
                            integer_extension->destination_register,
                            TypedValue(source_value.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerTruncation) {
                        auto integer_truncation = (IntegerTruncation*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, integer_truncation->source_register);

                        auto destination_llvm_type = get_llvm_integer_type(integer_truncation->destination_size);

                        llvm_instruction(value, LLVMBuildTrunc(
                            builder,
                            source_value.value,
                            destination_llvm_type,
                            "truncate"
                        ));

                        registers.append(Register(
                            integer_truncation->destination_register,
                            TypedValue(source_value.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::FloatArithmeticOperation) {
                        auto float_arithmetic_operation = (FloatArithmeticOperation*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, float_arithmetic_operation->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, float_arithmetic_operation->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Float);
                        assert(source_value_b.type.kind == IRTypeKind::Float);
                        assert(source_value_a.type.float_.size == source_value_b.type.float_.size);

                        auto value_a = source_value_a.value;
                        auto value_b = source_value_b.value;

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

                        if(LLVMIsAInstruction(value)) {
                            LLVMInstructionSetDebugLoc(value, debug_location);
                        }

                        registers.append(Register(
                            float_arithmetic_operation->destination_register,
                            TypedValue(source_value_a.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::FloatComparisonOperation) {
                        auto float_comparison_operation = (FloatComparisonOperation*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, float_comparison_operation->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Float);
                        assert(source_value_b.type.kind == IRTypeKind::Float);
                        assert(source_value_a.type.float_.size == source_value_b.type.float_.size);

                        auto value_a = source_value_a.value;
                        auto value_b = source_value_b.value;

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

                        llvm_instruction(value, LLVMBuildFCmp(builder, predicate, value_a, value_b, name));

                        llvm_instruction(extended_value, LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend"));

                        registers.append(Register(
                            float_comparison_operation->destination_register,
                            TypedValue(IRType::create_boolean(), extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::FloatConversion) {
                        auto float_conversion = (FloatConversion*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_conversion->source_register);

                        auto destination_llvm_type = get_llvm_float_type(float_conversion->destination_size);

                        llvm_instruction(value, LLVMBuildFPCast(builder, source_value.value, destination_llvm_type, "float_conversion"));

                        registers.append(Register(
                            float_conversion->destination_register,
                            TypedValue(source_value.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerFromFloat) {
                        auto integer_from_float = (IntegerFromFloat*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, integer_from_float->source_register);

                        auto destination_llvm_type = get_llvm_integer_type(integer_from_float->destination_size);

                        llvm_instruction(value, LLVMBuildFPToSI(builder, source_value.value, destination_llvm_type, "integer_from_float"));

                        registers.append(Register(
                            integer_from_float->destination_register,
                            TypedValue(source_value.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::FloatFromInteger) {
                        auto float_from_integer = (FloatFromInteger*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, float_from_integer->source_register);

                        auto destination_llvm_type = get_llvm_float_type(float_from_integer->destination_size);

                        llvm_instruction(value, LLVMBuildSIToFP(builder, source_value.value, destination_llvm_type, "float_from_integer"));

                        registers.append(Register(
                            float_from_integer->destination_register,
                            TypedValue(source_value.type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::PointerEquality) {
                        auto pointer_equality = (PointerEquality*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, pointer_equality->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, pointer_equality->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Pointer);
                        assert(source_value_b.type.kind == IRTypeKind::Pointer);

                        auto value_a = source_value_a.value;
                        auto value_b = source_value_b.value;

                        auto integer_llvm_type = get_llvm_integer_type(architecture_sizes.address_size);

                        auto pointer_llvm_type = get_llvm_type(architecture_sizes, source_value_a.type);

                        llvm_instruction(integer_value_a, LLVMBuildPtrToInt(builder, value_a, integer_llvm_type, "pointer_to_int"));
                        llvm_instruction(integer_value_b, LLVMBuildPtrToInt(builder, value_b, integer_llvm_type, "pointer_to_int"));

                        llvm_instruction(value, LLVMBuildICmp(builder, LLVMIntPredicate::LLVMIntEQ, integer_value_a, integer_value_b, "pointer_equality"));

                        llvm_instruction(extended_value, LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend"));

                        registers.append(Register(
                            pointer_equality->destination_register,
                            TypedValue(IRType::create_boolean(), extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::PointerConversion) {
                        auto pointer_conversion = (PointerConversion*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, pointer_conversion->source_register);

                        auto destination_type = IRType::create_pointer(heapify(pointer_conversion->destination_pointed_to_type));

                        auto destination_llvm_type = get_llvm_pointer_type(architecture_sizes, pointer_conversion->destination_pointed_to_type);

                        llvm_instruction(result_value, LLVMBuildPointerCast(builder, source_value.value, destination_llvm_type, "pointer_conversion"));

                        registers.append(Register(
                            pointer_conversion->destination_register,
                            TypedValue(destination_type, result_value)
                        ));
                    } else if(instruction->kind == InstructionKind::PointerFromInteger) {
                        auto pointer_from_integer = (PointerFromInteger*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, pointer_from_integer->source_register);

                        auto destination_type = IRType::create_pointer(heapify(pointer_from_integer->destination_pointed_to_type));

                        auto destination_llvm_type = get_llvm_pointer_type(architecture_sizes, pointer_from_integer->destination_pointed_to_type);

                        llvm_instruction(result_value, LLVMBuildIntToPtr(builder, source_value.value, destination_llvm_type, "integer_to_pointer"));

                        registers.append(Register(
                            pointer_from_integer->destination_register,
                            TypedValue(destination_type, result_value)
                        ));
                    } else if(instruction->kind == InstructionKind::IntegerFromPointer) {
                        auto integer_from_pointer = (IntegerFromPointer*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, integer_from_pointer->source_register);

                        auto destination_type = IRType::create_integer(integer_from_pointer->destination_size);
                        auto destination_llvm_type = get_llvm_integer_type(integer_from_pointer->destination_size);

                        llvm_instruction(result_value, LLVMBuildPtrToInt(builder, source_value.value, destination_llvm_type, "pointer_to_integer"));

                        registers.append(Register(
                            integer_from_pointer->destination_register,
                            TypedValue(destination_type, result_value)
                        ));
                    } else if(instruction->kind == InstructionKind::BooleanArithmeticOperation) {
                        auto boolean_arithmetic_operation = (BooleanArithmeticOperation*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, boolean_arithmetic_operation->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, boolean_arithmetic_operation->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Boolean);
                        assert(source_value_b.type.kind == IRTypeKind::Boolean);

                        llvm_instruction(value_a, LLVMBuildTrunc(builder, source_value_a.value, LLVMInt1Type(), "truncate"));
                        llvm_instruction(value_b, LLVMBuildTrunc(builder, source_value_b.value, LLVMInt1Type(), "truncate"));

                        LLVMValueRef value;
                        switch(boolean_arithmetic_operation->operation) {
                            case BooleanArithmeticOperation::Operation::BooleanAnd: {
                                value = LLVMBuildAnd(builder, value_a, value_b, "and");
                            } break;

                            case BooleanArithmeticOperation::Operation::BooleanOr: {
                                value = LLVMBuildOr(builder, value_a, value_b, "or");
                            } break;

                            default: {
                                abort();
                            } break;
                        }

                        if(LLVMIsAInstruction(value)) {
                            LLVMInstructionSetDebugLoc(value, debug_location);
                        }

                        llvm_instruction(extended_value, LLVMBuildZExt(
                            builder,
                            value,
                            get_llvm_integer_type(architecture_sizes.boolean_size),
                            "extend"
                        ));

                        registers.append(Register(
                            boolean_arithmetic_operation->destination_register,
                            TypedValue(source_value_a.type, extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::BooleanEquality) {
                        auto boolean_equality = (BooleanEquality*)instruction;

                        auto source_value_a = get_register_value(*function, function_value, registers, boolean_equality->source_register_a);
                        auto source_value_b = get_register_value(*function, function_value, registers, boolean_equality->source_register_b);

                        assert(source_value_a.type.kind == IRTypeKind::Boolean);
                        assert(source_value_b.type.kind == IRTypeKind::Boolean);

                        llvm_instruction(value_a, LLVMBuildTrunc(builder, source_value_a.value, LLVMInt1Type(), "truncate"));
                        llvm_instruction(value_b, LLVMBuildTrunc(builder, source_value_b.value, LLVMInt1Type(), "truncate"));

                        llvm_instruction(value, LLVMBuildICmp(builder, LLVMIntPredicate::LLVMIntEQ, value_a, value_b, "pointer_equality"));

                        llvm_instruction(extended_value, LLVMBuildZExt(builder, value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend"));

                        registers.append(Register(
                            boolean_equality->destination_register,
                            TypedValue(IRType::create_boolean(), extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::BooleanInversion) {
                        auto boolean_inversion = (BooleanInversion*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, boolean_inversion->source_register);

                        llvm_instruction(value, LLVMBuildTrunc(builder, source_value.value, LLVMInt1Type(), "truncate"));

                        llvm_instruction(result_value, LLVMBuildNot(builder, value, "boolean_inversion"));

                        llvm_instruction(extended_value, LLVMBuildZExt(builder, result_value, get_llvm_integer_type(architecture_sizes.boolean_size), "extend"));

                        registers.append(Register(
                            boolean_inversion->destination_register,
                            TypedValue(IRType::create_boolean(), extended_value)
                        ));
                    } else if(instruction->kind == InstructionKind::AssembleStaticArray) {
                        auto assemble_static_array = (AssembleStaticArray*)instruction;

                        auto first_element_value = get_register_value(*function, function_value, registers, assemble_static_array->element_registers[0]);

                        auto element_llvm_type = get_llvm_type(architecture_sizes, first_element_value.type);
                        auto llvm_type = LLVMArrayType2(element_llvm_type, assemble_static_array->element_registers.length);

                        auto initial_constant_values = allocate<LLVMValueRef>(assemble_static_array->element_registers.length);

                        for(size_t i = 0; i < assemble_static_array->element_registers.length; i += 1) {
                            initial_constant_values[i] = LLVMGetUndef(element_llvm_type);
                        }

                        auto current_array_value = LLVMConstArray2(
                            element_llvm_type,
                            initial_constant_values,
                            assemble_static_array->element_registers.length
                        );

                        current_array_value = LLVMBuildInsertValue(
                            builder,
                            current_array_value,
                            first_element_value.value,
                            0,
                            "insert_value"
                        );

                        if(LLVMIsAInstruction(current_array_value)) {
                            LLVMInstructionSetDebugLoc(current_array_value, debug_location);
                        }

                        for(size_t i = 1; i < assemble_static_array->element_registers.length; i += 1) {
                            auto element_value = get_register_value(*function, function_value, registers, assemble_static_array->element_registers[i]);

                            current_array_value = LLVMBuildInsertValue(
                                builder,
                                current_array_value,
                                element_value.value,
                                (unsigned int)i,
                                "insert_value"
                            );

                            if(LLVMIsAInstruction(current_array_value)) {
                                LLVMInstructionSetDebugLoc(current_array_value, debug_location);
                            }
                        }

                        auto type = IRType::create_static_array(
                            assemble_static_array->element_registers.length,
                            heapify(first_element_value.type)
                        );

                        registers.append(Register(
                            assemble_static_array->destination_register,
                            TypedValue(type, current_array_value)
                        ));
                    } else if(instruction->kind == InstructionKind::ReadStaticArrayElement) {
                        auto read_static_array_element = (ReadStaticArrayElement*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, read_static_array_element->source_register);

                        llvm_instruction(result_value, LLVMBuildExtractValue(
                            builder,
                            source_value.value,
                            (unsigned int)read_static_array_element->element_index,
                            "read_static_array_element"
                        ));

                        registers.append(Register(
                            read_static_array_element->destination_register,
                            TypedValue(*source_value.type.static_array.element_type, result_value)
                        ));
                    } else if(instruction->kind == InstructionKind::AssembleStruct) {
                        auto assemble_struct = (AssembleStruct*)instruction;

                        auto initial_constant_values = allocate<LLVMValueRef>(assemble_struct->member_registers.length);

                        for(size_t i = 0; i < assemble_struct->member_registers.length; i += 1) {
                            auto member_value = get_register_value(*function, function_value, registers, assemble_struct->member_registers[i]);

                            initial_constant_values[i] = LLVMGetUndef(get_llvm_type(architecture_sizes, member_value.type));
                        }

                        auto current_struct_value = LLVMConstStruct(
                            initial_constant_values,
                            assemble_struct->member_registers.length,
                            false
                        );

                        auto member_types = allocate<IRType>(assemble_struct->member_registers.length);

                        for(size_t i = 0; i < assemble_struct->member_registers.length; i += 1) {
                            auto member_value = get_register_value(*function, function_value, registers, assemble_struct->member_registers[i]);

                            member_types[i] = member_value.type;

                            current_struct_value = LLVMBuildInsertValue(
                                builder,
                                current_struct_value,
                                member_value.value,
                                (unsigned int)i,
                                "insert_value"
                            );

                            if(LLVMIsAInstruction(current_struct_value)) {
                                LLVMInstructionSetDebugLoc(current_struct_value, debug_location);
                            }
                        }

                        auto type = IRType::create_struct(Array(assemble_struct->member_registers.length, member_types));

                        registers.append(Register(
                            assemble_struct->destination_register,
                            TypedValue(type, current_struct_value)
                        ));
                    } else if(instruction->kind == InstructionKind::ReadStructMember) {
                        auto read_struct_member = (ReadStructMember*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, read_struct_member->source_register);

                        llvm_instruction(result_value, LLVMBuildExtractValue(
                            builder,
                            source_value.value,
                            (unsigned int)read_struct_member->member_index,
                            "read_struct_member"
                        ));

                        registers.append(Register(
                            read_struct_member->destination_register,
                            TypedValue(source_value.type.struct_.members[read_struct_member->member_index], result_value)
                        ));
                    } else if(instruction->kind == InstructionKind::Literal) {
                        auto literal = (Literal*)instruction;

                        auto llvm_constant_result = get_llvm_constant(architecture_sizes, literal->type, literal->value);

                        registers.append(Register(
                            literal->destination_register,
                            TypedValue(literal->type, llvm_constant_result.value)
                        ));
                    } else if(instruction->kind == InstructionKind::Jump) {
                        auto jump = (Jump*)instruction;

                        auto destination = get_instruction_block(blocks, function->instructions[jump->destination_instruction]);

                        llvm_instruction_ignore(LLVMBuildBr(builder, destination));

                        might_need_terminator = false;
                    } else if(instruction->kind == InstructionKind::Branch) {
                        auto branch = (Branch*)instruction;

                        auto condition_value = get_register_value(*function, function_value, registers, branch->condition_register);

                        llvm_instruction(truncated_condition_value, LLVMBuildTrunc(builder, condition_value.value, LLVMInt1Type(), "truncate"));

                        auto destination = get_instruction_block(blocks, function->instructions[branch->destination_instruction]);

                        auto next = get_instruction_block(blocks, function->instructions[i + 1]);

                        llvm_instruction_ignore(LLVMBuildCondBr(builder, truncated_condition_value, destination, next));

                        might_need_terminator = false;
                    } else if(instruction->kind == InstructionKind::FunctionCallInstruction) {
                        auto function_call = (FunctionCallInstruction*)instruction;

                        auto parameter_count = function_call->parameters.length;

                        auto parameter_types = allocate<LLVMTypeRef>(parameter_count);
                        auto parameter_values = allocate<LLVMValueRef>(parameter_count);
                        for(size_t i = 0; i < parameter_count; i += 1) {
                            auto parameter = function_call->parameters[i];

                            parameter_types[i] = get_llvm_type(architecture_sizes, parameter.type);

                            parameter_values[i] = get_register_value(*function, function_value, registers, parameter.register_index).value;
                        }

                        auto return_type = get_llvm_type(architecture_sizes, function_call->return_type);

                        auto function_type = LLVMFunctionType(return_type, parameter_types, (unsigned int)parameter_count, false);

                        auto function_pointer_type = LLVMPointerType(function_type, 0);

                        auto function_pointer_value = get_register_value(*function, function_value, registers, function_call->pointer_register);

                        const char* name;
                        if(function_call->return_type.kind != IRTypeKind::Void) {
                            name = "call";
                        } else {
                            name = "";
                        }

                        llvm_instruction(value, LLVMBuildCall2(
                            builder,
                            function_type,
                            function_pointer_value.value,
                            parameter_values,
                            (unsigned int)parameter_count,
                            name
                        ));

                        expect(calling_convention, get_llvm_calling_convention(
                            function->path,
                            function_call->range,
                            os,
                            architecture,
                            function_call->calling_convention
                        ));

                        LLVMSetInstructionCallConv(value, calling_convention);

                        if(function_call->return_type.kind != IRTypeKind::Void) {
                            registers.append(Register(
                                function_call->return_register,
                                TypedValue(function->return_type, value)
                            ));
                        }
                    } else if(instruction->kind == InstructionKind::ReturnInstruction) {
                        auto return_instruction = (ReturnInstruction*)instruction;

                        if(function->return_type.kind != IRTypeKind::Void) {
                            auto return_value = get_register_value(*function, function_value, registers, return_instruction->value_register);

                            llvm_instruction_ignore(LLVMBuildRet(builder, return_value.value));
                        } else {
                            llvm_instruction_ignore(LLVMBuildRetVoid(builder));
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

                        auto pointer_type = IRType::create_pointer(heapify(allocate_local->type));

                        registers.append(Register(
                            allocate_local->destination_register,
                            TypedValue(pointer_type, pointer_value)
                        ));
                    } else if(instruction->kind == InstructionKind::Load) {
                        auto load = (Load*)instruction;

                        auto pointer_register = get_register_value(*function, function_value, registers, load->pointer_register);

                        auto type = *pointer_register.type.pointer;
                        auto llvm_type = get_llvm_type(architecture_sizes, type);

                        llvm_instruction(value, LLVMBuildLoad2(builder, llvm_type, pointer_register.value, "load"));

                        registers.append(Register(
                            load->destination_register,
                            TypedValue(type, value)
                        ));
                    } else if(instruction->kind == InstructionKind::Store) {
                        auto store = (Store*)instruction;

                        auto source_value = get_register_value(*function, function_value, registers, store->source_register);

                        auto pointer_value = get_register_value(*function, function_value, registers, store->pointer_register);

                        llvm_instruction_ignore(LLVMBuildStore(builder, source_value.value, pointer_value.value));
                    } else if(instruction->kind == InstructionKind::StructMemberPointer) {
                        auto struct_member_pointer = (StructMemberPointer*)instruction;

                        auto pointer_value = get_register_value(*function, function_value, registers, struct_member_pointer->pointer_register);

                        auto struct_type = *pointer_value.type.pointer;
                        auto member_type = struct_type.struct_.members[struct_member_pointer->member_index];

                        auto struct_llvm_type = get_llvm_type(architecture_sizes, struct_type);

                        llvm_instruction(member_pointer_value, LLVMBuildStructGEP2(
                            builder,
                            struct_llvm_type,
                            pointer_value.value,
                            struct_member_pointer->member_index,
                            "struct_member_pointer"
                        ));

                        auto member_pointer_type = IRType::create_pointer(heapify(member_type));

                        registers.append(Register(
                            struct_member_pointer->destination_register,
                            TypedValue(member_pointer_type, member_pointer_value)
                        ));
                    } else if(instruction->kind == InstructionKind::PointerIndex) {
                        auto pointer_index = (PointerIndex*)instruction;

                        auto index_value = get_register_value(*function, function_value, registers, pointer_index->index_register);

                        auto pointer_value = get_register_value(*function, function_value, registers, pointer_index->pointer_register);

                        auto pointed_to_llvm_type = get_llvm_type(architecture_sizes, *pointer_value.type.pointer);

                        llvm_instruction(result_pointer_value, LLVMBuildGEP2(
                            builder,
                            pointed_to_llvm_type,
                            pointer_value.value,
                            &index_value.value,
                            1,
                            "pointer_index"
                        ));

                        registers.append(Register(
                            pointer_index->destination_register,
                            TypedValue(pointer_value.type, result_pointer_value)
                        ));
                    } else if(instruction->kind == InstructionKind::ReferenceStatic) {
                        auto reference_static = (ReferenceStatic*)instruction;

                        auto found = false;
                        TypedValue global_value;
                        for(size_t i = 0; i < statics.length; i += 1) {
                            if(statics[i] == reference_static->runtime_static) {
                                global_value = global_values[i];
                                found = true;

                                break;
                            }
                        }
                        assert(found);

                        registers.append(Register(
                            reference_static->destination_register,
                            TypedValue(IRType::create_pointer(heapify(global_value.type)), global_value.value)
                        ));
                    } else {
                        abort();
                    }

                    if(might_need_terminator) {
                        for(auto block : blocks) {
                            if(block.instruction == function->instructions[i + 1]) {
                                llvm_instruction_ignore(LLVMBuildBr(builder, block.block));
                            }
                        }
                    }
                }

                LLVMDIBuilderFinalizeSubprogram(debug_builder, function_debug_scope);
            }
        }
    }

    LLVMDIBuilderFinalize(debug_builder);

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