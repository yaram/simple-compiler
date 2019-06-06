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
    
    auto parser_result = parse_source(source_file_path);

    if(!parser_result.status) {
        return EXIT_FAILURE;
    }

    auto generator_result = generate_c_source(parser_result.value);

    if(!generator_result.status) {
        return EXIT_FAILURE;
    }

    auto output_file = fopen("out.c", "w");

    fprintf(output_file, "%s", generator_result.value);

    fclose(output_file);

    if(system("clang -o out out.c") != 0){
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}