#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
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
    Array<AnyJob*> jobs;
    Array<Error> errors;
};

struct SourceProviderState {
    List<SourceFile>* source_files;
};

static Result<Array<uint8_t>> source_provider(void* data, String path) {
    auto state = (SourceProviderState*)data;

    for(auto file : *state->source_files) {
        if(file.absolute_path == path) {
            if(file.is_claimed) {
                return ok(Array(file.source_text.length, (uint8_t*)file.source_text.elements));
            }

            break;
        }
    }

    return err();
}

static Result<void> compile_source_file(GlobalInfo info, SourceFile* file) {
    file->needs_compilation = false;

    for(auto old_job : file->jobs) {
        old_job->arena.free();
    }

    file->compilation_arena.reset();

    List<Error> errors(&file->compilation_arena);

    ErrorHandlerState error_handler_state {};
    error_handler_state.arena = &file->compilation_arena;
    error_handler_state.errors = &errors;

    register_error_handler(&error_handler, &error_handler_state);

    List<AnyJob*> jobs(&file->compilation_arena);

    {
        AnyJob job {};
        job.kind = JobKind::ParseFile;
        job.state = JobState::Working;
        job.parse_file.path = file->absolute_path;

        jobs.append(file->compilation_arena.heapify(job));
    }

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

                        auto tokens_result = tokenize_source(&job->arena, parse_file->path);
                        if(!tokens_result.status) {
                            file->jobs = jobs;
                            file->errors = errors;

                            return err();
                        }

                        auto statements_result = parse_tokens(&job->arena, parse_file->path, tokens_result.value);
                        if(!statements_result.status) {
                            file->jobs = jobs;
                            file->errors = errors;

                            return err();
                        }

                        auto scope = file->compilation_arena.allocate_and_construct<ConstantScope>();
                        scope->statements = statements_result.value;
                        scope->scope_constants = {};
                        scope->is_top_level = true;
                        scope->file_path = parse_file->path;

                        parse_file->scope = scope;
                        job->state = JobState::Done;

                        auto result = process_scope(&file->compilation_arena, &jobs, scope, statements_result.value, nullptr, true);
                        if(!result.status) {
                            file->jobs = jobs;
                            file->errors = errors;

                            return err();
                        }

                        auto job_after = jobs[job_index];
                    } break;

                    case JobKind::TypeStaticIf: {
                        auto type_static_if = job->type_static_if;

                        auto result = do_type_static_if(
                            info,
                            &jobs,
                            &file->compilation_arena,
                            &job->arena,
                            type_static_if.static_if,
                            type_static_if.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            &job->arena,
                            type_function_declaration.declaration,
                            type_function_declaration.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                                    AnyJob job {};
                                    job.kind = JobKind::TypeFunctionBody;
                                    job.state = JobState::Working;
                                    job.type_function_body.type = function_type;
                                    job.type_function_body.value = function_value;

                                    jobs.append(file->compilation_arena.heapify(job));
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
                            &file->compilation_arena,
                            &job->arena,
                            type_polymorphic_function.declaration,
                            type_polymorphic_function.parameters,
                            type_polymorphic_function.scope,
                            type_polymorphic_function.call_scope,
                            type_polymorphic_function.call_parameter_ranges
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            &job->arena,
                            type_constant_definition.definition,
                            type_constant_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_struct_definition.definition,
                            type_struct_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_polymorphic_struct.definition,
                            type_polymorphic_struct.parameters,
                            type_polymorphic_struct.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_union_definition.definition,
                            type_union_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_polymorphic_union.definition,
                            type_polymorphic_union.parameters,
                            type_polymorphic_union.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_enum_definition.definition,
                            type_enum_definition.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

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
                            &file->compilation_arena,
                            type_function_body.type,
                            type_function_body.value
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_function_body.scope = result.value.scope;
                            job->type_function_body.statements = result.value.statements;
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
                            &file->compilation_arena,
                            type_static_variable.declaration,
                            type_static_variable.scope
                        );

                        if(result.has_value) {
                            if(!result.status) {
                                file->jobs = jobs;
                                file->errors = errors;

                                return err();
                            }

                            job->state = JobState::Done;
                            job->type_static_variable.is_external = result.value.is_external;
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

                error(scope, range, "Circular dependency detected");
            }
        }

        file->jobs = jobs;
        file->errors = errors;

        return ok();
    }

    file->jobs = jobs;
    file->errors = errors;

    return ok();
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

    if(current_line == line && current_column == column) {
        UTF8PositionToUTF16PositionResult result {};
        result.line = result_line;
        if(one_past) {
            result.column = result_column + 1;
        } else {
            result.column = result_column;
        }

        return ok(result);
    }

    return err();
}

struct UTF16PositionToUTF8PositionResult {
    unsigned int line;
    unsigned int column;
};

// Input is zero-based, output is one-based (like FileRange and Token)
static Result<UTF16PositionToUTF8PositionResult> utf16_position_to_utf8_position(
    String text,
    unsigned int line,
    unsigned int column,
    bool one_past
) {
    unsigned int current_line = 0;
    unsigned int current_column = 0;

    unsigned int result_line = 1;
    unsigned int result_column = 1;

    size_t index = 0;
    while(index != text.length) {
        if(!one_past && current_line == line && current_column == column) {
            UTF16PositionToUTF8PositionResult result {};
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
            current_column = 0;

            result_line += 1;
            result_column = 1;
        } else if(codepoint =='\n') {
            current_line += 1;
            current_column = 0;

            result_line += 1;
            result_column = 1;
        } else {
            if(codepoint >= 0x010000) {
                current_column += 2;
            } else {
                current_column += 1;
            }

            result_column += codepoint_utf8_length;
        }

        if(one_past && old_current_line == line && old_current_column == column) {
            UTF16PositionToUTF8PositionResult result {};
            result.line = result_line;
            result.column = result_column;

            return ok(result);
        }
    }

    if(!one_past && current_line == line && current_column == column) {
        UTF16PositionToUTF8PositionResult result {};
        result.line = result_line;
        result.column = result_column;

        return ok(result);
    }

    return err();
}

static void compile_and_send_diagnostics(Arena* request_arena, GlobalInfo info, Array<SourceFile> source_files) {
    for(auto& file : source_files) {
        if(!file.is_claimed || !file.needs_compilation) {
            continue;
        }

        auto result = compile_source_file(info, &file);

        if(result.status) {
            assert(file.errors.length == 0);

            for(auto job : file.jobs) {
                assert(job->state == JobState::Done);
            }
        } else {
            assert(file.errors.length != 0);
        }

        StringBuffer uri(request_arena);
        uri.append(u8"file://"_S);
        uri.append(file.absolute_path);

        auto params = cJSON_CreateObject();

        cJSON_AddStringToObject(params, "uri", uri.to_c_string(request_arena));

        auto diagnostics = cJSON_CreateArray();

        for(auto error : file.errors) {
            auto diagnostic = cJSON_CreateObject();

            if(error.path == file.absolute_path) {
                auto range = cJSON_CreateObject();

                auto start = cJSON_CreateObject();

                auto start_result = utf8_position_to_utf16_position(
                    file.source_text,
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
                    file.source_text,
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
            } else {
                auto range = cJSON_CreateObject();

                auto start = cJSON_CreateObject();

                cJSON_AddNumberToObject(start, "line", 0);
                cJSON_AddNumberToObject(start, "character", 0);

                cJSON_AddItemToObject(range, "start", start);

                auto end = cJSON_CreateObject();

                cJSON_AddNumberToObject(end, "line", 0);
                cJSON_AddNumberToObject(end, "character", 0);

                cJSON_AddItemToObject(range, "end", end);

                cJSON_AddItemToObject(diagnostic, "range", range);

                StringBuffer message(request_arena);
                if(error.path == u8""_S) {
                    message.append(u8"Error: "_S);
                    message.append(error.text);
                } else {
                    message.append(u8"Error in imported file '"_S);
                    message.append(error.path);
                    message.append(u8"'"_S);
                }

                cJSON_AddStringToObject(diagnostic, "message", message.to_c_string(request_arena));
            }

            cJSON_AddItemToArray(diagnostics, diagnostic);
        }

        cJSON_AddItemToObject(params, "diagnostics", diagnostics);

        send_notification(request_arena, "textDocument/publishDiagnostics", params);
    }
}

inline bool is_position_in_range(FileRange range, unsigned int line, unsigned int column) {
    if(line < range.first_line || line > range.last_line) {
        return false;
    } else if(range.first_line == range.last_line) {
        return column >= range.first_column && column <= range.last_column;
    } else if(line == range.first_line) {
        return column >= range.first_column;
    } else if(line == range.last_line) {
        return column <= range.last_column;
    } else {
        return true;
    }
}

struct RangeInfo {
    FileRange range;
    AnyType type;
    bool has_value;
    AnyValue value;
};

static RangeInfo get_expression_range_info(TypedExpression top_expression, unsigned int line, unsigned int column) {
    auto current_expression = top_expression;

    while(true) {
        if(current_expression.kind == TypedExpressionKind::VariableReference) {
            break;
        } else if(current_expression.kind == TypedExpressionKind::StaticVariableReference) {
            break;
        } else if(current_expression.kind == TypedExpressionKind::ConstantLiteral) {
            break;
        } else if(current_expression.kind == TypedExpressionKind::BinaryOperation) {
            if(is_position_in_range(current_expression.binary_operation.left->range, line, column)) {
                current_expression = *current_expression.binary_operation.left;
            } else if(is_position_in_range(current_expression.binary_operation.right->range, line, column)) {
                current_expression = *current_expression.binary_operation.right;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::IndexReference) {
            if(is_position_in_range(current_expression.index_reference.value->range, line, column)) {
                current_expression = *current_expression.index_reference.value;
            } else if(is_position_in_range(current_expression.index_reference.index->range, line, column)) {
                current_expression = *current_expression.index_reference.index;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::MemberReference) {
            if(is_position_in_range(current_expression.member_reference.value->range, line, column)) {
                current_expression = *current_expression.member_reference.value;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::ArrayLiteral) {
            auto found = false;

            for(auto element : current_expression.array_literal.elements) {
                if(is_position_in_range(element.range, line, column)) {
                    current_expression = element;

                    found = true;
                    break;
                }
            }

            if(found) {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::StructLiteral) {
            auto found = false;

            for(auto member : current_expression.struct_literal.members) {
                if(is_position_in_range(member.member.range, line, column)) {
                    current_expression = member.member;

                    found = true;
                    break;
                }
            }

            if(!found) {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::FunctionCall) {
            if(is_position_in_range(current_expression.function_call.value->range, line, column)) {
                current_expression = *current_expression.function_call.value;
            } else {
                auto found = false;

                for(auto parameter : current_expression.function_call.parameters) {
                    if(is_position_in_range(parameter.range, line, column)) {
                        current_expression = parameter;

                        found = true;
                        break;
                    }
                }

                if(!found) {
                    break;
                }
            }
        } else if(current_expression.kind == TypedExpressionKind::UnaryOperation) {
            if(is_position_in_range(current_expression.unary_operation.value->range, line, column)) {
                current_expression = *current_expression.unary_operation.value;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::Cast) {
            if(is_position_in_range(current_expression.cast.value->range, line, column)) {
                current_expression = *current_expression.cast.value;
            } else if(is_position_in_range(current_expression.cast.type->range, line, column)) {
                current_expression = *current_expression.cast.type;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::Bake) {
            if(is_position_in_range(current_expression.bake.value->range, line, column)) {
                current_expression = *current_expression.bake.value;
            } else {
                auto found = false;

                for(auto parameter : current_expression.bake.parameters) {
                    if(is_position_in_range(parameter.range, line, column)) {
                        current_expression = parameter;

                        found = true;
                        break;
                    }
                }

                if(!found) {
                    break;
                }
            }
        } else if(current_expression.kind == TypedExpressionKind::ArrayType) {
            if(is_position_in_range(current_expression.array_type.element_type->range, line, column)) {
                current_expression = *current_expression.array_type.element_type;
            } else if(
                current_expression.array_type.length != nullptr &&
                is_position_in_range(current_expression.array_type.length->range, line, column)
            ) {
                current_expression = *current_expression.array_type.length;
            } else {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::FunctionType) {
            auto found = false;

            for(auto parameter : current_expression.function_type.parameters) {
                if(is_position_in_range(parameter.type.range, line, column)) {
                    current_expression = parameter.type;

                    found = true;
                    break;
                }
            }

            if(found) {
                continue;
            }

            for(auto return_type : current_expression.function_type.return_types) {
                if(is_position_in_range(return_type.range, line, column)) {
                    current_expression = return_type;

                    found = true;
                    break;
                }
            }

            if(!found) {
                break;
            }
        } else if(current_expression.kind == TypedExpressionKind::Coercion) {
            if(is_position_in_range(current_expression.coercion.original->range, line, column)) {
                current_expression = *current_expression.coercion.original;
            } else {
                break;
            }
        } else {
            abort();
        }
    }

    RangeInfo result {};
    result.range = current_expression.range;
    result.type = current_expression.type;
    result.has_value = true;
    result.value = current_expression.value;

    return result;
}

static Result<RangeInfo> get_statement_range_info(TypedStatement top_statement, unsigned int line, unsigned int column) {
    auto current_statement = top_statement;

    while(true) {
        if(current_statement.kind == TypedStatementKind::ExpressionStatement) {
            if(is_position_in_range(
                current_statement.expression_statement.expression.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.expression_statement.expression,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::VariableDeclaration) {
            if(
                current_statement.variable_declaration.has_type &&
                is_position_in_range(
                    current_statement.variable_declaration.type.range,
                    line,
                    column
                )
            ) {
                return ok(get_expression_range_info(
                    current_statement.variable_declaration.type,
                    line,
                    column
                ));
            }

            if(
                current_statement.variable_declaration.has_initializer &&
                is_position_in_range(
                    current_statement.variable_declaration.initializer.range,
                    line,
                    column
                )
            ) {
                return ok(get_expression_range_info(
                    current_statement.variable_declaration.initializer,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::MultiReturnVariableDeclaration) {
            if(is_position_in_range(
                current_statement.multi_return_variable_declaration.initializer.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.multi_return_variable_declaration.initializer,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::Assignment) {
            if(is_position_in_range(
                current_statement.assignment.target.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.assignment.target,
                    line,
                    column
                ));
            }

            if(is_position_in_range(
                current_statement.assignment.value.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.assignment.value,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::MultiReturnAssignment) {
            for(auto target : current_statement.multi_return_assignment.targets) {
                if(is_position_in_range(
                    target.range,
                    line,
                    column
                )) {
                    return ok(get_expression_range_info(
                        target,
                        line,
                        column
                    ));
                }
            }

            if(is_position_in_range(
                current_statement.multi_return_assignment.value.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.multi_return_assignment.value,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::BinaryOperationAssignment) {
            if(is_position_in_range(
                current_statement.binary_operation_assignment.operation.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.binary_operation_assignment.operation,
                    line,
                    column
                ));
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::IfStatement) {
            if(is_position_in_range(
                current_statement.if_statement.condition.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.if_statement.condition,
                    line,
                    column
                ));
            }

            auto found = false;
            for(auto statement : current_statement.if_statement.statements) {
                if(is_position_in_range(
                    statement.range,
                    line,
                    column
                )) {
                    current_statement = statement;

                    found = true;
                    break;
                }
            }

            if(found) {
                continue;
            }

            for(auto else_if : current_statement.if_statement.else_ifs) {
                if(is_position_in_range(
                    else_if.condition.range,
                    line,
                    column
                )) {
                    return ok(get_expression_range_info(
                        else_if.condition,
                        line,
                        column
                    ));
                }

                auto found = false;
                for(auto statement : else_if.statements) {
                    if(is_position_in_range(
                        statement.range,
                        line,
                        column
                    )) {
                        current_statement = statement;

                        found = true;
                        break;
                    }
                }

                if(found) {
                    break;
                }
            }

            if(found) {
                continue;
            }

            for(auto statement : current_statement.if_statement.else_statements) {
                if(is_position_in_range(
                    statement.range,
                    line,
                    column
                )) {
                    current_statement = statement;

                    found = true;
                    break;
                }
            }

            if(!found) {
                break;
            }
        } else if(current_statement.kind == TypedStatementKind::WhileLoop) {
            if(is_position_in_range(
                current_statement.while_loop.condition.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.while_loop.condition,
                    line,
                    column
                ));
            }

            auto found = false;
            for(auto statement : current_statement.while_loop.statements) {
                if(is_position_in_range(
                    statement.range,
                    line,
                    column
                )) {
                    current_statement = statement;
                }
            }

            if(!found) {
                break;
            }
        } else if(current_statement.kind == TypedStatementKind::ForLoop) {
            if(is_position_in_range(
                current_statement.for_loop.from.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.for_loop.from,
                    line,
                    column
                ));
            }

            if(is_position_in_range(
                current_statement.for_loop.to.range,
                line,
                column
            )) {
                return ok(get_expression_range_info(
                    current_statement.for_loop.to,
                    line,
                    column
                ));
            }

            auto found = false;
            for(auto statement : current_statement.for_loop.statements) {
                if(is_position_in_range(
                    statement.range,
                    line,
                    column
                )) {
                    current_statement = statement;
                }
            }

            if(!found) {
                break;
            }
        } else if(current_statement.kind == TypedStatementKind::Return) {
            for(auto value : current_statement.return_.values) {
                if(is_position_in_range(
                    value.range,
                    line,
                    column
                )) {
                    return ok(get_expression_range_info(
                        value,
                        line,
                        column
                    ));
                }
            }

            break;
        } else if(current_statement.kind == TypedStatementKind::Break) {
            break;
        } else if(current_statement.kind == TypedStatementKind::InlineAssembly) {
            for(auto binding : current_statement.inline_assembly.bindings) {
                if(is_position_in_range(
                    binding.value.range,
                    line,
                    column
                )) {
                    return ok(get_expression_range_info(
                        binding.value,
                        line,
                        column
                    ));
                }
            }

            break;
        } else {
            abort();
        }
    }

    return err();
}

static void mark_file_dirty(SourceFile* source_file, Array<SourceFile> source_files) {
    source_file->needs_compilation = true;

    for(auto& file : source_files) {
        if(&file != source_file && file.is_claimed) {
            for(auto job : file.jobs) {
                if(job->kind == JobKind::ParseFile && job->parse_file.path == source_file->absolute_path) {
                    file.needs_compilation = true;
                    break;
                }
            }
        }
    }
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

    SourceProviderState source_provider_state {};
    source_provider_state.source_files = &source_files;

    register_source_provider(&source_provider, &source_provider_state);

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

            cJSON_AddBoolToObject(capabilities, "hoverProvider", true);

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

                    mark_file_dirty(source_file, source_files);
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

                    auto index = source_files.append(new_source_file);

                    mark_file_dirty(&source_files[index], source_files);
                }

                compile_and_send_diagnostics(&request_arena, info, source_files);
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

                    mark_file_dirty(source_file, source_files);

                    compile_and_send_diagnostics(&request_arena, info, source_files);
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
            } else if(method == u8"textDocument/hover"_S) {
                if(id == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidRequest, u8"Message body \"id\" attribute does not exist"_S);
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

                auto position = cJSON_GetObjectItem(params_item, "position");
                if(position == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"position\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsObject(position)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Parameters \"position\" attribute is not an object"_S);
                    continue;
                }

                auto line_item = cJSON_GetObjectItem(position, "line");
                if(line_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsNumber(line_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"line\" attribute is not a number"_S);
                    continue;
                }

                auto line_double = cJSON_GetNumberValue(line_item);

                auto line = (unsigned int)line_double;
                if(line_double != (double)line) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position line is not an integer, is negative, or is too large"_S);
                    continue;
                }

                auto character_item = cJSON_GetObjectItem(position, "character");
                if(character_item == nullptr) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is missing"_S);
                    continue;
                }

                if(!cJSON_IsNumber(character_item)) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position \"character\" attribute is not a number"_S);
                    continue;
                }

                auto character_double = cJSON_GetNumberValue(character_item);

                auto character = (unsigned int)character_double;
                if(character_double != (double)character) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position character is not an integer, is negative, or is too large"_S);
                    continue;
                }

                auto actual_position_result = utf16_position_to_utf8_position(source_file->source_text, line, character, false);
                if(!actual_position_result.status) {
                    send_error_response(&request_arena, nullptr, ErrorCode::InvalidParams, u8"Position is beyond the end of the file"_S);
                    continue;
                }

                auto actual_position = actual_position_result.value;

                auto found_range = false;
                RangeInfo range_info;
                for(auto job : source_file->jobs) {
                    if(job->state == JobState::Done) {
                        if(job->kind == JobKind::TypeStaticIf) {
                            if(
                                job->type_static_if.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_static_if.static_if->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                if(is_position_in_range(
                                    job->type_static_if.condition.range,
                                    actual_position.line,
                                    actual_position.column
                                )) {
                                    found_range = true;
                                    range_info = get_expression_range_info(
                                        job->type_static_if.condition,
                                        actual_position.line,
                                        actual_position.column
                                    );
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypeFunctionDeclaration) {
                            if(
                                job->type_function_declaration.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_function_declaration.declaration->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                for(auto parameter : job->type_function_declaration.parameters) {
                                    if(is_position_in_range(
                                        parameter.type.range,
                                        actual_position.line,
                                        actual_position.column
                                    )) {
                                        found_range = true;
                                        range_info = get_expression_range_info(
                                            parameter.type,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        break;
                                    }
                                }

                                if(found_range) {
                                    break;
                                }

                                for(auto return_type : job->type_function_declaration.return_types) {
                                    if(is_position_in_range(
                                        return_type.range,
                                        actual_position.line,
                                        actual_position.column
                                    )) {
                                        found_range = true;
                                        range_info = get_expression_range_info(
                                            return_type,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        break;
                                    }
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypePolymorphicFunction) {
                            break;
                        } else if(job->kind == JobKind::TypeConstantDefinition) {
                            if(
                                job->type_constant_definition.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_constant_definition.definition->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                if(is_position_in_range(
                                    job->type_constant_definition.value.range,
                                    actual_position.line,
                                    actual_position.column
                                )) {
                                    found_range = true;
                                    range_info = get_expression_range_info(
                                        job->type_constant_definition.value,
                                        actual_position.line,
                                        actual_position.column
                                    );
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypeStructDefinition) {
                            if(
                                job->type_struct_definition.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_struct_definition.definition->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                for(auto member : job->type_struct_definition.members) {
                                    if(is_position_in_range(
                                        member.member.range,
                                        actual_position.line,
                                        actual_position.column
                                    )) {
                                        found_range = true;
                                        range_info = get_expression_range_info(
                                            member.member,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        break;
                                    }
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypePolymorphicStruct) {
                            break;
                        } else if(job->kind == JobKind::TypeUnionDefinition) {
                            if(
                                job->type_union_definition.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_union_definition.definition->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                for(auto member : job->type_union_definition.members) {
                                    if(is_position_in_range(
                                        member.member.range,
                                        actual_position.line,
                                        actual_position.column
                                    )) {
                                        found_range = true;
                                        range_info = get_expression_range_info(
                                            member.member,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        break;
                                    }
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypePolymorphicUnion) {
                            break;
                        } else if(job->kind == JobKind::TypeEnumDefinition) {
                            if(
                                job->type_enum_definition.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_enum_definition.definition->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                for(auto variant : job->type_enum_definition.variants) {
                                    if(
                                        variant.has_value &&
                                        is_position_in_range(
                                            variant.value.range,
                                            actual_position.line,
                                            actual_position.column
                                        )
                                    ) {
                                        found_range = true;
                                        range_info = get_expression_range_info(
                                            variant.value,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        break;
                                    }
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypeFunctionBody) {
                            if(
                                job->type_function_body.value.body_scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_function_body.value.declaration->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                for(auto statement : job->type_function_body.statements) {
                                    if(is_position_in_range(
                                        statement.range,
                                        actual_position.line,
                                        actual_position.column
                                    )) {
                                        auto result = get_statement_range_info(
                                            statement,
                                            actual_position.line,
                                            actual_position.column
                                        );

                                        if(result.status) {
                                            found_range = true;
                                            range_info = result.value;
                                        }

                                        break;
                                    }
                                }

                                break;
                            }
                        } else if(job->kind == JobKind::TypeStaticVariable) {
                            if(
                                job->type_static_variable.scope->get_file_path() == absolute_path &&
                                is_position_in_range(
                                    job->type_static_variable.declaration->range,
                                    actual_position.line,
                                    actual_position.column
                                )
                            ) {
                                if(
                                    (
                                        job->type_static_variable.is_external ||
                                        job->type_static_variable.declaration->type != nullptr
                                    ) &&
                                    is_position_in_range(
                                    job->type_static_variable.type.range,
                                    actual_position.line,
                                    actual_position.column
                                )) {
                                    found_range = true;
                                    range_info = get_expression_range_info(
                                        job->type_static_variable.type,
                                        actual_position.line,
                                        actual_position.column
                                    );

                                    break;
                                }

                                if(
                                    !job->type_static_variable.is_external &&
                                    is_position_in_range(
                                    job->type_static_variable.initializer.range,
                                    actual_position.line,
                                    actual_position.column
                                )) {
                                    found_range = true;
                                    range_info = get_expression_range_info(
                                        job->type_static_variable.initializer,
                                        actual_position.line,
                                        actual_position.column
                                    );

                                    break;
                                }

                                break;
                            }
                        }
                    }
                }

                if(found_range) {
                    auto result = cJSON_CreateObject();

                    StringBuffer buffer(&request_arena);

                    if(range_info.has_value && range_info.value.kind == ValueKind::ConstantValue) {
                        if(range_info.type.kind == TypeKind::Type) {
                            buffer.append(range_info.value.constant.unwrap_type().get_description(&request_arena));
                        } else {
                            buffer.append(range_info.value.constant.get_description(&request_arena));
                            buffer.append(u8" ("_S);
                            buffer.append(range_info.type.get_description(&request_arena));
                            buffer.append(u8" )"_S);
                        }
                    } else {
                        buffer.append(range_info.type.get_description(&request_arena));
                    }

                    cJSON_AddStringToObject(result, "contents", buffer.to_c_string(&request_arena));

                    auto range_item = cJSON_CreateObject();

                    auto start = cJSON_CreateObject();

                    auto start_result = utf8_position_to_utf16_position(
                        source_file->source_text,
                        range_info.range.first_line,
                        range_info.range.first_column,
                        false
                    );
                    assert(start_result.status);

                    cJSON_AddNumberToObject(start, "line", (double)start_result.value.line);
                    cJSON_AddNumberToObject(start, "character", (double)start_result.value.column);

                    cJSON_AddItemToObject(range_item, "start", start);

                    auto end = cJSON_CreateObject();

                    auto end_result = utf8_position_to_utf16_position(
                        source_file->source_text,
                        range_info.range.last_line,
                        range_info.range.last_column,
                        true
                    );
                    assert(end_result.status);

                    cJSON_AddNumberToObject(end, "line", (double)end_result.value.line);
                    cJSON_AddNumberToObject(end, "character", (double)end_result.value.column);

                    cJSON_AddItemToObject(range_item, "end", end);

                    cJSON_AddItemToObject(result, "range", range_item);

                    send_success_response(&request_arena, id, result);
                } else {
                    send_success_response(&request_arena, id, cJSON_CreateNull());
                    continue;
                }
            } else {
                if(!(method.length >= 2 && method.slice(0, 2) == u8"$/"_S && id == nullptr)) {
                    send_error_response(&request_arena, id, ErrorCode::MethodNotFound, u8"Unknown or unimplemented method"_S);
                    continue;
                }
            }
        }
    }
}