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

    fprintf(
        file,
        "]},\"profiles\":[{\"type\":\"evented\",\"name\":\"simple-compiler\",\"unit\":\"none\",\"startValue\":%llu,\"endValue\":%llu,\"events\":[",
        entries[0].time,
        entries[entries.count - 1].time
    );

    for(size_t i = 0; i < entries.count; i += 1) {
        auto frame_entry = entries[i];

        if(frame_entry.is_exit) {
            fprintf(file, "{\"type\":\"C\",\"frame\":%zu,\"at\":%llu}", frame_entry.type, frame_entry.time);
        } else {
            fprintf(file, "{\"type\":\"O\",\"frame\":%zu,\"at\":%llu}", frame_entry.type, frame_entry.time);
        }

        if(i != entries.count - 1) {
            fprintf(file, ",");
        }
    }

    fprintf(file, "]}]}");

    fclose(file);

    printf("Done!\n");
    printf("Profiler buffer use: %.2f%%\n", (double)buffer_used / profiler_buffer_size * 100);
}