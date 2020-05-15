#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "timing.h"
#include "profiler.h"
#include "lexer.h"
#include "parser.h"
#include "c_backend.h"
#include "util.h"
#include "platform.h"
#include "path.h"
#include "list.h"
#include "jobs.h"
#include "generator.h"
#include "types.h"

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
    fprintf(file, "  -config debug|release  (default: debug) Specify build configuration\n");
    fprintf(file, "  -arch x64  (default: %s) Specify CPU architecture to target\n", get_host_architecture());
    fprintf(file, "  -os windows|linux  (default: %s) Specify operating system to target\n", default_os);
    fprintf(file, "  -print-ast  Print abstract syntax tree\n");
    fprintf(file, "  -print-ir  Print internal intermediate representation\n");
    fprintf(file, "  -help  Display this help message then exit\n");
}

inline void append_global_type(List<GlobalConstant> *global_constants, const char *name, Type *type) {
    append(global_constants, {
        name,
        &type_type_singleton,
        new TypeConstant {
            type
        }
    });
}

inline void append_base_integer_type(List<GlobalConstant> *global_constants, const char *name, RegisterSize size, bool is_signed) {
    append_global_type(global_constants, name, new Integer { size, is_signed });
}

inline void append_builtin(List<GlobalConstant> *global_constants, const char *name) {
    append(global_constants, {
        name,
        &builtin_function_singleton,
        new BuiltinFunctionConstant {
            name
        }
    });
}

static_profiled_function(bool, cli_entry, (Array<const char*> arguments), (arguments)) {
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

    auto regsiter_sizes = get_register_sizes(architecture);

    List<GlobalConstant> global_constants{};

    append_base_integer_type(&global_constants, "u8", RegisterSize::Size8, false);
    append_base_integer_type(&global_constants, "u16", RegisterSize::Size16, false);
    append_base_integer_type(&global_constants, "u32", RegisterSize::Size32, false);
    append_base_integer_type(&global_constants, "u64", RegisterSize::Size64, false);

    append_base_integer_type(&global_constants, "i8", RegisterSize::Size8, true);
    append_base_integer_type(&global_constants, "i16", RegisterSize::Size16, true);
    append_base_integer_type(&global_constants, "i32", RegisterSize::Size32, true);
    append_base_integer_type(&global_constants, "i64", RegisterSize::Size64, true);

    append_base_integer_type(&global_constants, "usize", regsiter_sizes.address_size, false);
    append_base_integer_type(&global_constants, "isize", regsiter_sizes.address_size, true);

    append_global_type(
        &global_constants,
        "bool",
        &boolean_singleton
    );

    append_global_type(
        &global_constants,
        "void",
        &void_singleton
    );

    append_global_type(
        &global_constants,
        "f32",
        new FloatType {
            RegisterSize::Size32
        }
    );

    append_global_type(
        &global_constants,
        "f64",
        new FloatType {
            RegisterSize::Size64
        }
    );

    append(&global_constants, GlobalConstant {
        "true",
        &boolean_singleton,
        new BooleanConstant { true }
    });

    append(&global_constants, GlobalConstant {
        "false",
        &boolean_singleton,
        new BooleanConstant { false }
    });

    append_global_type(
        &global_constants,
        "type",
        &type_type_singleton
    );

    append_builtin(&global_constants, "size_of");
    append_builtin(&global_constants, "type_of");

    append_builtin(&global_constants, "memcpy");

    append(&global_constants, GlobalConstant {
        "X64",
        &boolean_singleton,
        new BooleanConstant {
            strcmp(architecture, "x64") == 0
        }
    });

    append(&global_constants, GlobalConstant {
        "WINDOWS",
        &boolean_singleton,
        new BooleanConstant {
            strcmp(os, "windows") == 0
        }
    });

    append(&global_constants, GlobalConstant {
        "LINUX",
        &boolean_singleton,
        new BooleanConstant {
            strcmp(os, "linux") == 0
        }
    });

    append(&global_constants, GlobalConstant {
        "DEBUG",
        &boolean_singleton,
        new BooleanConstant {
            strcmp(config, "debug") == 0
        }
    });

    append(&global_constants, GlobalConstant {
        "RELEASE",
        &boolean_singleton,
        new BooleanConstant {
            strcmp(config, "release") == 0
        }
    });

    GlobalInfo info {
        to_array(global_constants),
        regsiter_sizes.address_size,
        regsiter_sizes.default_size
    };

    List<Job*> jobs {};

    auto main_file_parse_job = new ParseFile;
    main_file_parse_job->done = false;
    main_file_parse_job->waiting_for = nullptr;
    main_file_parse_job->path = absolute_source_file_path;

    append(&jobs, (Job*)main_file_parse_job);

    List<RuntimeStatic*> runtime_statics {};
    List<const char*> libraries {};

    uint64_t total_parser_time = 0;
    uint64_t total_generator_time = 0;

    while(true) {
        auto did_work = false;
        for(auto job : jobs) {
            if(!job->done) {
                if(job->waiting_for != nullptr) {
                    if(!job->waiting_for->done) {
                        continue;
                    }

                    job->waiting_for = nullptr;
                }

                switch(job->kind) {
                    case JobKind::ParseFile: {
                        auto parse_file = (ParseFile*)job;

                        auto start_time = get_timer_counts();

                        expect(tokens, tokenize_source(parse_file->path));

                        expect(statements, parse_tokens(parse_file->path, tokens));

                        if(print_ast) {
                            printf("%s:\n", parse_file->path);
                            for(auto statement : statements) {
                                print_statement(statement);
                                printf("\n");
                            }
                        }

                        auto scope = new ConstantScope;
                        scope->statements = statements;
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        parse_file->done = true;

                        if(!process_scope(&jobs, scope, statements, nullptr, true)) {
                            return false;
                        }

                        auto end_time = get_timer_counts();

                        total_parser_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveStaticIf: {
                        auto resolve_static_if = (ResolveStaticIf*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_static_if(info, &jobs, resolve_static_if->static_if, resolve_static_if->scope);
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_static_if->done = true;
                            resolve_static_if->condition = result.value;
                        } else {
                            resolve_static_if->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveFunctionDeclaration: {
                        auto resolve_function_declaration = (ResolveFunctionDeclaration*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_function_declaration(
                            info,
                            &jobs,
                            resolve_function_declaration->declaration,
                            resolve_function_declaration->scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_function_declaration->done = true;
                            resolve_function_declaration->type = result.value.type;
                            resolve_function_declaration->value = result.value.value;

                            auto found = false;
                            for(auto job : jobs) {
                                if(job->kind == JobKind::GenerateFunction) { 
                                    auto generate_function = (GenerateFunction*)job;

                                    if(
                                        generate_function->value->declaration == resolve_function_declaration->declaration &&
                                        generate_function->value->body_scope == resolve_function_declaration->value->body_scope
                                    ) {
                                        found = true;
                                        break;
                                    }
                                }
                            }

                            if(!found) {
                                auto generate_function = new GenerateFunction;
                                generate_function->done = false;
                                generate_function->waiting_for = resolve_function_declaration;
                                generate_function->type = result.value.type;
                                generate_function->value = result.value.value;
                                generate_function->function = new Function;

                                append(&jobs, (Job*)generate_function);
                            }
                        } else {
                            resolve_function_declaration->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolvePolymorphicFunction: {
                        auto resolve_polymorphic_function = (ResolvePolymorphicFunction*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_polymorphic_function(
                            info,
                            &jobs,
                            resolve_polymorphic_function->declaration,
                            resolve_polymorphic_function->parameters,
                            resolve_polymorphic_function->scope,
                            resolve_polymorphic_function->call_scope,
                            resolve_polymorphic_function->call_parameter_ranges
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_polymorphic_function->done = true;
                            resolve_polymorphic_function->type = result.value.type;
                            resolve_polymorphic_function->value = result.value.value;
                        } else {
                            resolve_polymorphic_function->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveConstantDefinition: {
                        auto resolve_constant_definition = (ResolveConstantDefinition*)job;

                        auto start_time = get_timer_counts();

                        auto result = evaluate_constant_expression(
                            info,
                            &jobs,
                            resolve_constant_definition->scope,
                            nullptr,
                            resolve_constant_definition->definition->expression
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_constant_definition->done = true;
                            resolve_constant_definition->type = result.value.type;
                            resolve_constant_definition->value = result.value.value;
                        } else {
                            resolve_constant_definition->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveStructDefinition: {
                        auto resolve_struct_definition = (ResolveStructDefinition*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_struct_definition(
                            info,
                            &jobs,
                            resolve_struct_definition->definition,
                            resolve_struct_definition->scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_struct_definition->done = true;
                            resolve_struct_definition->type = result.value;
                        } else {
                            resolve_struct_definition->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolvePolymorphicStruct: {
                        auto resolve_polymorphic_struct = (ResolvePolymorphicStruct*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_polymorphic_struct(
                            info,
                            &jobs,
                            resolve_polymorphic_struct->definition,
                            resolve_polymorphic_struct->parameters,
                            resolve_polymorphic_struct->scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            resolve_polymorphic_struct->done = true;
                            resolve_polymorphic_struct->type = result.value;
                        } else {
                            resolve_polymorphic_struct->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::GenerateFunction: {
                        auto generate_function = (GenerateFunction*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_generate_function(
                            info,
                            &jobs,
                            generate_function->type,
                            generate_function->value,
                            generate_function->function
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            generate_function->done = true;
                            generate_function->static_constants = result.value;

                            append(&runtime_statics, (RuntimeStatic*)generate_function->function);

                            for(auto static_constant : result.value) {
                                append(&runtime_statics, (RuntimeStatic*)static_constant);
                            }

                            if(generate_function->function->is_external) {
                                for(auto library : generate_function->function->libraries) {
                                    auto already_registered = false;
                                    for(auto registered_library : libraries) {
                                        if(strcmp(registered_library, library) == 0) {
                                            already_registered = true;
                                            break;
                                        }
                                    }

                                    if(!already_registered) {
                                        append(&libraries, library);
                                    }
                                }
                            }
                        } else {
                            generate_function->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;

                        if(generate_function->done && print_ir) {
                            auto current_scope = generate_function->function->scope;
                            while(!current_scope->is_top_level) {
                                current_scope = current_scope->parent;
                            }

                            printf("%s:\n", current_scope->file_path);
                            print_static(generate_function->function);
                            printf("\n");

                            for(auto static_constant : result.value) {
                                print_static(static_constant);
                                printf("\n");
                            }
                        }
                    } break;

                    case JobKind::GenerateStaticVariable: {
                        auto generate_static_variable = (GenerateStaticVariable*)job;

                        auto start_time = get_timer_counts();

                        auto result = do_generate_static_variable(
                            info,
                            &jobs,
                            generate_static_variable->declaration,
                            generate_static_variable->scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        if(result.has_value) {
                            generate_static_variable->done = true;
                            generate_static_variable->static_variable = result.value.static_variable;
                            generate_static_variable->type = result.value.type;

                            append(&runtime_statics, (RuntimeStatic*)result.value.static_variable);

                            if(print_ir) {
                                auto current_scope = generate_static_variable->scope;
                                while(!current_scope->is_top_level) {
                                    current_scope = current_scope->parent;
                                }

                                printf("%s:\n", current_scope->file_path);
                                print_static(result.value.static_variable);
                                printf("\n");
                            }
                        } else {
                            generate_static_variable->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    default: abort();
                }

                did_work = true;
                break;
            }
        }

        if(!did_work) {
            break;
        }
    }

    auto all_jobs_done = true;
    for(auto job : jobs) {
        if(!job->done) {
            all_jobs_done = false;
        }
    }

    if(!all_jobs_done) {
        fprintf(stderr, "Error: Circular dependency detected\n");

        return false;
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

    uint64_t backend_time;
    {
        auto start_time = get_timer_counts();

        generate_c_object(to_array(runtime_statics), architecture, os, config, output_file_directory, output_file_name);

        auto end_time = get_timer_counts();

        backend_time = end_time - start_time;
    }

    uint64_t linker_time;
    {
        auto start_time = get_timer_counts();

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
        
        for(auto library : libraries) {
            string_buffer_append(&buffer, " -l");
            string_buffer_append(&buffer, library);
        }

        string_buffer_append(&buffer, " ");
        string_buffer_append(&buffer, output_file_directory);
        string_buffer_append(&buffer, output_file_name);
        string_buffer_append(&buffer, ".o");

        enter_region("linker");

        if(system(buffer.data) != 0) {
            return false;
        }

        leave_region();

        auto end_time = get_timer_counts();

        linker_time = end_time - start_time;
    }

    auto end_time = get_timer_counts();

    auto total_time = end_time - start_time;

    auto counts_per_second = get_timer_counts_per_second();

    printf("Total time: %.2fms\n", (double)total_time / counts_per_second * 1000);
    printf("  Parser time: %.2fms\n", (double)total_parser_time / counts_per_second * 1000);
    printf("  Generator time: %.2fms\n", (double)total_generator_time / counts_per_second * 1000);
    printf("  C Backend time: %.2fms\n", (double)backend_time / counts_per_second * 1000);
    printf("  Linker time: %.2fms\n", (double)linker_time / counts_per_second * 1000);

    return true;
}

int main(int argument_count, const char *arguments[]) {
#if defined(PROFILING)
    init_profiler();
#endif

    if(cli_entry({ (size_t)argument_count, arguments })) {
#if defined(PROFILING)
        dump_profile();
#endif

        return 0;
    } else {
#if defined(PROFILING)
        dump_profile();
#endif

        return 1;
    }
}