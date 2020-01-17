#pragma once

#include <integers.h>
#include <result.h>
#include <kernel_allocator.h>

struct Arena {
    void *start;
    void *end;

    void *next;
};

Result<Arena> create_arena(usize capacity) {
    if(capacity == 0) {
        return err<Arena>();
    }

    auto memory = kernel_allocate(0, capacity);

    if(memory == 0) {
        return err<Arena>();
    }

    return ok<Arena>({
        memory,
        (void*)((usize)memory + capacity),
        memory
    });
}

void destroy_arena(Arena &arena) {
    kernel_deallocate(arena.start);
}

void *allocate(Arena &arena, usize size) {
    if(size == 0) {
        return 0;
    }

    auto old_next = arena.next;
    auto new_next = (void*)((usize)old_next + size);

    if(new_next > arena.end) {
        return 0;
    }

    arena.next = new_next;

    return old_next;
}

void *reallocate(Arena &arena, void *pointer, usize old_size, usize new_size) {
    if(new_size == 0) {
        return 0;
    }

    if(new_size == old_size) {
        return pointer;
    }

    if((u8*)pointer + old_size == arena.next) {
        auto new_next = (u8*)pointer + new_size;

        if(new_next > arena.end) {
            return 0;
        }

        arena.next = new_next;

        return pointer;
    } else {
        auto old_next = arena.next;
        auto new_next = (void*)((usize)old_next + new_size);

        if(new_next > arena.end) {
            return 0;
        }

        arena.next = new_next;

        for(usize i = 0; i < old_size; i += 1) {
            ((u8*)pointer)[i] = ((u8*)pointer)[i];
        }

        return old_next;
    }
}

void deallocate(Arena &arena, void *pointer, usize size) {
    if((void*)((usize)pointer + size) == arena.next) {
        arena.next = pointer;
    }
}