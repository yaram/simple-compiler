#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

struct FileRange {
    unsigned int first_line;
    unsigned int first_character;

    unsigned int last_line;
    unsigned int last_character;
};

template <typename T>
inline T *heapify(T value) {
    auto pointer = (T*)malloc(sizeof(T));

    *pointer = value;

    return pointer;
}

template <typename T>
inline T *allocate(size_t count) {
    return (T*)malloc(sizeof(T) * count);
}

struct StringBuffer {
    size_t length;
    size_t capacity;

    char *data;
};
void string_buffer_append(StringBuffer *string_buffer, const char *string);
void string_buffer_append(StringBuffer *string_buffer, size_t number);
void string_buffer_append_character(StringBuffer *string_buffer, char character);

struct String {
    size_t length;

    char *data;
};

inline String operator "" _S(const char *data, size_t length) {
    return {
        length,
        (char*)data
    };
}

inline String string_buffer_string(StringBuffer string_buffer) {
    return {
        string_buffer.length,
        string_buffer.data
    };
}

void string_buffer_append(StringBuffer *string_buffer, String string);

bool equal(String a, String b);

const char *string_to_c_string(String string);

#define STRING_PRINT(string) (int)(string).length, (string).data

void error(const char *path, FileRange range, const char *format, va_list arguments);
void error(const char *path, FileRange range, const char *format, ...);