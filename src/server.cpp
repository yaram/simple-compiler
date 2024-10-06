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

struct SourceFile {
    String absolute_path;

    bool is_claimed;

    Arena source_text_arena;
    String source_text;

    bool needs_compilation;

    Arena compilation_arena;
    Array<AnyJob> jobs;
    Array<Error> errors;
};

static void compile_source_file(GlobalInfo info, SourceFile* file) {
    file->needs_compilation = false;

    for(auto old_job : file->jobs) {
        old_job.arena.free();
    }

    file->compilation_arena.reset();

    List<Error> errors(&file->compilation_arena);

    ErrorHandlerState error_handler_state {};
    error_handler_state.arena = &file->compilation_arena;
    error_handler_state.errors = &errors;

    register_error_handler(&error_handler, &error_handler_state);

    List<AnyJob> jobs(&file->compilation_arena);

    {
        AnyJob job {};
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = file->absolute_path;
        job.parse_file.has_source = true;
        job.parse_file.source = Array(file->source_text.length, (uint8_t*)file->source_text.elements);

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

                        Array<Token> tokens;
                        if(parse_file->has_source) {
                            auto tokens_result = tokenize_source(&job->arena, parse_file->path, parse_file->source);
                            if(!tokens_result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
                            }

                            tokens = tokens_result.value;
                        } else {
                            auto tokens_result = tokenize_source(&job->arena, parse_file->path);
                            if(!tokens_result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
                            }

                            tokens = tokens_result.value;
                        }

                        auto statements_result = parse_tokens(&job->arena, parse_file->path, tokens);
                        if(!statements_result.status) {
                            file->jobs = jobs;
                            file->errors = errors;

                            return;
                        }

                        auto scope = file->compilation_arena.allocate_and_construct<ConstantScope>();
                        scope->statements = statements_result.value;
                        scope->declarations = create_declaration_hash_table(&file->compilation_arena, statements_result.value);
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        auto result = process_scope(&file->compilation_arena, &jobs, scope, statements_result.value, nullptr, true);
                        if(!result.status) {
                            file->jobs = jobs;
                            file->errors = errors;

                            return;
                        }

                        auto job_after = jobs[job_index];
                    } break;

                    case JobKind::ResolveStaticIf: {
                        auto resolve_static_if = job->resolve_static_if;

                        auto result = do_resolve_static_if(
                            info,
                            &jobs,
                            &file->compilation_arena,
                            &job->arena,
                            resolve_static_if.static_if,
                            resolve_static_if.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                            &file->compilation_arena,
                            &job->arena,
                            resolve_function_declaration.declaration,
                            resolve_function_declaration.scope
                        );

                        auto job_after = &jobs[job_index];

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                            &file->compilation_arena,
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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
                                file->jobs = jobs;
                                file->errors = errors;

                                return;
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

    file->jobs = jobs;

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
    }

    file->errors = errors;
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

static void send_notification(Arena* arena, const char* method, cJSON* params) {
    auto json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "jsonrpc", "2.0");

    cJSON_AddStringToObject(json, "method", method);

    cJSON_AddItemToObject(json, "params", params);

    auto json_text = cJSON_PrintUnformatted(json);

    printf("Content-Length: %zu\r\n", strlen(json_text));
    printf("\r\n");
    fputs(json_text, stdout);
    fflush(stdout);
}

static char32_t get_codepoint_at(String text, size_t* index) {
    auto first_byte = text[*index];
    *index += 1;

    if(first_byte >> 7 == 0) {
        return (char32_t)first_byte;
    }

    auto second_byte = text[*index];
    *index += 1;

    if(first_byte >> 5 == 0b110) {
        auto codepoint = (((uint32_t)first_byte & 0b11111) << 6) | ((uint32_t)second_byte & 0b111111);

        return (char32_t)codepoint;
    }

    auto third_byte = text[*index];
    *index += 1;

    if(first_byte >> 4 == 0b1110) {
        auto codepoint =
            (((uint32_t)first_byte & 0b1111) << 12) |
            (((uint32_t)second_byte & 0b111111) << 6) |
            ((uint32_t)third_byte & 0b111111)
        ;

        return (char32_t)codepoint;
    }

    auto fourth_byte = text[*index];
    *index += 1;

    auto codepoint =
        (((uint32_t)first_byte & 0b111) << 18) |
        (((uint32_t)second_byte & 0b111111) << 12) |
        (((uint32_t)third_byte & 0b111111) << 6) |
        ((uint32_t)fourth_byte & 0b111111)
    ;

    return (char32_t)codepoint;
}

// Input is zero-based
static Result<size_t> utf16_position_to_utf8_offset(String text, unsigned int line, unsigned int column) {
    size_t index = 0;
    unsigned int current_line = 0;
    unsigned int current_column = 0;

    while(index != text.length) {
        if(current_line == line && current_column == column) {
            return ok(index);
        }

        auto codepoint = get_codepoint_at(text, &index);

        if(codepoint == '\r') {
            if(index != text.length) {
                auto temp_index = index;

                auto codepoint = get_codepoint_at(text, &temp_index);

                if(codepoint == '\n') {
                    index = temp_index;
                }
            }

            current_line += 1;
            current_column = 0;
        } else if(codepoint =='\n') {
            current_line += 1;
            current_column = 0;
        } else {
            if(codepoint >= 0x010000) {
                current_column += 2;
            } else {
                current_column += 1;
            }
        }
    }

    if(current_line == line && current_column == column) {
        return ok(index);
    }

    return err();
}

struct UTF8PositionToUTF16PositionResult {
    unsigned int line;
    unsigned int column;
};

// Input is one-based (like FileRange and Token), output is zero-based
static Result<UTF8PositionToUTF16PositionResult> utf8_position_to_utf16_position(
    String text,
    unsigned int line,
    unsigned int column,
    bool one_past
) {
    unsigned int current_line = 1;
    unsigned int current_column = 1;

    unsigned int result_line = 0;
    unsigned int result_column = 0;

    size_t index = 0;
    while(index != text.length) {
        if(!one_past && current_line == line && current_column == column) {
            UTF8PositionToUTF16PositionResult result {};
            result.line = result_line;
            result.column = result_column;

            return ok(result);
        }

        auto old_current_line = current_line;
        auto old_current_column = current_column;

        auto old_index = index;

        auto codepoint = get_codepoint_at(text, &index);

        auto codepoint_utf8_length = index - old_index;

        if(codepoint == '\r') {
            if(index != text.length) {
                auto temp_index = index;

                auto codepoint = get_codepoint_at(text, &temp_index);

                if(codepoint == '\n') {
                    index = temp_index;
                }
            }

            current_line += 1;
            current_column = 1;

            result_line += 1;
            result_column = 0;
        } else if(codepoint =='\n') {
            current_line += 1;
            current_column = 1;

            result_line += 1;
            result_column = 0;
        } else {
            current_column += codepoint_utf8_length;

            if(codepoint >= 0x010000) {
                result_column += 2;
            } else {
                result_column += 1;
            }
        }

        if(one_past && old_current_line == line && old_current_column == column) {
            UTF8PositionToUTF16PositionResult result {};
            result.line = result_line;
            result.column = result_column;

            return ok(result);
        }
    }

    if(!one_past && current_line == line && current_column == column) {
        UTF8PositionToUTF16PositionResult result {};
        result.line = result_line;
        result.column = result_column;

        return ok(result);
    }

    return err();
}

static void compile_and_send_diagnostics(Arena* request_arena, GlobalInfo info, String uri, SourceFile* file) {
    compile_source_file(info, file);

    auto params = cJSON_CreateObject();

    cJSON_AddStringToObject(params, "uri", uri.to_c_string(request_arena));

    auto diagnostics = cJSON_CreateArray();

    for(auto error : file->errors) {
        if(error.path == file->absolute_path) {
            auto diagnostic = cJSON_CreateObject();

            auto range = cJSON_CreateObject();

            auto start = cJSON_CreateObject();

            auto start_result = utf8_position_to_utf16_position(
                file->source_text,
                error.range.first_line,
                error.range.first_column,
                false
            );
            assert(start_result.status);

            cJSON_AddNumberToObject(start, "line", (double)start_result.value.line);
            cJSON_AddNumberToObject(start, "character", (double)start_result.value.column);

            cJSON_AddItemToObject(range, "start", start);

            auto end = cJSON_CreateObject();

            auto end_result = utf8_position_to_utf16_position(
                file->source_text,
                error.range.last_line,
                error.range.last_column,
                true
            );
            assert(end_result.status);

            cJSON_AddNumberToObject(end, "line", (double)end_result.value.line);
            cJSON_AddNumberToObject(end, "character", (double)end_result.value.column);

            cJSON_AddItemToObject(range, "end", end);

            cJSON_AddItemToObject(diagnostic, "range", range);

            cJSON_AddStringToObject(diagnostic, "message", error.text.to_c_string(request_arena));

            cJSON_AddItemToArray(diagnostics, diagnostic);
        }
    }

    cJSON_AddItemToObject(params, "diagnostics", diagnostics);

    send_notification(request_arena, "textDocument/publishDiagnostics", params);
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

    List<SourceFile> source_files(&global_arena);

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
        assert(jsonrpc_result.status);

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
        assert(method_result.status);
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
                assert(result.status);

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

            auto capabilities = cJSON_CreateObject();

            auto text_document_sync = cJSON_CreateObject();

            cJSON_AddBoolToObject(text_document_sync, "openClose", true);
            cJSON_AddNumberToObject(text_document_sync, "change", 2); // Incremental

            cJSON_AddItemToObject(capabilities, "textDocumentSync", text_document_sync);

            cJSON_AddItemToObject(result, "capabilities", capabilities);

            send_success_response(&request_arena, id, result);
        } else {
            if(!is_initialized && id != nullptr) {
                send_error_response(&request_arena, id, ErrorCode::ServerNotInitialized, u8"Server has not been initialized"_S);
                continue;
            }

            if(method == u8"initialized"_S) {
                // Do nothing
            } else if(method == u8"textDocument/didOpen"_S) {
                if(id != nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidRequest, u8"Message body \"id\" attribute should not exist"_S);
                    continue;
                }

                if(!cJSON_IsObject(params_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters should be an object"_S);
                    continue;
                }

                auto text_document = cJSON_GetObjectItem(params_item, "textDocument");
                if(text_document == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsObject(text_document)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute should be an object"_S);
                    continue;
                }

                auto uri_item = cJSON_GetObjectItem(text_document, "uri");
                if(uri_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"uri\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsString(uri_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"uri\" attribute should be a string"_S);
                    continue;
                }

                auto uri_result = String::from_c_string(cJSON_GetStringValue(uri_item));
                assert(uri_result.status);
                auto uri = uri_result.value;

                const auto uri_prefix = u8"file://"_S;

                if(uri.length < uri_prefix.length || uri.slice(0, uri_prefix.length) != uri_prefix) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI does not start with \"file://\""_S);
                    continue;
                }

                auto path = uri.slice(uri_prefix.length);

                auto absolute_path_result = path_relative_to_absolute(&request_arena, path);
                if(!absolute_path_result.status) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI is invalid"_S);
                    continue;
                }

                auto absolute_path = absolute_path_result.value;

                auto found_source_file = false;
                SourceFile* source_file;
                for(auto& file : source_files) {
                    if(file.absolute_path == absolute_path) {
                        found_source_file = true;
                        source_file = &file;
                        break;
                    }
                }

                auto language_id_item = cJSON_GetObjectItem(text_document, "languageId");
                if(language_id_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"languageId\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsString(language_id_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"languageId\" attribute should be a string"_S);
                    continue;
                }

                auto language_id_result = String::from_c_string(cJSON_GetStringValue(language_id_item));
                assert(language_id_result.status);

                auto language_id = language_id_result.value;

                if(language_id != u8"simple"_S) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Language ID should be \"simple\""_S);
                    continue;
                }

                auto version_item = cJSON_GetObjectItem(text_document, "version");
                if(version_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"version\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsNumber(version_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"version\" attribute should be a number"_S);
                    continue;
                }

                auto version = cJSON_GetNumberValue(version_item);

                auto text_item = cJSON_GetObjectItem(text_document, "text");
                if(text_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"text\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsString(text_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentItem \"text\" attribute should be a string"_S);
                    continue;
                }

                if(found_source_file) {
                    source_file->source_text_arena.reset();

                    auto text_result = String::from_c_string(&source_file->source_text_arena, cJSON_GetStringValue(text_item));
                    assert(text_result.status);

                    auto text = text_result.value;

                    source_file->is_claimed = true;
                    source_file->source_text = text;
                    source_file->needs_compilation = true;

                    compile_and_send_diagnostics(&request_arena, info, uri, source_file);
                } else {
                    Arena source_text_arena {};

                    auto text_result = String::from_c_string(&source_text_arena, cJSON_GetStringValue(text_item));
                    assert(text_result.status);

                    auto text = text_result.value;

                    auto global_absolute_path = absolute_path.clone(&global_arena);

                    SourceFile new_source_file {};
                    new_source_file.absolute_path = global_absolute_path;
                    new_source_file.is_claimed = true;
                    new_source_file.source_text_arena = source_text_arena;
                    new_source_file.source_text = text;
                    new_source_file.needs_compilation = true;

                    auto index = source_files.append(new_source_file);

                    compile_and_send_diagnostics(&request_arena, info, uri, &source_files[index]);
                }
            } else if(method == u8"textDocument/didChange"_S) {
                if(id != nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidRequest, u8"Message body \"id\" attribute should not exist"_S);
                    continue;
                }

                if(!cJSON_IsObject(params_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters should be an object"_S);
                    continue;
                }

                auto text_document = cJSON_GetObjectItem(params_item, "textDocument");
                if(text_document == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsObject(text_document)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute should be an object"_S);
                    continue;
                }

                auto uri_item = cJSON_GetObjectItem(text_document, "uri");
                if(uri_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"VersionedTextDocumentIdentifier \"uri\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsString(uri_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"VersionedTextDocumentIdentifier \"uri\" attribute should be a string"_S);
                    continue;
                }

                auto uri_result = String::from_c_string(cJSON_GetStringValue(uri_item));
                assert(uri_result.status);
                auto uri = uri_result.value;

                const auto uri_prefix = u8"file://"_S;

                if(uri.length < uri_prefix.length || uri.slice(0, uri_prefix.length) != uri_prefix) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI does not start with \"file://\""_S);
                    continue;
                }

                auto path = uri.slice(uri_prefix.length);

                auto absolute_path_result = path_relative_to_absolute(&request_arena, path);
                if(!absolute_path_result.status) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI is invalid"_S);
                    continue;
                }

                auto absolute_path = absolute_path_result.value;

                auto version_item = cJSON_GetObjectItem(text_document, "version");
                if(version_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"VersionedTextDocumentIdentifier \"version\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsNumber(version_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"VersionedTextDocumentIdentifier \"version\" attribute should be a number"_S);
                    continue;
                }

                auto version = cJSON_GetNumberValue(version_item);

                auto found_source_file = false;
                SourceFile* source_file;
                for(auto& file : source_files) {
                    if(file.absolute_path == absolute_path) {
                        found_source_file = true;
                        source_file = &file;
                        break;
                    }
                }

                if(!found_source_file || !source_file->is_claimed) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file has not been claimed by client with \"textDocument/didOpen\""_S);
                    continue;
                }

                auto content_changes = cJSON_GetObjectItem(params_item, "contentChanges");
                if(content_changes == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"contentChanges\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsArray(content_changes)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"contentChanges\" attribute should be an array"_S);
                    continue;
                }

                auto content_change_count = (size_t)cJSON_GetArraySize(content_changes);

                if(content_change_count != 0) {
                    Arena final_source_text_arena {};

                    auto current_source_text = source_file->source_text;

                    for(size_t i = 0; i < content_change_count; i += 1) {
                        Arena* current_arena;
                        if(i == content_change_count - 1) {
                            current_arena = &final_source_text_arena;
                        } else {
                            current_arena = &request_arena;
                        }

                        auto content_change = cJSON_GetArrayItem(content_changes, (int)i);

                        if(!cJSON_IsObject(content_change)) {
                            send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"contentChanges\" attribute element is not an object"_S);
                            continue;
                        }

                        auto range = cJSON_GetObjectItem(content_change, "range");

                        auto text_item = cJSON_GetObjectItem(content_change, "text");
                        if(text_item == nullptr) {
                            send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentContentChangeEvent \"text\" attribute is missing"_S);
                            continue;
                        }

                        if(range == nullptr) {
                            auto result = String::from_c_string(current_arena, cJSON_GetStringValue(text_item));
                            assert(result.status);

                            current_source_text = result.value;
                        } else {
                            if(!cJSON_IsObject(range)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentContentChangeEvent \"range\" attribute is not an object"_S);
                                continue;
                            }

                            auto start = cJSON_GetObjectItem(range, "start");
                            if(start == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range \"start\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsObject(start)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range \"start\" attribute is not an object"_S);
                                continue;
                            }

                            auto start_line_item = cJSON_GetObjectItem(start, "line");
                            if(start_line_item == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsNumber(start_line_item)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is not a number"_S);
                                continue;
                            }

                            auto start_line_double = cJSON_GetNumberValue(start_line_item);

                            auto start_line = (unsigned int)start_line_double;
                            if(start_line_double != (double)start_line) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range start line is not an integer, is negative, or is too large"_S);
                                continue;
                            }

                            auto start_character_item = cJSON_GetObjectItem(start, "character");
                            if(start_character_item == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsNumber(start_character_item)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is not a number"_S);
                                continue;
                            }

                            auto start_character_double = cJSON_GetNumberValue(start_character_item);

                            auto start_character = (unsigned int)start_character_double;
                            if(start_character_double != (double)start_character) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range start character is not an integer, is negative, or is too large"_S);
                                continue;
                            }

                            auto end = cJSON_GetObjectItem(range, "end");
                            if(end == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range \"end\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsObject(end)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range \"end\" attribute is not an object"_S);
                                continue;
                            }

                            auto end_line_item = cJSON_GetObjectItem(end, "line");
                            if(end_line_item == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsNumber(end_line_item)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is not a number"_S);
                                continue;
                            }

                            auto end_line_double = cJSON_GetNumberValue(end_line_item);

                            auto end_line = (unsigned int)end_line_double;
                            if(end_line_double != (double)end_line) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range end line is not an integer, is negative, or is too large"_S);
                                continue;
                            }

                            auto end_character_item = cJSON_GetObjectItem(end, "character");
                            if(end_character_item == nullptr) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is missing"_S);
                                continue;
                            }

                            if(!cJSON_IsNumber(end_character_item)) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is not a number"_S);
                                continue;
                            }

                            auto end_character_double = cJSON_GetNumberValue(end_character_item);

                            auto end_character = (unsigned int)end_character_double;
                            if(end_character_double != (double)end_character) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Range end character is not an integer, is negative, or is too large"_S);
                                continue;
                            }

                            auto start_index_result = utf16_position_to_utf8_offset(current_source_text, start_line, start_character);
                            if(!start_index_result.status) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Start range is beyond the end of the file"_S);
                                continue;
                            }

                            auto start_index = start_index_result.value;

                            auto end_index_result = utf16_position_to_utf8_offset(current_source_text, end_line, end_character);
                            if(!end_index_result.status) {
                                send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"End range is beyond the end of the file"_S);
                                continue;
                            }

                            auto end_index = end_index_result.value;

                            StringBuffer buffer(current_arena);

                            buffer.append(current_source_text.slice(0, start_index));

                            auto result = buffer.append_c_string(cJSON_GetStringValue(text_item));
                            assert(result.status);

                            buffer.append(current_source_text.slice(end_index));

                            current_source_text = buffer;
                        }
                    }

                    source_file->source_text_arena.free();

                    source_file->source_text_arena = final_source_text_arena;
                    source_file->source_text = current_source_text;
                    source_file->needs_compilation = true;

                    compile_and_send_diagnostics(&request_arena, info, uri, source_file);
                }
            } else if(method == u8"textDocument/didClose"_S) {
                if(id != nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidRequest, u8"Message body \"id\" attribute should not exist"_S);
                    continue;
                }

                if(!cJSON_IsObject(params_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters should be an object"_S);
                    continue;
                }

                auto text_document = cJSON_GetObjectItem(params_item, "textDocument");
                if(text_document == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsObject(text_document)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"textDocument\" attribute should be an object"_S);
                    continue;
                }

                auto uri_item = cJSON_GetObjectItem(text_document, "uri");
                if(uri_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentIdentifier \"uri\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsString(uri_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"TextDocumentIdentifier \"uri\" attribute should be a string"_S);
                    continue;
                }

                auto uri_result = String::from_c_string(cJSON_GetStringValue(uri_item));
                assert(uri_result.status);
                auto uri = uri_result.value;

                const auto uri_prefix = u8"file://"_S;

                if(uri.length < uri_prefix.length || uri.slice(0, uri_prefix.length) != uri_prefix) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI does not start with \"file://\""_S);
                    continue;
                }

                auto path = uri.slice(uri_prefix.length);

                auto absolute_path_result = path_relative_to_absolute(&request_arena, path);
                if(!absolute_path_result.status) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file URI is invalid"_S);
                    continue;
                }

                auto absolute_path = absolute_path_result.value;

                auto found_source_file = false;
                SourceFile* source_file;
                for(auto& file : source_files) {
                    if(file.absolute_path == absolute_path) {
                        found_source_file = true;
                        source_file = &file;
                        break;
                    }
                }

                if(!found_source_file || !source_file->is_claimed) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Source file has not been claimed by client with \"textDocument/didOpen\""_S);
                    continue;
                }

                source_file->is_claimed = false;
            } else {
                if(!(method.length >= 2 && method.slice(0, 2) == u8"$/"_S && id == nullptr)) {
                    send_error_response(&request_arena, id, ErrorCode::MethodNotFound, u8"Unknown or unimplemented method"_S);
                    continue;
                }
            }
        }
    }
}