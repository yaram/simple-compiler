#include "arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

const size_t maximum_alignment = alignof(max_align_t);
const size_t chunk_granularity = 1024;

inline size_t divide_round_up(size_t left, size_t right) {
    return (left + right - 1) / right;
}

void Arena::reset() {
    auto chunk_header_aligned_size = divide_round_up(sizeof(ArenaChunkHeader), maximum_alignment) * maximum_alignment;

    current_chunk = first_chunk;
    current_offset = chunk_header_aligned_size;
}

void Arena::free() {
    auto chunk = first_chunk;
    while(chunk != nullptr) {
        auto next_chunk = chunk->next_chunk;

        ::free(chunk);

        chunk = next_chunk;
    }
}

void* Arena::allocate_memory(size_t size) {
    auto chunk_header_aligned_size = divide_round_up(sizeof(ArenaChunkHeader), maximum_alignment) * maximum_alignment;

    auto aligned_size = divide_round_up(size, maximum_alignment) * maximum_alignment;
    auto minimum_chunk_size = divide_round_up(chunk_header_aligned_size + aligned_size, chunk_granularity) * chunk_granularity;

    ArenaChunkHeader* previous_chunk = nullptr;
    while(current_chunk != nullptr) {
        if(current_offset + aligned_size > current_chunk->size) {
            previous_chunk = current_chunk;
            current_chunk = current_chunk->next_chunk;
            current_offset = chunk_header_aligned_size;
        } else {
            break;
        }
    }

    if(current_chunk == nullptr) {
        auto new_chunk = (ArenaChunkHeader*)malloc(minimum_chunk_size);

        new_chunk->next_chunk = nullptr;
        new_chunk->size = minimum_chunk_size;

        if(previous_chunk == nullptr) {
            first_chunk = new_chunk;
        } else {
            previous_chunk->next_chunk = new_chunk;
        }

        current_chunk = new_chunk;
        current_offset = chunk_header_aligned_size;
    }

    auto result_address = (size_t)current_chunk + current_offset;

    current_offset += aligned_size;

    return (void*)result_address;
}