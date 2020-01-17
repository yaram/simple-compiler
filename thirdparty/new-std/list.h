#pragma once

#include <array.h>

template <typename T>
struct List {
    usize length;
    usize capacity;

    T *elements;

    T &operator[](usize index) {
        return elements[index];
    }
};

template <typename T>
T *begin(List<T> &list) {
    return list.elements;
}

template <typename T>
T *end(List<T> &list) {
    return list.elements + list.length;
}

template <typename T, typename A>
bool append(List<T> &list, A &allocator, T element) {
    const usize initial_capacity = 16;

    if(list.capacity == 0) {
        list.capacity = initial_capacity;

        auto elements = (T*)allocate(allocator, initial_capacity * sizeof(T));

        if(elements == nullptr) {
            return false;
        }

        list.elements = elements;
    } else if(list.length == list.capacity) {
        auto new_capacity = list.capacity * 2;

        auto new_elements = (T*)reallocate(allocator, (void*)list.elements, list.capacity * sizeof(T), new_capacity * sizeof(T));

        if(new_elements == nullptr) {
            return false;
        }

        list.capacity = new_capacity;
        list.elements = new_elements;
    }

    list.elements[list.length] = element;

    list.length += 1;

    return true;
}

template <typename T>
T take_last(List<T> &list) {
    auto last_element = list.elements[list.length - 1];

    list.length -= 1;

    return last_element;
}

template <typename T, typename A>
Result<Array<T>> to_array(List<T> list, A &allocator) {
    if(list.length == list.capacity) {
        return {
            list.length,
            list.elements
        };
    } else {
        auto shrunk_elements = (T*)reallocate(allocator, list.elements, list.capacity * sizeof(T), list.length * sizeof(T));

        if(shrunk_elements == nullptr) {
            return err<Array<T>>();
        }

        return {
            list.length,
            shrunk_elements
        };
    }
}