#pragma once

#include "string.h"

enum struct CallingConvention {
    Default,
    StdCall
};

String calling_convention_name(CallingConvention calling_convention);