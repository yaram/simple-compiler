#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "string.h"

struct FileRange {
    unsigned int first_line;
    unsigned int first_column;

    unsigned int last_line;
    unsigned int last_column;
};

template <typename T>
inline T* heapify(T value) {
    auto pointer = (T*)malloc(sizeof(T));

    *pointer = value;

    return pointer;
}

template <typename T>
inline T* allocate(size_t count) {
    return (T*)malloc(sizeof(T) * count);
}

template <typename T>
inline T* reallocate(T* old_data, size_t new_count) {
    return (T*)realloc(old_data, sizeof(T) * new_count);
}

void error(String path, FileRange range, const char* format, va_list arguments);
void error(String path, FileRange range, const char* format, ...);