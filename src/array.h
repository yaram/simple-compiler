#pragma once

#include <stddef.h>

template <typename T>
struct Array {
    inline Array() = default;
    inline Array(size_t length, T* elements) : length(length), elements(elements) {}

    size_t length;
    T* elements;

    inline T &operator[](size_t index) {
        return elements[index];
    }

    inline T* begin() {
        return elements;
    }

    inline T* end() {
        return elements + length;
    }
};