#pragma once

#include <stdlib.h>

template <typename T>
inline T *heapify(T value) {
    auto pointer = (T*)malloc(sizeof(T));

    *pointer = value;

    return pointer;
}