#pragma once

#include "array.h"

// Must contain valid ASCII
struct String : Array<char> {
    static const String from_c_string(const char* c_string);
    char* to_c_string();

    bool operator==(String other);
    bool operator!=(String other);
};

inline const String operator "" _S(const char* data, size_t length) {
    String string {};
    string.length = length;
    string.elements = (char*)data;
    return string;
}

#define STRING_PRINTF_ARGUMENTS(string) (int)(string).length, (string).elements

struct StringBuffer : String {
    size_t capacity;

    void append(String string);
    //void append(String c_string);
    void append_integer(size_t number);
    void append_character(char character);
};