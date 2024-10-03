#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "constant.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"
#include "platform.h"
#include "list.h"
#include "jobs.h"
#include "hl_generator.h"
#include "types.h"
#include "path.h"

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

static CJSON_CDECL Arena* cjson_arena;

static void* cjson_malloc(size_t sz) {
    return cjson_arena->allocate_memory(sz);
}

static void cjson_free(void* ptr) {}

static void error_handler(void* data, String path, FileRange range, const char* format, va_list arguments) {

}

int main(int argument_count, const char* arguments[]) {
    Arena global_arena {};

    auto architecture = get_host_architecture();
    auto os = get_host_os();
    auto toolchain = get_default_toolchain(os);

    auto config = u8"debug"_S;
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

    register_error_handler(&error_handler, nullptr);

    Arena compilation_arena {};

    cjson_arena = &compilation_arena;

    cJSON_Hooks cjson_hooks {};
    cjson_hooks.malloc_fn = &cjson_malloc;
    cjson_hooks.free_fn = &cjson_free;
    cJSON_InitHooks(&cjson_hooks);

    {
        compilation_arena.reset();

        List<AnyJob> jobs(&compilation_arena);

        size_t main_file_parse_job_index;
        {
            AnyJob job {};
            job.kind = JobKind::ParseFile;
            job.state = JobState::Working;
            job.parse_file.path = absolute_source_file_path;

            main_file_parse_job_index = jobs.append(job);
        }

        List<RuntimeStatic*> runtime_statics(&compilation_arena);
        List<String> libraries(&compilation_arena);

        if(os == u8"windows"_S || os == u8"mingw"_S) {
            libraries.append(u8"kernel32"_S);
        }

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

                            expect(tokens, tokenize_source(&job->arena, parse_file->path));

                            expect(statements, parse_tokens(&job->arena, parse_file->path, tokens));

                            auto scope = compilation_arena.allocate_and_construct<ConstantScope>();
                            scope->statements = statements;
                            scope->declarations = create_declaration_hash_table(&compilation_arena, statements);
                            scope->scope_constants = {};
                            scope->is_top_level = true;
                            scope->file_path = parse_file->path;

                            parse_file->scope = scope;
                            job->state = JobState::Done;

                            expect_void(process_scope(&compilation_arena, &jobs, scope, statements, nullptr, true));

                            auto job_after = jobs[job_index];
                        } break;

                        case JobKind::ResolveStaticIf: {
                            auto resolve_static_if = job->resolve_static_if;

                            auto result = do_resolve_static_if(
                                info,
                                &jobs,
                                &compilation_arena,
                                &job->arena,
                                resolve_static_if.static_if,
                                resolve_static_if.scope
                            );

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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolveFunctionDeclaration: {
                            auto resolve_function_declaration = job->resolve_function_declaration;

                            auto result = do_resolve_function_declaration(
                                info,
                                &jobs,
                                &compilation_arena,
                                &job->arena,
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
                                        AnyJob job {};
                                        job.kind = JobKind::GenerateFunction;
                                        job.state = JobState::Working;
                                        job.generate_function.type = function_type;
                                        job.generate_function.value = function_value;
                                        job.generate_function.function = compilation_arena.allocate_and_construct<Function>();

                                        jobs.append(job);
                                    }
                                }
                            } else {
                                job_after->state = JobState::Waiting;
                                job_after->waiting_for = result.waiting_for;
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolvePolymorphicFunction: {
                            auto resolve_polymorphic_function = job->resolve_polymorphic_function;

                            auto result = do_resolve_polymorphic_function(
                                info,
                                &jobs,
                                &compilation_arena,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolveConstantDefinition: {
                            auto resolve_constant_definition = job->resolve_constant_definition;

                            auto result = evaluate_constant_expression(
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolveStructDefinition: {
                            auto resolve_struct_definition = job->resolve_struct_definition;

                            auto result = do_resolve_struct_definition(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolvePolymorphicStruct: {
                            auto resolve_polymorphic_struct = job->resolve_polymorphic_struct;

                            auto result = do_resolve_polymorphic_struct(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolveUnionDefinition: {
                            auto resolve_union_definition = job->resolve_union_definition;

                            auto result = do_resolve_union_definition(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolvePolymorphicUnion: {
                            auto resolve_polymorphic_union = job->resolve_polymorphic_union;

                            auto result = do_resolve_polymorphic_union(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::ResolveEnumDefinition: {
                            auto resolve_enum_definition = job->resolve_enum_definition;

                            auto result = do_resolve_enum_definition(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::GenerateFunction: {
                            auto generate_function = job->generate_function;

                            auto result = do_generate_function(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
                            }
                        } break;

                        case JobKind::GenerateStaticVariable: {
                            auto generate_static_variable = job->generate_static_variable;

                            auto result = do_generate_static_variable(
                                info,
                                &jobs,
                                &job->arena,
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
                                job_after->arena.reset();
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
            if(job.state != JobState::Done) {
                all_jobs_done = false;
            }
        }

        if(!all_jobs_done) {
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

        return ok();
    }
}