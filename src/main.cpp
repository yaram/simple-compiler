#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "lexer.h"
#include "parser.h"
#include "generator.h"
#include "c_backend.h"
#include "util.h"
#include "platform.h"
#include "path.h"

bool cli_entry(Array<const char*> arguments) {
    auto start_time = clock();

    const char *source_file_path = nullptr;
    const char *output_file_path = nullptr;

#if defined(ARCH_X64)
    const char *architecture = "x64";
#endif

#if defined(OS_LINUX)
    const char *os = "linux";
#elif defined(OS_WINDOWS)
    const char *os = "windows";
#endif

    auto config = "debug";

    auto print_ir = false;

    int argument_index = 1;
    while(argument_index < arguments.count) {
        auto argument = arguments[argument_index];

        if(argument_index == arguments.count - 1) {
            source_file_path = argument;
        } else if(strcmp(argument, "-output") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Missing value for '-output' option\n");

                return false;
            }

            output_file_path = arguments[argument_index];
        } else if(strcmp(argument, "-arch") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Missing value for '-arch' option\n");

                return false;
            }

            architecture = arguments[argument_index];
        } else if(strcmp(argument, "-os") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Missing value for '-os' option\n");

                return false;
            }

            os = arguments[argument_index];
        } else if(strcmp(argument, "-config") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Missing value for '-config' option\n");

                return false;
            }

            config = arguments[argument_index];
        } else if(strcmp(argument, "-print-ir") == 0) {
            print_ir = true;
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argument);

            return false;
        }

        argument_index += 1;
    }

    if(
        strcmp(config, "debug") != 0 &&
        strcmp(config, "release") != 0
    ) {
        fprintf(stderr, "Unknown config '%s'\n", config);

        return false;
    }

    if(
        strcmp(os, "linux") != 0 &&
        strcmp(os, "windows") != 0
    ) {
        fprintf(stderr, "Unknown OS '%s'\n", os);

        return false;
    }

    if(strcmp(architecture, "x64") != 0) {
        fprintf(stderr, "Unknown architecture '%s'\n", architecture);

        return false;
    }

    if(source_file_path == nullptr) {
        fprintf(stderr, "No source file provided\n");

        return false;
    }

    expect(absolute_source_file_path, path_relative_to_absolute(source_file_path));

    if(output_file_path == nullptr) {
        if(strcmp(os, "windows") == 0) {
            output_file_path = "out.exe";
        } else {
            output_file_path = "out";
        }
    }

    expect(source_file_tokens, tokenize_source(absolute_source_file_path));

    expect(source_file_statements, parse_tokens(absolute_source_file_path, source_file_tokens));

    auto register_sizes = get_register_sizes(architecture);

    expect(ir, generate_ir(absolute_source_file_path, source_file_statements, register_sizes.address_size, register_sizes.default_size));

    if(print_ir) {
        for(auto runtime_static : ir.statics) {
            print_static(runtime_static);
            printf("\n");
        }
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

    generate_c_object(ir.statics, architecture, os, config, output_file_directory, output_file_name);

    {
        char *buffer{};

        const char *linker_options;
        if(strcmp(os, "windows") == 0) {
            if(strcmp(config, "debug") == 0) {
                linker_options = "/entry:main,/DEBUG";
            } else if(strcmp(config, "release") == 0) {
                linker_options = "/entry:main";
            } else {
                abort();
            }
        } else {
            linker_options = "--entry=main";
        }

        auto triple = get_llvm_triple(architecture, os);

        string_buffer_append(&buffer, "clang -nostdlib -fuse-ld=lld -target ");

        string_buffer_append(&buffer, triple);

        string_buffer_append(&buffer, " -Wl,");
        string_buffer_append(&buffer, linker_options);

        string_buffer_append(&buffer, " -o");
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
            return false;
        }
    }

    auto end_time = clock();

    auto total_time = end_time - start_time;

    printf("Total time: %.1fms\n", (double)total_time / CLOCKS_PER_SEC * 1000);

    return true;
}

int main(int argument_count, const char *arguments[]) {
    if(cli_entry({ (size_t)argument_count, arguments })) {
        return 0;
    } else {
        return 1;
    }
}