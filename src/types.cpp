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
            assert(a.integer != IntegerType::Undetermined && b.integer != IntegerType::Undetermined);

            return a.integer == b.integer;
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
        } break;
        
        case TypeCategory::Integer: {
            switch(type.integer) {
                case IntegerType::Undetermined: {
                    return "{integer}";
                } break;

                case IntegerType::Unsigned8: {
                    return "u8";
                } break;

                case IntegerType::Unsigned16: {
                    return "u16";
                } break;

                case IntegerType::Unsigned32: {
                    return "u32";
                } break;

                case IntegerType::Unsigned64: {
                    return "u64";
                } break;

                case IntegerType::Signed8: {
                    return "i8";
                } break;

                case IntegerType::Signed16: {
                    return "i16";
                } break;

                case IntegerType::Signed32: {
                    return "i32";
                } break;

                case IntegerType::Signed64: {
                    return "i64";
                } break;

                default: {
                    abort();
                } break;
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