#pragma once

#include <integers.h>

template <typename T>
struct Array {
    usize length;

    T *elements;

    T &operator[](usize index) {
        return elements[index];
    }
};

template <typename T>
T *begin(Array<T> &array) {
    return array.elements;
}

template <typename T>
T *end(Array<T> &array) {
    return array.elements + array.length;
}