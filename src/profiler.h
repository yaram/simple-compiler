#pragma once

#if defined(PROFILING)

#include <stdint.h>
#include <memory.h>
#include "platform.h"

#define static_profiled_function(return_type, name, parameters, parameter_names) \
    inline return_type __##name##_internal##parameters; \
    static return_type name##parameters{ \
        enter_region(__FUNCTION__); \
        return_type result = __##name##_internal##parameter_names; \
        leave_region(); \
        return result; \
    } \
    inline return_type __##name##_internal##parameters

#define static_profiled_function_void(name, parameters, parameter_names) \
    inline void __##name##_internal##parameters; \
    static void name##parameters{ \
        enter_region(__FUNCTION__); \
        __##name##_internal##parameter_names; \
        leave_region(); \
    } \
    inline void __##name##_internal##parameters

#define profiled_function(return_type, name, parameters, parameter_names) \
    inline return_type __##name##_internal##parameters; \
    return_type name##parameters{ \
        enter_region(__FUNCTION__); \
        return_type result = __##name##_internal##parameter_names; \
        leave_region(); \
        return result; \
    } \
    inline return_type __##name##_internal##parameters

#define profiled_function_void(name, parameters, parameter_names) \
    inline void __##name##_internal##parameters; \
    void name##parameters{ \
        enter_region(__FUNCTION__); \
        __##name##_internal##parameter_names; \
        leave_region(); \
    } \
    inline void __##name##_internal##parameters

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

inline void enter_region(const char *name) {
    // Write record type 0 (region entry)
    profiler_buffer_pointer[0] = 0;

    // Write region name pointer
    *((const char**)&profiler_buffer_pointer[1]) = name;

    // Read performance counter
    uint64_t performance_counter = read_performance_counter();

    // Write performance counter
    *((uint64_t*)&profiler_buffer_pointer[1 + sizeof(const char*)]) = performance_counter;

    // Increment buffer pointer
    profiler_buffer_pointer = &profiler_buffer_pointer[1 + sizeof(const char*) + sizeof(uint64_t)];
}

inline void leave_region() {
    // Write record type 1 (region exit)
    profiler_buffer_pointer[0] = 1;

    // Read performance counter
    uint64_t performance_counter = read_performance_counter();

    // Write performance counter
    *((uint64_t*)&profiler_buffer_pointer[1]) = performance_counter;

    // Increment buffer pointer
    profiler_buffer_pointer = &profiler_buffer_pointer[1 + sizeof(uint64_t)];
}

#else

#define static_profiled_function(return_type, name, parameters, parameter_names) static return_type name parameters
#define static_profiled_function_void(name, parameters, parameter_names) static void name parameters
#define profiled_function(return_type, name, parameters, parameter_names) return_type name parameters
#define profiled_function_void(name, parameters, parameter_names) void name parameters

#define enter_region(name)
#define leave_region()

#endif