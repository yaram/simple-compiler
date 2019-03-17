#include "types.h"
#include <assert.h>

bool types_equal(Type a, Type b) {
    if(a.category != b.category) {
        return false;
    }

    switch(a.category) {
        case TypeCategory::Function: {
            return true;
        } break;
        
        case TypeCategory::Integer: {
            return a.integer.is_signed == b.integer.is_signed && a.integer.size == b.integer.size;
        } break;

        default: {
            assert(false);

            return false;
        } break;
    }
}