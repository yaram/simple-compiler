#pragma once

#include <stddef.h>

template <typename T>
struct Array {
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