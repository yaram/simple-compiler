#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "parser.h"
#include "ir_generator.h"
#include "c_generator.h"
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

    ArchitectureInfo architecture_info {
        RegisterSize::Size64,
        RegisterSize::Size64
    };

    IR ir;
    {
        auto start_time = clock();

        auto result = generate_ir(files, architecture_info);

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

    const char *c_source;
    {
        auto start_time = clock();

        auto result = generate_c_source(ir.functions, ir.constants, architecture_info);

        if(!result.status) {
            return EXIT_FAILURE;
        }

        c_source = result.value;

        auto end_time = clock();

        auto time = end_time - start_time;

        printf("C backend time: %.1fms\n", (double)time / CLOCKS_PER_SEC * 1000);

        total_time += time;
    }

    const char *output_file_name;
    {
        auto full_name = path_get_file_component(output_file_path);

        auto dot_pointer = strchr(full_name, '.');

        if(dot_pointer == nullptr) {
            output_file_name = full_name;
        } else {
            auto length = (size_t)dot_pointer - (size_t)full_name;

            if(length == 0) {
                output_file_name = "out";
            } else {
                auto buffer = allocate<char>(length + 1);

                memcpy(buffer, full_name, length);
                buffer[length] = 0;

                output_file_name = buffer;
            }
        }
    }

    auto output_file_directory = path_get_directory_component(output_file_path);

    {
        auto start_time = clock();

        char *c_file_path_buffer{};

        string_buffer_append(&c_file_path_buffer, output_file_directory);
        string_buffer_append(&c_file_path_buffer, output_file_name);
        string_buffer_append(&c_file_path_buffer, ".c");

        auto c_file = fopen(c_file_path_buffer, "w");

        if(c_file == nullptr) {
            fprintf(stderr, "Unable to create C output file\n");

            return EXIT_FAILURE;
        }

        fprintf(c_file, "%s", c_source);

        fclose(c_file);

        char *command_buffer{};

        string_buffer_append(&command_buffer, "clang -ffreestanding -w -nostdinc -c -o ");

        string_buffer_append(&command_buffer, output_file_directory);
        string_buffer_append(&command_buffer, output_file_name);
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

#if defined(PLATFORM_UNIX)
        string_buffer_append(&buffer, "clang -nostdlib -Wl,--entry=main -fuse-ld=lld -o ");
#elif defined(PLATFORM_WINDOWS)
        string_buffer_append(&buffer, "clang -nostdlib -Wl,/entry:main -fuse-ld=lld -o ");
#endif

        string_buffer_append(&buffer, output_file_path);
        
        for(auto library : ir.libraries) {
            string_buffer_append(&buffer, " -l");

            string_buffer_append(&buffer, library);
        }

        string_buffer_append(&buffer, " ");
        string_buffer_append(&buffer, output_file_directory);
        string_buffer_append(&buffer, output_file_name);
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