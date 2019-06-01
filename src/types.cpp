#include "types.h"
#include <stdlib.h>

bool types_equal(Type a, Type b) {
    if(a.category != b.category) {
        return false;
    }

    switch(a.category) {
        case TypeCategory::Function: {
            if(a.function.parameters.count != b.function.parameters.count) {
                return false;
            }

            for(auto i = 0; i < a.function.parameters.count; i += 1) {
                if(!types_equal(a.function.parameters[i], b.function.parameters[i])) {
                    return false;
                }
            }

            return types_equal(*a.function.return_type, *b.function.return_type);
        } break;
        
        case TypeCategory::Integer: {
            return !a.integer.determined || !b.integer.determined || (a.integer.is_signed == b.integer.is_signed && a.integer.size == b.integer.size);
        } break;

        case TypeCategory::Type: {
            return true;
        } break;

        case TypeCategory::Void: {
            return true;
        }  break;

        case TypeCategory::Pointer: {
            return types_equal(*a.pointer, *b.pointer);
        } break;

        default: {
            abort();
        } break;
    }
}