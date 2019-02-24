#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace list {
    template <typename T>
    struct List {
        size_t count;
        size_t capacity;

        T *elements;
    };

    template <typename T>
    void append(List<T> *list, T element) {
        const size_t initial_capacity = 16;

        if(list->capacity == 0) {
            list->capacity = initial_capacity;

            list->elements = (T*)malloc(initial_capacity * sizeof(T));
        } else if(list->count == list->capacity) {
            auto new_capacity = list->capacity * 2;

            auto new_elements = (T*)malloc(initial_capacity * sizeof(T));

            free(list->elements);

            list->capacity = new_capacity;
            list->elements = new_elements;
        }

        list->elements[list->count] = element;

        list->count += 1;
    }
}