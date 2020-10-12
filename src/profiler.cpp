#include "profiler.h"
#include <stdio.h>
#include <assert.h>
#include "list.h"

// Allocate 10MB of .bss space for the profiler record buffer
const size_t profiler_buffer_size = 1024 * 1024 * 10;
uint8_t profiler_buffer[profiler_buffer_size];

uint8_t *profiler_buffer_pointer;

bool read_performance_frequency;
uint64_t performance_frequency;

uint64_t start_performance_counter;

#if defined(OS_WINDOWS)

#include <Windows.h>

void init_profiler() {
    start_performance_counter = read_performance_counter();

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

struct SpeedscopeEntry {
    size_t type;

    uint64_t time;

    bool is_exit;
};

static void process_stack_frame_for_speedscope(List<const char*> *type_names, List<SpeedscopeEntry> *entries, size_t *index) {
    assert(profiler_buffer[*index] == 0);

    auto region_name = *(const char**)&profiler_buffer[*index + 1];

    auto performance_counter = *(uint64_t*)&profiler_buffer[*index + 1 + sizeof(const char*)];

    *index += 1 + sizeof(const char*) + sizeof(uint64_t);

    auto found_type = false;
    size_t stack_frame_type;
    for(size_t i = 0; i < type_names->count; i += 1) {
        auto name = (*type_names)[i];

        if(name == region_name) {
            found_type = true;
            stack_frame_type = i;

            break;
        }
    }

    if(!found_type) {
        stack_frame_type = append(type_names, region_name);
    }

    append(entries, {
        stack_frame_type,
        performance_counter,
        false
    });

    while(true) {
        auto buffer_used = (size_t)profiler_buffer_pointer - (size_t)profiler_buffer;
        assert(*index < buffer_used);

        if(profiler_buffer[*index] == 0) {
            process_stack_frame_for_speedscope(type_names, entries, index);
        } else {
            assert(profiler_buffer[*index] == 1);

            auto performance_counter = *(uint64_t*)&profiler_buffer[*index + 1];

            *index += 1 + sizeof(uint64_t);

            append(entries, {
                stack_frame_type,
                performance_counter,
                true
            });

            return;
        }
    }
}

void dump_profile() {
    printf("Generating profiler dump...\n");

    List<const char*> type_names {};
    List<SpeedscopeEntry> entries {};

    size_t index = 0;

    auto buffer_used = (size_t)profiler_buffer_pointer - (size_t)profiler_buffer;

    while(index < buffer_used) {
        assert(profiler_buffer[index] == 0);

        process_stack_frame_for_speedscope(&type_names, &entries, &index);
    }

    auto file = fopen("simple-compiler.speedscope.json", "w");
    assert(file);

    fprintf(file, "{\"version\":\"0.0.1\",\"$schema\":\"https://www.speedscope.app/file-format-schema.json\",\"shared\":{\"frames\":[");

    for(size_t i = 0; i < type_names.count; i += 1) {
        auto name = type_names[i];

        fprintf(file, "{\"name\":\"%s\"}", name);

        if(i != type_names.count - 1) {
            fprintf(file, ",");
        }
    }

    const char *unit;
    if(read_performance_frequency) {
        unit = "nanoseconds";
    } else {
        unit = "none";
    }

    auto start_time = (double)entries[0].time;
    if(read_performance_frequency) {
        start_time = start_time * 1000000000.0 / performance_frequency;
    }

    auto end_time = (double)entries[entries.count - 1].time;
    if(read_performance_frequency) {
        end_time = end_time * 1000000000.0 / performance_frequency;
    }

    fprintf(
        file,
        "]},\"profiles\":[{\"type\":\"evented\",\"name\":\"simple-compiler\",\"unit\":\"%s\",\"startValue\":%f,\"endValue\":%f,\"events\":[",
        unit,
        start_time,
        end_time
    );

    for(size_t i = 0; i < entries.count; i += 1) {
        auto entry = entries[i];

        const char *type;
        if(entry.is_exit) {
            type = "C";
        } else {
            type = "O";
        }

        auto time = (double)entry.time;
        if(read_performance_frequency) {
            time = time * 1000000000 / performance_frequency;
        }

        fprintf(file, "{\"type\":\"%s\",\"frame\":%zu,\"at\":%f}", type, entry.type, time);

        if(i != entries.count - 1) {
            fprintf(file, ",");
        }
    }

    fprintf(file, "]}]}");

    fclose(file);

    printf("Done!\n");
    printf("Profiler buffer use: %.2f%%\n", (double)buffer_used / profiler_buffer_size * 100);
}