#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "parser.h"
#include "generator.h"
#include "util.h"
#include "platform.h"
#include "path.h"

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "No source file provided\n");

        return EXIT_FAILURE;
    }

    auto source_file_path = argv[1];

    clock_t total_time = 0;

    Array<File> files;
    {
        auto start_time = clock();
        
        auto result = parse_source(source_file_path);

        if(!result.status) {
            return EXIT_FAILURE;
        }

        files = result.value;

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("Parser time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    CSource c_source;
    {
        auto start_time = clock();

        auto result = generate_c_source(files);

        if(!result.status) {
            return EXIT_FAILURE;
        }

        c_source = result.value;

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("Generator time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    const char *source_file_name;
    {
        auto full_name = path_get_file_component(source_file_path);

        auto dot_pointer = strchr(full_name, '.');

        if(dot_pointer == nullptr) {
            source_file_name = full_name;
        } else {
            auto length = (size_t)full_name - (size_t)dot_pointer;

            auto buffer = allocate<char>(length + 1);

            strncpy(buffer, full_name, length);

            source_file_name = buffer;
        }
    }

    {
        auto start_time = clock();

        char *c_file_path_buffer{};

        string_buffer_append(&c_file_path_buffer, source_file_name);
        string_buffer_append(&c_file_path_buffer, ".c");

        auto c_file = fopen(c_file_path_buffer, "w");

        if(c_file == nullptr) {
            fprintf(stderr, "Unable to create C output file\n");

            return EXIT_FAILURE;
        }

        fprintf(c_file, "%s", c_source.source);

        fclose(c_file);

        char *command_buffer{};

        string_buffer_append(&command_buffer, "clang -c -o ");

        string_buffer_append(&command_buffer, source_file_name);
        string_buffer_append(&command_buffer, ".o ");

        string_buffer_append(&command_buffer, c_file_path_buffer);

        if(system(command_buffer) != 0) {
            return EXIT_FAILURE;
        }

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("Backend time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    {
        auto start_time = clock();

        char *buffer{};

        string_buffer_append(&buffer, "clang -fuse-ld=lld -o ");

        string_buffer_append(&buffer, source_file_name);

#if defined(PLATFORM_WINDOWS)
        string_buffer_append(&buffer, ".exe");
#endif
        
        for(auto library : c_source.libraries) {
            string_buffer_append(&buffer, " -l");

            string_buffer_append(&buffer, library);
        }

        string_buffer_append(&buffer, " ");
        string_buffer_append(&buffer, source_file_name);
        string_buffer_append(&buffer, ".o");

        if(system(buffer) != 0) {
            return EXIT_FAILURE;
        }

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("Linker time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    printf("Total time: %.1fms\n", (double)total_time / CLOCKS_PER_SEC * 1000);

    return EXIT_SUCCESS;
}