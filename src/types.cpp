#include "types.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "util.h"

bool types_equal(Type a, Type b) {
    if(a.category != b.category) {
        return false;
    }

    switch(a.category) {
        case TypeCategory::Function: {
            if(a.function.is_polymorphic || b.function.is_polymorphic) {
                return false;
            }

            if(a.function.parameters.count != b.function.parameters.count) {
                return false;
            }

            for(size_t i = 0; i < a.function.parameters.count; i += 1) {
                if(!types_equal(a.function.parameters[i], b.function.parameters[i])) {
                    return false;
                }
            }

            return types_equal(*a.function.return_type, *b.function.return_type);
        } break;
        
        case TypeCategory::Integer: {
            return a.integer.size == b.integer.size && (a.integer.is_signed == b.integer.is_signed);
        } break;

        case TypeCategory::Pointer: {
            return types_equal(*a.pointer, *b.pointer);
        } break;

        case TypeCategory::Array: {
            return types_equal(*a.array, *b.array);
        } break;

        case TypeCategory::StaticArray: {
            return types_equal(*a.static_array.type, *b.static_array.type) && a.static_array.length == b.static_array.length;
        } break;

        case TypeCategory::Struct: {
            return strcmp(a._struct, b._struct) == 0;
        } break;

        default: {
            return true;
        } break;
    }
}

const char *type_description(Type type) {
    switch(type.category) {
        case TypeCategory::Function: {
            if(type.function.is_polymorphic) {
                return "{polymorphic}";
            } else {
                char *buffer{};

                string_buffer_append(&buffer, "(");

                for(size_t i = 0; i < type.function.parameters.count; i += 1) {
                    string_buffer_append(&buffer, type_description(type.function.parameters[i]));

                    if(i != type.function.parameters.count - 1) {
                        string_buffer_append(&buffer, ",");
                    }
                }

                string_buffer_append(&buffer, ")");
                
                if(type.function.return_type != nullptr) {
                    string_buffer_append(&buffer, " -> ");
                    string_buffer_append(&buffer, type_description(*type.function.return_type));
                }

                return buffer;
            }
        } break;
        
        case TypeCategory::Integer: {
            if(type.integer.is_undetermined) {
                return "{integer}";
            } else if(type.integer.is_signed) {
                switch(type.integer.size) {
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
                switch(type.integer.size) {
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
        } break;

        case TypeCategory::Boolean: {
            return "bool";
        } break;

        case TypeCategory::Type: {
            return "{type}";
        } break;

        case TypeCategory::Void: {
            return "void";
        } break;

        case TypeCategory::Pointer: {
            char *buffer{};

            string_buffer_append(&buffer, "*");
            string_buffer_append(&buffer, type_description(*type.pointer));

            return buffer;
        } break;

        case TypeCategory::Array: {
            char *buffer{};

            string_buffer_append(&buffer, "[]");
            string_buffer_append(&buffer, type_description(*type.array));

            return buffer;
        } break;

        case TypeCategory::StaticArray: {
            char *buffer{};

            string_buffer_append(&buffer, "[");

            char length_buffer[32];
            sprintf(length_buffer, "%z");
            string_buffer_append(&buffer, length_buffer);

            string_buffer_append(&buffer, "]");
            string_buffer_append(&buffer, type_description(*type.static_array.type));

            return buffer;
        } break;

        case TypeCategory::Struct: {
            return type._struct;
        } break;

        case TypeCategory::FileModule: {
            return "{module}";
        } break;

        default: {
            abort();
        } break;
    }
}