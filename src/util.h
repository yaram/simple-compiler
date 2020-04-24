#pragma once

#include <stdlib.h>
#include <stdint.h>

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