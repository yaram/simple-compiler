#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
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
    
    auto parser_result = parse_source(source_file_path);

    if(!parser_result.status) {
        return EXIT_FAILURE;
    }

    auto generator_result = generate_c_source(parser_result.value);

    if(!generator_result.status) {
        return EXIT_FAILURE;
    }

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

    if(system(buffer) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}