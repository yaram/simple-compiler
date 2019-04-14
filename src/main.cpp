#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "parser.h"
#include "generator.h"

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "No source file provided\n");

        return EXIT_FAILURE;
    }

    auto source_file_path = argv[1];
    auto source_file = fopen(source_file_path, "rb");

    if(source_file == NULL) {
        fprintf(stderr, "Unable to read source file: %s\n", strerror(errno));

        return EXIT_FAILURE;
    }

    auto parser_result = parse_source(source_file_path, source_file);

    if(!parser_result.status) {
        return EXIT_FAILURE;
    }

    auto generator_result = generate_c_source(parser_result.top_level_statements);

    if(!generator_result.status) {
        return EXIT_FAILURE;
    }

    auto output_file = fopen("out.c", "w");

    fprintf(output_file, "%s", generator_result.source);

    fclose(output_file);

    if(system("clang -o out out.c") != 0){
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}