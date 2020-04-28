#include "profiler.h"
#include <stdio.h>
#include <assert.h>
#include "list.h"

// Allocate 10MB of .bss space for the profiler record buffer
const size_t profiler_buffer_size = 1024 * 1024 * 10;
uint8_t profiler_buffer[profiler_buffer_size];

uint8_t *profiler_buffer_pointer;

void init_profiler() {
    profiler_buffer_pointer = profiler_buffer;
}

// Output collapsed stack traces suitable for https://github.com/brendangregg/FlameGraph

static void dump_stack_frame(FILE *file, List<const char*> *stack, size_t *index, size_t performance_counter) {
    while(true) {
        assert(stack->count != 0);

        auto buffer_used = (size_t)profiler_buffer_pointer - (size_t)profiler_buffer;
        assert(*index < buffer_used);

        if(profiler_buffer[*index] == 0) {
            auto function_name = *(const char**)&profiler_buffer[*index + 1];

            auto performance_counter = *(uint64_t*)&profiler_buffer[*index + 1 + sizeof(const char*)];

            *index += 1 + sizeof(const char*) + sizeof(uint64_t);

            append(stack, function_name);

            dump_stack_frame(file, stack, index, performance_counter);

            stack->count -= 1;
        } else {
            assert(profiler_buffer[*index] == 1);

            auto end_performance_counter = *(uint64_t*)&profiler_buffer[*index + 1];

            *index += 1 + sizeof(uint64_t);

            for(auto stack_frame : *stack) {
                fprintf(file, "%s;", stack_frame);
            }

            fprintf(file, " %llu\n", end_performance_counter - performance_counter);

            return;
        }
    }
}

void dump_profile() {
    printf("Generating profiler dump...\n");

    auto file = fopen("collapsed_stacks.txt", "w");
    assert(file);

    List<const char*> stack {};

    size_t index = 0;

    auto buffer_used = (size_t)profiler_buffer_pointer - (size_t)profiler_buffer;

    while(index < buffer_used) {
        assert(profiler_buffer[index] == 0);
        assert(stack.count == 0);

        auto function_name = *(const char**)&profiler_buffer[index + 1];

        auto performance_counter = *(uint64_t*)&profiler_buffer[index + 1 + sizeof(const char*)];

        index += 1 + sizeof(const char*) + sizeof(uint64_t);

        append(&stack, function_name);

        dump_stack_frame(file, &stack, &index, performance_counter);

        stack.count -= 1;
    }

    printf("Done!\n");
    printf("Profiler buffer use: %.2f%%\n", (double)buffer_used / profiler_buffer_size * 100);
}