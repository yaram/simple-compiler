#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "timing.h"
#include "profiler.h"
#include "lexer.h"
#include "parser.h"
#include "generator.h"
#include "c_backend.h"
#include "util.h"
#include "platform.h"
#include "path.h"

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
    enter_function_region();

    auto start_time = get_timer_counts();

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

    if(!does_os_exist(os)) {
        fprintf(stderr, "Error: Unknown OS '%s'\n\n", os);
        print_help_message(stderr);

        return false;
    }

    if(!does_architecture_exist(architecture)) {
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

    auto main_file_parser_start = get_timer_counts();

    expect(source_file_tokens, tokenize_source(absolute_source_file_path));

    expect(source_file_statements, parse_tokens(absolute_source_file_path, source_file_tokens));

    auto main_file_parser_end = get_timer_counts();

    auto main_file_parser_time = main_file_parser_end - main_file_parser_start;

    auto register_sizes = get_register_sizes(architecture);

    expect(generator_result, generate_ir(absolute_source_file_path, source_file_statements, register_sizes.address_size, register_sizes.default_size));

    if(print_ir) {
        for(auto runtime_static : generator_result.statics) {
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

    auto backend_start = get_timer_counts();

    generate_c_object(generator_result.statics, architecture, os, config, output_file_directory, output_file_name);

    auto backend_end = get_timer_counts();

    auto backend_time = backend_end - backend_start;

    uint64_t linker_time;
    {
        StringBuffer buffer {};

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
        
        for(auto library : generator_result.libraries) {
            string_buffer_append(&buffer, " -l");
            string_buffer_append(&buffer, library);
        }

        string_buffer_append(&buffer, " ");
        string_buffer_append(&buffer, output_file_directory);
        string_buffer_append(&buffer, output_file_name);
        string_buffer_append(&buffer, ".o");

        auto start = get_timer_counts();

        enter_region("linker");

        if(system(buffer.data) != 0) {
            return false;
        }

        leave_region();

        auto end = get_timer_counts();

        linker_time = end - start;
    }

    auto end_time = get_timer_counts();

    auto total_time = end_time - start_time;

    auto counts_per_second = get_timer_counts_per_second();

    printf("Total time: %.2fms\n", (double)total_time / counts_per_second * 1000);
    printf("  Parser time: %.2fms\n", (double)(main_file_parser_time + generator_result.parser_time) / counts_per_second * 1000);
    printf("  Generator time: %.2fms\n", (double)generator_result.generator_time / counts_per_second * 1000);
    printf("  C Backend time: %.2fms\n", (double)backend_time / counts_per_second * 1000);
    printf("  Linker time: %.2fms\n", (double)linker_time / counts_per_second * 1000);

    leave_region();

    return true;
}

int main(int argument_count, const char *arguments[]) {
#if defined(PROFILING)
    init_profiler();
#endif

    enter_function_region();

    if(cli_entry({ (size_t)argument_count, arguments })) {
        leave_region();

#if defined(PROFILING)
        dump_profile();
#endif

        return 0;
    } else {
        return 1;
    }
}