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

static void error(Context context, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "%s(%d:%d): ", context.source_file_path, context.line, context.character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    va_end(arguments);
}

static void skip_whitespace(Context *context) {
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

                    while(true) {
                        auto character = fgetc(context->source_file);

                        if(character == '\r') {
                            auto character = fgetc(context->source_file);

                            if(character == '\n') {
                                context->line += 1;
                                context->character = 1;
                            } else {
                                ungetc(character, context->source_file);

                                context->line += 1;
                                context->character = 1;
                            }

                            break;
                        } else if(character == '\n') {
                            context->line += 1;
                            context->character = 1;

                            break;
                        } else if(character == EOF) {
                            ungetc(character, context->source_file);
                        } else {
                            context->character += 1;
                        }
                    }
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

static char *parse_identifier(Context *context) {
    List<char> buffer{};

    while(true) {
        auto character = fgetc(context->source_file);

        if(isalnum(character)) {
            context->character += 1;

            append(&buffer, (char)character);
        } else {
            ungetc(character, context->source_file);

            append(&buffer, '\0');

            break;
        }
    }

    return buffer.elements;
}

static bool expect_character(Context *context, char expected_character) {
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

static Result<Expression> parse_any_expression(Context *context);

static Result<Expression> parse_right_expressions(Context *context, Expression left_expression) {
    auto current_expression = left_expression;

    while(true) {
        skip_whitespace(context);

        auto first_line = context->line;
        auto first_character = context->character;

        auto character = fgetc(context->source_file);

        if(character == '(') {
            context->character += 1;
            
            skip_whitespace(context);

            List<Expression> parameters{};

            auto character = fgetc(context->source_file);

            if(character == ')') {
                context->character += 1;
            } else {
                ungetc(character, context->source_file);

                while(true) {
                    auto result = parse_any_expression(context);

                    if(!result.status) {
                        return { false };
                    }

                    append(&parameters, result.value);

                    skip_whitespace(context);

                    character = getc(context->source_file);

                    if(character == ',') {
                        context->character += 1;

                        skip_whitespace(context);

                        continue;
                    } else if(character == ')') {
                        context->character += 1;

                        break;
                    } else if(character == EOF) {
                        error(*context, "Unexpected End of File");
                    } else {
                        error(*context, "Expected ',' or ')', got '%c'", character);

                        return { false };
                    }
                }
            }

            auto expression = (Expression*)malloc(sizeof(Expression));
            *expression = current_expression;

            current_expression.type = ExpressionType::FunctionCall;
            current_expression.source_file_path = context->source_file_path;
            current_expression.line = first_line;
            current_expression.character = first_character;
            current_expression.function_call.expression = expression;
            current_expression.function_call.parameters = to_array(parameters);
        } else if(character == '.') {
            context->character += 1;

            skip_whitespace(context);

            auto character = fgetc(context->source_file);

            if(isalnum(character)) {
                ungetc(character, context->source_file);

                auto name = parse_identifier(context);

                auto expression = (Expression*)malloc(sizeof(Expression));
                *expression = current_expression;

                current_expression.type = ExpressionType::MemberReference;
                current_expression.source_file_path = context->source_file_path;
                current_expression.line = first_line;
                current_expression.character = first_character;
                current_expression.member_reference.expression = expression;
                current_expression.member_reference.name = name;
            } else if(character == EOF) {
                error(*context, "Unexpected End of File");

                return { false };
            } else {
                error(*context, "Expected a-z, A-Z or 0-9. Got '%c'", character);

                return { false };
            }
        } else if(character == '[') {
            context->character += 1;

            skip_whitespace(context);

            auto character = fgetc(context->source_file);

            if(character == ']') {
                context->character += 1;

                auto expression = (Expression*)malloc(sizeof(Expression));
                *expression = current_expression;

                current_expression.type = ExpressionType::ArrayType;
                current_expression.source_file_path = context->source_file_path;
                current_expression.line = first_line;
                current_expression.character = first_character;
                current_expression.array_type = expression;
            } else {
                ungetc(character, context->source_file);

                auto result = parse_any_expression(context);

                if(!result.status) {
                    return { false };
                }

                skip_whitespace(context);

                if(!expect_character(context, ']')) {
                    return { false };
                }

                auto expression = (Expression*)malloc(sizeof(Expression));
                *expression = current_expression;

                auto index = (Expression*)malloc(sizeof(Expression));
                *index = result.value;

                current_expression.type = ExpressionType::IndexReference;
                current_expression.source_file_path = context->source_file_path;
                current_expression.line = first_line;
                current_expression.character = first_character;
                current_expression.index_reference.expression = expression;
                current_expression.index_reference.index = index;
            }
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

static Result<Expression> parse_any_expression(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    Expression expression;
    if(isalpha(character)) {
        context->character += 1;

        List<char> buffer{};

        append(&buffer, (char)character);

        while(true) {
            auto character = fgetc(context->source_file);

            if(isalnum(character)) {
                context->character += 1;

                append(&buffer, (char)character);
            } else {
                ungetc(character, context->source_file);

                break;
            }
        }

        append(&buffer, '\0');

        expression.type = ExpressionType::NamedReference;
        expression.named_reference = buffer.elements;
    } else if(isdigit(character) || character == '-'){
        auto definitely_identifier = false;
        auto definitely_numeric = false;

        if(character == '-') {
            definitely_numeric = true;
        }

        context->character += 1;

        List<char> buffer{};

        append(&buffer, (char)character);

        auto character_count = 1;
        while(true) {
            auto character = fgetc(context->source_file);

            if(isdigit(character)) {
                context->character += 1;

                if(definitely_identifier) {
                    error(*context, "Expected a-z or A-Z, got '%c'", character);

                    return { false };
                }

                append(&buffer, (char)character);

                character_count += 1;
            } else if(isalpha(character)) {
                context->character += 1;

                if(definitely_numeric) {
                    error(*context, "Expected 0-9, got '%c'", character);

                    return { false };
                }

                definitely_identifier = true;

                append(&buffer, (char)character);
            } else {
                ungetc(character, context->source_file);

                break;
            }
        }

        append(&buffer, '\0');

        if(definitely_numeric || !definitely_identifier) {
            auto value = strtoll(buffer.elements, NULL, 10);

            if((value == LLONG_MAX || value == LLONG_MIN) && errno == ERANGE) {
                error(*context, "Integer literal out of range");

                return { false };
            }

            expression.type = ExpressionType::IntegerLiteral;
            expression.integer_literal = value;
        } else {
            expression.type = ExpressionType::NamedReference;
            expression.named_reference = buffer.elements;
        }
    } else if(character == '*') {
        context->character += 1;

        auto result = parse_any_expression(context);

        if(!result.status) {
            return { false };
        }

        auto pointer_expression = (Expression*)malloc(sizeof(Expression));
        *pointer_expression = result.value;

        expression.type = ExpressionType::Pointer;
        expression.pointer = pointer_expression;
    } else if(character == '"') {
        context->character += 1;

        List<char> buffer{};

        while(true) {
            auto character = fgetc(context->source_file);

            auto done = false;
            switch(character) {
                case '\\': {
                    context->character += 1;

                    auto character = fgetc(context->source_file);

                    switch(character) {
                        case '\\':
                        case '"': {
                            context->character += 1;

                            append(&buffer, (char)character);
                        } break;

                        case 'r': {
                            context->character += 1;

                            append(&buffer, '\r');
                        } break;

                        case 'n': {
                            context->character += 1;

                            append(&buffer, '\n');
                        } break;

                        case '0': {
                            context->character += 1;

                            append(&buffer, '\0');
                        } break;

                        case '\n':
                        case '\r': {
                            error(*context, "Unexpected newline");

                            return { false };
                        } break;

                        case EOF: {
                            error(*context, "Unexpected End of File");

                            return { false };
                        } break;

                        default: {
                            error(*context, "Unknown escape code %c", character);

                            return { false };
                        } break;
                    }
                } break;

                case '"': {
                    context->character += 1;

                    done = true;
                } break;

                case '\n':
                case '\r': {
                    error(*context, "Unexpected newline");

                    return { false };
                } break;

                case EOF: {
                    error(*context, "Unexpected End of File");

                    return { false };
                } break;

                default: {
                    context->character += 1;

                    append(&buffer, (char)character);
                } break;
            }

            if(done) {
                break;
            }
        }

        expression.type = ExpressionType::StringLiteral;
        expression.string_literal = to_array(buffer);
    } else if(character == EOF) {
        error(*context, "Unexpected End of File");

        return { false };
    } else {
        error(*context, "Expected a-z, A-Z, 0-9, '-' or '*'. Got '%c'", character);

        return { false };
    }

    expression.source_file_path = context->source_file_path;
    expression.line = first_line;
    expression.character = first_character;

    auto result = parse_right_expressions(context, expression);

    if(!result.status) {
        return { false };
    }

    return {
        true,
        result.value,
    };
}

static Result<Statement> parse_expression_statement_or_variable_assignment(Context *context, Expression expression) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    switch(character) {
        case '=': {
            context->character += 1;

            skip_whitespace(context);

            auto value_result = parse_any_expression(context);

            if(!value_result.status) {
                return { false };
            }

            skip_whitespace(context);

            if(!expect_character(context, ';')) {
                return { false };
            }

            Statement statement;
            statement.type = StatementType::Assignment;
            statement.source_file_path = context->source_file_path;
            statement.line = first_line;
            statement.character = first_character;
            statement.assignment.target = expression;
            statement.assignment.value = value_result.value;

            return {
                true,
                statement
            };
        } break;
        
        case ';': {
            context->character += 1;

            Statement statement;
            statement.type = StatementType::Expression;
            statement.source_file_path = context->source_file_path;
            statement.line = first_line;
            statement.character = first_character;
            statement.expression = expression;

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
            error(*context, "Expected '=' or ';'. Got '%c'", character);

            return { false };
        } break;
    }
}

static Result<Statement> parse_statement(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    if(isalpha(character)) {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        skip_whitespace(context);

        auto character = fgetc(context->source_file);

        switch(character) {
            case ':': {
                context->character += 1;

                auto character = fgetc(context->source_file);

                if(character == ':') {
                    context->character += 1;

                    skip_whitespace(context);

                    auto character = fgetc(context->source_file);

                    if(character == '(') {
                        context->character += 1;

                        skip_whitespace(context);

                        List<FunctionParameter> parameters{};
                        
                        auto character = fgetc(context->source_file);

                        if(character == ')') {
                            context->character += 1;
                        } else {
                            ungetc(character, context->source_file);

                            while(true) {
                                auto name = parse_identifier(context);

                                skip_whitespace(context);

                                if(!expect_character(context, ':')) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                auto result = parse_any_expression(context);

                                if(!result.status) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                FunctionParameter parameter {
                                    name,
                                    result.value
                                };

                                append(&parameters, parameter);

                                character = fgetc(context->source_file);

                                if(character == ',') {
                                    context->character += 1;

                                    skip_whitespace(context);

                                    continue;
                                } else if(character == ')') {
                                    context->character += 1;

                                    break;
                                } else if(character == EOF) {
                                    error(*context, "Unexpected End of File");

                                    return { false };
                                } else {
                                    error(*context, "Expected ',' or ')', got '%c'", character);

                                    return { false };
                                }
                            }
                        }

                        skip_whitespace(context);

                        character = fgetc(context->source_file);

                        bool is_external;
                        bool has_return_type;
                        Expression return_type;
                        if(character == '-') {
                            context->character += 1;

                            if(!expect_character(context, '>')) {
                                return { false };
                            }

                            skip_whitespace(context);

                            auto result = parse_any_expression(context);

                            if(!result.status) {
                                return { false };
                            }

                            has_return_type = true;
                            return_type = result.value;

                            skip_whitespace(context);
                            
                            auto character = fgetc(context->source_file);

                            if(character == '{') {
                                context->character += 1;

                                is_external = false;
                            } else if(isalpha(character)) {
                                ungetc(character, context->source_file);

                                auto identifier = parse_identifier(context);

                                if(strcmp(identifier, "extern") != 0) {
                                    error(*context, "Expected 'extern' or '{', got '%s'", identifier);

                                    return { false };
                                }

                                if(!expect_character(context, ';')) {
                                    return { false };
                                }

                                is_external = true;
                            } else if(character == EOF) {
                                error(*context, "Unexpected End of File", character);

                                return { false };
                            } else {
                                error(*context, "Expected 'extern' or '{', got '%c'", character);

                                return { false };
                            }
                        } else if(isalpha(character)) {
                            ungetc(character, context->source_file);

                            auto identifier = parse_identifier(context);

                            if(strcmp(identifier, "extern") != 0) {
                                error(*context, "Expected 'extern', '-' or '{', got '%s'", identifier);

                                return { false };
                            }

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            is_external = true;
                            has_return_type = false;
                        } else if(character == '{') {
                            context->character += 1;

                            has_return_type = false;
                        } else if(character == EOF) {
                            error(*context, "Unexpected End of File", character);

                            return { false };
                        } else {
                            error(*context, "Expected 'extern', '-' or '{', got '%c'", character);

                            return { false };
                        }

                        skip_whitespace(context);

                        Array<Statement> statements;
                        if(!is_external) {
                            List<Statement> statement_list{};

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

                                append(&statement_list, result.value);

                                skip_whitespace(context);
                            }

                            statements = to_array(statement_list);
                        }

                        Statement statement;
                        statement.type = StatementType::FunctionDeclaration;
                        statement.source_file_path = context->source_file_path;
                        statement.line = first_line;
                        statement.character = first_character;
                        statement.function_declaration.name = identifier;
                        statement.function_declaration.parameters = to_array(parameters);
                        statement.function_declaration.has_return_type = has_return_type;
                        statement.function_declaration.is_external = is_external;

                        if(has_return_type) {
                            statement.function_declaration.return_type = return_type;
                        }

                        if(!is_external) {
                            statement.function_declaration.statements = statements;
                        }

                        return {
                            true,
                            statement
                        };
                    } else {
                        ungetc(character, context->source_file);

                        auto result = parse_any_expression(context);

                        if(!result.status) {
                            return { false };
                        }

                        if(!expect_character(context, ';')) {
                            return { false };
                        }

                        Statement statement;
                        statement.type = StatementType::ConstantDefinition;
                        statement.source_file_path = context->source_file_path;
                        statement.line = first_line;
                        statement.character = first_character;
                        statement.constant_definition = {
                            identifier,
                            result.value
                        };

                        return {
                            true,
                            statement
                        };
                    }
                } else {
                    ungetc(character, context->source_file);

                    skip_whitespace(context);

                    auto character = fgetc(context->source_file);

                    bool has_type;
                    Expression type;
                    if(character != '=') {
                        ungetc(character, context->source_file);

                        auto result = parse_any_expression(context);

                        if(!result.status) {
                            return { false };
                        }

                        has_type = true;
                        type = result.value;

                        skip_whitespace(context);

                        character = fgetc(context->source_file);
                    } else {
                        has_type = false;
                    }

                    bool has_initializer;
                    Expression initializer;
                    if(character == '=') {
                        context->character += 1;

                        skip_whitespace(context);

                        auto result = parse_any_expression(context);

                        if(!result.status) {
                            return { false };
                        }

                        has_initializer = true;
                        initializer = result.value;

                        skip_whitespace(context);

                        if(!expect_character(context, ';')) {
                            return { false };
                        }
                    } else if(character == ';') {
                        has_initializer = false;
                    } else if(character == EOF) {
                        error(*context, "Unexpected End of File");

                        return { false };
                    } else {
                        error(*context, "Expected '=' or ';', got '%c'", character);

                        return { false };
                    }

                    Statement statement;
                    statement.type = StatementType::VariableDeclaration;
                    statement.source_file_path = context->source_file_path;
                    statement.line = first_line;
                    statement.character = first_character;
                    statement.variable_declaration.name = identifier;
                    statement.variable_declaration.has_type = has_type;
                    statement.variable_declaration.has_initializer = has_initializer;

                    if(has_type) {
                        statement.variable_declaration.type = type;
                    }

                    if(has_initializer) {
                        statement.variable_declaration.initializer = initializer;
                    }

                    return {
                        true,
                        statement
                    };
                }
            } break;

            case '=': {
                context->character += 1;

                Expression target;
                target.type = ExpressionType::NamedReference;
                target.named_reference = identifier;

                skip_whitespace(context);

                auto result = parse_any_expression(context);

                if(!result.status) {
                    return { false };
                }

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                Statement statement;
                statement.type = StatementType::Assignment;
                statement.source_file_path = context->source_file_path;
                statement.line = first_line;
                statement.character = first_character;
                statement.assignment.target = target;
                statement.assignment.value = result.value;

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
                expression.source_file_path = context->source_file_path;
                expression.line = first_line;
                expression.character = first_character;
                expression.type = ExpressionType::NamedReference;
                expression.named_reference = identifier;

                auto result = parse_right_expressions(context, expression);

                if(!result.status) {
                    return { false };
                }

                skip_whitespace(context);

                auto statement_result = parse_expression_statement_or_variable_assignment(context, result.value);

                if(!statement_result.status) {
                    return { false };
                }

                return {
                    true,
                    statement_result.value
                };
            } break;
        }
    } else {
        ungetc(character, context->source_file);

        auto expression_result = parse_any_expression(context);

        if(!expression_result.status) {
            return { false };
        }

        auto statement_result = parse_expression_statement_or_variable_assignment(context, expression_result.value);

        if(!statement_result.status) {
            return { false };
        }

        return {
            true,
            statement_result.value
        };
    }
}

Result<Array<Statement>> parse_source(const char *source_file_path, FILE *source_file) {
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

        append(&top_level_statements, result.value);

        skip_whitespace(&context);
    }

    return {
        true,
        to_array(top_level_statements)
    };
}