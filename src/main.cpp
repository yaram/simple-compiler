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

    size_t function_count = 0;
    size_t static_variable_count = 0;

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

                    job->waiting_for = nullptr;
                }

                switch(job->kind) {
                    case JobKind::ParseFile: {
                        auto parse_file = (ParseFile*)job;

                        expect(tokens, tokenize_source(parse_file->path));

                        expect(statements, parse_tokens(parse_file->path, tokens));

                        auto scope = new ConstantScope;
                        scope->statements = statements;
                        scope->constant_parameters = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        for(auto statement : statements) {
                            switch(statement->kind) {
                                case StatementKind::FunctionDeclaration: {
                                    auto function_declaration = (FunctionDeclaration*)statement;

                                    auto is_polymorphic = false;
                                    for(auto parameter : function_declaration->parameters) {
                                        if(parameter.is_polymorphic_determiner || parameter.is_constant) {
                                            is_polymorphic = true;
                                            break;
                                        }
                                    }

                                    if(!is_polymorphic) {
                                        auto resolve_declaration = new ResolveDeclaration;
                                        resolve_declaration->done = false;
                                        resolve_declaration->waiting_for = nullptr;
                                        resolve_declaration->declaration = function_declaration;
                                        resolve_declaration->parameters = {};
                                        resolve_declaration->scope = scope;

                                        append(&jobs, (Job*)resolve_declaration);
                                    }
                                } break;

                                case StatementKind::ConstantDefinition: {
                                    auto constant_definition = (ConstantDefinition*)statement;

                                    auto resolve_declaration = new ResolveDeclaration;
                                    resolve_declaration->done = false;
                                    resolve_declaration->waiting_for = nullptr;
                                    resolve_declaration->declaration = constant_definition;
                                    resolve_declaration->parameters = {};
                                    resolve_declaration->scope = scope;

                                    append(&jobs, (Job*)resolve_declaration);
                                } break;

                                case StatementKind::StructDefinition: {
                                    auto struct_definition = (StructDefinition*)statement;

                                    if(struct_definition->parameters.count == 0) {
                                        auto resolve_declaration = new ResolveDeclaration;
                                        resolve_declaration->done = false;
                                        resolve_declaration->waiting_for = nullptr;
                                        resolve_declaration->declaration = struct_definition;
                                        resolve_declaration->parameters = {};
                                        resolve_declaration->scope = scope;

                                        append(&jobs, (Job*)resolve_declaration);
                                    }
                                } break;

                                case StatementKind::VariableDeclaration: {
                                    auto variable_declaration = (VariableDeclaration*)statement;

                                    StringBuffer name_buffer {};
                                    string_buffer_append(&name_buffer, "variable_");
                                    string_buffer_append(&name_buffer, static_variable_count);

                                    auto generate_static_variable = new GenerateStaticVariable;
                                    generate_static_variable->done = false;
                                    generate_static_variable->waiting_for = false;
                                    generate_static_variable->declaration = variable_declaration;
                                    generate_static_variable->name = name_buffer.data;
                                    generate_static_variable->scope = scope;

                                    append(&jobs, (Job*)generate_static_variable);

                                    static_variable_count += 1;
                                } break;

                                case StatementKind::ExpressionStatement:
                                case StatementKind::Assignment:
                                case StatementKind::BinaryOperationAssignment:
                                case StatementKind::IfStatement:
                                case StatementKind::WhileLoop:
                                case StatementKind::ForLoop:
                                case StatementKind::ReturnStatement:
                                case StatementKind::BreakStatement: {
                                    error(*scope, statement->range, "This kind of statement cannot be top-level");

                                    return false;
                                } break;

                                case StatementKind::Import: {
                                    auto import = (Import*)statement;

                                    auto source_file_directory = path_get_directory_component(scope->file_path);

                                    StringBuffer import_file_path {};

                                    string_buffer_append(&import_file_path, source_file_directory);
                                    string_buffer_append(&import_file_path, import->path);

                                    expect(import_file_path_absolute, path_relative_to_absolute(import_file_path.data));

                                    auto job_already_added = false;
                                    for(auto job : jobs) {
                                        if(job->kind == JobKind::ParseFile) {
                                            auto parse_file = (ParseFile*)job;

                                            if(strcmp(parse_file->path, import_file_path_absolute) == 0) {
                                                job_already_added = true;
                                                break;
                                            }
                                        }
                                    }

                                    if(!job_already_added) {
                                        auto parse_file = new ParseFile;
                                        parse_file->done = false;
                                        parse_file->waiting_for = nullptr;
                                        parse_file->path = import_file_path_absolute;

                                        append(&jobs, (Job*)parse_file);
                                    }
                                } break;

                                case StatementKind::UsingStatement: break;

                                default: abort();
                            }
                        }

                        parse_file->statements = statements;
                        parse_file->done = true;
                    } break;

                    case JobKind::ResolveDeclaration: {

                    } break;

                    case JobKind::GenerateFunction: {

                    } break;

                    case JobKind::GenerateStaticVariable: {

                    } break;

                    default: abort();
                }
            }
        }

        if(all_jobs_done) {
            break;
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