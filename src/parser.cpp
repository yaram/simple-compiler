#include "parser.h"
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "list.h"

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

char *parse_identifier(Context *context) {
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
        error(*context, "Unexpected End of File");

        return false;
    }
    
    if(character != expected_character) {
        error(*context, "Expected '%c', got '%c'", expected_character, character);

        return false;
    }

    context->character += 1;

    return true;
}

struct ParseRightExpressionResult {
    bool status;

    Expression right_most_expression;
};

ParseRightExpressionResult parse_right_expressions(Context *context, Expression left_expression) {
    auto current_expression = left_expression;

    while(true) {
        skip_whitespace(context);

        auto character = fgetc(context->source_file);

        context->character += 1;

        if(character == '(') {
            skip_whitespace(context);

            // TODO: Arguments

            if(!expect_character(context, ')')){
                return { false };
            }

            auto function_expression = (Expression*)malloc(sizeof(Expression));
            *function_expression = current_expression;

            Expression expression;
            expression.type = ExpressionType::FunctionCall;
            expression.function_call.expression = function_expression;

            current_expression = expression;
        } else {
            ungetc(character, context->source_file);

            break;
        }
    }

    return { 
        true,
        current_expression
    };
}

struct ParseStatementResult {
    bool status;

    Statement statement;
};

ParseStatementResult parse_statement(Context *context) {
    auto character = fgetc(context->source_file);

    if(isalpha(character)) {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        skip_whitespace(context);

        auto character = fgetc(context->source_file);

        switch(character) {
            case ':': {
                context->character += 1;

                if(!expect_character(context, ':')){
                    return { false };
                }

                skip_whitespace(context);

                // TODO: Other definitions

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
                statement.type = StatementType::FunctionDefinition;
                statement.function_definition = {
                    identifier,
                    to_array(statements)
                };

                return {
                    true,
                    statement
                };
            } break;

            case EOF: {
                error(*context, "Unexpected End of File");

                return { false };
            } break;

            default: {
                ungetc(character, context->source_file);

                Expression expression;
                expression.type = ExpressionType::NamedReference;
                expression.named_reference = identifier;

                auto result = parse_right_expressions(context, expression);

                if(!result.status) {
                    return { false };
                }

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                Statement statement;
                statement.type = StatementType::Expression;
                statement.expression = result.right_most_expression;

                return {
                    true,
                    statement
                };
            } break;
        }
    } else if(isdigit(character) || character == '-') {
        context->character += 1;

        const auto buffer_size = 32;

        char buffer[buffer_size + 1];

        buffer[0] = character;

        auto index = 1;
        while(true) {
            if(index == buffer_size - 1) {
                error(*context, "Integer literal too long");

                return { false };
            }

            auto character = fgetc(context->source_file);

            if(isdigit(character)) {
                context->character += 1;

                buffer[index] = (char)character;

                index += 1;
            } else {
                ungetc(character, context->source_file);

                break;
            }
        }

        buffer[index] = 0;

        auto value = strtoll(buffer, NULL, 10);

        if((value == LLONG_MAX || value == LLONG_MIN) && errno == ERANGE) {
            error(*context, "Integer literal out of range");

            return { false };
        }
        
        Expression expression;
        expression.type = ExpressionType::IntegerLiteral;
        expression.integer_literal = value;

        auto result = parse_right_expressions(context, expression);

        if(!result.status) {
            return { false };
        }

        skip_whitespace(context);

        if(!expect_character(context, ';')) {
            return { false };
        }

        Statement statement;
        statement.type = StatementType::Expression;
        statement.expression = result.right_most_expression;

        return {
            true,
            statement
        };
    } else if(character == EOF) {
        error(*context, "Unexpected End of File");

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

    List<Statement> top_level_statements{};

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

        append(&top_level_statements, result.statement);

        skip_whitespace(&context);
    }

    return {
        true,
        to_array(top_level_statements)
    };
}