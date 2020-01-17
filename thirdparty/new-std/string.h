#pragma once

#include <integers.h>
#include <array.h>

struct String {
    usize length;

    u8 *bytes;

    u8 &operator[](usize index) {
        return bytes[index];
    }
};

u8 *begin(String &string) {
    return string.bytes;
}

u8 *end(String &string) {
    return string.bytes + string.length;
}

String to_string(char *c_string) {
    usize length = 0;

    while(c_string[length] != '\0') {
        length += 1;
    }

    return {
        length,
        (u8*)c_string
    };
}

String substring(String string, usize index, usize length) {
    return {
        length,
        string.bytes + index
    };
}

String operator "" _S(const char *c_string, usize length) {
    return {
        length,
        (u8*)c_string
    };
}