#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "timing.h"
#include "profiler.h"
#include "lexer.h"
#include "parser.h"
#include "llvm_backend.h"
#include "util.h"
#include "platform.h"
#include "path.h"
#include "list.h"
#include "jobs.h"
#include "generator.h"
#include "types.h"

static const char *get_default_output_file(const char *os, bool no_link) {
    if(no_link) {
        return "out.o";
    } else {
        if(strcmp(os, "windows") == 0) {
            return "out.exe";
        } else if(strcmp(os, "emscripten") == 0) {
            return "out.js";
        } else {
            return "out";
        }
    }
}

static void print_help_message(FILE *file) {
    fprintf(file, "Usage: compiler [options] <source file>\n\n");

    auto default_os = get_host_os();

    fprintf(file, "Options:\n");
    fprintf(file, "  -output <output file>  (default: %s) Specify output file path\n", get_default_output_file(default_os, false));
    fprintf(file, "  -config debug|release  (default: debug) Specify build configuration\n");
    fprintf(file, "  -arch x64|wasm32  (default: %s) Specify CPU architecture to target\n", get_host_architecture());
    fprintf(file, "  -os windows|linux|emscripten  (default: %s) Specify operating system to target\n", default_os);
    fprintf(file, "  -no-link  Don't run the linker\n");
    fprintf(file, "  -print-ast  Print abstract syntax tree\n");
    fprintf(file, "  -print-ir  Print internal intermediate representation\n");
    fprintf(file, "  -help  Display this help message then exit\n");
}

inline void append_global_type(List<GlobalConstant> *global_constants, String name, AnyType type) {
    append(global_constants, {
        name,
        create_type_type(),
        wrap_type_constant(type)
    });
}

inline void append_base_integer_type(List<GlobalConstant> *global_constants, String name, RegisterSize size, bool is_signed) {
    append_global_type(global_constants, name, wrap_integer_type({ size, is_signed }));
}

inline void append_builtin(List<GlobalConstant> *global_constants, String name) {
    append(global_constants, {
        name,
        create_builtin_function_type(),
        wrap_builtin_function_constant({
            name
        })
    });
}

static_profiled_function(bool, cli_entry, (Array<const char*> arguments), (arguments)) {
    auto start_time = get_timer_counts();

    const char *source_file_path = nullptr;
    const char *output_file_path = nullptr;

    auto architecture = get_host_architecture();

    auto os = get_host_os();

    auto config = "debug";

    auto no_link = false;
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
        } else if(strcmp(argument, "-no-link") == 0) {
            no_link = true;
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

    if(!is_supported_target(os, architecture)) {
        fprintf(stderr, "Error: '%s' and '%s' is not a supported OS and architecture combination\n\n", os, architecture);
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
        output_file_path = get_default_output_file(os, no_link);
    }

    auto architecture_sizes = get_architecture_sizes(architecture);

    List<GlobalConstant> global_constants{};

    append_base_integer_type(&global_constants, "u8"_S, RegisterSize::Size8, false);
    append_base_integer_type(&global_constants, "u16"_S, RegisterSize::Size16, false);
    append_base_integer_type(&global_constants, "u32"_S, RegisterSize::Size32, false);
    append_base_integer_type(&global_constants, "u64"_S, RegisterSize::Size64, false);

    append_base_integer_type(&global_constants, "i8"_S, RegisterSize::Size8, true);
    append_base_integer_type(&global_constants, "i16"_S, RegisterSize::Size16, true);
    append_base_integer_type(&global_constants, "i32"_S, RegisterSize::Size32, true);
    append_base_integer_type(&global_constants, "i64"_S, RegisterSize::Size64, true);

    append_base_integer_type(&global_constants, "usize"_S, architecture_sizes.address_size, false);
    append_base_integer_type(&global_constants, "isize"_S, architecture_sizes.address_size, true);

    append_base_integer_type(&global_constants, "uint"_S, architecture_sizes.default_integer_size, false);
    append_base_integer_type(&global_constants, "int"_S, architecture_sizes.default_integer_size, true);

    append_global_type(
        &global_constants,
        "bool"_S,
        create_boolean_type()
    );

    append_global_type(
        &global_constants,
        "void"_S,
        create_void_type()
    );

    append_global_type(
        &global_constants,
        "f32"_S,
        wrap_float_type({
            RegisterSize::Size32
        })
    );

    append_global_type(
        &global_constants,
        "f64"_S,
        wrap_float_type({
            RegisterSize::Size64
        })
    );

    append_global_type(
        &global_constants,
        "float"_S,
        wrap_float_type({
            architecture_sizes.default_float_size
        })
    );

    append(&global_constants, GlobalConstant {
        "true"_S,
        create_boolean_type(),
        wrap_boolean_constant(true)
    });

    append(&global_constants, GlobalConstant {
        "false"_S,
        create_boolean_type(),
        wrap_boolean_constant(false)
    });

    append_global_type(
        &global_constants,
        "type"_S,
        create_type_type()
    );

    append_builtin(&global_constants, "size_of"_S);
    append_builtin(&global_constants, "type_of"_S);

    append_builtin(&global_constants, "memcpy"_S);

    append(&global_constants, GlobalConstant {
        "X86"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(architecture, "x86") == 0)
    });

    append(&global_constants, GlobalConstant {
        "X64"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(architecture, "x64") == 0)
    });

    append(&global_constants, GlobalConstant {
        "WASM32"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(config, "wasm32") == 0)
    });

    append(&global_constants, GlobalConstant {
        "WINDOWS"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(os, "windows") == 0)
    });

    append(&global_constants, GlobalConstant {
        "LINUX"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(os, "linux") == 0)
    });

    append(&global_constants, GlobalConstant {
        "EMSCRIPTEN"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(os, "emscripten") == 0)
    });

    append(&global_constants, GlobalConstant {
        "DEBUG"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(config, "debug") == 0)
    });

    append(&global_constants, GlobalConstant {
        "RELEASE"_S,
        create_boolean_type(),
        wrap_boolean_constant(strcmp(config, "release") == 0)
    });

    GlobalInfo info {
        to_array(global_constants),
        architecture_sizes
    };

    List<AnyJob> jobs {};

    size_t main_file_parse_job_index;
    {
        AnyJob job;
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = absolute_source_file_path;

        main_file_parse_job_index = append(&jobs, job);
    }

    List<RuntimeStatic*> runtime_statics {};
    List<const char*> libraries {};

    if(strcmp(os, "windows") == 0) {
        append(&libraries, "kernel32.lib");
    }

    auto main_function_state = JobState::Waiting;
    auto main_function_waiting_for = main_file_parse_job_index;
    Function *main_function;

    uint64_t total_parser_time = 0;
    uint64_t total_generator_time = 0;

    while(true) {
        auto did_work = false;
        for(size_t job_index = 0; job_index < jobs.count; job_index += 1) {
            auto job = &jobs[job_index];

            if(job->state != JobState::Done) {
                if(job->state == JobState::Waiting) {
                    if(jobs[job->waiting_for].state != JobState::Done) {
                        continue;
                    }

                    job->state = JobState::Working;
                }

                switch(job->kind) {
                    case JobKind::ParseFile: {
                        auto parse_file = &job->parse_file;

                        auto start_time = get_timer_counts();

                        expect(tokens, tokenize_source(parse_file->path));

                        expect(statements, parse_tokens(parse_file->path, tokens));

                        auto scope = new ConstantScope;
                        scope->statements = statements;
                        scope->declarations = construct_declaration_hash_table(statements);
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        if(!process_scope(&jobs, scope, statements, nullptr, true)) {
                            return false;
                        }

                        auto end_time = get_timer_counts();

                        total_parser_time += end_time - start_time;

                        auto job_after = jobs[job_index];

                        if(print_ast) {
                            printf("%s:\n", job_after.parse_file.path);

                            for(auto statement : statements) {
                                print_statement(statement);
                                printf("\n");
                            }
                        }
                    } break;

                    case JobKind::ResolveStaticIf: {
                        auto resolve_static_if = job->resolve_static_if;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_static_if(info, &jobs, resolve_static_if.static_if, resolve_static_if.scope);
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_static_if.condition = result.value.condition;
                            job_after->resolve_static_if.declarations = result.value.declarations;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveFunctionDeclaration: {
                        auto resolve_function_declaration = job->resolve_function_declaration;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_function_declaration(
                            info,
                            &jobs,
                            resolve_function_declaration.declaration,
                            resolve_function_declaration.scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_function_declaration.type = result.value.type;
                            job_after->resolve_function_declaration.value = result.value.value;

                            if(job_after->resolve_function_declaration.type.kind == TypeKind::FunctionTypeType) {
                                auto function_type = job_after->resolve_function_declaration.type.function;

                                auto function_value = unwrap_function_constant(job_after->resolve_function_declaration.value);

                                auto found = false;
                                for(auto job : jobs) {
                                    if(job.kind == JobKind::GenerateFunction) { 
                                        auto generate_function = job.generate_function;

                                        if(
                                            generate_function.value.declaration == function_value.declaration &&
                                            generate_function.value.body_scope == function_value.body_scope
                                        ) {
                                            found = true;
                                            break;
                                        }
                                    }
                                }

                                if(!found) {
                                    AnyJob job;
                                    job.kind = JobKind::GenerateFunction;
                                    job.state = JobState::Working;
                                    job.generate_function.type = function_type;
                                    job.generate_function.value = function_value;
                                    job.generate_function.function = new Function;

                                    append(&jobs, job);
                                }
                            }
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolvePolymorphicFunction: {
                        auto resolve_polymorphic_function = job->resolve_polymorphic_function;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_polymorphic_function(
                            info,
                            &jobs,
                            resolve_polymorphic_function.declaration,
                            resolve_polymorphic_function.parameters,
                            resolve_polymorphic_function.scope,
                            resolve_polymorphic_function.call_scope,
                            resolve_polymorphic_function.call_parameter_ranges
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_polymorphic_function.type = result.value.type;
                            job_after->resolve_polymorphic_function.value = result.value.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveConstantDefinition: {
                        auto resolve_constant_definition = job->resolve_constant_definition;

                        auto start_time = get_timer_counts();

                        auto result = evaluate_constant_expression(
                            info,
                            &jobs,
                            resolve_constant_definition.scope,
                            nullptr,
                            resolve_constant_definition.definition->expression
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_constant_definition.type = result.value.type;
                            job_after->resolve_constant_definition.value = result.value.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveStructDefinition: {
                        auto resolve_struct_definition = job->resolve_struct_definition;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_struct_definition(
                            info,
                            &jobs,
                            resolve_struct_definition.definition,
                            resolve_struct_definition.scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_struct_definition.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolvePolymorphicStruct: {
                        auto resolve_polymorphic_struct = job->resolve_polymorphic_struct;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_polymorphic_struct(
                            info,
                            &jobs,
                            resolve_polymorphic_struct.definition,
                            resolve_polymorphic_struct.parameters,
                            resolve_polymorphic_struct.scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->resolve_polymorphic_struct.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::GenerateFunction: {
                        auto generate_function = job->generate_function;

                        auto start_time = get_timer_counts();

                        auto result = do_generate_function(
                            info,
                            &jobs,
                            generate_function.type,
                            generate_function.value,
                            generate_function.function
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;

                            append(&runtime_statics, (RuntimeStatic*)job_after->generate_function.function);

                            for(auto static_constant : result.value) {
                                append(&runtime_statics, (RuntimeStatic*)static_constant);
                            }

                            if(job_after->generate_function.function->is_external) {
                                for(auto library : job_after->generate_function.function->libraries) {
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
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;

                        if(job_after->state == JobState::Done && print_ir) {
                            printf("%s:\n", job_after->generate_function.function->path);
                            print_static(job_after->generate_function.function);
                            printf("\n");

                            for(auto static_constant : result.value) {
                                print_static(static_constant);
                                printf("\n");
                            }
                        }
                    } break;

                    case JobKind::GenerateStaticVariable: {
                        auto generate_static_variable = job->generate_static_variable;

                        auto start_time = get_timer_counts();

                        auto result = do_generate_static_variable(
                            info,
                            &jobs,
                            generate_static_variable.declaration,
                            generate_static_variable.scope
                        );
                        if(!result.status) {
                            return false;
                        }

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            job_after->state = JobState::Done;
                            job_after->generate_static_variable.static_variable = result.value.static_variable;
                            job_after->generate_static_variable.type = result.value.type;

                            append(&runtime_statics, (RuntimeStatic*)result.value.static_variable);

                            if(job_after->generate_static_variable.static_variable->is_external) {
                                for(auto library : job_after->generate_static_variable.static_variable->libraries) {
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
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;

                        if(job_after->state == JobState::Done &&print_ir) {
                            printf("%s:\n", get_scope_file_path(*job_after->generate_static_variable.scope));
                            print_static(result.value.static_variable);
                            printf("\n");
                        }
                    } break;

                    default: abort();
                }

                did_work = true;
                break;
            }
        }

        if(main_function_state != JobState::Done) {
            if(main_function_state == JobState::Waiting) {
                if(jobs[main_function_waiting_for].state != JobState::Done) {
                    continue;
                }

                main_function_state = JobState::Working;
            }

            assert(jobs[main_file_parse_job_index].state == JobState::Done);

            auto scope = jobs[main_file_parse_job_index].parse_file.scope;

            did_work = true;

            auto result = search_for_declaration(
                info,
                &jobs,
                "main"_S,
                calculate_string_hash("main"_S),
                scope,
                scope->statements,
                scope->declarations,
                false,
                nullptr
            );
            if(!result.status) {
                return false;
            }

            if(!result.has_value) {
                main_function_state = JobState::Waiting;
                main_function_waiting_for = result.waiting_for;

                continue;
            }

            main_function_state = JobState::Done;

            if(!result.value.found) {
                fprintf(stderr, "Error: Cannot find 'main'\n");

                return false;
            }

            if(result.value.type.kind != TypeKind::FunctionTypeType) {
                fprintf(stderr, "Error: 'main' must be a function. Got '%s'\n", type_description(result.value.type));

                return false;
            }

            auto function_type = result.value.type.function;

            auto function_value = unwrap_function_constant(result.value.value);

            if(function_type.parameters.count != 0) {
                error(scope, function_value.declaration->range, "'main' must have zero parameters");

                return false;
            }

            auto expected_main_return_integer = wrap_integer_type({ RegisterSize::Size32, true });

            if(!types_equal(*function_type.return_type, expected_main_return_integer)) {
                error(
                    scope,
                    function_value.declaration->range,
                    "Incorrect 'main' return type. Expected '%s', got '%s'",
                    type_description(expected_main_return_integer),
                    type_description(*function_type.return_type)
                );

                return false;
            }

            auto found = false;
            for(auto job : jobs) {
                if(job.kind == JobKind::GenerateFunction) {
                    auto generate_function = job.generate_function;

                    if(
                        types_equal(wrap_function_type(generate_function.type), wrap_function_type(function_type)) &&
                        generate_function.value.declaration == function_value.declaration &&
                        generate_function.value.body_scope == function_value.body_scope
                    ) {
                        found = true;

                        main_function = generate_function.function;

                        break;
                    }
                }
            }
            assert(found);
        }

        if(!did_work) {
            break;
        }
    }

    auto all_jobs_done = true;
    for(auto job : jobs) {
        if(job.state != JobState::Done) {
            all_jobs_done = false;
        }
    }

    if(!all_jobs_done || main_function == nullptr) {
        fprintf(stderr, "Error: Circular dependency detected!\n");
        fprintf(stderr, "Error: The following areas depend on eathother:\n");

        for(auto job : jobs) {
            if(job.state != JobState::Done) {
                ConstantScope *scope;
                FileRange range;
                switch(job.kind) {
                    case JobKind::ParseFile: {
                        abort();
                    } break;

                    case JobKind::ResolveStaticIf: {
                        auto resolve_static_if = job.resolve_static_if;

                        scope = resolve_static_if.scope;
                        range = resolve_static_if.static_if->range;
                    } break;

                    case JobKind::ResolveFunctionDeclaration: {
                        auto resolve_function_declaration = job.resolve_function_declaration;

                        scope = resolve_function_declaration.scope;
                        range = resolve_function_declaration.declaration->range;
                    } break;

                    case JobKind::ResolvePolymorphicFunction: {
                        auto resolve_polymorphic_function = job.resolve_polymorphic_function;

                        scope = resolve_polymorphic_function.scope;
                        range = resolve_polymorphic_function.declaration->range;
                    } break;

                    case JobKind::ResolveConstantDefinition: {
                        auto resolve_constant_definition = job.resolve_constant_definition;

                        scope = resolve_constant_definition.scope;
                        range = resolve_constant_definition.definition->range;
                    } break;

                    case JobKind::ResolveStructDefinition: {
                        auto resolve_struct_definition = job.resolve_struct_definition;

                        scope = resolve_struct_definition.scope;
                        range = resolve_struct_definition.definition->range;
                    } break;

                    case JobKind::ResolvePolymorphicStruct: {
                        auto resolve_polymorphic_struct = job.resolve_polymorphic_struct;

                        scope = resolve_polymorphic_struct.scope;
                        range = resolve_polymorphic_struct.definition->range;
                    } break;

                    case JobKind::GenerateFunction: {
                        auto generate_function = job.generate_function;

                        scope = generate_function.value.body_scope->parent;
                        range = generate_function.value.declaration->range;
                    } break;

                    case JobKind::GenerateStaticVariable: {
                        auto generate_static_variable = job.generate_static_variable;

                        scope = generate_static_variable.scope;
                        range = generate_static_variable.declaration->range;
                    } break;

                    default: abort();
                }

                error(scope, range, "Here");
            }
        }

        return false;
    }

    auto output_file_directory = path_get_directory_component(output_file_path);

    const char *object_file_path;
    if(no_link) {
        object_file_path = output_file_path;
    } else {
        auto full_name = path_get_file_component(output_file_path);

        auto dot_pointer = strchr(full_name, '.');

        const char *output_file_name;
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

        StringBuffer buffer {};

        string_buffer_append(&buffer, output_file_directory);
        string_buffer_append(&buffer, output_file_name);
        string_buffer_append(&buffer, ".o");

        object_file_path = buffer.data;
    }

    uint64_t backend_time;
    String main_function_name;
    {
        List<String> reserved_names {};

        if(strcmp(os, "emscripten") == 0) {
            append(&reserved_names, "main"_S);
        } else {
            append(&reserved_names, "entry"_S);
        }

        if(strcmp(os, "windows") == 0) {
            append(&reserved_names, "_fltused"_S);
            append(&reserved_names, "__chkstk"_S);
        }

        auto start_time = get_timer_counts();

        expect(name_mappings, generate_llvm_object(
            to_array(runtime_statics),
            architecture,
            os,
            config,
            object_file_path,
            to_array(reserved_names)
        ));

        auto found = false;
        for(auto name_mapping : name_mappings) {
            if(name_mapping.runtime_static == main_function) {
                main_function_name = name_mapping.name;
                found = true;

                break;
            }
        }
        assert(found);

        auto end_time = get_timer_counts();

        backend_time = end_time - start_time;
    }

    uint64_t linker_time;
    if(!no_link) {
        auto start_time = get_timer_counts();

        StringBuffer command_buffer {};

        const char *frontend;
        if(strcmp(os, "emscripten") == 0) {
            frontend = "emcc";
        } else {
            frontend = "clang";
        }

        const char *linker_options;
        if(strcmp(os, "windows") == 0) {
            if(strcmp(config, "debug") == 0) {
                linker_options = "/entry:entry,/DEBUG,/SUBSYSTEM:CONSOLE";
            } else if(strcmp(config, "release") == 0) {
                linker_options = "/entry:entry,/SUBSYSTEM:CONSOLE";
            } else {
                abort();
            }
        } else if(strcmp(os, "emscripten") == 0) {
            linker_options = nullptr;
        } else {
            linker_options = "--entry=entry";
        }

        auto triple = get_llvm_triple(architecture, os);

        string_buffer_append(&command_buffer, frontend);

        string_buffer_append(&command_buffer, " -nostdlib -fuse-ld=lld --target=");

        string_buffer_append(&command_buffer, triple);

        if(linker_options != nullptr) {
            string_buffer_append(&command_buffer, " -Wl,"); 
            string_buffer_append(&command_buffer, linker_options);
        }

        string_buffer_append(&command_buffer, " -o");
        string_buffer_append(&command_buffer, output_file_path);
        
        for(auto library : libraries) {
            string_buffer_append(&command_buffer, " -l");
            string_buffer_append(&command_buffer, library);
        }

        if(strcmp(os, "emscripten") == 0) {
            string_buffer_append(&command_buffer, " -lcompiler_rt");
        }

        string_buffer_append(&command_buffer, " ");
        string_buffer_append(&command_buffer, object_file_path);

        auto executable_path = get_executable_path();
        auto executable_directory = path_get_directory_component(executable_path);

        StringBuffer runtime_command_buffer {};

        string_buffer_append(&runtime_command_buffer, "clang -std=gnu99 -ffreestanding -nostdinc -c -target ");

        string_buffer_append(&runtime_command_buffer, triple);

        string_buffer_append(&runtime_command_buffer, " -DMAIN=");
        string_buffer_append(&runtime_command_buffer, main_function_name);
        
        string_buffer_append(&runtime_command_buffer, " -o ");
        string_buffer_append(&runtime_command_buffer, output_file_directory);
        string_buffer_append(&runtime_command_buffer, "runtime.o ");

        string_buffer_append(&runtime_command_buffer, executable_directory);
        string_buffer_append(&runtime_command_buffer, "runtime_");
        string_buffer_append(&runtime_command_buffer, os);
        string_buffer_append(&runtime_command_buffer, "_");
        string_buffer_append(&runtime_command_buffer, architecture);
        string_buffer_append(&runtime_command_buffer, ".c");

        enter_region("clang");
        if(system(runtime_command_buffer.data) != 0) {
            fprintf(stderr, "Error: 'clang' returned non-zero while compiling runtime\n");

            return false;
        }
        leave_region();

        string_buffer_append(&command_buffer, " ");
        string_buffer_append(&command_buffer, output_file_directory);
        string_buffer_append(&command_buffer, "runtime.o");

        enter_region("linker");

        if(system(command_buffer.data) != 0) {
            fprintf(stderr, "Error: '%s' returned non-zero while linking\n", frontend);

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
    printf("  LLVM Backend time: %.2fms\n", (double)backend_time / counts_per_second * 1000);
    if(!no_link) {
        printf("  Linker time: %.2fms\n", (double)linker_time / counts_per_second * 1000);
    }

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