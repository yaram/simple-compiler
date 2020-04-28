#pragma once

#if defined(PROFILING)

#include <stdint.h>
#include <memory.h>
#include "platform.h"

#define enter_function() enter_function_impl(__FUNCTION__)

extern uint8_t *profiler_buffer_pointer;

void init_profiler();
void dump_profile();

#if defined(OS_WINDOWS)

#include <intrin.h>

inline uint64_t read_performance_counter() {
    return __rdtsc();
}

#else

#error Profiling not supported on this OS

#endif

inline void enter_function_impl(const char *function_name) {
    // Write record type 0 (function entry)
    profiler_buffer_pointer[0] = 0;

    // Write function name pointer
    *((const char**)&profiler_buffer_pointer[1]) = function_name;

    // Read performance counter
    uint64_t performance_counter = read_performance_counter();

    // Write performance counter
    *((uint64_t*)&profiler_buffer_pointer[1 + sizeof(const char*)]) = performance_counter;

    // Increment buffer pointer
    profiler_buffer_pointer = &profiler_buffer_pointer[1 + sizeof(const char*) + sizeof(uint64_t)];
}

inline void leave_function() {
    // Write record type 1 (function exit)
    profiler_buffer_pointer[0] = 1;

    // Read performance counter
    uint64_t performance_counter = read_performance_counter();

    // Write performance counter
    *((uint64_t*)&profiler_buffer_pointer[1]) = performance_counter;

    // Increment buffer pointer
    profiler_buffer_pointer = &profiler_buffer_pointer[1 + sizeof(uint64_t)];
}

#else

#define enter_function()
#define leave_function()

#endif