#pragma once

#include <stddef.h>

struct ArenaChunkHeader {
    ArenaChunkHeader* next_chunk;
    size_t size;
};

struct Arena {
    ArenaChunkHeader* first_chunk;
    ArenaChunkHeader* current_chunk;
    size_t current_offset;

    void reset();
    void free();

    void* allocate_memory(size_t size);

    template <typename T>
    inline T* heapify(T value) {
        auto pointer = (T*)allocate_memory(sizeof(T));

        *pointer = value;

        return pointer;
    }

    template <typename T>
    inline T* allocate(size_t count) {
        return (T*)allocate_memory(sizeof(T) * count);
    }

    template <typename T, typename... Args>
    inline T* allocate_and_construct(Args... args) {
        auto pointer = (T*)allocate_memory(sizeof(T));

        *pointer = T(args...);

        return pointer;
    }
};