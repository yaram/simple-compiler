#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "constant.h"
#include "timing.h"
#include "profiler.h"
#include "lexer.h"
#include "parser.h"
#include "hl_llvm_backend.h"
#include "util.h"
#include "platform.h"
#include "path.h"
#include "list.h"
#include "jobs.h"
#include "hl_generator.h"
#include "types.h"

inline String get_default_output_file(String os, bool no_link) {
    if(no_link) {
        return u8"out.o"_S;
    } else {
        if(os == u8"windows"_S) {
            return u8"out.exe"_S;
        } else if(os == u8"emscripten"_S) {
            return u8"out.js"_S;
        } else if(os == u8"wasi"_S) {
            return u8"out.wasm"_S;
        } else {
            return u8"out"_S;
        }
    }
}

static void print_help_message(FILE* file) {
    fprintf(file, "Usage: compiler [options] <source file>\n\n");

    auto default_architecture = get_host_architecture();
    auto default_os = get_host_os();
    auto default_output_file = get_default_output_file(default_os, false);
    auto default_toolchain = get_default_toolchain(default_os);

    fprintf(file, "Options:\n");
    fprintf(file, "  -output <output file>  (default: %.*s) Specify output file path\n", STRING_PRINTF_ARGUMENTS(default_output_file));
    fprintf(file, "  -config debug|release  (default: debug) Specify build configuration\n");
    fprintf(file, "  -arch x86|x64|riscv32|riscv64|wasm32  (default: %.*s) Specify CPU architecture to target\n", STRING_PRINTF_ARGUMENTS(default_architecture));
    fprintf(file, "  -os windows|linux|emscripten|wasi  (default: %.*s) Specify operating system to target\n", STRING_PRINTF_ARGUMENTS(default_os));
    fprintf(file, "  -os gnu|msvc  (default: %.*s) Specify toolchain to use\n", STRING_PRINTF_ARGUMENTS(default_toolchain));
    fprintf(file, "  -no-link  Don't run the linker\n");
    fprintf(file, "  -print-ast  Print abstract syntax tree\n");
    fprintf(file, "  -print-ir  Print internal intermediate representation\n");
    fprintf(file, "  -print-llvm  Print LLVM IR\n");
    fprintf(file, "  -help  Display this help message then exit\n");
}

inline void append_global_constant(List<GlobalConstant>* global_constants, String name, AnyType type, AnyConstantValue value) {
    GlobalConstant global_constant {};
    global_constant.name = name;
    global_constant.type = type;
    global_constant.value = value;

    global_constants->append(global_constant);
}

inline void append_global_type(List<GlobalConstant>* global_constants, String name, AnyType type) {
    append_global_constant(global_constants, name, AnyType::create_type_type(), AnyConstantValue(type));
}

inline void append_base_integer_type(List<GlobalConstant>* global_constants, String name, RegisterSize size, bool is_signed) {
    Integer integer {};
    integer.size = size;
    integer.is_signed = is_signed;

    append_global_type(global_constants, name, AnyType(integer));
}

inline void append_builtin(List<GlobalConstant>* global_constants, String name) {
    BuiltinFunctionConstant constant {};
    constant.name = name;

    append_global_constant(global_constants,
        name,
        AnyType::create_builtin_function(),
        AnyConstantValue(constant)
    );
}

static_profiled_function(Result<void>, cli_entry, (Array<const char*> arguments), (arguments)) {
    auto start_time = get_timer_counts();

    bool has_source_file_path = false;
    String source_file_path;
    bool has_output_file_path = false;
    String output_file_path;

    auto architecture = get_host_architecture();

    auto os = get_host_os();

    auto has_toolchain = false;
    String toolchain;

    auto config = u8"debug"_S;

    auto no_link = false;
    auto print_ast = false;
    auto print_ir = false;
    auto print_llvm = false;

    int argument_index = 1;
    while(argument_index < arguments.length) {
        auto argument = arguments[argument_index];

        if(argument_index == arguments.length - 1 && argument[0] != '-') {
            has_source_file_path = true;

            auto result = String::from_c_string(argument);
            if(!result.status) {
                fprintf(stderr, "Error: Invalid source file path '%s'\n", argument);

                return err();
            }

            source_file_path = result.value;
        } else if(strcmp(argument, "-output") == 0) {
            argument_index += 1;

            if(argument_index == arguments.length - 1) {
                fprintf(stderr, "Error: Missing value for '-output' option\n\n");
                print_help_message(stderr);

                return err();
            }

            has_output_file_path = true;

            auto result = String::from_c_string(arguments[argument_index]);
            if(!result.status) {
                fprintf(stderr, "Error: Invalid output file path '%s'\n", arguments[argument_index]);

                return err();
            }

            output_file_path = result.value;
        } else if(strcmp(argument, "-arch") == 0) {
            argument_index += 1;

            if(argument_index == arguments.length - 1) {
                fprintf(stderr, "Error: Missing value for '-arch' option\n\n");
                print_help_message(stderr);

                return err();
            }

            auto result = String::from_c_string(arguments[argument_index]);
            if(!result.status) {
                fprintf(stderr, "Error: '%s' is not a valid '-arch' option value\n\n", arguments[argument_index]);
                print_help_message(stderr);

                return err();
            }

            architecture = result.value;
        } else if(strcmp(argument, "-os") == 0) {
            argument_index += 1;

            if(argument_index == arguments.length - 1) {
                fprintf(stderr, "Error: Missing value for '-os' option\n\n");
                print_help_message(stderr);

                return err();
            }

            auto result = String::from_c_string(arguments[argument_index]);
            if(!result.status) {
                fprintf(stderr, "Error: '%s' is not a valid '-os' option value\n\n", arguments[argument_index]);
                print_help_message(stderr);

                return err();
            }

            os = result.value;
        } else if(strcmp(argument, "-toolchain") == 0) {
            has_toolchain = true;

            argument_index += 1;

            if(argument_index == arguments.length - 1) {
                fprintf(stderr, "Error: Missing value for '-toolchain' option\n\n");
                print_help_message(stderr);

                return err();
            }

            auto result = String::from_c_string(arguments[argument_index]);
            if(!result.status) {
                fprintf(stderr, "Error: '%s' is not a valid '-toolchain' option value\n\n", arguments[argument_index]);
                print_help_message(stderr);

                return err();
            }

            toolchain = result.value;
        } else if(strcmp(argument, "-config") == 0) {
            argument_index += 1;

            if(argument_index == arguments.length - 1) {
                fprintf(stderr, "Error: Missing value for '-config' option\n\n");
                print_help_message(stderr);

                return err();
            }

            auto result = String::from_c_string(arguments[argument_index]);
            if(!result.status) {
                fprintf(stderr, "Error: '%s' is not a valid '-config' option value\n\n", arguments[argument_index]);
                print_help_message(stderr);

                return err();
            }

            config = result.value;
        } else if(strcmp(argument, "-no-link") == 0) {
            no_link = true;
        } else if(strcmp(argument, "-print-ast") == 0) {
            print_ast = true;
        } else if(strcmp(argument, "-print-ir") == 0) {
            print_ir = true;
        } else if(strcmp(argument, "-print-llvm") == 0) {
            print_llvm = true;
        } else if(strcmp(argument, "-help") == 0) {
            print_help_message(stdout);

            return ok();
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n\n", argument);
            print_help_message(stderr);

            return err();
        }

        argument_index += 1;
    }

    if(
        config == u8"debug"_S &&
        config == u8"release"_S
    ) {
        fprintf(stderr, "Error: Unknown config '%.*s'\n\n", STRING_PRINTF_ARGUMENTS(config));
        print_help_message(stderr);

        return err();
    }

    if(!does_os_exist(os)) {
        fprintf(stderr, "Error: Unknown OS '%.*s'\n\n", STRING_PRINTF_ARGUMENTS(os));
        print_help_message(stderr);

        return err();
    }

    if(!does_architecture_exist(architecture)) {
        fprintf(stderr, "Error: Unknown architecture '%.*s'\n\n", STRING_PRINTF_ARGUMENTS(architecture));
        print_help_message(stderr);

        return err();
    }

    if(has_toolchain) {
        if(!does_toolchain_exist(toolchain)) {
            fprintf(stderr, "Error: Unknown toolchain '%.*s'\n\n", STRING_PRINTF_ARGUMENTS(toolchain));
            print_help_message(stderr);

            return err();
        }
    } else {
        toolchain = get_default_toolchain(os);
    }

    if(!is_supported_target(os, architecture, toolchain)) {
        fprintf(
            stderr,
            "Error: '%.*s', '%.*s', and '%.*s' is not a supported OS, architecture, and toolchain combination\n\n",
            STRING_PRINTF_ARGUMENTS(os),
            STRING_PRINTF_ARGUMENTS(architecture),
            STRING_PRINTF_ARGUMENTS(toolchain)
        );
        print_help_message(stderr);

        return err();
    }

    if(!has_source_file_path) {
        fprintf(stderr, "Error: No source file provided\n\n");
        print_help_message(stderr);

        return err();
    }

    expect(absolute_source_file_path, path_relative_to_absolute(source_file_path));

    if(!has_output_file_path) {
        output_file_path = get_default_output_file(os, no_link);
    }

    auto architecture_sizes = get_architecture_sizes(architecture);

    List<GlobalConstant> global_constants {};

    append_base_integer_type(&global_constants, u8"u8"_S, RegisterSize::Size8, false);
    append_base_integer_type(&global_constants, u8"u16"_S, RegisterSize::Size16, false);
    append_base_integer_type(&global_constants, u8"u32"_S, RegisterSize::Size32, false);
    append_base_integer_type(&global_constants, u8"u64"_S, RegisterSize::Size64, false);

    append_base_integer_type(&global_constants, u8"i8"_S, RegisterSize::Size8, true);
    append_base_integer_type(&global_constants, u8"i16"_S, RegisterSize::Size16, true);
    append_base_integer_type(&global_constants, u8"i32"_S, RegisterSize::Size32, true);
    append_base_integer_type(&global_constants, u8"i64"_S, RegisterSize::Size64, true);

    append_base_integer_type(&global_constants, u8"usize"_S, architecture_sizes.address_size, false);
    append_base_integer_type(&global_constants, u8"isize"_S, architecture_sizes.address_size, true);

    append_base_integer_type(&global_constants, u8"uint"_S, architecture_sizes.default_integer_size, false);
    append_base_integer_type(&global_constants, u8"int"_S, architecture_sizes.default_integer_size, true);

    append_global_type(
        &global_constants,
        u8"bool"_S,
        AnyType::create_boolean()
    );

    append_global_type(
        &global_constants,
        u8"void"_S,
        AnyType::create_void()
    );

    append_global_type(
        &global_constants,
        u8"f32"_S,
        AnyType(FloatType(RegisterSize::Size32))
    );

    append_global_type(
        &global_constants,
        u8"f64"_S,
        AnyType(FloatType(RegisterSize::Size64))
    );

    append_global_type(
        &global_constants,
        u8"float"_S,
        AnyType(FloatType(architecture_sizes.default_float_size))
    );

    append_global_constant(
        &global_constants,
        u8"true"_S,
        AnyType::create_boolean(),
        AnyConstantValue(true)
    );

    append_global_constant(
        &global_constants,
        u8"false"_S,
        AnyType::create_boolean(),
        AnyConstantValue(false)
    );

    append_global_type(
        &global_constants,
        u8"type"_S,
        AnyType::create_type_type()
    );

    append_global_constant(
        &global_constants,
        u8"undef"_S,
        AnyType::create_undef(),
        AnyConstantValue::create_undef()
    );

    append_builtin(&global_constants, u8"size_of"_S);
    append_builtin(&global_constants, u8"type_of"_S);

    append_builtin(&global_constants, u8"globalify"_S);
    append_builtin(&global_constants, u8"stackify"_S);

    append_builtin(&global_constants, u8"sqrt"_S);

    append_global_constant(
        &global_constants,
        u8"X86"_S,
        AnyType::create_boolean(),
        AnyConstantValue(architecture == u8"x86"_S)
    );

    append_global_constant(
        &global_constants,
        u8"X64"_S,
        AnyType::create_boolean(),
        AnyConstantValue(architecture == u8"x64"_S)
    );

    append_global_constant(
        &global_constants,
        u8"RISCV32"_S,
        AnyType::create_boolean(),
        AnyConstantValue(architecture == u8"riscv32"_S)
    );

    append_global_constant(
        &global_constants,
        u8"RISCV64"_S,
        AnyType::create_boolean(),
        AnyConstantValue(architecture == u8"riscv64"_S)
    );

    append_global_constant(
        &global_constants,
        u8"WASM32"_S,
        AnyType::create_boolean(),
        AnyConstantValue(config == u8"wasm32"_S)
    );

    append_global_constant(
        &global_constants,
        u8"WINDOWS"_S,
        AnyType::create_boolean(),
        AnyConstantValue(os == u8"windows"_S)
    );

    append_global_constant(
        &global_constants,
        u8"LINUX"_S,
        AnyType::create_boolean(),
        AnyConstantValue(os == u8"linux"_S)
    );

    append_global_constant(
        &global_constants,
        u8"EMSCRIPTEN"_S,
        AnyType::create_boolean(),
        AnyConstantValue(os == u8"emscripten"_S)
    );

    append_global_constant(
        &global_constants,
        u8"WASI"_S,
        AnyType::create_boolean(),
        AnyConstantValue(os == u8"wasi"_S)
    );

    append_global_constant(
        &global_constants,
        u8"GNU"_S,
        AnyType::create_boolean(),
        AnyConstantValue(toolchain == u8"gnu"_S)
    );

    append_global_constant(
        &global_constants,
        u8"MSVC"_S,
        AnyType::create_boolean(),
        AnyConstantValue(toolchain == u8"msvc"_S)
    );

    append_global_constant(
        &global_constants,
        u8"DEBUG"_S,
        AnyType::create_boolean(),
        AnyConstantValue(config == u8"debug"_S)
    );

    append_global_constant(
        &global_constants,
        u8"RELEASE"_S,
        AnyType::create_boolean(),
        AnyConstantValue(config == u8"release"_S)
    );

    GlobalInfo info {
        global_constants,
        architecture_sizes
    };

    List<AnyJob> jobs {};

    size_t main_file_parse_job_index;
    {
        AnyJob job;
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = absolute_source_file_path;

        main_file_parse_job_index = jobs.append(job);
    }

    List<RuntimeStatic*> runtime_statics {};
    List<String> libraries {};

    if(os == u8"windows"_S || os == u8"mingw"_S) {
        libraries.append(u8"kernel32"_S);
    }

    auto main_function_state = JobState::Waiting;
    auto main_function_waiting_for = main_file_parse_job_index;
    Function* main_function;

    uint64_t total_parser_time = 0;
    uint64_t total_generator_time = 0;

    while(true) {
        auto did_work = false;
        for(size_t job_index = 0; job_index < jobs.length; job_index += 1) {
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
                        scope->declarations = create_declaration_hash_table(statements);
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        expect_void(process_scope(&jobs, scope, statements, nullptr, true));

                        auto end_time = get_timer_counts();

                        total_parser_time += end_time - start_time;

                        auto job_after = jobs[job_index];

                        if(print_ast) {
                            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(job_after.parse_file.path));

                            for(auto statement : statements) {
                                statement->print();
                                printf("\n");
                            }
                        }
                    } break;

                    case JobKind::ResolveStaticIf: {
                        auto resolve_static_if = job->resolve_static_if;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_static_if(info, &jobs, resolve_static_if.static_if, resolve_static_if.scope);

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_function_declaration.type = result.value.type;
                            job_after->resolve_function_declaration.value = result.value.value;

                            if(job_after->resolve_function_declaration.type.kind == TypeKind::FunctionTypeType) {
                                auto function_type = job_after->resolve_function_declaration.type.function;

                                auto function_value = job_after->resolve_function_declaration.value.unwrap_function();

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

                                    jobs.append(job);
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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_polymorphic_struct.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveUnionDefinition: {
                        auto resolve_union_definition = job->resolve_union_definition;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_union_definition(
                            info,
                            &jobs,
                            resolve_union_definition.definition,
                            resolve_union_definition.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_union_definition.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolvePolymorphicUnion: {
                        auto resolve_polymorphic_union = job->resolve_polymorphic_union;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_polymorphic_union(
                            info,
                            &jobs,
                            resolve_polymorphic_union.definition,
                            resolve_polymorphic_union.parameters,
                            resolve_polymorphic_union.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_polymorphic_union.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;
                    } break;

                    case JobKind::ResolveEnumDefinition: {
                        auto resolve_enum_definition = job->resolve_enum_definition;

                        auto start_time = get_timer_counts();

                        auto result = do_resolve_enum_definition(
                            info,
                            &jobs,
                            resolve_enum_definition.definition,
                            resolve_enum_definition.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_enum_definition.type = result.value;
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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;

                            runtime_statics.append(job_after->generate_function.function);

                            if(job_after->generate_function.function->is_external) {
                                for(auto library : job_after->generate_function.function->libraries) {
                                    auto already_registered = false;
                                    for(auto registered_library : libraries) {
                                        if(registered_library == library) {
                                            already_registered = true;
                                            break;
                                        }
                                    }

                                    if(!already_registered) {
                                        libraries.append(library);
                                    }
                                }
                            }

                            for(auto static_constant : result.value) {
                                runtime_statics.append(static_constant);
                            }
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                        }

                        auto end_time = get_timer_counts();

                        total_generator_time += end_time - start_time;

                        if(job_after->state == JobState::Done && print_ir) {
                            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(job_after->generate_function.function->path));
                            job_after->generate_function.function->print();
                            printf("\n");
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

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job_after->state = JobState::Done;
                            job_after->generate_static_variable.static_variable = result.value.static_variable;
                            job_after->generate_static_variable.type = result.value.type;

                            runtime_statics.append((RuntimeStatic*)result.value.static_variable);

                            if(job_after->generate_static_variable.static_variable->is_external) {
                                for(auto library : job_after->generate_static_variable.static_variable->libraries) {
                                    auto already_registered = false;
                                    for(auto registered_library : libraries) {
                                        if(registered_library == library) {
                                            already_registered = true;
                                            break;
                                        }
                                    }

                                    if(!already_registered) {
                                        libraries.append(library);
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
                            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(get_scope_file_path(*job_after->generate_static_variable.scope)));
                            result.value.static_variable->print();
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
                if(jobs[main_function_waiting_for].state == JobState::Done) {
                    main_function_state = JobState::Working;
                }
            }

            if(main_function_state == JobState::Working) {
                auto scope = jobs[main_file_parse_job_index].parse_file.scope;

                did_work = true;

                auto result = search_for_name(
                    info,
                    &jobs,
                    u8"main"_S,
                    calculate_string_hash(u8"main"_S),
                    scope,
                    scope->statements,
                    scope->declarations,
                    false,
                    nullptr
                );

                if(!result.has_value) {
                    main_function_state = JobState::Waiting;
                    main_function_waiting_for = result.waiting_for;
                } else {
                    if(!result.status) {
                        return err();
                    }

                    main_function_state = JobState::Done;

                    if(!result.value.found) {
                        fprintf(stderr, "Error: Cannot find 'main'\n");

                        return err();
                    }

                    if(result.value.type.kind != TypeKind::FunctionTypeType) {
                        fprintf(stderr, "Error: 'main' must be a function. Got '%.*s'\n", STRING_PRINTF_ARGUMENTS(result.value.type.get_description()));

                        return err();
                    }

                    auto function_type = result.value.type.function;

                    auto function_value = result.value.value.unwrap_function();

                    if(function_type.parameters.length != 0) {
                        error(scope, function_value.declaration->range, "'main' must have zero parameters");

                        return err();
                    }

                    auto expected_main_return_integer = AnyType(Integer(RegisterSize::Size32, true));

                    if(function_type.return_types.length != 1) {
                        error(
                            scope,
                            function_value.declaration->range,
                            "Incorrect number of return types for 'main'. Expected 1, got %zu",
                            function_type.return_types.length
                        );

                        return err();
                    }

                    if(function_type.return_types[0] != expected_main_return_integer) {
                        error(
                            scope,
                            function_value.declaration->range,
                            "Incorrect 'main' return type. Expected '%.*s', got '%.*s'",
                            STRING_PRINTF_ARGUMENTS(expected_main_return_integer.get_description()),
                            STRING_PRINTF_ARGUMENTS(function_type.return_types[0].get_description())
                        );

                        return err();
                    }

                    auto found = false;
                    for(auto job : jobs) {
                        if(job.kind == JobKind::GenerateFunction) {
                            auto generate_function = job.generate_function;

                            if(
                                AnyType(generate_function.type) == AnyType(function_type) &&
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
            }
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
                ConstantScope* scope;
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

                    case JobKind::ResolveUnionDefinition: {
                        auto resolve_union_definition = job.resolve_union_definition;

                        scope = resolve_union_definition.scope;
                        range = resolve_union_definition.definition->range;
                    } break;

                    case JobKind::ResolvePolymorphicUnion: {
                        auto resolve_polymorphic_union = job.resolve_polymorphic_union;

                        scope = resolve_polymorphic_union.scope;
                        range = resolve_polymorphic_union.definition->range;
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

        return err();
    }

    expect(output_file_directory, path_get_directory_component(output_file_path));

    String object_file_path;
    if(no_link) {
        object_file_path = output_file_path;
    } else {
        expect(full_name, path_get_file_component(output_file_path));

        auto found_dot = false;
        size_t dot_index;
        for(size_t i = 0; i < full_name.length; i += 1) {
            if(full_name[i] == '.') {
                found_dot = true;
                dot_index = i;
                break;
            }
        }

        String output_file_name;
        if(!found_dot) {
            output_file_name = full_name;
        } else {
            auto length = dot_index;

            if(length == 0) {
                output_file_name = u8"out"_S;
            } else {
                output_file_name = {};
                output_file_name.elements = full_name.elements;
                output_file_name.length = length;
            }
        }

        StringBuffer buffer {};

        buffer.append(output_file_directory);
        buffer.append(output_file_name);
        buffer.append(u8".o"_S);

        object_file_path = buffer;
    }

    uint64_t backend_time;
    String main_function_name;
    {
        List<String> reserved_names {};

        if(os == u8"emscripten"_S) {
            reserved_names.append(u8"main"_S);
        } else if(os == u8"wasi"_S) {
            reserved_names.append(u8"_start"_S);
        } else {
            reserved_names.append(u8"entry"_S);
        }

        if(os == u8"windows"_S) {
            reserved_names.append(u8"_fltused"_S);
            reserved_names.append(u8"__chkstk"_S);
        }

        auto start_time = get_timer_counts();

        expect(name_mappings, generate_llvm_object(
            source_file_path,
            runtime_statics,
            architecture,
            os,
            toolchain,
            config,
            object_file_path,
            reserved_names,
            print_llvm
        ));

        auto main_found = false;
        for(auto name_mapping : name_mappings) {
            if(name_mapping.runtime_static == main_function) {
                main_function_name = name_mapping.name;
                main_found = true;

                break;
            }
        }
        assert(main_found);

        auto end_time = get_timer_counts();

        backend_time = end_time - start_time;
    }

    uint64_t linker_time;
    if(!no_link) {
        auto start_time = get_timer_counts();

        StringBuffer command_buffer {};

        String frontend;
        if(os == u8"emscripten"_S) {
            frontend = u8"emcc"_S;
        } else {
            frontend = u8"clang"_S;
        }

        String linker_options;
        if(os == u8"windows"_S) {
            if(toolchain == u8"msvc"_S) {
                if(config == u8"debug"_S) {
                    linker_options = u8"/entry:entry,/DEBUG,/SUBSYSTEM:CONSOLE"_S;
                } else if(config == u8"release"_S) {
                    linker_options = u8"/entry:entry,/SUBSYSTEM:CONSOLE"_S;
                } else {
                    abort();
                }
            } else if(toolchain == u8"gnu"_S) {
                linker_options = u8"--entry=entry,--subsystem=console"_S;
            } else {
                abort();
            }
        } else if(os == u8"emscripten"_S) {
            linker_options = u8""_S;
        } else if(os == u8"wasi"_S) {
            linker_options = u8""_S;
        } else {
            linker_options = u8"--entry=entry"_S;
        }

        auto triple = get_llvm_triple(architecture, os, toolchain);
        auto features = get_llvm_features(architecture);

        command_buffer.append(frontend);

        if(os == u8"linux"_S) {
            command_buffer.append(u8" -pie"_S);
        }

        command_buffer.append(u8" -nostdlib -fuse-ld=lld --target="_S);

        command_buffer.append(triple);

        command_buffer.append(u8" -march="_S);

        command_buffer.append(features);

        if(linker_options.length != 0) {
            command_buffer.append(u8" -Wl,"_S); 
            command_buffer.append(linker_options);
        }

        command_buffer.append(u8" -o"_S);
        command_buffer.append(output_file_path);
        
        for(auto library : libraries) {
            command_buffer.append(u8" -l"_S);
            command_buffer.append(library);
        }

        if(os == u8"emscripten"_S) {
            command_buffer.append(u8" -lcompiler_rt"_S);
        }

        command_buffer.append(u8" "_S);
        command_buffer.append(object_file_path);

        expect(executable_path, get_executable_path());
        expect(executable_directory, path_get_directory_component(executable_path));

        auto found_runtime_source = false;
        String runtime_source_path;
        {
            StringBuffer buffer {};
            buffer.append(executable_directory);
            buffer.append(u8"runtime_"_S);
            buffer.append(os);
            buffer.append(u8"_"_S);
            buffer.append(architecture);
            buffer.append(u8".c"_S);

            auto file_test = fopen(buffer.to_c_string(), "rb");

            if(file_test != nullptr) {
                fclose(file_test);

                found_runtime_source = true;
                runtime_source_path = buffer;
            }
        }
        if(!found_runtime_source) {
            StringBuffer buffer {};
            buffer.append(executable_directory);
            buffer.append(u8"../share/simple-compiler/runtime_"_S);
            buffer.append(os);
            buffer.append(u8"_"_S);
            buffer.append(architecture);
            buffer.append(u8".c"_S);

            auto file_test = fopen(buffer.to_c_string(), "rb");

            if(file_test != nullptr) {
                fclose(file_test);

                found_runtime_source = true;
                runtime_source_path = buffer;
            }
        }

        if(!found_runtime_source) {
            fprintf(stderr, "Error: Unable to locate runtime source file\n");
        }

        StringBuffer runtime_command_buffer {};

        runtime_command_buffer.append(u8"clang -std=gnu99 -ffreestanding -nostdinc -c -target "_S);

        runtime_command_buffer.append(triple);

        command_buffer.append(u8" -march="_S);

        command_buffer.append(features);

        runtime_command_buffer.append(u8" -DMAIN="_S);
        runtime_command_buffer.append(main_function_name);
        
        runtime_command_buffer.append(u8" -o "_S);
        runtime_command_buffer.append(output_file_directory);
        runtime_command_buffer.append(u8"runtime.o "_S);

        runtime_command_buffer.append(runtime_source_path);

        enter_region("clang");
        if(system(runtime_command_buffer.to_c_string()) != 0) {
            fprintf(stderr, "Error: 'clang' returned non-zero while compiling runtime\n");

            return err();
        }
        leave_region();

        command_buffer.append(u8" "_S);
        command_buffer.append(output_file_directory);
        command_buffer.append(u8"runtime.o"_S);

        enter_region("linker");

        if(system(command_buffer.to_c_string()) != 0) {
            fprintf(stderr, "Error: '%.*s' returned non-zero while linking\n", STRING_PRINTF_ARGUMENTS(frontend));

            return err();
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

    return ok();
}

int main(int argument_count, const char* arguments[]) {
#if defined(PROFILING)
    init_profiler();
#endif

    Array<const char*> arguments_array {};
    arguments_array.length = (size_t)argument_count;
    arguments_array.elements = arguments;

    if(cli_entry(arguments_array).status) {
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