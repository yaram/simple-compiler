#pragma once

#include <stdlib.h>
#include <assert.h>
#include "array.h"

template <typename T>
struct List : Array<T> {
    size_t capacity;

    size_t append(T element) {
        const size_t initial_capacity = 16;

        if(capacity == 0) {
            capacity = initial_capacity;

            this->elements = (T*)malloc(initial_capacity * sizeof(T));
        } else if(this->length == capacity) {
            auto new_capacity = capacity * 2;

            auto new_elements = (T*)realloc((void*)this->elements, new_capacity * sizeof(T));

            capacity = new_capacity;
            this->elements = new_elements;
        }

        auto index = this->length;

        this->elements[index] = element;

        this->length += 1;

        return index;
    }

    T take_last() {
        assert(this->length != 0);

        auto last_element = this->elements[this->length - 1];

        this->length -= 1;

        return last_element;
    }
};