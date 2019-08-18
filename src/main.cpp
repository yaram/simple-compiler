#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "parser.h"
#include "generator.h"
#include "util.h"
#include "platform.h"

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "No source file provided\n");

        return EXIT_FAILURE;
    }

    auto source_file_path = argv[1];

    auto parser_start_time = clock();
    
    auto parser_result = parse_source(source_file_path);

    if(!parser_result.status) {
        return EXIT_FAILURE;
    }

    auto parser_end_time = clock();

    auto parser_time = parser_end_time - parser_start_time;

    printf("Parser time: %.1fms\n", (double)parser_time / CLOCKS_PER_SEC * 1000);

    auto generator_start_time = clock();

    auto generator_result = generate_c_source(parser_result.value);

    if(!generator_result.status) {
        return EXIT_FAILURE;
    }

    auto generator_end_time = clock();

    auto generator_time = generator_end_time - generator_start_time;

    printf("Generator time: %.1fms\n", (double)generator_time / CLOCKS_PER_SEC * 1000);

    auto output_file = fopen("out.c", "w");

    fprintf(output_file, "%s", generator_result.value.source);

    fclose(output_file);

    char *buffer{};

#if defined(PLATFORM_UNIX)
    string_buffer_append(&buffer, "clang -o out ");
#elif defined(PLATFORM_WINDOWS)
    string_buffer_append(&buffer, "clang -o out.exe ");
#endif

    for(auto library : generator_result.value.libraries) {
        string_buffer_append(&buffer, "-l");

        string_buffer_append(&buffer, library);

        string_buffer_append(&buffer, " ");
    }

    string_buffer_append(&buffer, "out.c");

    auto backend_start_time = clock();

    if(system(buffer) != 0) {
        return EXIT_FAILURE;
    }

    auto backend_end_time = clock();

    auto backend_time = backend_end_time - backend_start_time;

    printf("Backend time: %.1fms\n", (double)backend_time / CLOCKS_PER_SEC * 1000);

    auto total_time = parser_time + generator_time + backend_end_time;

    printf("Total time: %.1fms\n", (double)total_time / CLOCKS_PER_SEC * 1000);

    return EXIT_SUCCESS;
}