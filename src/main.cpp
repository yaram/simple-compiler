#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "parser.h"

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

    auto result = parse_source(path, file);

    if(!result.status) {
        return EXIT_FAILURE;
    }

    for(auto i = 0; i < result.top_level_statement_count; i += 1) {
        debug_print_statement(result.top_level_statements[i]);
        
        if(i != result.top_level_statement_count - 1) {
            printf(",");
            printf("\n");
        }
    }

    return EXIT_SUCCESS;
}