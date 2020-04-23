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

static const char *get_host_architecture() {
#if defined(ARCH_X64)
    return "x64";
#endif
}

static const char *get_host_os() {
#if defined(OS_LINUX)
    return "linux";
#elif defined(OS_WINDOWS)
    return "windows";
#endif
}

static const char *get_default_output_file(const char *os) {
    if(strcmp(os, "windows") == 0) {
        return "out.exe";
    } else {
        return "out";
    }
}

static void print_help_message(FILE *file) {
    fprintf(file, "Usage: compiler [options] <source file>\n\n");

    auto default_os = get_host_os();

    fprintf(file, "Options:\n");
    fprintf(file, "  -output <output file>  (default: %s) Specify executable file path\n", get_default_output_file(default_os));
    fprintf(file, "  -arch x64  (default: %s) Specify CPU architecture to target\n", get_host_architecture());
    fprintf(file, "  -os windows|linux  (default: %s) Specify operating system to target\n", default_os);
    fprintf(file, "  -config debug|release  (default: debug) Specify build configuration\n");
    fprintf(file, "  -print-ir  Print internal intermediate representation\n");
    fprintf(file, "  -help  Display this help message then exit\n");
}

bool cli_entry(Array<const char*> arguments) {
    auto start_time = clock();

    const char *source_file_path = nullptr;
    const char *output_file_path = nullptr;

    auto architecture = get_host_architecture();

    auto os = get_host_os();

    auto config = "debug";

    auto print_ir = false;

    int argument_index = 1;
    while(argument_index < arguments.count) {
        auto argument = arguments[argument_index];

        if(argument_index == arguments.count - 1 && argument[0] != '-') {
            source_file_path = argument;
        } else if(strcmp(argument, "-output") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Error: Missing value for '-output' option\n\n");
                print_help_message(stderr);

                return false;
            }

            output_file_path = arguments[argument_index];
        } else if(strcmp(argument, "-arch") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Error: Missing value for '-arch' option\n\n");
                print_help_message(stderr);

                return false;
            }

            architecture = arguments[argument_index];
        } else if(strcmp(argument, "-os") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Error: Missing value for '-os' option\n\n");
                print_help_message(stderr);

                return false;
            }

            os = arguments[argument_index];
        } else if(strcmp(argument, "-config") == 0) {
            argument_index += 1;

            if(argument_index == arguments.count - 1) {
                fprintf(stderr, "Error: Missing value for '-config' option\n\n");
                print_help_message(stderr);

                return false;
            }

            config = arguments[argument_index];
        } else if(strcmp(argument, "-print-ir") == 0) {
            print_ir = true;
        } else if(strcmp(argument, "-help") == 0) {
            print_help_message(stdout);

            return true;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n\n", argument);
            print_help_message(stderr);

            return false;
        }

        argument_index += 1;
    }

    if(
        strcmp(config, "debug") != 0 &&
        strcmp(config, "release") != 0
    ) {
        fprintf(stderr, "Error: Unknown config '%s'\n\n", config);
        print_help_message(stderr);

        return false;
    }

    if(
        strcmp(os, "linux") != 0 &&
        strcmp(os, "windows") != 0
    ) {
        fprintf(stderr, "Error: Unknown OS '%s'\n\n", os);
        print_help_message(stderr);

        return false;
    }

    if(strcmp(architecture, "x64") != 0) {
        fprintf(stderr, "Error: Unknown architecture '%s'\n\n", architecture);
        print_help_message(stderr);

        return false;
    }

    if(source_file_path == nullptr) {
        fprintf(stderr, "Error: No source file provided\n\n");
        print_help_message(stderr);

        return false;
    }

    expect(absolute_source_file_path, path_relative_to_absolute(source_file_path));

    if(output_file_path == nullptr) {
        output_file_path = get_default_output_file(os);
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