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
#include "typed_tree_generator.h"
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

struct Error {
    String path;
    FileRange range;
    String text;
};

struct ErrorHandlerState {
    Arena* arena;
    List<Error>* errors;
};

static void error_handler(void* data, String path, FileRange range, const char* format, va_list arguments) {
    auto state = (ErrorHandlerState*)data;

    Error error {};
    error.path = path;
    error.range = range;

    char buffer[1024];
    vsnprintf(buffer, 1024, format, arguments);

    auto result = String::from_c_string(state->arena, buffer);
    if(result.status) {
        error.text = result.value;
    } else {
        error.text = u8"Unable to allocate error string"_S;
    }

    state->errors->append(error);
}

struct CompiledFile {
    String absolute_path;

    Arena compilation_arena;

    Array<AnyJob> jobs;

    Array<Error> errors;
};

struct CompileSourceFileResult {
    bool status;

    CompiledFile file;
};

static CompileSourceFileResult compile_source_file(GlobalInfo info, String absolute_path) {
    Arena compilation_arena {};

    List<Error> errors(&compilation_arena);

    ErrorHandlerState error_handler_state {};
    error_handler_state.arena = &compilation_arena;
    error_handler_state.errors = &errors;

    register_error_handler(&error_handler, &error_handler_state);

    errors.length = 0;

    List<AnyJob> jobs(&compilation_arena);

    {
        AnyJob job {};
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = absolute_path;

        jobs.append(job);
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

                        auto tokens_result = tokenize_source(&job->arena, parse_file->path);
                        if(!tokens_result.status) {
                            CompiledFile file {};
                            file.absolute_path = absolute_path;
                            file.compilation_arena = compilation_arena;
                            file.jobs = jobs;
                            file.errors = errors;

                            CompileSourceFileResult result {};
                            result.status = false;
                            result.file = file;

                            return result;
                        }

                        auto statements_result = parse_tokens(&job->arena, parse_file->path, tokens_result.value);
                        if(!statements_result.status) {
                            CompiledFile file {};
                            file.absolute_path = absolute_path;
                            file.compilation_arena = compilation_arena;
                            file.jobs = jobs;
                            file.errors = errors;

                            CompileSourceFileResult result {};
                            result.status = false;
                            result.file = file;

                            return result;
                        }

                        auto scope = compilation_arena.allocate_and_construct<ConstantScope>();
                        scope->statements = statements_result.value;
                        scope->declarations = create_declaration_hash_table(&compilation_arena, statements_result.value);
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        auto result = process_scope(&compilation_arena, &jobs, scope, statements_result.value, nullptr, true);
                        if(!result.status) {
                            CompiledFile file {};
                            file.absolute_path = absolute_path;
                            file.compilation_arena = compilation_arena;
                            file.jobs = jobs;
                            file.errors = errors;

                            CompileSourceFileResult result {};
                            result.status = false;
                            result.file = file;

                            return result;
                        }

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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_function_declaration.type = result.value.type;
                            job_after->resolve_function_declaration.value = result.value.value;

                            if(job_after->resolve_function_declaration.type.kind == TypeKind::FunctionTypeType) {
                                auto function_type = job_after->resolve_function_declaration.type.function;

                                auto function_value = job_after->resolve_function_declaration.value.unwrap_function();

                                auto found = false;
                                for(auto job : jobs) {
                                    if(job.kind == JobKind::TypeFunctionBody) {
                                        auto type_function_body = job.type_function_body;

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
                                    AnyJob job {};
                                    job.kind = JobKind::TypeFunctionBody;
                                    job.state = JobState::Working;
                                    job.type_function_body.type = function_type;
                                    job.type_function_body.value = function_value;

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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
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
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
                            }

                            job_after->state = JobState::Done;
                            job_after->resolve_enum_definition.type = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                            job_after->arena.reset();
                        }
                    } break;

                    case JobKind::TypeFunctionBody: {
                        auto type_function_body = job->type_function_body;

                        auto result = do_type_function_body(
                            info,
                            &jobs,
                            &job->arena,
                            type_function_body.type,
                            type_function_body.value
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
                            }

                            job_after->state = JobState::Done;
                            job_after->type_function_body.statements = result.value;
                        } else {
                            job_after->state = JobState::Waiting;
                            job_after->waiting_for = result.waiting_for;
                            job_after->arena.reset();
                        }
                    } break;

                    case JobKind::TypeStaticVariable: {
                        auto type_static_variable = job->type_static_variable;

                        auto result = do_type_static_variable(
                            info,
                            &jobs,
                            &job->arena,
                            type_static_variable.declaration,
                            type_static_variable.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                CompiledFile file {};
                                file.absolute_path = absolute_path;
                                file.compilation_arena = compilation_arena;
                                file.jobs = jobs;
                                file.errors = errors;

                                CompileSourceFileResult result {};
                                result.status = false;
                                result.file = file;

                                return result;
                            }

                            job_after->state = JobState::Done;
                            job_after->type_static_variable.type = result.value;
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

                    case JobKind::TypeFunctionBody: {
                        auto type_function_body = job.type_function_body;

                        scope = type_function_body.value.body_scope->parent;
                        range = type_function_body.value.declaration->range;
                    } break;

                    case JobKind::TypeStaticVariable: {
                        auto type_static_variable = job.type_static_variable;

                        scope = type_static_variable.scope;
                        range = type_static_variable.declaration->range;
                    } break;

                    default: abort();
                }

                error(scope, range, "Circular dependency detected");
            }
        }

        CompiledFile file {};
        file.absolute_path = absolute_path;
        file.compilation_arena = compilation_arena;
        file.jobs = jobs;
        file.errors = errors;

        CompileSourceFileResult result {};
        result.status = false;
        result.file = file;

        return result;
    }

    CompiledFile file {};
    file.absolute_path = absolute_path;
    file.compilation_arena = compilation_arena;
    file.jobs = jobs;
    file.errors = errors;

    CompileSourceFileResult result {};
    result.status = true;
    result.file = file;

    return result;
}

enum ErrorCode {
	UnknownErrorCode = -32001,
	ServerNotInitialized = -32002,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ParseError = -32700,
    RequestCancelled = -32800,
    ContentModified = -32801,
    ServerCancelled = -32802,
    RequestFailed = -32803,
};

static void send_success_response(Arena* arena, cJSON* id, cJSON* result) {
    auto json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "jsonrpc", "2.0");

    cJSON_AddItemToObject(json, "id", id);

    cJSON_AddItemToObject(json, "result", result);

    auto json_text = cJSON_PrintUnformatted(json);

    printf("Content-Length: %zu\r\n", strlen(json_text));
    printf("\r\n");
    fputs(json_text, stdout);
    fflush(stdout);
}

static void send_error_response(Arena* arena, cJSON* id, ErrorCode error_code, String error_message) {
    auto json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "jsonrpc", "2.0");

    if(id == nullptr) {
        cJSON_AddNullToObject(json, "id");
    } else {
        cJSON_AddItemToObject(json, "id", id);
    }

    auto error = cJSON_CreateObject();

    cJSON_AddNumberToObject(error, "code", error_code);

    cJSON_AddStringToObject(error, "message", error_message.to_c_string(arena));

    cJSON_AddItemToObject(json, "error", error);

    auto json_text = cJSON_PrintUnformatted(json);

    printf("Content-Length: %zu\r\n", strlen(json_text));
    printf("\r\n");
    fputs(json_text, stdout);
    fflush(stdout);
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

    cJSON_Hooks cjson_hooks {};
    cjson_hooks.malloc_fn = &cjson_malloc;
    cjson_hooks.free_fn = &cjson_free;
    cJSON_InitHooks(&cjson_hooks);

    List<CompiledFile> compiled_files(&global_arena);

    Arena request_arena {};

    cjson_arena = &request_arena;

    auto is_initialized = false;

    bool has_root_uri;
    String root_uri;

    List<uint8_t> header_line_buffer(&global_arena);
    while(true) {
        request_arena.reset();

        auto valid_header = true;
        auto found_content_length = false;
        size_t content_length;
        while(true) {
            header_line_buffer.length = 0;

            bool valid_header_line;
            while(true) {
                auto character = fgetc(stdin);

                if(character == EOF) {
                    return 1;
                }

                if(character == '\r') {
                    auto character = fgetc(stdin);

                    if(character == EOF) {
                        return 1;
                    }

                    if(character == '\n') {
                        valid_header_line = true;
                    } else {
                        valid_header_line = false;
                    }

                    break;
                } else if(character == '\n') {
                    valid_header_line = false;
                    break;
                }

                header_line_buffer.append((uint8_t)character);
            }

            if(!valid_header_line) {
                valid_header = false;
                break;
            }

            if(!validate_ascii_string(header_line_buffer.elements, header_line_buffer.length).status) {
                valid_header = false;
                break;
            }

            String line {};
            line.elements = (char8_t*)header_line_buffer.elements;
            line.length = header_line_buffer.length;

            if(line.length == 0) {
                break;
            }

            auto found_separator = false;
            size_t separator_index;
            for(size_t i = 0; i < line.length; i += 1) {
                if(line[i] == ':') {
                    separator_index = i;
                    found_separator = true;
                    break;
                }
            }

            if(!found_separator) {
                valid_header = false;
                break;
            }

            auto field_name = line.slice(0, separator_index).strip_whitespace();
            auto field_value = line.slice(separator_index + 1).strip_whitespace();

            if(field_name == u8"Content-Length"_S) {
                if(sscanf(field_value.to_c_string(&request_arena), "%zu", &content_length) != 1) {
                    valid_header = false;
                    break;
                }

                found_content_length = true;
            } else if(field_name == u8"Content-Type"_S) {
                if(field_value != u8"application/vscode-jsonrpc; charset=utf-8"_S) {
                    valid_header = false;
                    break;
                }
            } else {
                valid_header = false;
                break;
            }
        }

        if(!valid_header || !found_content_length) {
            send_error_response(&request_arena, nullptr, ErrorCode::UnknownErrorCode, u8"Invalid message header received"_S);
            continue;
        }

        auto content = request_arena.allocate<uint8_t>(content_length);

        if(fread(content, content_length, 1, stdin) != 1) {
            return 1;
        }

        if(!validate_utf8_string(content, content_length).status) {
            send_error_response(&request_arena, nullptr, ErrorCode::UnknownErrorCode, u8"Message content is not valid UTF-8"_S);
            continue;
        }

        auto json = cJSON_ParseWithLength((char*)content, content_length);
        if(json == nullptr) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message content is not valid JSON"_S);
            continue;
        }
        
        if(!cJSON_IsObject(json)) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body is not an object"_S);
            continue;
        }

        auto jsonrpc_item = cJSON_GetObjectItem(json, "jsonrpc");
        if(jsonrpc_item == nullptr) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body is missing \"jsonrpc\" attribute"_S);
            continue;
        }

        if(!cJSON_IsString(jsonrpc_item)) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"jsonrpc\" attribute is not a string"_S);
            continue;
        }

        auto jsonrpc_result = String::from_c_string(cJSON_GetStringValue(jsonrpc_item));
        if(!jsonrpc_result.status) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"jsonrpc\" attribute is not a valid UTF-8 string"_S);
            continue;
        }

        if(jsonrpc_result.value != u8"2.0"_S) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"jsonrpc\" attribute is not \"2.0\""_S);
            continue;
        }

        auto method_item = cJSON_GetObjectItem(json, "method");
        if(method_item == nullptr) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body is missing \"method\" attribute"_S);
            continue;
        }

        if(!cJSON_IsString(method_item)) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"method\" attribute is not a string"_S);
            continue;
        }

        auto method_result = String::from_c_string(cJSON_GetStringValue(method_item));
        if(!method_result.status) {
            send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"method\" attribute is not a valid UTF-8 string"_S);
            continue;
        }

        auto method = method_result.value;

        auto id = cJSON_DetachItemFromObject(json, "id");

        if(id != nullptr) {
            if(!cJSON_IsString(id) && !cJSON_IsNumber(id)) {
                send_error_response(&request_arena, nullptr, ErrorCode::ParseError, u8"Message body \"id\" attribute is incorrect type"_S);
                continue;
            }
        }

        auto params_item = cJSON_GetObjectItem(json, "params");

        if(method == u8"initialize"_S) {
            if(id == nullptr) {
                send_error_response(&request_arena, nullptr, ErrorCode::InvalidRequest, u8"Message body \"id\" attribute is missing"_S);
                continue;
            }

            if(is_initialized) {
                send_error_response(&request_arena, id, ErrorCode::RequestFailed, u8"Server has already been initialized"_S);
                continue;
            }

            if(!cJSON_IsObject(params_item)) {
                send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters should be an object"_S);
                continue;
            }

            auto process_id_item = cJSON_GetObjectItem(params_item, "processId");
            if(process_id_item == nullptr) {
                send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters \"processId\" attribute is missing"_S);
                continue;
            }

            if(!cJSON_IsNumber(process_id_item) && !cJSON_IsNull(process_id_item)) {
                send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters \"processId\" attribute is incorrect type"_S);
                continue;
            }

            auto root_uri_item = cJSON_GetObjectItem(params_item, "rootUri");
            if(root_uri_item == nullptr) {
                send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters \"rootUri\" attribute is missing"_S);
                continue;
            }

            if(cJSON_IsString(root_uri_item)) {
                auto result = String::from_c_string(&global_arena, cJSON_GetStringValue(root_uri_item));

                if(!result.status) {
                    send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters \"rootUri\" attribute is not a valid UTF-8 string"_S);
                    continue;
                }

                has_root_uri = true;
                root_uri = result.value;
            } else if(cJSON_IsNull(root_uri_item)) {
                has_root_uri = false;
            } else {
                send_error_response(&request_arena, id, ErrorCode::InvalidParams, u8"Parameters \"rootUri\" attribute is incorrect type"_S);
                continue;
            }

            is_initialized = true;

            auto result = cJSON_CreateObject();

            cJSON_AddObjectToObject(result, "capabilities");

            send_success_response(&request_arena, id, result);
        } else {
            if(!is_initialized && id != nullptr) {
                send_error_response(&request_arena, id, ErrorCode::ServerNotInitialized, u8"Server has not been initialized"_S);
                continue;
            }

            if(method == u8"initialized"_S) {
                // Do nothing
            } else {
                if(!(method.length >= 2 && method.slice(0, 2) == u8"$/"_S && id == nullptr)) {
                    send_error_response(&request_arena, id, ErrorCode::MethodNotFound, u8"Unknown or unimplemented method"_S);
                    continue;
                }
            }
        }
    }
}