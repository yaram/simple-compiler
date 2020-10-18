#include "types.h"
#include <string.h>
#include "util.h"
#include "constant.h"

bool types_equal(AnyType a, AnyType b) {
    if(a.kind != b.kind) {
        return false;
    }

    auto kind = a.kind;

    if(kind == TypeKind::FunctionTypeType) {
        auto a_function_type = a.function;
        auto b_function_type = b.function;

        if(a_function_type.parameters.count != b_function_type.parameters.count) {
            return false;
        }

        for(size_t i = 0; i < a_function_type.parameters.count; i += 1) {
            if(!types_equal(a_function_type.parameters[i], b_function_type.parameters[i])) {
                return false;
            }
        }

        if(a_function_type.calling_convention != b_function_type.calling_convention) {
            return false;
        }

        return types_equal(*a_function_type.return_type, *b_function_type.return_type);
    } else if(kind == TypeKind::PolymorphicFunction) {
        return false;
    } else if(kind == TypeKind::BuiltinFunction) {
        return false;
    } else if(kind == TypeKind::Integer) {
        auto a_integer = a.integer;
        auto b_integer = b.integer;

        return a_integer.size == b_integer.size && a_integer.is_signed == b_integer.is_signed;
    } else if(kind == TypeKind::UndeterminedInteger) {
        return true;
    } else if(kind == TypeKind::Boolean) {
        return true;
    } else if(kind == TypeKind::FloatType) {
        auto a_float_type = a.float_;
        auto b_float_type = b.float_;

        return a_float_type.size == b_float_type.size;
    } else if(kind == TypeKind::UndeterminedFloat) {
        return true;
    } else if(kind == TypeKind::Type) {
        return true;
    } else if(kind == TypeKind::Void) {
        return true;
    } else if(kind == TypeKind::Pointer) {
        auto a_pointer = a.pointer;
        auto b_pointer = b.pointer;

        return types_equal(*a_pointer.type, *b_pointer.type);
    } else if(kind == TypeKind::ArrayTypeType) {
        auto a_array_type = a.array;
        auto b_array_type = b.array;

        return types_equal(*a_array_type.element_type, *b_array_type.element_type);
    } else if(kind == TypeKind::StaticArray) {
        auto a_static_array = a.static_array;
        auto b_static_array = b.static_array;

        return types_equal(*a_static_array.element_type, *b_static_array.element_type) && a_static_array.length == b_static_array.length;
    } else if(kind == TypeKind::StructType) {
        auto a_struct = a.struct_;
        auto b_struct = b.struct_;

        if(a_struct.definition != b_struct.definition) {
            return false;
        }

        if(a_struct.members.count != b_struct.members.count) {
            return false;
        }

        for(size_t i = 0; i < a_struct.members.count; i += 1) {
            if(
                !equal(a_struct.members[i].name, b_struct.members[i].name) ||
                !types_equal(a_struct.members[i].type, b_struct.members[i].type)
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::PolymorphicStruct) {
        auto a_polymorphic_struct = a.polymorphic_struct;
        auto b_polymorphic_struct = b.polymorphic_struct;

        return a_polymorphic_struct.definition != b_polymorphic_struct.definition;
    } else if(kind == TypeKind::UndeterminedStruct) {
        auto a_undetermined_struct = a.undetermined_struct;
        auto b_undetermined_struct = b.undetermined_struct;

        if(a_undetermined_struct.members.count != b_undetermined_struct.members.count) {
            return false;
        }

        for(size_t i = 0; i < a_undetermined_struct.members.count; i += 1) {
            if(
                !equal(a_undetermined_struct.members[i].name, b_undetermined_struct.members[i].name) ||
                !types_equal(a_undetermined_struct.members[i].type, b_undetermined_struct.members[i].type)
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::FileModule) {
        return true;
    } else {
        abort();
    }
}

const char *type_description(AnyType type) {
    if(type.kind == TypeKind::FunctionTypeType) {
        auto function = type.function;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "(");

        for(size_t i = 0; i < function.parameters.count; i += 1) {
            string_buffer_append(&buffer, type_description(function.parameters[i]));

            if(i != function.parameters.count - 1) {
                string_buffer_append(&buffer, ",");
            }
        }

        string_buffer_append(&buffer, ")");

        if(function.return_type != nullptr) {
            string_buffer_append(&buffer, " . ");
            string_buffer_append(&buffer, type_description(*function.return_type));
        }

        if(function.calling_convention != CallingConvention::Default) {
            string_buffer_append(&buffer, " #call_conv(\"");

            switch(function.calling_convention) {
                case CallingConvention::StdCall: {
                    string_buffer_append(&buffer, "stdcall");
                } break;

                default: {
                    abort();
                }
            }

            string_buffer_append(&buffer, "\")");
        }

        return buffer.data;
    } else if(type.kind == TypeKind::PolymorphicFunction) {
        return "{function}";
    } else if(type.kind == TypeKind::BuiltinFunction) {
        return "{builtin}";
    } else if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        if(integer.is_signed) {
            switch(integer.size) {
                case RegisterSize::Size8: {
                    return "i8";
                } break;

                case RegisterSize::Size16: {
                    return "i16";
                } break;

                case RegisterSize::Size32: {
                    return "i32";
                } break;

                case RegisterSize::Size64: {
                    return "i64";
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            switch(integer.size) {
                case RegisterSize::Size8: {
                    return "u8";
                } break;

                case RegisterSize::Size16: {
                    return "u16";
                } break;

                case RegisterSize::Size32: {
                    return "u32";
                } break;

                case RegisterSize::Size64: {
                    return "u64";
                } break;

                default: {
                    abort();
                } break;
            }
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        return "{integer}";
    } else if(type.kind == TypeKind::Boolean) {
        return "bool";
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        switch(float_type.size) {
            case RegisterSize::Size32: {
                return "f32";
            } break;

            case RegisterSize::Size64: {
                return "f64";
            } break;

            default: {
                abort();
            } break;
        }
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        return "{float}";
    } else if(type.kind == TypeKind::Type) {
        return "{type}";
    } else if(type.kind == TypeKind::Void) {
        return "void";
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "*");
        string_buffer_append(&buffer, type_description(*pointer.type));

        return buffer.data;
    } else if(type.kind == TypeKind::ArrayTypeType) {
        auto array = type.array;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "[]");
        string_buffer_append(&buffer, type_description(*array.element_type));

        return buffer.data;
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        StringBuffer buffer {};

        string_buffer_append(&buffer, "[");
        string_buffer_append(&buffer, static_array.length);
        string_buffer_append(&buffer, "]");
        string_buffer_append(&buffer, type_description(*static_array.element_type));

        return buffer.data;
    } else if(type.kind == TypeKind::StructType) {
        auto struct_type = type.struct_;

        return string_to_c_string(struct_type.definition->name.text);
    } else if(type.kind == TypeKind::PolymorphicStruct) {
        auto polymorphic_struct = type.polymorphic_struct;

        return string_to_c_string(polymorphic_struct.definition->name.text);
    } else if(type.kind == TypeKind::UndeterminedStruct) {
        return "{struct}";
    } else if(type.kind == TypeKind::FileModule) {
        return "{module}";
    } else {
        abort();
    }
}

bool is_runtime_type(AnyType type) {
    if(
        type.kind == TypeKind::Integer ||
        type.kind == TypeKind::Boolean ||
        type.kind == TypeKind::FloatType ||
        type.kind == TypeKind::Pointer ||
        type.kind == TypeKind::ArrayTypeType ||
        type.kind == TypeKind::StaticArray ||
        type.kind == TypeKind::StructType
    ) {
        return true;
    } else {
        return false;
    }
}

uint64_t get_type_alignment(ArchitectureSizes architecture_sizes, AnyType type) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        return register_size_to_byte_size(integer.size);
    } else if(type.kind == TypeKind::Boolean) {
        return register_size_to_byte_size(architecture_sizes.boolean_size);
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        return register_size_to_byte_size(float_type.size);
    } else if(type.kind == TypeKind::Pointer) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(type.kind == TypeKind::ArrayTypeType) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        return get_type_alignment(architecture_sizes, *static_array.element_type);
    } else if(type.kind == TypeKind::StructType) {
        auto struct_type = type.struct_;

        return get_struct_alignment(architecture_sizes, struct_type);
    } else {
        abort();
    }
}

uint64_t get_type_size(ArchitectureSizes architecture_sizes, AnyType type) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        return register_size_to_byte_size(integer.size);
    } else if(type.kind == TypeKind::Boolean) {
        return register_size_to_byte_size(architecture_sizes.boolean_size);
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        return register_size_to_byte_size(float_type.size);
    } else if(type.kind == TypeKind::Pointer) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(type.kind == TypeKind::ArrayTypeType) {
        return 2 * register_size_to_byte_size(architecture_sizes.address_size);
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        return static_array.length * get_type_alignment(architecture_sizes, *static_array.element_type);
    } else if(type.kind == TypeKind::StructType) {
        auto struct_type = type.struct_;

        return get_struct_size(architecture_sizes, struct_type);
    } else {
        abort();
    }
}

uint64_t get_struct_alignment(ArchitectureSizes architecture_sizes, StructType type) {
    size_t current_alignment = 1;

    for(auto member : type.members) {
        auto alignment = get_type_alignment(architecture_sizes, member.type);

        if(alignment > current_alignment) {
            current_alignment = alignment;
        }
    }

    return current_alignment;
}

uint64_t get_struct_size(ArchitectureSizes architecture_sizes, StructType type) {
    uint64_t current_size = 0;

    for(auto member : type.members) {
        if(type.definition->is_union) {
            auto size = get_type_size(architecture_sizes, member.type);

            if(size > current_size) {
                current_size = size;
            }
        } else {
            auto alignment = get_type_alignment(architecture_sizes, member.type);

            auto alignment_difference = current_size % alignment;

            uint64_t offset;
            if(alignment_difference != 0) {
                offset = alignment - alignment_difference;
            } else {
                offset = 0;
            }

            auto size = get_type_size(architecture_sizes, member.type);

            current_size += offset + size;
        }        
    }

    return current_size;
}

uint64_t get_struct_member_offset(ArchitectureSizes architecture_sizes, StructType type, size_t member_index) {
    if(type.definition->is_union) {
        return 0;
    }

    uint64_t current_offset = 0;

    for(auto i = 0; i < member_index; i += 1) {
        auto alignment = get_type_alignment(architecture_sizes, type.members[i].type);

        auto alignment_difference = current_offset % alignment;

        uint64_t offset;
        if(alignment_difference != 0) {
            offset = alignment - alignment_difference;
        } else {
            offset = 0;
        }

        auto size = get_type_size(architecture_sizes, type.members[i].type);

        current_offset += offset + size;
    }
    
    auto alignment = get_type_alignment(architecture_sizes, type.members[member_index].type);

    auto alignment_difference = current_offset % alignment;

    uint64_t offset;
    if(alignment_difference != 0) {
        offset = alignment - alignment_difference;
    } else {
        offset = 0;
    }

    return current_offset + offset;
}