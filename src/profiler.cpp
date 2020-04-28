#include "profiler.h"
#include <stdio.h>
#include <string.h>
#include "platform.h"

// Allocate 10MB of .bss space for the profiler record buffer
const size_t profiler_buffer_size = 1024 * 1024 * 10;
uint8_t profiler_buffer[profiler_buffer_size];

uint8_t *profiler_buffer_pointer;

bool read_performance_frequency;
uint64_t performance_frequency;


#if defined(OS_WINDOWS)

#include <Windows.h>

void init_profiler() {
    profiler_buffer_pointer = profiler_buffer;

    read_performance_frequency = false;

    HKEY key;
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type;
        DWORD value;
        DWORD dword_size = sizeof(DWORD);
        if(
            RegQueryValueExA(key, "~MHz", 0, &type, (LPBYTE)&value, &dword_size) == ERROR_SUCCESS &&
            type == REG_DWORD
        ) {
            read_performance_frequency = true;
            performance_frequency = (uint64_t)value * 1000000;
        }
    }
}

#endif

static void dump_profile_internal(uint8_t **pointer, unsigned int indentation_level, uint64_t entry_performance_counter) {
    while((*pointer) < profiler_buffer_pointer) {
        if((*pointer)[0] == 0) {
            auto function_name = *(const char**)&(*pointer)[1];

            auto performance_counter = *(uint64_t*)&(*pointer)[1 + sizeof(const char*)];

            for(unsigned int i = 0; i < indentation_level; i += 1) {
                printf(" ");
            }

            printf("%s\n", function_name);

            *pointer = &(*pointer)[1 + sizeof(const char*) + sizeof(uint64_t)];

            dump_profile_internal(pointer, indentation_level + 1, performance_counter);
        } else {
            auto performance_counter = *(uint64_t*)&(*pointer)[1];

            *pointer = &(*pointer)[1 + sizeof(uint64_t)];

            for(unsigned int i = 0; i < indentation_level - 1; i += 1) {
                printf(" ");
            }

            auto ticks = performance_counter - entry_performance_counter;

            if(read_performance_frequency) {
                if(ticks < performance_frequency / 1000000) {
                    printf("%f ns\n",  (double)ticks / performance_frequency * 1000000000);
                } else if(ticks < performance_frequency / 1000) {
                    printf("%f us\n",  (double)ticks / performance_frequency * 1000000);
                } else if(ticks < performance_frequency) {
                    printf("%f ms\n",  (double)ticks / performance_frequency * 1000);
                } else {
                    printf("%f s\n",  (double)ticks / performance_frequency);
                }
            } else {
                printf("%llu clocks\n", ticks);
            }

            return;
        }
    }
}

void dump_profile() {
    auto pointer = profiler_buffer;

    while(pointer < profiler_buffer_pointer) {
        auto function_name = *(const char**)&pointer[1];

        auto performance_counter = *(uint64_t*)&pointer[1 + sizeof(const char*)];

        printf("%s\n", function_name);

        pointer = &pointer[1 + sizeof(const char*) + sizeof(uint64_t)];

        dump_profile_internal(&pointer, 1, performance_counter);
    }

    auto buffer_used = (size_t)profiler_buffer_pointer - (size_t)profiler_buffer;

    printf("Profiler buffer use: %.2f%%\n", (double)buffer_used / profiler_buffer_size * 100);
}