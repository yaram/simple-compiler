#pragma once

#include <stdlib.h>

template <typename T>
struct Array {
    size_t count;
    T *elements;

    T &operator[](size_t index) {
        return elements[index];
    }
};

template <typename T>
T *begin(Array<T> &array) {
    return array.elements;
}

template <typename T>
T *end(Array<T> &array) {
    return array.elements + array.count;
}