#pragma once

#include <stdint.h>
#include "arena.h"
#include "array.h"
#include "result.h"

Result<void> validate_utf8_string(uint8_t* bytes, size_t length);
Result<size_t> validate_c_string(const char* c_string);

// Must contain valid UTF-8
struct String : Array<char8_t> {
    static const Result<String> from_c_string(Arena* arena, const char* c_string);
    char* to_c_string(Arena* arena);

    inline String slice(size_t index, size_t length) {
        assert(index + length <= this->length);

        String result {};
        result.elements = (char8_t*)((size_t)elements + index);
        result.length = length;

        return result;
    }

    inline String slice(size_t index) {
        assert(index <= length);

        return slice(index, length - index);
    }

    bool operator==(String other);
    bool operator!=(String other);
};

inline const String operator "" _S(const char8_t* data, size_t length) {
    String string {};
    string.length = length;
    string.elements = (char8_t*)data;
    return string;
}

#define STRING_PRINTF_ARGUMENTS(string) (int)(string).length, (char*)(string).elements

struct StringBuffer : String {
    Arena* arena;

    size_t capacity;

    StringBuffer() = default;
    explicit inline StringBuffer(Arena* arena) : String({}), arena(arena), capacity(0) {}

    void append(String string);
    Result<void> append_c_string(const char* c_string);
    void append_integer(size_t number);
    void append_character(char32_t character);
};