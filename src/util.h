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

void string_buffer_append(char **string_buffer, const char *string);
void string_buffer_append(char **string_buffer, size_t number);
void string_buffer_append_character(char **string_buffer, char character);