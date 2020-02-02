#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "parser.h"
#include "ir_generator.h"
#include "util.h"
#include "platform.h"
#include "path.h"

int main(int argument_count, char *arguments[]) {
    const char *source_file_path = nullptr;

#if defined(PLATFORM_UNIX)
    const char *output_file_path = "out";
#elif defined(PLATFORM_WINDOWS)
    const char *output_file_path = "out.exe";
#endif

    auto print_ast = false;
    auto print_ir = false;

    int argument_index = 1;
    while(argument_index < argument_count) {
        auto argument = arguments[argument_index];

        if(argument_index == argument_count - 1) {
            source_file_path = argument;
        } else if(strcmp(argument, "--output") == 0) {
            argument_index += 1;

            if(argument_index == argument_count - 1) {
                fprintf(stderr, "Missing value for '--output' option\n");

                return EXIT_FAILURE;
            }

            output_file_path = arguments[argument_index];
        } else if(strcmp(argument, "--print-ast") == 0) {
            print_ast = true;
        } else if(strcmp(argument, "--print-ir") == 0) {
            print_ir = true;
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argument);

            return EXIT_FAILURE;
        }

        argument_index += 1;
    }

    if(source_file_path == nullptr) {
        fprintf(stderr, "No source file provided\n");

        return EXIT_FAILURE;
    }

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

    if(print_ast) {
        for(auto file : files) {
            for(auto statement : file.statements) {
                print_statement(statement);

                printf("\n");
            }
        }
    }

    IR ir;
    {
        auto start_time = clock();

        auto result = generate_ir(files);

        if(!result.status) {
            return EXIT_FAILURE;
        }

        ir = result.value;

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("Generator time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    if(print_ir) {
        for(auto function : ir.functions) {
            print_function(function);
            printf("\n");
        }
    }

    printf("Total time: %.1fms\n", (double)total_time / CLOCKS_PER_SEC * 1000);

    return EXIT_SUCCESS;
}