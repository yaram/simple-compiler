#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
#include "typed_tree_generator.h"
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

    Arena global_arena {};

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

            auto result = String::from_c_string(&global_arena, argument);
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

            auto result = String::from_c_string(&global_arena, arguments[argument_index]);
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

            auto result = String::from_c_string(&global_arena, arguments[argument_index]);
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

            auto result = String::from_c_string(&global_arena, arguments[argument_index]);
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

            auto result = String::from_c_string(&global_arena, arguments[argument_index]);
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

            auto result = String::from_c_string(&global_arena, arguments[argument_index]);
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

    expect(absolute_source_file_path, path_relative_to_absolute(&global_arena, source_file_path));

    if(!has_output_file_path) {
        output_file_path = get_default_output_file(os, no_link);
    }

    auto architecture_sizes = get_architecture_sizes(architecture);

    List<GlobalConstant> global_constants(&global_arena);

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

    List<AnyJob*> jobs(&global_arena);

    size_t main_file_parse_job_index;
    {
        AnyJob job {};
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = absolute_source_file_path;

        main_file_parse_job_index = jobs.append(global_arena.heapify(job));
    }

    uint64_t total_parser_time = 0;
    uint64_t total_generator_time = 0;

    while(true) {
        auto did_work = false;
        for(size_t job_index = 0; job_index < jobs.length; job_index += 1) {
            auto job = jobs[job_index];

            if(job->state != JobState::Done) {
                if(job->state == JobState::Waiting) {
                    if(jobs[job->waiting_for]->state != JobState::Done) {
                        continue;
                    }

                    job->state = JobState::Working;
                }

                switch(job->kind) {
                    case JobKind::ParseFile: {
                        auto parse_file = &job->parse_file;

                        auto start_time = get_timer_counts();

                        expect(tokens, tokenize_source(&job->arena, parse_file->path));

                        expect(statements, parse_tokens(&job->arena, parse_file->path, tokens));

                        auto scope = global_arena.allocate_and_construct<ConstantScope>();
                        scope->statements = statements;
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        expect_void(process_scope(&global_arena, &jobs, scope, statements, nullptr, true));

                        auto end_time = get_timer_counts();

                        total_parser_time += end_time - start_time;

                        auto job = jobs[job_index];

                        if(print_ast) {
                            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(job->parse_file.path));

                            for(auto statement : statements) {
                                statement->print();
                                printf("\n");
                            }
                        }
                    } break;

                    case JobKind::TypeStaticIf: {
                        auto type_static_if = job->type_static_if;

                        auto result = do_type_static_if(
                            info,
                            &jobs,
                            &global_arena,
                            &job->arena,
                            type_static_if.static_if,
                            type_static_if.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_static_if.condition = result.value.condition;
                            job->type_static_if.condition_value = result.value.condition_value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeFunctionDeclaration: {
                        auto type_function_declaration = job->type_function_declaration;

                        auto result = do_type_function_declaration(
                            info,
                            &jobs,
                            &global_arena,
                            &job->arena,
                            type_function_declaration.declaration,
                            type_function_declaration.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_function_declaration.parameters = result.value.parameters;
                            job->type_function_declaration.return_types = result.value.return_types;
                            job->type_function_declaration.type = result.value.type;
                            job->type_function_declaration.value = result.value.value;

                            if(job->type_function_declaration.type.kind == TypeKind::FunctionTypeType) {
                                auto function_type = job->type_function_declaration.type.function;

                                auto function_value = job->type_function_declaration.value.unwrap_function();

                                auto found = false;
                                for(auto job : jobs) {
                                    if(job->kind == JobKind::TypeFunctionBody) {
                                        auto type_function_body = job->type_function_body;

                                        if(
                                            type_function_body.value.declaration == function_value.declaration &&
                                            type_function_body.value.body_scope == function_value.body_scope
                                        ) {
                                            found = true;
                                            break;
                                        }
                                    }
                                }

                                if(!found) {
                                    AnyJob new_job {};
                                    new_job.kind = JobKind::TypeFunctionBody;
                                    new_job.state = JobState::Working;
                                    new_job.type_function_body.type = function_type;
                                    new_job.type_function_body.value = function_value;

                                    jobs.append(global_arena.heapify(new_job));
                                }
                            }
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypePolymorphicFunction: {
                        auto type_polymorphic_function = job->type_polymorphic_function;

                        auto result = do_type_polymorphic_function(
                            info,
                            &jobs,
                            &global_arena,
                            &job->arena,
                            type_polymorphic_function.declaration,
                            type_polymorphic_function.parameters,
                            type_polymorphic_function.scope,
                            type_polymorphic_function.call_scope,
                            type_polymorphic_function.call_parameter_ranges
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_polymorphic_function.type = result.value.type;
                            job->type_polymorphic_function.value = result.value.value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeConstantDefinition: {
                        auto type_constant_definition = job->type_constant_definition;

                        auto result = do_type_constant_definition(
                            info,
                            &jobs,
                            &global_arena,
                            &job->arena,
                            type_constant_definition.definition,
                            type_constant_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_constant_definition.value = result.value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeStructDefinition: {
                        auto type_struct_definition = job->type_struct_definition;

                        auto result = do_type_struct_definition(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_struct_definition.definition,
                            type_struct_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_struct_definition.members = result.value.members;
                            job->type_struct_definition.type = result.value.type;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypePolymorphicStruct: {
                        auto type_polymorphic_struct = job->type_polymorphic_struct;

                        auto result = do_type_polymorphic_struct(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_polymorphic_struct.definition,
                            type_polymorphic_struct.parameters,
                            type_polymorphic_struct.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_polymorphic_struct.type = result.value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeUnionDefinition: {
                        auto type_union_definition = job->type_union_definition;

                        auto result = do_type_union_definition(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_union_definition.definition,
                            type_union_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_union_definition.members = result.value.members;
                            job->type_union_definition.type = result.value.type;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypePolymorphicUnion: {
                        auto type_polymorphic_union = job->type_polymorphic_union;

                        auto result = do_type_polymorphic_union(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_polymorphic_union.definition,
                            type_polymorphic_union.parameters,
                            type_polymorphic_union.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_polymorphic_union.type = result.value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeEnumDefinition: {
                        auto type_enum_definition = job->type_enum_definition;

                        auto result = do_type_enum_definition(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_enum_definition.definition,
                            type_enum_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_enum_definition.backing_type = result.value.backing_type;
                            job->type_enum_definition.variants = result.value.variants;
                            job->type_enum_definition.type = result.value.type;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeFunctionBody: {
                        auto type_function_body = job->type_function_body;

                        auto result = do_type_function_body(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_function_body.type,
                            type_function_body.value
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_function_body.statements = result.value;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
                    } break;

                    case JobKind::TypeStaticVariable: {
                        auto type_static_variable = job->type_static_variable;

                        auto result = do_type_static_variable(
                            info,
                            &jobs,
                            &job->arena,
                            &global_arena,
                            type_static_variable.declaration,
                            type_static_variable.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_static_variable.is_external = result.value.is_external;
                            job->type_static_variable.is_no_mangle = result.value.is_no_mangle;
                            job->type_static_variable.type = result.value.type;
                            job->type_static_variable.initializer = result.value.initializer;
                            job->type_static_variable.actual_type = result.value.actual_type;
                            job->type_static_variable.external_libraries = result.value.external_libraries;
                        } else {
                            job->state = JobState::Waiting;
                            job->waiting_for = result.waiting_for;
                            job->arena.reset();
                        }
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
        if(job->state != JobState::Done) {
            all_jobs_done = false;
        }
    }

    if(!all_jobs_done) {
        fprintf(stderr, "Error: Circular dependency detected!\n");
        fprintf(stderr, "Error: The following areas depend on eathother:\n");

        for(auto job : jobs) {
            if(job->state != JobState::Done) {
                ConstantScope* scope;
                FileRange range;
                switch(job->kind) {
                    case JobKind::ParseFile: {
                        abort();
                    } break;

                    case JobKind::TypeStaticIf: {
                        auto type_static_if = job->type_static_if;

                        scope = type_static_if.scope;
                        range = type_static_if.static_if->range;
                    } break;

                    case JobKind::TypeFunctionDeclaration: {
                        auto type_function_declaration = job->type_function_declaration;

                        scope = type_function_declaration.scope;
                        range = type_function_declaration.declaration->range;
                    } break;

                    case JobKind::TypePolymorphicFunction: {
                        auto type_polymorphic_function = job->type_polymorphic_function;

                        scope = type_polymorphic_function.scope;
                        range = type_polymorphic_function.declaration->range;
                    } break;

                    case JobKind::TypeConstantDefinition: {
                        auto type_constant_definition = job->type_constant_definition;

                        scope = type_constant_definition.scope;
                        range = type_constant_definition.definition->range;
                    } break;

                    case JobKind::TypeStructDefinition: {
                        auto type_struct_definition = job->type_struct_definition;

                        scope = type_struct_definition.scope;
                        range = type_struct_definition.definition->range;
                    } break;

                    case JobKind::TypePolymorphicStruct: {
                        auto type_polymorphic_struct = job->type_polymorphic_struct;

                        scope = type_polymorphic_struct.scope;
                        range = type_polymorphic_struct.definition->range;
                    } break;

                    case JobKind::TypeUnionDefinition: {
                        auto type_union_definition = job->type_union_definition;

                        scope = type_union_definition.scope;
                        range = type_union_definition.definition->range;
                    } break;

                    case JobKind::TypePolymorphicUnion: {
                        auto type_polymorphic_union = job->type_polymorphic_union;

                        scope = type_polymorphic_union.scope;
                        range = type_polymorphic_union.definition->range;
                    } break;

                    case JobKind::TypeFunctionBody: {
                        auto type_function_body = job->type_function_body;

                        scope = type_function_body.value.body_scope->parent;
                        range = type_function_body.value.declaration->range;
                    } break;

                    case JobKind::TypeStaticVariable: {
                        auto type_static_variable = job->type_static_variable;

                        scope = type_static_variable.scope;
                        range = type_static_variable.declaration->range;
                    } break;

                    default: abort();
                }

                error(scope, range, "Here");
            }
        }

        return err();
    }

    auto main_search_result = search_for_main(
        info,
        &jobs,
        &global_arena,
        &global_arena,
        jobs[main_file_parse_job_index]->parse_file.scope
    );

    assert(main_search_result.has_value);

    if(!main_search_result.status) {
        return err();
    }

    List<TypedFunction> typed_functions(&global_arena);
    List<TypedStaticVariable> typed_static_variables(&global_arena);
    for(auto job : jobs) {
        if(job->kind == JobKind::TypeFunctionDeclaration) {
            if(job->type_function_declaration.type.kind == TypeKind::FunctionTypeType) {
                TypedFunction typed_function {};
                typed_function.type = job->type_function_declaration.type.function;
                typed_function.constant = job->type_function_declaration.value.unwrap_function();

                typed_function.function = global_arena.allocate_and_construct<Function>();

                typed_functions.append(typed_function);
            }
        } else if(job->kind == JobKind::TypePolymorphicFunction) {
            TypedFunction typed_function {};
            typed_function.type = job->type_polymorphic_function.type;
            typed_function.constant = job->type_polymorphic_function.value;

            typed_function.function = global_arena.allocate_and_construct<Function>();

            typed_functions.append(typed_function);
        } else if(job->kind == JobKind::TypeStaticVariable) {
            TypedStaticVariable typed_static_variable {};
            typed_static_variable.type = job->type_static_variable.actual_type;
            typed_static_variable.scope = job->type_static_variable.scope;
            typed_static_variable.declaration = job->type_static_variable.declaration;

            typed_static_variable.static_variable = global_arena.allocate_and_construct<StaticVariable>();

            typed_static_variables.append(typed_static_variable);
        }
    }

    List<RuntimeStatic*> runtime_statics(&global_arena);
    List<String> libraries(&global_arena);

    if(os == u8"windows"_S || os == u8"mingw"_S) {
        libraries.append(u8"kernel32"_S);
    }

    auto found_main_function = false;
    Function* main_function;
    for(auto typed_function : typed_functions) {
        auto start_time = get_timer_counts();

        if(
            AnyType(typed_function.type) == AnyType(main_search_result.value.type) &&
            typed_function.constant.body_scope == main_search_result.value.value.body_scope &&
            typed_function.constant.declaration == main_search_result.value.value.declaration
        ) {
            found_main_function = true;
            main_function = typed_function.function;
        }

        auto found = false;
        Array<TypedStatement> statements;
        for(auto job : jobs) {
            if(
                job->kind == JobKind::TypeFunctionBody &&
                AnyType(typed_function.type) == AnyType(job->type_function_body.type) &&
                typed_function.constant.body_scope == job->type_function_body.value.body_scope &&
                typed_function.constant.declaration == job->type_function_body.value.declaration
            ) {
                found = true;
                statements = job->type_function_body.statements;
                break;
            }
        }

        assert(found);

        auto static_constants = do_generate_function(
            info,
            typed_functions,
            typed_static_variables,
            &global_arena,
            typed_function.type,
            typed_function.constant,
            statements,
            typed_function.function
        );

        runtime_statics.append(typed_function.function);

        if(typed_function.function->is_external) {
            for(auto library : typed_function.function->libraries) {
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

        for(auto static_constant : static_constants) {
            runtime_statics.append(static_constant);
        }

        auto end_time = get_timer_counts();

        total_generator_time += end_time - start_time;

        if(print_ir) {
            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(typed_function.function->path));
            typed_function.function->print();
            printf("\n");
        }
    }

    assert(found_main_function);

    for(auto typed_static_variable : typed_static_variables) {
        auto start_time = get_timer_counts();

        auto found = false;
        bool is_external;
        bool is_no_mangle;
        TypedExpression type;
        TypedExpression initializer;
        AnyType actual_type;
        Array<String> external_libraries;
        for(auto job : jobs) {
            if(
                job->kind == JobKind::TypeStaticVariable &&
                typed_static_variable.scope == job->type_static_variable.scope &&
                typed_static_variable.declaration == job->type_static_variable.declaration
            ) {
                found = true;
                is_external = job->type_static_variable.is_external;
                is_no_mangle = job->type_static_variable.is_no_mangle;
                type = job->type_static_variable.type;
                initializer = job->type_static_variable.initializer;
                actual_type = job->type_static_variable.actual_type;
                external_libraries = job->type_static_variable.external_libraries;
                break;
            }
        }

        assert(found);

        do_generate_static_variable(
            info,
            &global_arena,
            typed_static_variable.declaration,
            typed_static_variable.scope,
            is_external,
            is_no_mangle,
            type,
            initializer,
            actual_type,
            external_libraries,
            typed_static_variable.static_variable
        );

        runtime_statics.append(typed_static_variable.static_variable);

        if(typed_static_variable.static_variable->is_external) {
            for(auto library : typed_static_variable.static_variable->libraries) {
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

        auto end_time = get_timer_counts();

        total_generator_time += end_time - start_time;

        if(print_ir) {
            printf("%.*s:\n", STRING_PRINTF_ARGUMENTS(typed_static_variable.scope->get_file_path()));
            typed_static_variable.static_variable->print();
            printf("\n");
        }
    }

    expect(output_file_directory, path_get_directory_component(&global_arena, output_file_path));

    String object_file_path;
    if(no_link) {
        object_file_path = output_file_path;
    } else {
        expect(full_name, path_get_file_component(&global_arena, output_file_path));

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

        StringBuffer buffer(&global_arena);

        buffer.append(output_file_directory);
        buffer.append(output_file_name);
        buffer.append(u8".o"_S);

        object_file_path = buffer;
    }

    uint64_t backend_time;
    String main_function_name;
    {
        List<String> reserved_names(&global_arena);

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
            &global_arena,
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

        StringBuffer command_buffer(&global_arena);

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

        auto triple = get_llvm_triple(&global_arena, architecture, os, toolchain);
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

        expect(executable_path, get_executable_path(&global_arena));
        expect(executable_directory, path_get_directory_component(&global_arena, executable_path));

        auto found_runtime_source = false;
        String runtime_source_path;
        {
            StringBuffer buffer(&global_arena);
            buffer.append(executable_directory);
            buffer.append(u8"runtime_"_S);
            buffer.append(os);
            buffer.append(u8"_"_S);
            buffer.append(architecture);
            buffer.append(u8".c"_S);

            auto file_test = fopen(buffer.to_c_string(&global_arena), "rb");

            if(file_test != nullptr) {
                fclose(file_test);

                found_runtime_source = true;
                runtime_source_path = buffer;
            }
        }
        if(!found_runtime_source) {
            StringBuffer buffer(&global_arena);
            buffer.append(executable_directory);
            buffer.append(u8"../share/simple-compiler/runtime_"_S);
            buffer.append(os);
            buffer.append(u8"_"_S);
            buffer.append(architecture);
            buffer.append(u8".c"_S);

            auto file_test = fopen(buffer.to_c_string(&global_arena), "rb");

            if(file_test != nullptr) {
                fclose(file_test);

                found_runtime_source = true;
                runtime_source_path = buffer;
            }
        }

        if(!found_runtime_source) {
            fprintf(stderr, "Error: Unable to locate runtime source file\n");
        }

        StringBuffer runtime_command_buffer(&global_arena);

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
        if(system(runtime_command_buffer.to_c_string(&global_arena)) != 0) {
            fprintf(stderr, "Error: 'clang' returned non-zero while compiling runtime\n");

            return err();
        }
        leave_region();

        command_buffer.append(u8" "_S);
        command_buffer.append(output_file_directory);
        command_buffer.append(u8"runtime.o"_S);

        enter_region("linker");

        if(system(command_buffer.to_c_string(&global_arena)) != 0) {
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