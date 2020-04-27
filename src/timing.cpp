#include "timing.h"
#include <assert.h>
#include "platform.h"

#if defined(OS_LINUX)

#include <time.h>

uint64_t get_timer_counts_per_second() {;
    return 1000000000;
}

uint64_t get_timer_counts() {
    timespec time;
    auto result = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(result == 0);

    return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}

#elif defined(OS_WINDOWS)

#include <Windows.h>

uint64_t get_timer_counts_per_second() {
    LARGE_INTEGER performance_frequency;
    auto success = QueryPerformanceFrequency(&performance_frequency);
    assert(success);

    return (uint64_t)performance_frequency.QuadPart;
}

uint64_t get_timer_counts() {
    LARGE_INTEGER performance_counter;
    auto success = QueryPerformanceCounter(&performance_counter);
    assert(success);

    return (uint64_t)performance_counter.QuadPart;
}

#endif
