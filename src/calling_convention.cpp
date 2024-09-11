#include "calling_convention.h"
#include <stdlib.h>

String calling_convention_name(CallingConvention calling_convention) {
    switch(calling_convention) {
        case CallingConvention::Default: {
            return "cdecl"_S;
        };

        case CallingConvention::StdCall: {
            return "stdcall"_S;
        } break;

        default: abort();
    }
}