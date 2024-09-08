#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "profiler.h"
#include "path.h"
#include "list.h"
#include "util.h"

struct Context {
    String path;

    Array<Token> tokens;

    size_t next_token_index;
};

static FileRange token_range(Token token) {
    return {
        token.line,
        token.first_column,
        token.line,
        token.last_column
    };
}

static void error(Context context, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(context.path, token_range(context.tokens[context.next_token_index]), format, arguments);

    va_end(arguments);
}

static FileRange span_range(FileRange first, FileRange last) {
    return {
        first.first_line,
        first.first_column,
        last.last_column,
        last.last_line
    };
}

inline Result<Token> peek_token(Context context) {
    if(context.next_token_index < context.tokens.length) {
        auto token = context.tokens[context.next_token_index];

        return ok(token);
    } else {
        fprintf(stderr, "Error: %.*s: Unexpected end of file\n", STRING_PRINTF_ARGUMENTS(context.path));

        return err();
    }
}

inline void consume_token(Context* context) {
    context->next_token_index += 1;
}

static Result<void> expect_basic_token(Context* context, TokenKind type) {
    expect(token, peek_token(*context));

    if(token.kind != type) {
        Token expected_token;
        expected_token.kind = type;

        error(*context, "Expected '%.*s', got '%.*s'", STRING_PRINTF_ARGUMENTS(expected_token.get_text()), STRING_PRINTF_ARGUMENTS(token.get_text()));

        return err();
    }

    consume_token(context);

    return ok();
}

static Result<FileRange> expect_basic_token_with_range(Context* context, TokenKind type) {
    expect(token, peek_token(*context));

    if(token.kind != type) {
        Token expected_token;
        expected_token.kind = type;

        error(*context, "Expected '%.*s', got '%.*s'", STRING_PRINTF_ARGUMENTS(expected_token.get_text()), STRING_PRINTF_ARGUMENTS(token.get_text()));

        return err();
    }

    consume_token(context);

    auto range = token_range(token);

    return ok(range);
}

static Result<String> expect_string(Context* context) {
    expect(token, peek_token(*context));

    if(token.kind != TokenKind::String) {
        error(*context, "Expected a string, got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

        return err();
    }

    consume_token(context);

    return ok(token.string);
}

inline Identifier identifier_from_token(Context context, Token token) {
    Identifier identifier {};
    identifier.text = token.identifier;
    identifier.range = token_range(token);

    return {
        token.identifier,
        token_range(token)
    };
}

static Result<Identifier> expect_identifier(Context* context) {
    expect(token, peek_token(*context));

    if(token.kind != TokenKind::Identifier) {
        error(*context, "Expected an identifier, got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

        return err();
    }

    consume_token(context);

    return ok(identifier_from_token(*context, token));
}

inline Expression* named_reference_from_identifier(Identifier identifier) {
    return new NamedReference(
        identifier.range,
        identifier
    );
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

static Result<FunctionParameter> parse_function_parameter_second_half(Context* context, Identifier name, bool is_constant);

static Result<Array<Tag>> parse_tags(Context* context);

static Result<Expression*> parse_expression_continuation(Context* context, OperatorPrecedence minimum_precedence, Expression* expression);

// Precedence sorting based on https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
static_profiled_function(
    Result<Expression*>,
    parse_expression,
    (
        Context* context,
        OperatorPrecedence minimum_precedence
    ),
    (
        context,
        minimum_precedence
    )
) {
    Expression* left_expression;

    expect(token, peek_token(*context));

    // Parse atomic & prefix-unary expressions (non-left-recursive)
    switch(token.kind) {
        case TokenKind::Identifier: {
            consume_token(context);

            auto identifier = identifier_from_token(*context, token);

            left_expression = named_reference_from_identifier(identifier);
        } break;

        case TokenKind::Integer: {
            consume_token(context);

            left_expression = new IntegerLiteral {
                token_range(token),
                token.integer
            };
        } break;

        case TokenKind::FloatingPoint: {
            consume_token(context);

            left_expression = new FloatLiteral {
                token_range(token),
                token.floating_point
            };
        } break;

        case TokenKind::Asterisk: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(token), expression->range),
                UnaryOperation::Operator::Pointer,
                expression
            };
        } break;

        case TokenKind::Hash: {
            consume_token(context);

            expect(identifier, expect_identifier(context));

            if(identifier.text == "bake"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

                if(expression->kind != ExpressionKind::FunctionCall) {
                    error(context->path, expression->range, "Expected a function call");

                    return err();
                }

                auto function_call = (FunctionCall*)expression;

                left_expression = new Bake {
                    span_range(token_range(token), function_call->range),
                    function_call
                };
            } else {
                error(context->path, identifier.range, "Expected 'bake', got '%.*s'", STRING_PRINTF_ARGUMENTS(identifier.text));

                return err();
            }
        } break;

        case TokenKind::Bang: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(token), expression->range),
                UnaryOperation::Operator::BooleanInvert,
                expression
            };
        } break;

        case TokenKind::Dash: {
            consume_token(context);

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new UnaryOperation {
                span_range(token_range(token), expression->range),
                UnaryOperation::Operator::Negation,
                expression
            };
        } break;

        case TokenKind::String: {
            consume_token(context);

            left_expression = new StringLiteral {
                token_range(token),
                token.string
            };
        } break;

        case TokenKind::OpenRoundBracket: {
            consume_token(context);

            auto first_range = token_range(token);

            expect(token, peek_token(*context));

            switch(token.kind) {
                case TokenKind::Dollar: {
                    consume_token(context);

                    List<FunctionParameter> parameters{};

                    expect(name, expect_identifier(context));

                    expect(parameter, parse_function_parameter_second_half(context, name, true));

                    parameters.append(parameter);

                    expect(token, peek_token(*context));

                    FileRange last_range;
                    switch(token.kind) {
                        case TokenKind::Comma: {
                            consume_token(context);

                            while(true) {
                                expect(pre_token, peek_token(*context));

                                bool is_constant;
                                if(pre_token.kind == TokenKind::Dollar) {
                                    consume_token(context);

                                    is_constant = true;
                                } else {
                                    is_constant = false;
                                }

                                expect(identifier, expect_identifier(context));

                                expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                parameters.append(parameter);

                                expect(token, peek_token(*context));

                                auto done = false;
                                switch(token.kind) {
                                    case TokenKind::Comma: {
                                        consume_token(context);
                                    } break;

                                    case TokenKind::CloseRoundBracket: {
                                        consume_token(context);

                                        done = true;
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                        return err();
                                    } break;
                                }

                                if(done) {
                                    last_range = token_range(token);

                                    break;
                                }
                            }
                        } break;

                        case TokenKind::CloseRoundBracket: {
                            consume_token(context);

                            last_range = token_range(token);
                        } break;

                        default: {
                            error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                            return err();
                        } break;
                    }

                    expect(post_token, peek_token(*context));

                    Expression* return_type;
                    if(post_token.kind == TokenKind::Arrow) {
                        consume_token(context);

                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        return_type = expression;
                        last_range = expression->range;
                    } else {
                        return_type = nullptr;
                    }

                    expect(tags, parse_tags(context));

                    left_expression = new FunctionType {
                        span_range(first_range, last_range),
                        parameters,
                        return_type,
                        tags
                    };
                } break;

                case TokenKind::CloseRoundBracket: {
                    consume_token(context);

                    auto last_range = token_range(token);

                    expect(token, peek_token(*context));

                    Expression* return_type;
                    if(token.kind == TokenKind::Arrow) {
                        consume_token(context);

                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        return_type = expression;
                        last_range = expression->range;
                    } else {
                        return_type = nullptr;
                    }

                    expect(tags, parse_tags(context));

                    left_expression = new FunctionType {
                        span_range(first_range, last_range),
                        {},
                        return_type,
                        tags
                    };
                } break;

                case TokenKind::Identifier: {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::Colon) {
                        List<FunctionParameter> parameters{};

                        expect(parameter, parse_function_parameter_second_half(context, identifier, false));

                        parameters.append(parameter);

                        expect(token, peek_token(*context));

                        FileRange last_range;
                        switch(token.kind) {
                            case TokenKind::Comma: {
                                consume_token(context);

                                while(true) {
                                    expect(pre_token, peek_token(*context));

                                    bool is_constant;
                                    if(pre_token.kind == TokenKind::Dollar) {
                                        consume_token(context);

                                        is_constant = true;
                                    } else {
                                        is_constant = false;
                                    }

                                    expect(identifier, expect_identifier(context));

                                    expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                    parameters.append(parameter);

                                    expect(token, peek_token(*context));

                                    auto done = false;
                                    switch(token.kind) {
                                        case TokenKind::Comma: {
                                            consume_token(context);
                                        } break;

                                        case TokenKind::CloseRoundBracket: {
                                            consume_token(context);

                                            done = true;
                                        } break;

                                        default: {
                                            error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                            return err();
                                        } break;
                                    }

                                    if(done) {
                                        last_range = token_range(token);

                                        break;
                                    }
                                }
                            } break;

                            case TokenKind::CloseRoundBracket: {
                                consume_token(context);

                                last_range = token_range(token);
                            } break;

                            default: {
                                error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                return err();
                            } break;
                        }

                        expect(post_token, peek_token(*context));

                        Expression* return_type;
                        if(post_token.kind == TokenKind::Arrow) {
                            consume_token(context);

                            expect(expression, parse_expression(context, OperatorPrecedence::None));

                            return_type = expression;
                            last_range = expression->range;
                        } else {
                            return_type = nullptr;
                        }

                        expect(tags, parse_tags(context));

                        left_expression = new FunctionType {
                            span_range(first_range, last_range),
                            parameters,
                            return_type,
                            tags
                        };
                    } else {
                        auto expression = named_reference_from_identifier(identifier);

                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                        expect_void(expect_basic_token(context, TokenKind::CloseRoundBracket));

                        left_expression = right_expression;
                    }
                } break;

                default: {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    expect_void(expect_basic_token(context, TokenKind::CloseRoundBracket));

                    left_expression = expression;
                } break;
            }
        } break;

        case TokenKind::OpenCurlyBracket: {
            consume_token(context);

            auto first_range = token_range(token);

            expect(token, peek_token(*context));

            switch(token.kind) {
                case TokenKind::CloseCurlyBracket: {
                    consume_token(context);

                    left_expression = new ArrayLiteral {
                        span_range(first_range, token_range(token)),
                        {}
                    };
                } break;

                case TokenKind::Identifier: {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    switch(token.kind) {
                        case TokenKind::Equals: {
                            consume_token(context);

                            expect(first_expression, parse_expression(context, OperatorPrecedence::None));

                            List<StructLiteral::Member> members{};

                            StructLiteral::Member first_member {};
                            first_member.name = identifier;
                            first_member.value = first_expression;

                            members.append(first_member);

                            expect(token, peek_token(*context));

                            FileRange last_range;
                            switch(token.kind) {
                                case TokenKind::Comma: {
                                    consume_token(context);

                                    while(true) {
                                        expect(identifier, expect_identifier(context));

                                        expect_void(expect_basic_token(context, TokenKind::Equals));

                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        StructLiteral::Member member {};
                                        member.name = identifier;
                                        member.value = expression;

                                        members.append(member);

                                        expect(token, peek_token(*context));

                                        auto done = false;
                                        switch(token.kind) {
                                            case TokenKind::Comma: {
                                                consume_token(context);
                                            } break;

                                            case TokenKind::CloseCurlyBracket: {
                                                consume_token(context);

                                                done = true;
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                return err();
                                            } break;
                                        }

                                        if(done) {
                                            last_range = token_range(token);

                                            break;
                                        }
                                    }
                                } break;

                                case TokenKind::CloseCurlyBracket: {
                                    consume_token(context);

                                    last_range = token_range(token);
                                } break;

                                default: {
                                    error(*context, "Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                    return err();
                                } break;
                            }

                            left_expression = new StructLiteral {
                                span_range(first_range, last_range),
                                members
                            };
                        } break;

                        case TokenKind::CloseCurlyBracket: {
                            consume_token(context);

                            auto first_element = named_reference_from_identifier(identifier);

                            left_expression = new ArrayLiteral {
                                span_range(first_range, token_range(token)),
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

                            elements.append(right_expression);

                            expect(token, peek_token(*context));

                            FileRange last_range;
                            switch(token.kind) {
                                case TokenKind::Comma: {
                                    consume_token(context);

                                    while(true) {
                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        elements.append(expression);

                                        expect(token, peek_token(*context));

                                        auto done = false;
                                        switch(token.kind) {
                                            case TokenKind::Comma: {
                                                consume_token(context);
                                            } break;

                                            case TokenKind::CloseCurlyBracket: {
                                                consume_token(context);

                                                done = true;
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                return err();
                                            } break;
                                        }

                                        if(done) {
                                            last_range = token_range(token);

                                            break;
                                        }
                                    }
                                } break;

                                case TokenKind::CloseCurlyBracket: {
                                    consume_token(context);

                                    last_range = token_range(token);
                                } break;

                                default: {
                                    error(*context, "Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                    return err();
                                }
                            }

                            left_expression = new ArrayLiteral {
                                span_range(first_range, last_range),
                                elements
                            };
                        } break;
                    }
                } break;

                default: {
                    expect(first_expression, parse_expression(context, OperatorPrecedence::None));

                    List<Expression*> elements{};

                    elements.append(first_expression);

                    expect(token, peek_token(*context));

                    FileRange last_range;
                    switch(token.kind) {
                        case TokenKind::Comma: {
                            consume_token(context);

                            while(true) {
                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                elements.append(expression);

                                expect(token, peek_token(*context));

                                auto done = false;
                                switch(token.kind) {
                                    case TokenKind::Comma: {
                                        consume_token(context);
                                    } break;

                                    case TokenKind::CloseCurlyBracket: {
                                        consume_token(context);

                                        done = true;
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                        return err();
                                    } break;
                                }

                                if(done) {
                                    last_range = token_range(token);

                                    break;
                                }
                            }
                        } break;

                        case TokenKind::CloseCurlyBracket: {
                            consume_token(context);

                            last_range = token_range(token);
                        } break;
                    }

                    left_expression = new ArrayLiteral {
                        span_range(first_range, last_range),
                        elements
                    };
                } break;
            }
        } break;

        case TokenKind::OpenSquareBracket: {
            consume_token(context);

            expect(token, peek_token(*context));

            Expression* index;
            FileRange last_range;
            if(token.kind == TokenKind::CloseSquareBracket) {
                consume_token(context);

                index = nullptr;
                last_range = token_range(token);
            } else {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect(range, expect_basic_token_with_range(context, TokenKind::CloseSquareBracket));

                index = expression;
                last_range = range;
            }

            expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

            left_expression = new ArrayType {
                span_range(token_range(token), expression->range),
                expression,
                index
            };
        } break;

        default: {
            error(*context, "Expected an expression. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

            return err();
        } break;
    }

    expect(final_expression, parse_expression_continuation(context, minimum_precedence, left_expression));

    return ok(final_expression);
}

static_profiled_function(Result<Expression*>, parse_expression_continuation, (
    Context* context,
    OperatorPrecedence minimum_precedence,
    Expression* expression
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
        switch(token.kind) {
            case TokenKind::Dot: {
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

            case TokenKind::Plus: {
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

            case TokenKind::Dash: {
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

            case TokenKind::Asterisk: {
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

            case TokenKind::ForwardSlash: {
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

            case TokenKind::Percent: {
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

            case TokenKind::Ampersand: {
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

            case TokenKind::DoubleAmpersand: {
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

            case TokenKind::Pipe: {
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

            case TokenKind::DoublePipe: {
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

            case TokenKind::DoubleEquals: {
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

            case TokenKind::BangEquals: {
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

            case TokenKind::LeftArrow: {
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

            case TokenKind::RightArrow: {
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

            case TokenKind::OpenRoundBracket: {
                if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                List<Expression*> parameters{};

                expect(token, peek_token(*context));

                FileRange last_range;
                if(token.kind == TokenKind::CloseRoundBracket) {
                    consume_token(context);

                    last_range = token_range(token);
                } else {
                    while(true) {
                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                        parameters.append(expression);

                        expect(token, peek_token(*context));

                        auto done = false;
                        switch(token.kind) {
                            case TokenKind::Comma: {
                                consume_token(context);
                            } break;

                            case TokenKind::CloseRoundBracket: {
                                consume_token(context);

                                done = true;
                            } break;

                            default: {
                                error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                return err();
                            } break;
                        }

                        if(done) {
                            last_range = token_range(token);

                            break;
                        }
                    }
                }

                current_expression = new FunctionCall {
                    span_range(current_expression->range, last_range),
                    current_expression,
                    parameters
                };
            } break;

            case TokenKind::OpenSquareBracket: {
                if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                    done = true;

                    break;
                }

                consume_token(context);

                auto first_range = token_range(token);

                expect(token, peek_token(*context));

                Expression* index;
                FileRange last_range;
                if(token.kind == TokenKind::CloseSquareBracket) {
                    consume_token(context);

                    index = nullptr;
                    last_range = token_range(token);
                } else {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    expect(range, expect_basic_token_with_range(context, TokenKind::CloseSquareBracket));

                    index = expression;
                    last_range = range;
                }

                current_expression = new IndexReference {
                    span_range(current_expression->range, last_range),
                    current_expression,
                    index
                };
            } break;

            case TokenKind::Identifier: {
                if(token.identifier == "as"_S) {
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

    return ok(current_expression);
}

static Result<FunctionParameter> parse_function_parameter_second_half(Context* context, Identifier name, bool is_constant) {
    expect_void(expect_basic_token(context, TokenKind::Colon));

    FunctionParameter parameter;
    parameter.name = name;
    parameter.is_constant = is_constant;

    expect(token, peek_token(*context));

    switch(token.kind) {
        case TokenKind::Dollar: {
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

    return ok(parameter);
}

static Result<Array<Tag>> parse_tags(Context* context) {
    expect(token, peek_token(*context));

    if(token.kind != TokenKind::Hash) {
        return ok(Array<Tag> {});
    }

    auto first_range = token_range(token);

    consume_token(context);

    List<Tag> tags {};
    while(true) {
        expect(name, expect_identifier(context));

        auto last_range = name.range;

        expect(token, peek_token(*context));

        List<Expression*> parameters {};
        if(token.kind == TokenKind::OpenRoundBracket) {
            consume_token(context);

            expect(token, peek_token(*context));

            if(token.kind == TokenKind::CloseRoundBracket) {
                consume_token(context);

                last_range = token_range(token);
            } else {
                while(true) {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    parameters.append(expression);

                    expect(token, peek_token(*context));

                    auto done = false;
                    switch(token.kind) {
                        case TokenKind::Comma: {
                            consume_token(context);
                        } break;

                        case TokenKind::CloseRoundBracket: {
                            consume_token(context);

                            done = true;
                        } break;

                        default: {
                            error(*context, "Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                            return err();
                        } break;
                    }

                    if(done) {
                        last_range = token_range(token);

                        break;
                    }
                }
            }
        }

        Tag tag {};
        tag.name = name;
        tag.parameters = parameters;
        tag.range = span_range(first_range, last_range);

        tags.append(tag);

        expect(post_token, peek_token(*context));

        if(post_token.kind == TokenKind::Hash) {
            consume_token(context);

            first_range = token_range(post_token);
        } else {
            break;
        }
    }

    return ok((Array<Tag>)tags);
}

static Result<Statement*> parse_statement(Context* context);

static Result<Statement*> continue_parsing_function_declaration(
    Context* context,
    Identifier name,
    Array<FunctionParameter> parameters,
    FileRange parameters_range
) {
    auto last_range = parameters_range;

    expect(pre_token, peek_token(*context));

    Expression* return_type;
    switch(pre_token.kind) {
        case TokenKind::Arrow: {
            consume_token(context);

            last_range = token_range(pre_token);

            expect(expression, parse_expression(context, OperatorPrecedence::None));

            return_type = expression;
        } break;

        default: {
            return_type = nullptr;
        } break;
    }

    expect(tags, parse_tags(context));

    expect(token, peek_token(*context));

    switch(token.kind) {
        case TokenKind::OpenCurlyBracket: {
            consume_token(context);

            List<Statement*> statements{};

            while(true) {
                expect(token, peek_token(*context));

                if(token.kind == TokenKind::CloseCurlyBracket) {
                    consume_token(context);

                    last_range = token_range(token);

                    break;
                } else {
                    expect(statement, parse_statement(context));

                    statements.append(statement);
                }
            }

            return ok((Statement*)new FunctionDeclaration(
                span_range(name.range, last_range),
                name,
                parameters,
                return_type,
                tags,
                statements
            ));
        } break;

        case TokenKind::Semicolon: {
            consume_token(context);

            return ok((Statement*)new FunctionDeclaration(
                span_range(name.range, last_range),
                name,
                parameters,
                return_type,
                tags
            ));
        } break;

        default: {
            error(*context, "Expected '->', '{', ';', or '#', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

            return err();
        } break;
    }
}

static_profiled_function(Result<Statement*>, parse_statement, (Context* context), (context)) {
    expect(token, peek_token(*context));

    auto first_range = token_range(token);

    switch(token.kind) {
        case TokenKind::Hash: {
            consume_token(context);

            expect(token, peek_token(*context));

            if(token.kind != TokenKind::Identifier) {
                error(*context, "Expected 'import', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }

            consume_token(context);

            if(token.identifier == "import"_S) {
                expect(string, expect_string(context));

                expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                auto source_file_directory = path_get_directory_component(context->path);

                StringBuffer import_file_path {};

                import_file_path.append(source_file_directory);
                import_file_path.append(string);

                expect(import_file_path_absolute, path_relative_to_absolute(import_file_path));

                auto name = path_get_file_component(string);

                return ok((Statement*)new Import(
                    span_range(first_range, last_range),
                    string,
                    import_file_path_absolute,
                    name
                ));
            } else if(token.identifier == "if"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket));

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        statements.append(statement);
                    }
                }

                return ok((Statement*)new StaticIf(
                    span_range(first_range, last_range),
                    expression,
                    statements
                ));
            } else if(token.identifier == "bake"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::PrefixUnary));

                if(expression->kind != ExpressionKind::FunctionCall) {
                    error(context->path, expression->range, "Expected a function call");

                    return err();
                }

                expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                auto function_call = (FunctionCall*)expression;

                auto bake = new Bake {
                    span_range(first_range, function_call->range),
                    function_call
                };

                return ok((Statement*)new ExpressionStatement(
                    span_range(first_range, last_range),
                    bake
                ));
            } else {
                error(*context, "Expected 'import', 'if' or 'bake', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }
        } break;

        case TokenKind::Identifier: {
            consume_token(context);

            if(token.identifier == "if"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket));

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        statements.append(statement);
                    }
                }

                List<IfStatement::ElseIf> else_ifs{};

                auto has_else = false;
                List<Statement*> else_statements{};
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::Identifier && token.identifier == "else"_S) {
                        consume_token(context);

                        expect(token, peek_token(*context));

                        switch(token.kind) {
                            case TokenKind::OpenCurlyBracket: {
                                consume_token(context);

                                while(true) {
                                    expect(token, peek_token(*context));

                                    if(token.kind == TokenKind::CloseCurlyBracket) {
                                        consume_token(context);

                                        last_range = token_range(token);

                                        break;
                                    } else {
                                        expect(statement, parse_statement(context));

                                        statements.append(statement);
                                    }
                                }
                            } break;

                            case TokenKind::Identifier: {
                                if(token.identifier != "if"_S) {
                                    error(*context, "Expected '{' or 'if', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                    return err();
                                }

                                consume_token(context);

                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket));
                                
                                List<Statement*> statements{};

                                while(true) {
                                    expect(token, peek_token(*context));

                                    if(token.kind == TokenKind::CloseCurlyBracket) {
                                        consume_token(context);

                                        last_range = token_range(token);

                                        break;
                                    } else {
                                        expect(statement, parse_statement(context));

                                        statements.append(statement);
                                    }
                                }

                                IfStatement::ElseIf else_if {};
                                else_if.condition = expression;
                                else_if.statements = statements;

                                else_ifs.append(else_if);
                            } break;

                            default: {
                                error(*context, "Expected '{' or 'if', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                return err();
                            } break;
                        }

                        if(has_else) {
                            break;
                        }
                    } else {
                        break;
                    }
                }

                return ok((Statement*)new IfStatement(
                    span_range(first_range, last_range),
                    expression,
                    statements,
                    else_ifs,
                    else_statements
                ));
            } else if(token.identifier == "while"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket));

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        statements.append(statement);
                    }
                }

                return ok((Statement*)new WhileLoop(
                    span_range(first_range, last_range),
                    expression,
                    statements
                ));
            } else if(token.identifier == "for"_S) {
                expect(token, peek_token(*context));

                bool has_index_name;
                Identifier index_name;
                Expression* from;
                if(token.kind == TokenKind::Identifier) {
                    consume_token(context);

                    auto identifier = identifier_from_token(*context, token);

                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::Colon) {
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

                expect_void(expect_basic_token(context, TokenKind::DoubleDot));

                expect(to, parse_expression(context, OperatorPrecedence::None));

                { expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket)); }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, peek_token(*context));

                    if(token.kind == TokenKind::CloseCurlyBracket) {
                        consume_token(context);

                        last_range = token_range(token);

                        break;
                    } else {
                        expect(statement, parse_statement(context));

                        statements.append(statement);
                    }
                }

                if(has_index_name) {
                    return ok((Statement*)new ForLoop(
                        span_range(first_range, last_range),
                        index_name,
                        from,
                        to,
                        statements
                    ));
                } else {
                    return ok((Statement*)new ForLoop(
                        span_range(first_range, last_range),
                        from,
                        to,
                        statements
                    ));
                }
            } else if(token.identifier == "return"_S) {
                expect(token, peek_token(*context));

                Expression* value;
                FileRange last_range;
                if(token.kind == TokenKind::Semicolon) {
                    consume_token(context);

                    last_range = token_range(token);

                    value = nullptr;
                } else {
                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                    expect(range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                    last_range = range;

                    value = expression;
                }

                return ok((Statement*)new ReturnStatement(
                    span_range(first_range, last_range),
                    value
                ));
            } else if(token.identifier == "break"_S) {
                expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                return ok((Statement*)new BreakStatement(
                    span_range(first_range, last_range)
                ));
            } else if(token.identifier == "using"_S) {
                expect(expression, parse_expression(context, OperatorPrecedence::None));

                expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                return ok((Statement*)new UsingStatement(
                    span_range(first_range, last_range),
                    expression
                ));
            } else {
                auto identifier = identifier_from_token(*context, token);

                expect(token, peek_token(*context));

                if(token.kind == TokenKind::Colon) {
                    consume_token(context);

                    expect(token, peek_token(*context));

                    switch(token.kind) {
                        case TokenKind::Colon: {
                            consume_token(context);

                            expect(token, peek_token(*context));

                            switch(token.kind) {
                                case TokenKind::OpenRoundBracket: {
                                    consume_token(context);

                                    auto parameters_first_range = token_range(token);

                                    expect(token, peek_token(*context));

                                    if(token.kind == TokenKind::Dollar) {
                                        consume_token(context);

                                        expect(name, expect_identifier(context));

                                        List<FunctionParameter> parameters{};

                                        expect(parameter, parse_function_parameter_second_half(context, name, true));

                                        parameters.append(parameter);

                                        expect(token, peek_token(*context));

                                        FileRange last_range;
                                        switch(token.kind) {
                                            case TokenKind::Comma: {
                                                consume_token(context);

                                                while(true) {
                                                    expect(pre_token, peek_token(*context));

                                                    bool is_constant;
                                                    if(pre_token.kind == TokenKind::Dollar) {
                                                        consume_token(context);

                                                        is_constant = true;
                                                    } else {
                                                        is_constant = false;
                                                    }

                                                    expect(identifier, expect_identifier(context));

                                                    expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                    parameters.append(parameter);

                                                    expect(token, peek_token(*context));

                                                    auto done = false;
                                                    switch(token.kind) {
                                                        case TokenKind::Comma: {
                                                            consume_token(context);
                                                        } break;

                                                        case TokenKind::CloseRoundBracket: {
                                                            consume_token(context);

                                                            done = true;
                                                        } break;

                                                        default: {
                                                            error(*context, "Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                            return err();
                                                        } break;
                                                    }

                                                    if(done) {
                                                        last_range = token_range(token);

                                                        break;
                                                    }
                                                }
                                            } break;

                                            case TokenKind::CloseRoundBracket: {
                                                consume_token(context);

                                                last_range = token_range(token);
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                return err();
                                            } break;
                                        }

                                        return continue_parsing_function_declaration(
                                            context,
                                            identifier,
                                            parameters,
                                            span_range(parameters_first_range, last_range)
                                        );
                                    } else if(token.kind == TokenKind::CloseRoundBracket) {
                                        consume_token(context);

                                        return continue_parsing_function_declaration(
                                            context,
                                            identifier,
                                            {},
                                            span_range(parameters_first_range, token_range(token))
                                        );
                                    } else if(token.kind == TokenKind::Identifier) {
                                        consume_token(context);

                                        auto first_identifier = identifier_from_token(*context, token);

                                        expect(token, peek_token(*context));

                                        if(token.kind == TokenKind::Colon) {
                                            List<FunctionParameter> parameters{};

                                            expect(parameter, parse_function_parameter_second_half(context, first_identifier, false));

                                            parameters.append(parameter);

                                            expect(token, peek_token(*context));

                                            FileRange last_range;
                                            switch(token.kind) {
                                                case TokenKind::Comma: {
                                                    consume_token(context);

                                                    while(true) {
                                                        expect(pre_token, peek_token(*context));

                                                        bool is_constant;
                                                        if(pre_token.kind == TokenKind::Dollar) {
                                                            consume_token(context);

                                                            is_constant = true;
                                                        } else {
                                                            is_constant = false;
                                                        }

                                                        expect(identifier, expect_identifier(context));

                                                        expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                        parameters.append(parameter);

                                                        expect(token, peek_token(*context));

                                                        auto done = false;
                                                        switch(token.kind) {
                                                            case TokenKind::Comma: {
                                                                consume_token(context);
                                                            } break;

                                                            case TokenKind::CloseRoundBracket: {
                                                                consume_token(context);

                                                                done = true;
                                                            } break;

                                                            default: {
                                                                error(*context, "Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                                return err();
                                                            } break;
                                                        }

                                                        if(done) {
                                                            last_range = token_range(token);

                                                            break;
                                                        }
                                                    }
                                                } break;

                                                case TokenKind::CloseRoundBracket: {
                                                    consume_token(context);

                                                    last_range = token_range(token);
                                                } break;

                                                default: {
                                                    error(*context, "Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                    return err();
                                                } break;
                                            }

                                            return continue_parsing_function_declaration(
                                                context,
                                                identifier,
                                                parameters,
                                                span_range(parameters_first_range, last_range)
                                            );
                                        } else {
                                            auto expression = named_reference_from_identifier(first_identifier);

                                            expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                            expect_void(expect_basic_token(context, TokenKind::CloseRoundBracket));

                                            expect(outer_right_expression, parse_expression_continuation(context, OperatorPrecedence::None, right_expression));

                                            expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                                            return ok((Statement*)new ConstantDefinition(
                                                span_range(first_range, last_range),
                                                identifier,
                                                outer_right_expression
                                            ));
                                        }
                                    } else {
                                        expect(expression, parse_expression(context, OperatorPrecedence::None));

                                        expect_void(expect_basic_token(context, TokenKind::CloseRoundBracket));

                                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                        expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                                        return ok((Statement*)new ConstantDefinition(
                                            span_range(first_range, last_range),
                                            identifier,
                                            right_expression
                                        ));
                                    }
                                } break;

                                case TokenKind::Identifier: {
                                    consume_token(context);

                                    if(token.identifier == "struct"_S) {
                                        expect(maybe_union_token, peek_token(*context));

                                        auto is_union = false;
                                        if(maybe_union_token.kind == TokenKind::Identifier && maybe_union_token.identifier == "union"_S) {
                                            consume_token(context);

                                            is_union = true;
                                        }

                                        expect(maybe_parameter_token, peek_token(*context));

                                        List<StructDefinition::Parameter> parameters{};

                                        if(maybe_parameter_token.kind == TokenKind::OpenRoundBracket) {
                                            consume_token(context);

                                            expect(token, peek_token(*context));

                                            if(token.kind == TokenKind::CloseRoundBracket) {
                                                consume_token(context);
                                            } else {
                                                while(true) {
                                                    expect(name, expect_identifier(context));

                                                    expect_void(expect_basic_token(context, TokenKind::Colon));

                                                    expect(type, parse_expression(context, OperatorPrecedence::None));

                                                    StructDefinition::Parameter parameter {};
                                                    parameter.name = name;
                                                    parameter.type = type;

                                                    parameters.append(parameter);

                                                    expect(token, peek_token(*context));

                                                    auto done = false;
                                                    switch(token.kind) {
                                                        case TokenKind::Comma: {
                                                            consume_token(context);
                                                        } break;

                                                        case TokenKind::CloseRoundBracket: {
                                                            consume_token(context);

                                                            done = true;
                                                        } break;

                                                        default: {
                                                            error(*context, "Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));
                                                        } break;
                                                    }

                                                    if(done) {
                                                        break;
                                                    }
                                                }
                                            }
                                        }

                                        expect_void(expect_basic_token(context, TokenKind::OpenCurlyBracket));

                                        List<StructDefinition::Member> members{};

                                        expect(token, peek_token(*context));

                                        FileRange last_range;
                                        if(token.kind == TokenKind::CloseCurlyBracket) {
                                            consume_token(context);

                                            last_range = token_range(token);
                                        } else {
                                            while(true) {
                                                expect(identifier, expect_identifier(context));

                                                expect_void(expect_basic_token(context, TokenKind::Colon));

                                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                                StructDefinition::Member member {};
                                                member.name = identifier;
                                                member.type = expression;

                                                members.append(member);

                                                expect(token, peek_token(*context));

                                                auto done = false;
                                                switch(token.kind) {
                                                    case TokenKind::Comma: {
                                                        consume_token(context);
                                                    } break;

                                                    case TokenKind::CloseCurlyBracket: {
                                                        consume_token(context);

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error(*context, "Expected ',' or '}', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                        return err();
                                                    } break;
                                                }

                                                if(done) {
                                                    last_range = token_range(token);

                                                    break;
                                                }
                                            }
                                        }

                                        return ok((Statement*)new StructDefinition(
                                            span_range(first_range, token_range(token)),
                                            identifier,
                                            is_union,
                                            parameters,
                                            members
                                        ));
                                    } else {
                                        auto sub_identifier = identifier_from_token(*context, token);

                                        auto expression = named_reference_from_identifier(sub_identifier);

                                        expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                                        expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                                        return ok((Statement*)new ConstantDefinition(
                                            span_range(first_range, last_range),
                                            identifier,
                                            right_expression
                                        ));
                                    }
                                } break;

                                default: {
                                    expect(expression, parse_expression(context, OperatorPrecedence::None));

                                    expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                                    return ok((Statement*)new ConstantDefinition(
                                        span_range(first_range, last_range),
                                        identifier,
                                        expression
                                    ));
                                } break;
                            }
                        } break;

                        default: {
                            Expression* type = nullptr;
                            if(token.kind != TokenKind::Equals) {
                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                type = expression;
                            }

                            expect(pre_token, peek_token(*context));

                            Expression* initializer = nullptr;
                            if(pre_token.kind == TokenKind::Equals || !type) {
                                expect_void(expect_basic_token(context, TokenKind::Equals));

                                expect(expression, parse_expression(context, OperatorPrecedence::None));

                                initializer = expression;
                            }

                            expect(tags, parse_tags(context));

                            expect_void(expect_basic_token(context, TokenKind::Semicolon));

                            return ok((Statement*)new VariableDeclaration(
                                span_range(first_range, token_range(token)),
                                identifier,
                                type,
                                initializer,
                                tags
                            ));
                        } break;
                    }
                } else {
                    auto expression = named_reference_from_identifier(identifier);

                    expect(right_expression, parse_expression_continuation(context, OperatorPrecedence::None, expression));

                    expect(token, peek_token(*context));

                    switch(token.kind) {
                        case TokenKind::Semicolon: {
                            consume_token(context);

                            return ok((Statement*)new ExpressionStatement(
                                span_range(first_range, token_range(token)),
                                right_expression
                            ));
                        } break;

                        default: {
                            bool is_binary_operation_assignment;
                            BinaryOperation::Operator binary_operator;
                            switch(token.kind) {
                                case TokenKind::Equals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = false;
                                } break;

                                case TokenKind::PlusEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Addition;
                                } break;

                                case TokenKind::DashEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Subtraction;
                                } break;

                                case TokenKind::AsteriskEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Multiplication;
                                } break;

                                case TokenKind::ForwardSlashEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Division;
                                } break;

                                case TokenKind::PercentEquals: {
                                    consume_token(context);

                                    is_binary_operation_assignment = true;
                                    binary_operator = BinaryOperation::Operator::Modulo;
                                } break;

                                default: {
                                    error(*context, "Expected '=', '+=', '-=', '*=', '/=', '%=' or ';', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                    return err();
                                } break;
                            }

                            expect(expression, parse_expression(context, OperatorPrecedence::None));

                            expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                            if(is_binary_operation_assignment) {
                                return ok((Statement*)new BinaryOperationAssignment(
                                    span_range(first_range, last_range),
                                    right_expression,
                                    binary_operator,
                                    expression
                                ));
                            } else {
                                return ok((Statement*)new Assignment(
                                    span_range(first_range, last_range),
                                    right_expression,
                                    expression
                                ));
                            }
                        } break;
                    }
                }
            }
        } break;

        default: {
            expect(expression, parse_expression(context, OperatorPrecedence::None));

            expect(token, peek_token(*context));

            switch(token.kind) {
                case TokenKind::Semicolon: {
                    consume_token(context);

                    return ok((Statement*)new ExpressionStatement(
                        span_range(first_range, token_range(token)),
                        expression
                    ));
                } break;

                case TokenKind::Equals: {
                    consume_token(context);

                    expect(value_expression, parse_expression(context, OperatorPrecedence::None));

                    expect(last_range, expect_basic_token_with_range(context, TokenKind::Semicolon));

                    return ok((Statement*)new Assignment(
                        span_range(first_range, last_range),
                        expression,
                        value_expression
                    ));
                } break;

                default: {
                    error(*context, "Expected '=' or ';', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                    return err();
                } break;
            }
        } break;
    }
}

profiled_function(Result<Array<Statement*>>, parse_tokens, (String path, Array<Token> tokens), (path, tokens)) {
    Context context {
        path,
        tokens
    };

    List<Statement*> statements{};

    while(context.next_token_index < tokens.length) {
        expect(statement, parse_statement(&context));

        statements.append(statement);
    }

    return ok((Array<Statement*>)statements);
}