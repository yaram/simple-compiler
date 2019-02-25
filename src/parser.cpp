#include "parser.h"
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "list.h"

using namespace list;

struct Context {
    const char *source_file_path;

    FILE *source_file;

    unsigned int line;
    unsigned int character;
};

void error(Context context, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "%s(%d:%d): ", context.source_file_path, context.line, context.character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    va_end(arguments);
}

void skip_whitespace(Context *context) {
    while(true){
        auto character = fgetc(context->source_file);

        switch(character) {
            case ' ':
            case '\t': {
                context->character += 1;
            } break;

            case '\r': {
                auto character = fgetc(context->source_file);

                if(character == '\n') {
                    context->line += 1;
                    context->character = 1;
                } else {
                    ungetc(character, context->source_file);

                    context->line += 1;
                    context->character = 1;
                }
            } break;

            case '\n': {
                context->line += 1;
                context->character = 1;
            } break;

            case '/': {
                context->character += 1;

                auto character = fgetc(context->source_file);

                if(character == '/') {
                    context->character += 1;
                } else if(character == '*') {
                    context->character += 1;

                    unsigned int depth = 1;

                    while(depth != 0) {
                        auto character = fgetc(context->source_file);

                        switch(character) {
                            case '\r': {
                                auto character = fgetc(context->source_file);

                                if(character == '\n') {
                                    context->line += 1;
                                    context->character = 1;
                                } else {
                                    ungetc(character, context->source_file);

                                    context->line += 1;
                                    context->character = 1;
                                }
                            } break;

                            case '\n': {
                                context->line += 1;
                                context->character = 1;
                            } break;
                            
                            case '/': {
                                context->character += 1;

                                auto character = fgetc(context->source_file);

                                if(character == '*') {
                                    context->character += 1;

                                    depth += 1;
                                } else {
                                    ungetc(character, context->source_file);
                                }
                            } break;

                            case '*': {
                                context->character += 1;

                                auto character = fgetc(context->source_file);

                                if(character == '/') {
                                    context->character += 1;

                                    depth -= 1;
                                } else {
                                    ungetc(character, context->source_file);
                                }
                            } break;

                            case EOF: {
                                return;
                            } break;

                            default: {
                                context->character += 1;
                            } break;
                        }
                    }
                } else {
                    ungetc(character, context->source_file);

                    return;
                }
            } break;

            default: {
                ungetc(character, context->source_file);

                return;
            } break;
        }
    }
}

const char *parse_identifier(Context *context) {
    List<char> buffer{};

    while(true) {
        auto character = fgetc(context->source_file);

        if(isalnum(character)) {
            context->character += 1;

            append(&buffer, (char)character);
        } else {
            ungetc(character, context->source_file);

            append(&buffer, '\0');

            return buffer.elements;
        }
    }
}

bool expect_character(Context *context, char expected_character) {
    auto character = fgetc(context->source_file);

    if(character == EOF) {
        error(*context, "Unexpected EOF");

        return false;
    }
    
    if(character != expected_character) {
        error(*context, "Expected '%c', got '%c'", expected_character, character);

        return false;
    }

    context->character += 1;

    return true;
}

struct ParseStatementResult {
    bool status;

    Statement statement;
};

ParseStatementResult parse_statement(Context *context) {
    auto character = fgetc(context->source_file);

    if(isalpha(character)) {
        ungetc(character, context->source_file);

        auto name = parse_identifier(context);

        skip_whitespace(context);

        if(!expect_character(context, ':')){
            return { false };
        }

        if(!expect_character(context, ':')){
            return { false };
        }

        skip_whitespace(context);

        // TODO: Other statement types

        if(!expect_character(context, '(')){
            return { false };
        }

        skip_whitespace(context);

        // TODO: Arguments

        if(!expect_character(context, ')')){
            return { false };
        }

        skip_whitespace(context);

        if(!expect_character(context, '{')){
            return { false };
        }

        skip_whitespace(context);

        List<Statement> statements{};

        while(true) {
            auto character = fgetc(context->source_file);

            if(character == '}') {
                break;
            } else {
                ungetc(character, context->source_file);
            }

            auto result = parse_statement(context);

            if(!result.status) {
                return { false };
            }

            append(&statements, result.statement);

            skip_whitespace(context);
        }

        // TODO: Return types

        Statement statement;
        statement.type = StatementType::FunctionDeclaration;
        statement.function_declaration = {
            name,
            statements.elements,
            statements.count
        };

        return {
            true,
            statement
        };
    } else if(character == EOF) {
        error(*context, "Unexpected EOF");

        return { false };
    } else {
        error(*context, "Unexpected character '%c'", character);

        return { false };
    }
}

ParseSourceResult parse_source(const char *source_file_path, FILE *source_file) {
    Context context{
        source_file_path,
        source_file,
        1, 1        
    };

    List<Statement> statements{};

    skip_whitespace(&context);

    while(true) {
        auto character = fgetc(source_file);

        if(character == EOF) {
            break;
        } else {
            ungetc(character, source_file);
        }

        auto result = parse_statement(&context);

        if(!result.status) {
            return { false };
        }

        append(&statements, result.statement);

        skip_whitespace(&context);
    }

    return {
        true,
        statements.elements,
        statements.count
    };
}