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
#include "list.h"
#include "jobs.h"

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
    fprintf(file, "  -print-ast  Print abstract syntax tree\n");
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

    auto print_ast = false;
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
        } else if(strcmp(argument, "-print-ast") == 0) {
            print_ast = true;
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

    List<Job*> jobs {};

    auto main_file_parse_job = new ParseFile;
    main_file_parse_job->done = false;
    main_file_parse_job->waiting_for = nullptr;
    main_file_parse_job->path = absolute_source_file_path;

    append(&jobs, (Job*)main_file_parse_job);

    while(true) {
        auto all_jobs_done = true;
        for(auto job : jobs) {
            if(!job->done) {
                all_jobs_done = false;

                if(job->waiting_for != nullptr) {
                    auto waiting_for = job->waiting_for;

                    auto waiting_for_done = false;
                    for(auto job : jobs) {
                        if(job == waiting_for) {
                            waiting_for_done = job->done;
                        }
                    }

                    if(!waiting_for_done) {
                        continue;
                    }
                }

                switch(job->kind) {
                    case JobKind::ParseFile: {
                        auto parse_file = (ParseFile*)job;

                        expect(tokens, tokenize_source(parse_file->path));
                        expect(statements, parse_tokens(parse_file->path, tokens));

                        parse_file->statements = statements;
                        parse_file->done = true;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        }

        if(all_jobs_done) {
            break;
        }
    }

    for(auto job : jobs) {
        if(job->waiting_for != nullptr) {
            fprintf(stderr, "Error: Circular dependency detected\n");

            return false;
        }
    }

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