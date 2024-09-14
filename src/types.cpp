#include "types.h"
#include <string.h>
#include "util.h"

bool AnyType::operator==(AnyType other) {
    if(kind != other.kind) {
        return false;
    }

    if(kind == TypeKind::FunctionTypeType) {
        if(function.parameters.length != other.function.parameters.length) {
            return false;
        }

        for(size_t i = 0; i < function.parameters.length; i += 1) {
            if(function.parameters[i] != other.function.parameters[i]) {
                return false;
            }
        }

        if(function.calling_convention != other.function.calling_convention) {
            return false;
        }

        return *function.return_type == *other.function.return_type;
    } else if(kind == TypeKind::PolymorphicFunction) {
        return false;
    } else if(kind == TypeKind::BuiltinFunction) {
        return false;
    } else if(kind == TypeKind::Integer) {
        return integer.size == other.integer.size && integer.is_signed == other.integer.is_signed;
    } else if(kind == TypeKind::UndeterminedInteger) {
        return true;
    } else if(kind == TypeKind::Boolean) {
        return true;
    } else if(kind == TypeKind::FloatType) {
        return float_.size == other.float_.size;
    } else if(kind == TypeKind::UndeterminedFloat) {
        return true;
    } else if(kind == TypeKind::Type) {
        return true;
    } else if(kind == TypeKind::Void) {
        return true;
    } else if(kind == TypeKind::Pointer) {
        auto a_pointer = pointer;
        auto b_pointer = other.pointer;

        return *a_pointer.pointed_to_type == *b_pointer.pointed_to_type;
    } else if(kind == TypeKind::ArrayTypeType) {
        return *array.element_type == *other.array.element_type;
    } else if(kind == TypeKind::StaticArray) {
        auto a_static_array = static_array;
        auto b_static_array = other.static_array;

        return *a_static_array.element_type == *b_static_array.element_type && a_static_array.length == b_static_array.length;
    } else if(kind == TypeKind::StructType) {
        auto a_struct = struct_;
        auto b_struct = other.struct_;

        if(a_struct.definition != b_struct.definition) {
            return false;
        }

        if(a_struct.members.length != b_struct.members.length) {
            return false;
        }

        for(size_t i = 0; i < a_struct.members.length; i += 1) {
            if(
                a_struct.members[i].name != b_struct.members[i].name ||
                a_struct.members[i].type !=  b_struct.members[i].type
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::PolymorphicStruct) {
        auto a_polymorphic_struct = polymorphic_struct;
        auto b_polymorphic_struct = other.polymorphic_struct;

        return a_polymorphic_struct.definition != b_polymorphic_struct.definition;
    } else if(kind == TypeKind::UnionType) {
        auto a_union = union_;
        auto b_union = other.union_;

        if(a_union.definition != b_union.definition) {
            return false;
        }

        if(a_union.members.length != b_union.members.length) {
            return false;
        }

        for(size_t i = 0; i < a_union.members.length; i += 1) {
            if(
                a_union.members[i].name != b_union.members[i].name ||
                a_union.members[i].type !=  b_union.members[i].type
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::PolymorphicUnion) {
        auto a_polymorphic_union = polymorphic_union;
        auto b_polymorphic_union = other.polymorphic_union;

        return a_polymorphic_union.definition != b_polymorphic_union.definition;
    } else if(kind == TypeKind::UndeterminedStruct) {
        auto a_undetermined_struct = undetermined_struct;
        auto b_undetermined_struct = other.undetermined_struct;

        if(a_undetermined_struct.members.length != b_undetermined_struct.members.length) {
            return false;
        }

        for(size_t i = 0; i < a_undetermined_struct.members.length; i += 1) {
            if(
                a_undetermined_struct.members[i].name != b_undetermined_struct.members[i].name ||
                a_undetermined_struct.members[i].type != b_undetermined_struct.members[i].type
            ) {
                return false;
            }
        }

        return true;
    } else if(kind == TypeKind::Enum) {
        auto a_enum = enum_;
        auto b_enum = other.enum_;

        return a_enum.definition != b_enum.definition;
    } else if(kind == TypeKind::FileModule) {
        return true;
    } else {
        abort();
    }
}

bool AnyType::operator!=(AnyType other) {
    return !(*this == other);
}

String AnyType::get_description() {
    if(kind == TypeKind::FunctionTypeType) {
        StringBuffer buffer {};

        buffer.append("("_S);

        for(size_t i = 0; i < function.parameters.length; i += 1) {
            buffer.append(function.parameters[i].get_description());

            if(i != function.parameters.length - 1) {
                buffer.append(","_S);
            }
        }

        buffer.append(")"_S);

        if(function.return_type != nullptr) {
            buffer.append(" . "_S);
            buffer.append(function.return_type->get_description());
        }

        if(function.calling_convention != CallingConvention::Default) {
            buffer.append(" #call_conv(\""_S);

            switch(function.calling_convention) {
                case CallingConvention::StdCall: {
                    buffer.append("stdcall"_S);
                } break;

                default: {
                    abort();
                }
            }

            buffer.append("\")"_S);
        }

        return buffer;
    } else if(kind == TypeKind::PolymorphicFunction) {
        return "{function}"_S;
    } else if(kind == TypeKind::BuiltinFunction) {
        return "{builtin}"_S;
    } else if(kind == TypeKind::Integer) {
        if(integer.is_signed) {
            switch(integer.size) {
                case RegisterSize::Size8: {
                    return "i8"_S;
                } break;

                case RegisterSize::Size16: {
                    return "i16"_S;
                } break;

                case RegisterSize::Size32: {
                    return "i32"_S;
                } break;

                case RegisterSize::Size64: {
                    return "i64"_S;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            switch(integer.size) {
                case RegisterSize::Size8: {
                    return "u8"_S;
                } break;

                case RegisterSize::Size16: {
                    return "u16"_S;
                } break;

                case RegisterSize::Size32: {
                    return "u32"_S;
                } break;

                case RegisterSize::Size64: {
                    return "u64"_S;
                } break;

                default: {
                    abort();
                } break;
            }
        }
    } else if(kind == TypeKind::UndeterminedInteger) {
        return "{integer}"_S;
    } else if(kind == TypeKind::Boolean) {
        return "bool"_S;
    } else if(kind == TypeKind::FloatType) {
        switch(float_.size) {
            case RegisterSize::Size32: {
                return "f32"_S;
            } break;

            case RegisterSize::Size64: {
                return "f64"_S;
            } break;

            default: {
                abort();
            } break;
        }
    } else if(kind == TypeKind::UndeterminedFloat) {
        return "{float}"_S;
    } else if(kind == TypeKind::Type) {
        return "{type}"_S;
    } else if(kind == TypeKind::Void) {
        return "void"_S;
    } else if(kind == TypeKind::Pointer) {
        StringBuffer buffer {};

        buffer.append("*"_S);
        buffer.append(pointer.pointed_to_type->get_description());

        return buffer;
    } else if(kind == TypeKind::ArrayTypeType) {
        StringBuffer buffer {};

        buffer.append("[]"_S);
        buffer.append(array.element_type->get_description());

        return buffer;
    } else if(kind == TypeKind::StaticArray) {
        StringBuffer buffer {};

        buffer.append("["_S);
        buffer.append_integer(static_array.length);
        buffer.append("]"_S);
        buffer.append(static_array.element_type->get_description());

        return buffer;
    } else if(kind == TypeKind::StructType) {
        return struct_.definition->name.text;
    } else if(kind == TypeKind::PolymorphicStruct) {
        return polymorphic_struct.definition->name.text;
    } else if(kind == TypeKind::UnionType) {
        return union_.definition->name.text;
    } else if(kind == TypeKind::PolymorphicUnion) {
        return polymorphic_union.definition->name.text;
    } else if(kind == TypeKind::UndeterminedStruct) {
        return "{struct}"_S;
    } else if(kind == TypeKind::Enum) {
        return enum_.definition->name.text;
    } else if(kind == TypeKind::FileModule) {
        return "{module}"_S;
    } else {
        abort();
    }
}

bool AnyType::is_runtime_type() {
    if(
        kind == TypeKind::Integer ||
        kind == TypeKind::Boolean ||
        kind == TypeKind::FloatType ||
        kind == TypeKind::Pointer ||
        kind == TypeKind::ArrayTypeType ||
        kind == TypeKind::StaticArray ||
        kind == TypeKind::StructType ||
        kind == TypeKind::UnionType ||
        kind == TypeKind::Enum
    ) {
        return true;
    } else {
        return false;
    }
}

bool AnyType::is_pointable_type() {
    if(
        kind == TypeKind::FunctionTypeType ||
        kind == TypeKind::Integer ||
        kind == TypeKind::Boolean ||
        kind == TypeKind::FloatType ||
        kind == TypeKind::Void ||
        kind == TypeKind::Pointer ||
        kind == TypeKind::ArrayTypeType ||
        kind == TypeKind::StaticArray ||
        kind == TypeKind::StructType ||
        kind == TypeKind::UnionType ||
        kind == TypeKind::Enum
    ) {
        return true;
    } else {
        return false;
    }
}

uint64_t AnyType::get_alignment(ArchitectureSizes architecture_sizes) {
    if(kind == TypeKind::Integer) {
        return register_size_to_byte_size(integer.size);
    } else if(kind == TypeKind::Boolean) {
        return register_size_to_byte_size(architecture_sizes.boolean_size);
    } else if(kind == TypeKind::FloatType) {
        return register_size_to_byte_size(float_.size);
    } else if(kind == TypeKind::Pointer) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(kind == TypeKind::ArrayTypeType) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(kind == TypeKind::StaticArray) {
        return static_array.element_type->get_alignment(architecture_sizes);
    } else if(kind == TypeKind::StructType) {
        return struct_.get_alignment(architecture_sizes);
    } else if(kind == TypeKind::UnionType) {
        return union_.get_alignment(architecture_sizes);
    } else if(kind == TypeKind::Enum) {
        return register_size_to_byte_size(enum_.backing_type->size);
    } else {
        abort();
    }
}

uint64_t AnyType::get_size(ArchitectureSizes architecture_sizes) {
    if(kind == TypeKind::Integer) {
        return register_size_to_byte_size(integer.size);
    } else if(kind == TypeKind::Boolean) {
        return register_size_to_byte_size(architecture_sizes.boolean_size);
    } else if(kind == TypeKind::FloatType) {
        return register_size_to_byte_size(float_.size);
    } else if(kind == TypeKind::Pointer) {
        return register_size_to_byte_size(architecture_sizes.address_size);
    } else if(kind == TypeKind::ArrayTypeType) {
        return 2 * register_size_to_byte_size(architecture_sizes.address_size);
    } else if(kind == TypeKind::StaticArray) {
        return static_array.length * static_array.element_type->get_alignment(architecture_sizes);
    } else if(kind == TypeKind::StructType) {
        return struct_.get_size(architecture_sizes);
    } else if(kind == TypeKind::UnionType) {
        return union_.get_size(architecture_sizes);
    } else if(kind == TypeKind::Enum) {
        return register_size_to_byte_size(enum_.backing_type->size);
    } else {
        abort();
    }
}

uint64_t StructType::get_alignment(ArchitectureSizes architecture_sizes) {
    size_t current_alignment = 1;

    for(auto member : members) {
        auto member_alignment = member.type.get_alignment(architecture_sizes);

        if(member_alignment > current_alignment) {
            current_alignment = member_alignment;
        }
    }

    return current_alignment;
}

uint64_t StructType::get_size(ArchitectureSizes architecture_sizes) {
    uint64_t current_size = 0;

    for(auto member : members) {
        auto member_alignment = member.type.get_alignment(architecture_sizes);

        auto alignment_difference = current_size % member_alignment;

        uint64_t offset;
        if(alignment_difference != 0) {
            offset = member_alignment - alignment_difference;
        } else {
            offset = 0;
        }

        auto member_size = member.type.get_size(architecture_sizes);

        current_size += offset + member_size;     
    }

    return current_size;
}

uint64_t StructType::get_member_offset(ArchitectureSizes architecture_sizes, size_t member_index) {
    uint64_t current_offset = 0;

    for(auto i = 0; i < member_index; i += 1) {
        auto member_alignment = members[i].type.get_alignment(architecture_sizes);

        auto alignment_difference = current_offset % member_alignment;

        uint64_t offset;
        if(alignment_difference != 0) {
            offset = member_alignment - alignment_difference;
        } else {
            offset = 0;
        }

        auto member_size = members[i].type.get_size(architecture_sizes);

        current_offset += offset + member_size;
    }

    auto member_alignment = members[member_index].type.get_alignment(architecture_sizes);

    auto alignment_difference = current_offset % member_alignment;

    uint64_t offset;
    if(alignment_difference != 0) {
        offset = member_alignment - alignment_difference;
    } else {
        offset = 0;
    }

    return current_offset + offset;
}

uint64_t UnionType::get_alignment(ArchitectureSizes architecture_sizes) {
    size_t current_alignment = 1;

    for(auto member : members) {
        auto member_alignment = member.type.get_alignment(architecture_sizes);

        if(member_alignment > current_alignment) {
            current_alignment = member_alignment;
        }
    }

    return current_alignment;
}

uint64_t UnionType::get_size(ArchitectureSizes architecture_sizes) {
    uint64_t current_size = 0;

    for(auto member : members) {
        auto member_size = member.type.get_size(architecture_sizes);

        if(member_size > current_size) {
            current_size = member_size;
        }    
    }

    return current_size;
}