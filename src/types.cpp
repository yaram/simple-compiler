#include "types.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

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