#include "calling_convention.h"
#include <stdlib.h>

String calling_convention_name(CallingConvention calling_convention) {
    switch(calling_convention) {
        case CallingConvention::Default: {
            return u8"cdecl"_S;
        };

        case CallingConvention::StdCall: {
            return u8"stdcall"_S;
        } break;

        default: abort();
    }
}