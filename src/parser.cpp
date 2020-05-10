#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
#include "profiler.h"
#include "path.h"
#include "list.h"
#include "util.h"

struct Context {
    const char *path;

    Array<Token> tokens;

    size_t next_token_index;
};

static void error(Context context, const char *format, ...) {
    auto token = context.tokens[context.next_token_index];

    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "Error: %s(%d,%d): ", context.path, token.line, token.first_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    auto file = fopen(context.path, "rb");

    if(file != nullptr) {
        unsigned int current_line = 1;

        while(current_line != token.line) {
            auto character = fgetc(file);

            switch(character) {
                case '\r': {
                    auto character = fgetc(file);

                    if(character == '\n') {
                        current_line += 1;
                    } else {
                        ungetc(character, file);

                        current_line += 1;
                    }
                } break;

                case '\n': {
                    current_line += 1;
                } break;

                case EOF: {
                    fclose(file);

                    va_end(arguments);

                    return;
                } break;
            }
        }

        unsigned int skipped_spaces = 0;
        auto done_skipping_spaces = false;

        auto done = false;
        while(!done) {
            auto character = fgetc(file);

            switch(character) {
                case '\r':
                case '\n': {
                    done = true;
                } break;

                case ' ': {
                    if(!done_skipping_spaces) {
                        skipped_spaces += 1;
                    } else {
                        fprintf(stderr, "%c", character);
                    }
                } break;

                case EOF: {
                    fclose(file);

                    va_end(arguments);

                    return;
                } break;

                default: {
                    fprintf(stderr, "%c", character);

                    done_skipping_spaces = true;
                } break;
            }
        }

        fprintf(stderr, "\n");

        for(unsigned int i = 1; i < token.first_character - skipped_spaces; i += 1) {
            fprintf(stderr, " ");
        }

        if(token.last_character - token.first_character == 0) {
            fprintf(stderr, "^");
        } else {
            for(unsigned int i = token.first_character; i <= token.last_character; i += 1) {
                fprintf(stderr, "-");
            }
        }

        fprintf(stderr, "\n");

        fclose(file);
    }

    va_end(arguments);
}

static FileRange token_range(Context context, Token token) {
    return {
        token.line,
        token.first_character,
        token.line,
        token.last_character
    };
}

static FileRange span_range(FileRange first, FileRange last) {
    return {
        first.first_line,
        first.first_character,
        last.last_character,
        last.last_line
    };
}

static Result<Token> peek_token(Context context) {
    if(context.next_token_index < context.tokens.count) {
        auto token = context.tokens[context.next_token_index];

        return {
            true,
            token
        };
    } else {
        fprintf(stderr, "Error: %s: Unexpected end of file\n", context.path);

        return { false };
    }
}

static void consume_token(Context *context) {
    context->next_token_index += 1;
}

static bool expect_basic_token(Context *context, TokenType type) {
    expect(token, peek_token(*context));

    if(token.type != type) {
        Token expected_token;
        expected_token.type = type;

        error(*context, "Expected '%s', got '%s'", get_token_text(expected_token), get_token_text(token));

        return false;
    }

    consume_token(context);

    return true;
}

static Result<FileRange> expect_basic_token_with_range(Context *context, TokenType type) {
    expect(token, peek_token(*context));

    if(token.type != type) {
        Token expected_token;
        expected_token.type = type;

        error(*context, "Expected '%s', got '%s'", get_token_text(expected_token), get_token_text(token));

        return { false };
    }

    consume_token(context);

    auto range = token_range(*context, token);

    return {
        true,
        range
    };
}

static Result<Array<char>> expect_string(Context *context) {
    expect(token, peek_token(*context));

    if(token.type != TokenType::String) {
        error(*context, "Expected a string, got '%s'", get_token_text(token));

        return { false };
    }

    consume_token(context);

    return {
        true,
        token.string
    };
}

static Result<Identifier> expect_identifier(Context *context) {
    expect(token, peek_token(*context));

    if(token.type != TokenType::Identifier) {
        error(*context, "Expected an identifier, got '%s'", get_token_text(token));

        return { false };
    }

    consume_token(context);

    return {
        true,
        {
            token.identifier,
            token_range(*context, token)
        }
    };
}

static Identifier identifier_from_token(Context context, Token token) {
    return {
        token.identifier,
        token_range(context, token)
    };
}

static Expression *named_reference_from_identifier(Identifier identifier) {
    return new NamedReference {
        identifier.range,
        identifier
    };
}

enum struct OperatorPrecedence {
    None,
    BooleanOr,
    BooleanAnd,
    BitwiseOr,
    BitwiseAnd,
    Comparison,
    Additive,
    Multiplicitive,
    Cast,
    PrefixUnary,
    PostfixUnary
};

static Result<FunctionParameter> parse_function_parameter_second_half(Context *context, Identifier name, bool is_constant);

static Result<Expression*> parse_expression_continuation(Context *context, OperatorPrecedence minimum_precedence, Expression *expression);

// Precedence sorting based on https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
static_profiled_function(Result<Expression*>, parse_expression, (Context *context, OperatorPrecedence minimum_precedence), (context, minimum_precedence)) {
    Expression *left_expression;

    expect(token, peek_token(*context));

    // Parse atomic & prefix-unary expressions (non-left-recursive)
    switch(token.type) {
        case TokenType::Identifier: {
            consume_token(context);

            auto identifier = identifier_from_token(*context, token);

            left_expression = named_reference_from_identifier(identifier);
        } break;

        case TokenType::Integer: {
            consume_token(context);

            left_expression = new IntegerLiteral {
                token_range(*context, token),
                token.integer
            };
        } break;

        case TokenType::FloatingPoint: {
            consume_token(context);

            left_expression = new FloatLiteral {
                token_range(*context, token),
                token.floating_point
            };
        } break;

        case TokenType::Asterisk: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(*context, token), expression->range),
                UnaryOperation::Operator::Pointer,
                expression
            };
        } break;

        case TokenType::Bang: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(*context, token), expression->range),
                UnaryOperation::Operator::BooleanInvert,
                expression
            };
        } break;

        case TokenType::Dash: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(*context, token), expression->range),
                UnaryOperation::Operator::Negation,
                expression
            };
        } break;

        case TokenType::String: {
            consume_token(context);

            left_expression = new StringLiteral {
                token_range(*context, token),
                token.string
            };
        } break;

        case TokenType::OpenRoundBracket: {
            consume_token(context);

            auto first_range = token_range(*context, token);

            expect(token, peek_token(*context));

            switch(token.type) {
                case TokenType::Dollar: {
                    consume_token(context);

                    List<FunctionParameter> parameters{};

                    expect(name, expect_identifier(context));

                    expect(parameter, parse_function_parameter_second_half(context, name, true));

                    append(&parameters, parameter);

                    expect(token, peek_token(*context));

                    FileRange last_range;
                    switch(token.type) {
                        case TokenType::Comma: {
                            consume_token(context);

                            while(true) {
                                expect(pre_token, peek_token(*context));

                                bool is_constant;
                                if(pre_token.type == TokenType::Dollar) {
                                    consume_token(context);

                                    is_constant = true;
                                } else {
                                    is_constant = false;
                                }

                                expect(identifier, expect_identifier(context));

                                expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                append(&parameters, parameter);

                                expect(token, peek_token(*context));

                                auto done = false;
                                switch(token.type) {
                                    case TokenType::Comma: {
                                        consume_token(context);
                                    } break;

                                    case TokenType::CloseRoundBracket: {
                                        consume_token(context);

                                        done = true;
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                        return { false };
                                    } break;
                                }

                                if(done) {
                                    last_range = token_range(*context, token);

                                    break;
                                }
                            }
                        } break;

                        case TokenType::CloseRoundBracket: {
                            consume_token(context);

                            last_range = token_range(*context, token);
                        } break;

                        default: {
                            error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                            return { false };
                        } break;
                    }

                    expect(post_token, peek_token(*context));

                    Expression *return_type;
                    if(post_token.type == TokenType::Arrow) {
                        consume_token(context);

                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        return_type = expression;
                        last_range = expression->range;
                    } else {
                        return_type = nullptr;
                    }

                    left_expression = new FunctionType {
                        span_range(first_range, last_range),
                        to_array(parameters),
                        return_type
                    };
                } break;

                case TokenType::CloseRoundBracket: {
                    consume_token(context);

                    auto last_range = token_range(*context, token);

                    expect(token, peek_token(*context));

                    Expression* return_type;
                    if(token.type == TokenType::Arrow) {
                        consume_token(context);

                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        return_type = expression;
                        last_range = expression->range;
                    } else {
                        return_type = nullptr;
                    }

                    left_expression = new FunctionType {
                        span_range(first_range, last_range),
                        {},
                        return_type
                    };
                } break;

                case TokenType::Identifier: {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    if(token.type == TokenType::Colon) {
                        List<FunctionParameter> parameters{};

                        expect(parameter, parse_function_parameter_second_half(context, identifier, false));

                        append(&parameters, parameter);

                        expect(token, peek_token(*context));

                        FileRange last_range;
                        switch(token.type) {
                            case TokenType::Comma: {
                                consume_token(context);

                                while(true) {
                                    expect(pre_token, peek_token(*context));

                                    bool is_constant;
                                    if(pre_token.type == TokenType::Dollar) {
                                        consume_token(context);

                                        is_constant = true;
                                    } else {
                                        is_constant = false;
                                    }

                                    expect(identifier, expect_identifier(context));

                                    expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                    append(&parameters, parameter);

                                    expect(token, peek_token(*context));

                                    auto done = false;
                                    switch(token.type) {
                                        case TokenType::Comma: {
                                            consume_token(context);
                                        } break;

                                        case TokenType::CloseRoundBracket: {
                                            consume_token(context);

                                            done = true;
                                        } break;

                                        default: {
                                            error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                            return { false };
                                        } break;
                                    }

                                    if(done) {
                                        last_range = token_range(*context, token);

                                        break;
                                    }
                                }
                            } break;

                            case TokenType::CloseRoundBracket: {
                                consume_token(context);

                                last_range = token_range(*context, token);
                            } break;

                            default: {
                                error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                return { false };
                            } break;
                        }

                        expect(post_token, peek_token(*context));

                        Expression *return_type;
                        if(post_token.type == TokenType::Arrow) {
                            consume_token(context);

                            expect(expression, parse_expression(context, OperatorPrecedence::None));

                            return_type = expression;
                            last_range = expression->range;
                        } else {
                            return_type = nullptr;
                        }

                        left_expression = new FunctionType {
                            span_range(first_range, last_range),
                            to_array(parameters),
                            return_type
                        };
                    } else {
                        auto expression = named_reference_from_identifier(identifier);

                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                        if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                            return { false };
                        }

                        left_expression = right_expression;
                    }
                } break;

                default: {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                        return { false };
                    }

                    left_expression = expression;
                } break;
            }
        } break;

        case TokenType::OpenCurlyBracket: {
            consume_token(context);

            auto first_range = token_range(*context, token);

            expect(token, peek_token(*context));

            switch(token.type) {
                case TokenType::CloseCurlyBracket: {
                    consume_token(context);

                    left_expression = new ArrayLiteral {
                        span_range(first_range, token_range(*context, token)),
                        {}
                    };
                } break;

                case TokenType::Identifier: {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    switch(token.type) {
                        case TokenType::Equals: {
                            consume_token(context);

                            expect(first_expression, parse_expression(context, OperatorPrecedence::None));

                            List<StructLiteral::Member> members{};

                            append(&members, {
                                identifier,
                                first_expression
                            });

                            expect(token, peek_token(*context));

                            FileRange last_range;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    consume_token(context);

                                    while(true) {
                                        expect(identifier, expect_identifier(context));

                                        if(!expect_basic_token(context, TokenType::Equals)) {
                                            return { false };
                                        }

                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        append(&members, {
                                            identifier,
                                            expression
                                        });

                                        expect(token, peek_token(*context));

                                        auto done = false;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                consume_token(context);
                                            } break;

                                            case TokenType::CloseCurlyBracket: {
                                                consume_token(context);

                                                done = true;
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                                return { false };
                                            } break;
                                        }

                                        if(done) {
                                            last_range = token_range(*context, token);

                                            break;
                                        }
                                    }
                                } break;

                                case TokenType::CloseCurlyBracket: {
                                    consume_token(context);

                                    last_range = token_range(*context, token);
                                } break;

                                default: {
                                    error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                    return { false };
                                } break;
                            }

                            left_expression = new StructLiteral {
                                span_range(first_range, last_range),
                                to_array(members)
                            };
                        } break;

                        case TokenType::CloseCurlyBracket: {
                            consume_token(context);

                            auto first_element = named_reference_from_identifier(identifier);

                            left_expression = new ArrayLiteral {
                                span_range(first_range, token_range(*context, token)),
                                {
                                    1,
                                    heapify(first_element)
                                }
                            };
                        } break;

                        default: {
                            auto sub_expression = named_reference_from_identifier(identifier);

                            expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, sub_expression));

                            List<Expression*> elements{};

                            append(&elements, right_expression);

                            expect(token, peek_token(*context));

                            FileRange last_range;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    consume_token(context);

                                    while(true) {
                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        append(&elements, expression);

                                        expect(token, peek_token(*context));

                                        auto done = false;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                consume_token(context);
                                            } break;

                                            case TokenType::CloseCurlyBracket: {
                                                consume_token(context);

                                                done = true;
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                                return { false };
                                            } break;
                                        }

                                        if(done) {
                                            last_range = token_range(*context, token);

                                            break;
                                        }
                                    }
                                } break;

                                case TokenType::CloseCurlyBracket: {
                                    consume_token(context);

                                    last_range = token_range(*context, token);
                                } break;

                                default: {
                                    error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                    return { false };
                                }
                            }

                            left_expression = new ArrayLiteral {
                                span_range(first_range, last_range),
                                to_array(elements)
                            };
                        } break;
                    }
                } break;

                default: {
                    expect(first_expression, parse_expression(context, OperatorPrecedence::None));

                    List<Expression*> elements{};

                    append(&elements, first_expression);

                    expect(token, peek_token(*context));

                    FileRange last_range;
                    switch(token.type) {
                        case TokenType::Comma: {
                            consume_token(context);

                            while(true) {
                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                append(&elements, expression);

                                expect(token, peek_token(*context));

                                auto done = false;
                                switch(token.type) {
                                    case TokenType::Comma: {
                                        consume_token(context);
                                    } break;

                                    case TokenType::CloseCurlyBracket: {
                                        consume_token(context);

                                        done = true;
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                        return { false };
                                    } break;
                                }

                                if(done) {
                                    last_range = token_range(*context, token);

                                    break;
                                }
                            }
                        } break;

                        case TokenType::CloseCurlyBracket: {
                            consume_token(context);

                            last_range = token_range(*context, token);
                        } break;
                    }

                    left_expression = new ArrayLiteral {
                        span_range(first_range, last_range),
                        to_array(elements)
                    };
                } break;
            }
        } break;

        case TokenType::OpenSquareBracket: {
            consume_token(context);

            expect(token, peek_token(*context));

            Expression *index;
            FileRange last_range;
            if(token.type == TokenType::CloseSquareBracket) {
                consume_token(context);

                index = nullptr;
                last_range = token_range(*context, token);
            } else {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect(range, expect_basic_token_with_range(context, TokenType::CloseSquareBracket));

                index = expression;
                last_range = range;
            }

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new ArrayType {
                span_range(token_range(*context, token), expression->range),
                expression,
                index
            };
        } break;

        default: {
            error(*context, "Expected an expression. Got '%s'", get_token_text(token));

            return { false };
        } break;
    }

    expect(final_expression, parse_expression_continuation(context, minimum_precedence, left_expression));

    return {
        true,
        final_expression
    };
}

static_profiled_function(Result<Expression*>, parse_expression_continuation, (
    Context *context,
    OperatorPrecedence minimum_precedence,
    Expression *expression
), (
    context,
    minimum_precedence,
    expression
)) {
    auto current_expression = expression;

    auto done = false;
    while(!done) {
        expect(token, peek_token(*context));

        auto done = false;
        switch(token.type) {
            case TokenType::Dot: {
                if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(identifier, expect_identifier(context));

                current_expression = new MemberReference {
                    span_range(current_expression->range, identifier.range),
                    current_expression,
                    identifier
                };
            } break;

            case TokenType::Plus: {
                if(OperatorPrecedence::Additive <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Additive));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Addition,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::Dash: {
                if(OperatorPrecedence::Additive <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Additive));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Subtraction,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::Asterisk: {
                if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Multiplicitive));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Multiplication,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::ForwardSlash: {
                if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Multiplicitive));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Division,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::Percent: {
                if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Multiplicitive));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Modulo,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::Ampersand: {
                if(OperatorPrecedence::BitwiseAnd <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::BitwiseAnd));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::BitwiseAnd,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::DoubleAmpersand: {
                if(OperatorPrecedence::BooleanAnd <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::BooleanAnd));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::BooleanAnd,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::Pipe: {
                if(OperatorPrecedence::BitwiseOr <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::BitwiseOr));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::BitwiseOr,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::DoublePipe: {
                if(OperatorPrecedence::BooleanOr <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::BooleanOr));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::BooleanOr,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::DoubleEquals: {
                if(OperatorPrecedence::Comparison <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Comparison));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::Equal,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::BangEquals: {
                if(OperatorPrecedence::Comparison <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Comparison));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::NotEqual,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::LeftArrow: {
                if(OperatorPrecedence::Comparison <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Comparison));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::LessThan,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::RightArrow: {
                if(OperatorPrecedence::Comparison <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                expect(expression, parse_expression(context, OperatorPrecedence::Comparison));

                current_expression = new BinaryOperation {
                    span_range(current_expression->range, expression->range),
                    BinaryOperation::Operator::GreaterThan,
                    current_expression,
                    expression
                };
            } break;

            case TokenType::OpenRoundBracket: {
                if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                List<Expression*> parameters{};

                expect(token, peek_token(*context));

                FileRange last_range;
                if(token.type == TokenType::CloseRoundBracket) {
                    consume_token(context);

                    last_range = token_range(*context, token);
                } else {
                    while(true) {
                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        append(&parameters, expression);

                        expect(token, peek_token(*context));

                        auto done = false;
                        switch(token.type) {
                            case TokenType::Comma: {
                                consume_token(context);
                            } break;

                            case TokenType::CloseRoundBracket: {
                                consume_token(context);

                                done = true;
                            } break;

                            default: {
                                error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                return { false };
                            } break;
                        }

                        if(done) {
                            last_range = token_range(*context, token);

                            break;
                        }
                    }
                }

                current_expression = new FunctionCall {
                    span_range(current_expression->range, last_range),
                    current_expression,
                    to_array(parameters)
                };
            } break;

            case TokenType::OpenSquareBracket: {
                if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                auto first_range = token_range(*context, token);

                expect(token, peek_token(*context));

                Expression *index;
                FileRange last_range;
                if(token.type == TokenType::CloseSquareBracket) {
                    consume_token(context);

                    index = nullptr;
                    last_range = token_range(*context, token);
                } else {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    expect(range, expect_basic_token_with_range(context, TokenType::CloseSquareBracket));

                    index = expression;
                    last_range = range;
                }

                current_expression = new IndexReference {
                    span_range(current_expression->range, last_range),
                    current_expression,
                    index
                };
            } break;

            case TokenType::Identifier: {
                if(strcmp(token.identifier, "as") == 0) {
                    if(OperatorPrecedence::Cast <= minimum_precedence) {
                        done = true;

                        break;
                    }

                    consume_token(context);

                    expect(expression, parse_expression(context, OperatorPrecedence::Cast));

                    current_expression = new Cast {
                        span_range(current_expression->range, expression->range),
                        current_expression,
                        expression
                    };
                } else {
                    done = true;
                }
            } break;

            default: {
                done = true;
            } break;
        }

        if(done) {
            break;
        }
    }

    return {
        true,
        current_expression
    };
}

static Result<FunctionParameter> parse_function_parameter_second_half(Context *context, Identifier name, bool is_constant) {
    if(!expect_basic_token(context, TokenType::Colon)) {
        return { false };
    }

    FunctionParameter parameter;
    parameter.name = name;
    parameter.is_constant = is_constant;

    expect(token, peek_token(*context));

    switch(token.type) {
        case TokenType::Dollar: {
            consume_token(context);

            expect(name, expect_identifier(context));

            parameter.is_polymorphic_determiner = true;
            parameter.polymorphic_determiner = name;
        } break;

        default: {
            expect(expression, parse_expression(context, OperatorPrecedence::None));

            parameter.is_polymorphic_determiner = false;
            parameter.type = expression;
        } break;
    }

    return {
        true,
        parameter
    };
}

static Result<Statement*> parse_statement(Context *context);

static Result<Statement*> continue_parsing_function_declaration_or_function_type_constant(Context *context, Identifier name, Array<FunctionParameter> parameters, FileRange parameters_range) {
    auto last_range = parameters_range;

    expect(pre_token, peek_token(*context));

    Expression *return_type;
    switch(pre_token.type) {
        case TokenType::Arrow: {
            consume_token(context);

            last_range = token_range(*context, pre_token);

            expect(expression, parse_expression(context, OperatorPrecedence::None));

            return_type = expression;
        } break;

        default: {
            return_type = nullptr;
        } break;
    }

    expect(token, peek_token(*context));

    switch(token.type) {
        case TokenType::OpenCurlyBracket: {
            consume_token(context);

            List<Statement*> statements{};

            while(true) {
                expect(token, peek_token(*context));

                if(token.type == TokenType::CloseCurlyBracket) {
                    consume_token(context);

                    last_range = token_range(*context, token);

                    break;
                } else {
                    expect(statement, parse_statement(context));

                    append(&statements, statement);
                }
            }

            auto function_declaration = new FunctionDeclaration {
                span_range(name.range, last_range),
                name,
                parameters,
                return_type,
                false,
                to_array(statements)
            };

            return {
                true,
                function_declaration
            };
        } break;

        case TokenType::Semicolon: {
            consume_token(context);

            auto function_type = new FunctionType {
                span_range(parameters_range, last_range),
                parameters,
                return_type
            };

            auto constant_definition = new ConstantDefinition {
                span_range(parameters_range, token_range(*context, token)),
                name,
                function_type
            };

            return {
                true,
                constant_definition
            };
        } break;

        case TokenType::Identifier: {
            if(strcmp(token.identifier, "extern") == 0) {
                consume_token(context);

                List<const char *> libraries{};

                expect(token, peek_token(*context));

                FileRange last_range;
                switch(token.type) {
                    case TokenType::Semicolon: {
                        consume_token(context);

                        last_range = token_range(*context, token);
                    } break;

                    case TokenType::String: {
                        while(true) {
                            expect(string, expect_string(context));

                            auto library = allocate<char>(string.count + 1);
                            memcpy(library, string.elements, string.count);
                            library[string.count] = 0;

                            append(&libraries, (const char *)library);

                            expect(token, peek_token(*context));

                            auto done = false;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    consume_token(context);
                                } break;

                                case TokenType::Semicolon: {
                                    consume_token(context);

                                    done = true;
                                } break;

                                default: {
                                    error(*context, "Expected ',' or ';', got '%s'", get_token_text(token));

                                    return { false };
                                } break;
                            }

                            if(done) {
                                last_range = token_range(*context, token);

                                break;
                            }
                        }
                    } break;

                    default: {
                        error(*context, "Expected ';' or a string, got '%s'", get_token_text(token));

                        return { false };
                    } break;
                }

                auto function_declaration = new FunctionDeclaration {
                    span_range(name.range, last_range),
                    name,
                    parameters,
                    return_type,
                    to_array(libraries)
                };

                return {
                    true,
                    function_declaration
                };
            } else if(strcmp(token.identifier, "no_mangle") == 0) {
                consume_token(context);

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                while(true) {
                    expect(token, peek_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(*context, token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        append(&statements, statement);
                    }
                }

                auto function_declaration = new FunctionDeclaration {
                    span_range(name.range, last_range),
                    name,
                    parameters,
                    return_type,
                    true,
                    to_array(statements)
                };

                return {
                    true,
                    function_declaration
                };
            } else {
                error(*context, "Expected '->', '{', ';', 'extern' or 'no_mangle', got '%s'", token.identifier);

                return { false };
            }

            
        } break;

        default: {
            error(*context, "Expected '->', '{', ';', 'extern' or 'no_mangle', got '%s'", get_token_text(token));

            return { false };
        } break;
    }
}

static_profiled_function(Result<Statement*>, parse_statement, (Context *context), (context)) {
    expect(token, peek_token(*context));

    auto first_range = token_range(*context, token);

    switch(token.type) {
        case TokenType::Hash: {
            consume_token(context);

            expect(token, peek_token(*context));

            if(token.type != TokenType::Identifier) {
                error(*context, "Expected 'import', got '%s'", get_token_text(token));

                return { false };
            }

            consume_token(context);

            if(strcmp(token.identifier, "import") == 0) {
                expect(string, expect_string(context));

                expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                auto import_path = allocate<char>(string.count + 1);
                memcpy(import_path, string.elements, string.count);
                import_path[string.count] = 0;

                auto import = new Import {
                    span_range(first_range, last_range),
                    import_path
                };

                return {
                    true,
                    import
                };
            } else {
                error(*context, "Expected 'import' or 'library', got '%s'", get_token_text(token));

                return { false };
            }
        } break;

        case TokenType::Identifier: {
            consume_token(context);

            if(strcmp(token.identifier, "if") == 0) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(*context, token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        append(&statements, statement);
                    }
                }

                List<IfStatement::ElseIf> else_ifs{};

                auto has_else = false;
                List<Statement*> else_statements{};
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.type == TokenType::Identifier && strcmp(token.identifier, "else") == 0) {
                        consume_token(context);

                        expect(token, peek_token(*context));

                        switch(token.type) {
                            case TokenType::OpenCurlyBracket: {
                                consume_token(context);

                                while(true) {
                                    expect(token, peek_token(*context));

                                    if(token.type == TokenType::CloseCurlyBracket) {
                                        consume_token(context);

                                        last_range = token_range(*context, token);

                                        break;
                                    } else {
                                        expect(statement, parse_statement(context));

                                        append(&else_statements, statement);
                                    }
                                }
                            } break;

                            case TokenType::Identifier: {
                                if(strcmp(token.identifier, "if") != 0) {
                                    error(*context, "Expected '{' or 'if', got '%s'", get_token_text(token));

                                    return { false };
                                }

                                consume_token(context);

                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                                    return { false };
                                }
                                
                                List<Statement*> statements{};

                                while(true) {
                                    expect(token, peek_token(*context));

                                    if(token.type == TokenType::CloseCurlyBracket) {
                                        consume_token(context);

                                        last_range = token_range(*context, token);

                                        break;
                                    } else {
                                        expect(statement, parse_statement(context));

                                        append(&statements, statement);
                                    }
                                }

                                append(&else_ifs, {
                                    expression,
                                    to_array(statements)
                                });
                            } break;

                            default: {
                                error(*context, "Expected '{' or 'if', got '%s'", get_token_text(token));

                                return { false };
                            } break;
                        }

                        if(has_else) {
                            break;
                        }
                    } else {
                        break;
                    }
                }

                auto if_statement = new IfStatement {
                    span_range(first_range, last_range),
                    expression,
                    to_array(statements),
                    to_array(else_ifs),
                    to_array(else_statements)
                };

                return {
                    true,
                    if_statement
                };
            } else if(strcmp(token.identifier, "while") == 0) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(*context, token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        append(&statements, statement);
                    }
                }

                auto while_loop = new WhileLoop {
                    span_range(first_range, last_range),
                    expression,
                    to_array(statements)
                };

                return {
                    true,
                    while_loop
                };
            } else if(strcmp(token.identifier, "for") == 0) {
                expect(token, peek_token(*context));

                bool has_index_name;
                Identifier index_name;
                Expression *from;
                if(token.type == TokenType::Identifier) {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    if(token.type == TokenType::Colon) {
                        consume_token(context);

                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        has_index_name = true;
                        index_name = identifier;

                        from = expression;
                    } else {
                        auto named_reference = named_reference_from_identifier(identifier);

                        expect(expression, parse_expression_continuation(context, OperatorPrecedence::None, named_reference));

                        has_index_name = false;

                        from = expression;
                    }
                } else {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    has_index_name = false;

                    from = expression;
                }

                if(!expect_basic_token(context, TokenType::DoubleDot)) {
                    return { false };
                }

                expect(to, parse_expression(context, OperatorPrecedence::None));

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(*context, token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        append(&statements, statement);
                    }
                }

                ForLoop *for_loop;
                if(has_index_name) {
                    for_loop = new ForLoop(
                        span_range(first_range, last_range),
                        index_name,
                        from,
                        to,
                        to_array(statements)
                    );
                } else {
                    for_loop = new ForLoop(
                        span_range(first_range, last_range),
                        from,
                        to,
                        to_array(statements)
                    );
                }

                return {
                    true,
                    for_loop
                };
            } else if(strcmp(token.identifier, "return") == 0) {
                expect(token, peek_token(*context));

                Expression *value;
                FileRange last_range;
                if(token.type == TokenType::Semicolon) {
                    consume_token(context);

                    last_range = token_range(*context, token);

                    value = nullptr;
                } else {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    expect(range, expect_basic_token_with_range(context, TokenType::Semicolon));

                    last_range = range;

                    value = expression;
                }

                auto return_statement = new ReturnStatement {
                    span_range(first_range, last_range),
                    value
                };

                return {
                    true,
                    return_statement
                };
            } else if(strcmp(token.identifier, "break") == 0) {
                expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                auto break_statement = new BreakStatement {
                    span_range(first_range, last_range)
                };

                return {
                    true,
                    break_statement
                };
            } else if(strcmp(token.identifier, "using") == 0) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                auto using_statement = new UsingStatement {
                    span_range(first_range, last_range),
                    expression
                };

                return {
                    true,
                    using_statement
                };
            } else {
                auto identifier = identifier_from_token(*context, token);

                expect(token, peek_token(*context));

                if(token.type == TokenType::Colon) {
                    consume_token(context);

                    expect(token, peek_token(*context));

                    switch(token.type) {
                        case TokenType::Colon: {
                            consume_token(context);

                            expect(token, peek_token(*context));

                            switch(token.type) {
                                case TokenType::OpenRoundBracket: {
                                    consume_token(context);

                                    auto parameters_first_range = token_range(*context, token);

                                    expect(token, peek_token(*context));

                                    if(token.type == TokenType::Dollar) {
                                        consume_token(context);

                                        expect(name, expect_identifier(context));

                                        List<FunctionParameter> parameters{};

                                        expect(parameter, parse_function_parameter_second_half(context, name, true));

                                        append(&parameters, parameter);

                                        expect(token, peek_token(*context));

                                        FileRange last_range;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                consume_token(context);

                                                while(true) {
                                                    expect(pre_token, peek_token(*context));

                                                    bool is_constant;
                                                    if(pre_token.type == TokenType::Dollar) {
                                                        consume_token(context);

                                                        is_constant = true;
                                                    } else {
                                                        is_constant = false;
                                                    }

                                                    expect(identifier, expect_identifier(context));

                                                    expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                    append(&parameters, parameter);

                                                    expect(token, peek_token(*context));

                                                    auto done = false;
                                                    switch(token.type) {
                                                        case TokenType::Comma: {
                                                            consume_token(context);
                                                        } break;

                                                        case TokenType::CloseRoundBracket: {
                                                            consume_token(context);

                                                            done = true;
                                                        } break;

                                                        default: {
                                                            error(*context, "Expected ',' or ')', got '%s'", get_token_text(token));

                                                            return { false };
                                                        } break;
                                                    }

                                                    if(done) {
                                                        last_range = token_range(*context, token);

                                                        break;
                                                    }
                                                }
                                            } break;

                                            case TokenType::CloseRoundBracket: {
                                                consume_token(context);

                                                last_range = token_range(*context, token);
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or ')', got '%s'", get_token_text(token));

                                                return { false };
                                            } break;
                                        }

                                        expect(statement, continue_parsing_function_declaration_or_function_type_constant(
                                            context,
                                            identifier,
                                            to_array(parameters),
                                            span_range(parameters_first_range, last_range)
                                        ));

                                        return {
                                            true,
                                            statement
                                        };
                                    } else if(token.type == TokenType::CloseRoundBracket) {
                                        consume_token(context);

                                        expect(statement, continue_parsing_function_declaration_or_function_type_constant(
                                            context,
                                            identifier,
                                            {},
                                            span_range(parameters_first_range, token_range(*context, token)))
                                        );

                                        return {
                                            true,
                                            statement
                                        };
                                    } else if(token.type == TokenType::Identifier) {
                                        consume_token(context);

                                        auto first_identifier = identifier_from_token(*context, token);

                                        expect(token, peek_token(*context));

                                        if(token.type == TokenType::Colon) {
                                            List<FunctionParameter> parameters{};

                                            expect(parameter, parse_function_parameter_second_half(context, first_identifier, false));

                                            append(&parameters, parameter);

                                            expect(token, peek_token(*context));

                                            FileRange last_range;
                                            switch(token.type) {
                                                case TokenType::Comma: {
                                                    consume_token(context);

                                                    while(true) {
                                                        expect(pre_token, peek_token(*context));

                                                        bool is_constant;
                                                        if(pre_token.type == TokenType::Dollar) {
                                                            consume_token(context);

                                                            is_constant = true;
                                                        } else {
                                                            is_constant = false;
                                                        }

                                                        expect(identifier, expect_identifier(context));

                                                        expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                        append(&parameters, parameter);

                                                        expect(token, peek_token(*context));

                                                        auto done = false;
                                                        switch(token.type) {
                                                            case TokenType::Comma: {
                                                                consume_token(context);
                                                            } break;

                                                            case TokenType::CloseRoundBracket: {
                                                                consume_token(context);

                                                                done = true;
                                                            } break;

                                                            default: {
                                                                error(*context, "Expected ',' or ')', got '%s'", get_token_text(token));

                                                                return { false };
                                                            } break;
                                                        }

                                                        if(done) {
                                                            last_range = token_range(*context, token);

                                                            break;
                                                        }
                                                    }
                                                } break;

                                                case TokenType::CloseRoundBracket: {
                                                    consume_token(context);

                                                    last_range = token_range(*context, token);
                                                } break;

                                                default: {
                                                    error(*context, "Expected ',' or ')', got '%s'", get_token_text(token));

                                                    return { false };
                                                } break;
                                            }

                                            expect(statement, continue_parsing_function_declaration_or_function_type_constant(
                                                context,
                                                identifier,
                                                to_array(parameters),
                                                span_range(parameters_first_range, last_range)
                                            ));

                                            return {
                                                true,
                                                statement
                                            };
                                        } else {
                                            auto expression = named_reference_from_identifier(first_identifier);

                                            expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                            if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                                return { false };
                                            }

                                            expect(outer_right_expression, parse_expression_continuation(context, OperatorPrecedence::None, right_expression));

                                            expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                                            auto constant_definition = new ConstantDefinition {
                                                span_range(first_range, last_range),
                                                identifier,
                                                outer_right_expression
                                            };

                                            return {
                                                true,
                                                constant_definition
                                            };
                                        }
                                    } else {
                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                            return { false };
                                        }

                                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                        expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                                        auto constant_definition = new ConstantDefinition {
                                            span_range(first_range, last_range),
                                            identifier,
                                            right_expression
                                        };

                                        return {
                                            true,
                                            constant_definition
                                        };
                                    }
                                } break;

                                case TokenType::Identifier: {
                                    consume_token(context);

                                    if(strcmp(token.identifier, "struct") == 0) {
                                        expect(maybe_union_token, peek_token(*context));

                                        auto is_union = false;
                                        if(maybe_union_token.type == TokenType::Identifier && strcmp(maybe_union_token.identifier, "union") == 0) {
                                            consume_token(context);

                                            is_union = true;
                                        }

                                        expect(maybe_parameter_token, peek_token(*context));

                                        List<StructDefinition::Parameter> parameters{};

                                        if(maybe_parameter_token.type == TokenType::OpenRoundBracket) {
                                            consume_token(context);

                                            expect(token, peek_token(*context));

                                            if(token.type == TokenType::CloseRoundBracket) {
                                                consume_token(context);
                                            } else {
                                                while(true) {
                                                    expect(name, expect_identifier(context));

                                                    if(!expect_basic_token(context, TokenType::Colon)) {
                                                        return { false };
                                                    }

                                                    expect(type, parse_expression(context, OperatorPrecedence::None));

                                                    append(&parameters, {
                                                        name,
                                                        type
                                                    });

                                                    expect(token, peek_token(*context));

                                                    auto done = false;
                                                    switch(token.type) {
                                                        case TokenType::Comma: {
                                                            consume_token(context);
                                                        } break;

                                                        case TokenType::CloseRoundBracket: {
                                                            consume_token(context);

                                                            done = true;
                                                        } break;

                                                        default: {
                                                            error(*context, "Expected ',' or ')', got '%s'", get_token_text(token));
                                                        } break;
                                                    }

                                                    if(done) {
                                                        break;
                                                    }
                                                }
                                            }
                                        }

                                        if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                                            return { false };
                                        }

                                        List<StructDefinition::Member> members{};

                                        expect(token, peek_token(*context));

                                        FileRange last_range;
                                        if(token.type == TokenType::CloseCurlyBracket) {
                                            consume_token(context);

                                            last_range = token_range(*context, token);
                                        } else {
                                            while(true) {
                                                expect(identifier, expect_identifier(context));

                                                if(!expect_basic_token(context, TokenType::Colon)) {
                                                    return { false };
                                                }

                                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                                append(&members, {
                                                    identifier,
                                                    expression
                                                });

                                                expect(token, peek_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        consume_token(context);
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        consume_token(context);

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error(*context, "Expected ',' or '}', got '%s'", get_token_text(token));

                                                        return { false };
                                                    } break;
                                                }

                                                if(done) {
                                                    last_range = token_range(*context, token);

                                                    break;
                                                }
                                            }
                                        }

                                        auto struct_definition = new StructDefinition {
                                            span_range(first_range, token_range(*context, token)),
                                            identifier,
                                            is_union,
                                            to_array(parameters),
                                            to_array(members)
                                        };

                                        return {
                                            true,
                                            struct_definition
                                        };
                                    } else {
                                        auto sub_identifier = identifier_from_token(*context, token);

                                        auto expression = named_reference_from_identifier(sub_identifier);

                                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                        expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                                        auto constant_definition = new ConstantDefinition {
                                            span_range(first_range, last_range),
                                            identifier,
                                            right_expression
                                        };

                                        return {
                                            true,
                                            constant_definition
                                        };
                                    }
                                } break;

                                default: {
                                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                                    expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                                    auto constant_definition = new ConstantDefinition {
                                        span_range(first_range, last_range),
                                        identifier,
                                        expression
                                    };

                                    return {
                                        true,
                                        constant_definition
                                    };
                                } break;
                            }
                        } break;

                        default: {
                            Expression *type = nullptr;
                            if(token.type != TokenType::Equals) {
                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                type = expression;
                            }

                            expect(pre_token, peek_token(*context));

                            Expression *initializer = nullptr;
                            if(pre_token.type == TokenType::Equals || !type) {
                                if(!expect_basic_token(context, TokenType::Equals)) {
                                    return { false };
                                }

                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                initializer = expression;
                            }

                            expect(token, peek_token(*context));

                            auto is_external = false;
                            auto is_no_mangle = false;
                            switch(token.type) {
                                case TokenType::Semicolon: {
                                    consume_token(context);
                                } break;

                                case TokenType::Identifier: {
                                    if(strcmp(token.identifier, "extern") == 0) {
                                        consume_token(context);

                                        is_external = true;
                                    } else if(strcmp(token.identifier, "no_mangle") == 0) {
                                        consume_token(context);

                                        is_no_mangle = true;
                                    } else {
                                        error(*context, "Expected ;', 'extern' or 'no_mangle', got '%s'", token.identifier);

                                        return { false };
                                    }
                                } break;

                                default: {
                                    error(*context, "Expected ;', 'extern' or 'no_mangle', got '%s'", token.identifier);

                                    return { false };
                                } break;
                            }

                            auto variable_declaration = new VariableDeclaration {
                                span_range(first_range, token_range(*context, token)),
                                identifier,
                                type,
                                initializer,
                                is_external,
                                is_no_mangle
                            };

                            return {
                                true,
                                variable_declaration
                            };
                        } break;
                    }
                } else {
                    auto expression = named_reference_from_identifier(identifier);

                    expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                    expect(token, peek_token(*context));

                    switch(token.type) {
                        case TokenType::Semicolon: {
                            consume_token(context);

                            auto expression_statement = new ExpressionStatement {
                                span_range(first_range, token_range(*context, token)),
                                right_expression
                            };

                            return {
                                true,
                                expression_statement
                            };
                        } break;

                        default: {
                            bool is_binary_operation_assignment;
                            BinaryOperation::Operator binary_operator;
                            switch(token.type) {
                                case TokenType::Equals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = false;
                                } break;

                                case TokenType::PlusEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Addition;
                                } break;

                                case TokenType::DashEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Subtraction;
                                } break;

                                case TokenType::AsteriskEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Multiplication;
                                } break;

                                case TokenType::ForwardSlashEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Division;
                                } break;

                                case TokenType::PercentEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Modulo;
                                } break;

                                default: {
                                    error(*context, "Expected '=', '+=', '-=', '*=', '/=', '%=' or ';', got '%s'", get_token_text(token));

                                    return { false };
                                } break;
                            }

                            expect(expression, parse_expression(context, OperatorPrecedence::None));

                            expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                            if(is_binary_operation_assignment) {
                                auto assignment = new BinaryOperationAssignment {
                                    span_range(first_range, last_range),
                                    right_expression,
                                    binary_operator,
                                    expression
                                };

                                return {
                                    true,
                                    assignment
                                };
                            } else {
                                auto assignment = new Assignment {
                                    span_range(first_range, last_range),
                                    right_expression,
                                    expression
                                };

                                return {
                                    true,
                                    assignment
                                };
                            }
                        } break;
                    }
                }
            }
        } break;

        default: {
            expect(expression, parse_expression(context, OperatorPrecedence::None));

            expect(token, peek_token(*context));

            switch(token.type) {
                case TokenType::Semicolon: {
                    consume_token(context);

                    auto expression_statement = new ExpressionStatement {
                        span_range(first_range, token_range(*context, token)),
                        expression
                    };

                    return {
                        true,
                        expression_statement
                    };
                } break;

                case TokenType::Equals: {
                    consume_token(context);

                    expect(value_expression, parse_expression(context, OperatorPrecedence::None));

                    expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                    auto assignment = new Assignment {
                        span_range(first_range, last_range),
                        expression,
                        value_expression
                    };

                    return {
                        true,
                        assignment
                    };
                } break;

                default: {
                    error(*context, "Expected '=' or ';', got '%s'", get_token_text(token));

                    return { false };
                } break;
            }
        } break;
    }
}

profiled_function(Result<Array<Statement*>>, parse_tokens, (const char *path, Array<Token> tokens), (path, tokens)) {
    Context context {
        path,
        tokens
    };

    List<Statement*> statements{};

    while(context.next_token_index < tokens.count) {
        expect(statement, parse_statement(&context));

        append(&statements, statement);
    }

    return {
        true,
        to_array(statements)
    };
}