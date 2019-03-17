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

    auto path = argv[1];
    auto file = fopen(path, "rb");

    if(file == NULL) {
        fprintf(stderr, "Unable to read source file: %s\n", strerror(errno));

        return EXIT_FAILURE;
    }

    auto parser_result = parse_source(path, file);

    if(!parser_result.status) {
        return EXIT_FAILURE;
    }

    auto generator_result = generate_c_source(parser_result.top_level_statements, parser_result.top_level_statement_count);

    if(!generator_result.status) {
        return EXIT_FAILURE;
    }

    printf("%s\n", generator_result.source);

    return EXIT_SUCCESS;
}