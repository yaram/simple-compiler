#pragma once

#include <stdint.h>

enum struct RegisterSize {
    Size8,
    Size16,
    Size32,
    Size64
};

uint64_t register_size_to_byte_size(RegisterSize size);