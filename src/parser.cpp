#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "profiler.h"
#include "path.h"
#include "list.h"
#include "util.h"

inline FileRange token_range(Token token) {
    FileRange range {};
    range.first_line = token.line;
    range.first_column = token.first_column;
    range.last_line = token.line;
    range.last_column = token.last_column;

    return range;
}

inline FileRange span_range(FileRange first, FileRange last) {
    FileRange range {};
    range.first_line = first.first_line;
    range.first_column = first.first_column;
    range.last_line = last.last_line;
    range.last_column = last.last_column;

    return range;
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
    BitwiseShift,
    Additive,
    Multiplicitive,
    Cast,
    PrefixUnary,
    PostfixUnary
};

namespace {
    struct Parser {
        String path;

        Array<Token> tokens;

        size_t next_token_index;

        void error(const char* format, ...) {
            va_list arguments;
            va_start(arguments, format);

            ::error(path, token_range(tokens[next_token_index]), format, arguments);

            va_end(arguments);
        }

        inline Result<Token> peek_token() {
            if(next_token_index < tokens.length) {
                auto token = tokens[next_token_index];

                return ok(token);
            } else {
                fprintf(stderr, "Error: %.*s: Unexpected end of file\n", STRING_PRINTF_ARGUMENTS(path));

                return err();
            }
        }

        inline void consume_token() {
            next_token_index += 1;
        }

        Result<void> expect_basic_token(TokenKind type) {
            expect(token, peek_token());

            if(token.kind != type) {
                Token expected_token;
                expected_token.kind = type;

                error("Expected '%.*s', got '%.*s'", STRING_PRINTF_ARGUMENTS(expected_token.get_text()), STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }

            consume_token();

            return ok();
        }

        Result<FileRange> expect_basic_token_with_range(TokenKind type) {
            expect(token, peek_token());

            if(token.kind != type) {
                Token expected_token;
                expected_token.kind = type;

                error("Expected '%.*s', got '%.*s'", STRING_PRINTF_ARGUMENTS(expected_token.get_text()), STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }

            consume_token();

            auto range = token_range(token);

            return ok(range);
        }

        Result<String> expect_string() {
            expect(token, peek_token());

            if(token.kind != TokenKind::String) {
                error("Expected a string, got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }

            consume_token();

            return ok(token.string);
        }

        inline Identifier identifier_from_token(Token token) {
            Identifier identifier {};
            identifier.text = token.identifier;
            identifier.range = token_range(token);

            return identifier;
        }

        Result<Identifier> expect_identifier() {
            expect(token, peek_token());

            if(token.kind != TokenKind::Identifier) {
                error("Expected an identifier, got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                return err();
            }

            consume_token();

            return ok(identifier_from_token(token));
        }

        // Precedence sorting based on https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
        profiled_function(Result<Expression*>, parse_expression, (OperatorPrecedence minimum_precedence), (minimum_precedence)) {
            Expression* left_expression;

            expect(token, peek_token());

            // Parse atomic & prefix-unary expressions (non-left-recursive)
            switch(token.kind) {
                case TokenKind::Identifier: {
                    consume_token();

                    auto identifier = identifier_from_token(token);

                    left_expression = named_reference_from_identifier(identifier);
                } break;

                case TokenKind::Integer: {
                    consume_token();

                    left_expression = new IntegerLiteral(
                        token_range(token),
                        token.integer
                    );
                } break;

                case TokenKind::FloatingPoint: {
                    consume_token();

                    left_expression = new FloatLiteral(
                        token_range(token),
                        token.floating_point
                    );
                } break;

                case TokenKind::Asterisk: {
                    consume_token();

                    expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                    left_expression = new UnaryOperation(
                        span_range(token_range(token), expression->range),
                        UnaryOperation::Operator::Pointer,
                        expression
                    );
                } break;

                case TokenKind::Hash: {
                    consume_token();

                    expect(identifier, expect_identifier());

                    if(identifier.text == "bake"_S) {
                        expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                        if(expression->kind != ExpressionKind::FunctionCall) {
                            ::error(path, expression->range, "Expected a function call");

                            return err();
                        }

                        auto function_call = (FunctionCall*)expression;

                        left_expression = new Bake(
                            span_range(token_range(token), function_call->range),
                            function_call
                        );
                    } else {
                        ::error(path, identifier.range, "Expected 'bake', got '%.*s'", STRING_PRINTF_ARGUMENTS(identifier.text));

                        return err();
                    }
                } break;

                case TokenKind::Bang: {
                    consume_token();

                    expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                    left_expression = new UnaryOperation(
                        span_range(token_range(token), expression->range),
                        UnaryOperation::Operator::BooleanInvert,
                        expression
                    );
                } break;

                case TokenKind::Dash: {
                    consume_token();

                    expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                    left_expression = new UnaryOperation(
                        span_range(token_range(token), expression->range),
                        UnaryOperation::Operator::Negation,
                        expression
                    );
                } break;

                case TokenKind::String: {
                    consume_token();

                    left_expression = new StringLiteral(
                        token_range(token),
                        token.string
                    );
                } break;

                case TokenKind::OpenRoundBracket: {
                    consume_token();

                    auto first_range = token_range(token);

                    expect(token, peek_token());

                    switch(token.kind) {
                        case TokenKind::Dollar: {
                            consume_token();

                            List<FunctionParameter> parameters {};

                            expect(name, expect_identifier());

                            expect(parameter, parse_function_parameter_second_half(name, true));

                            parameters.append(parameter);

                            expect(token, peek_token());

                            FileRange last_range;
                            switch(token.kind) {
                                case TokenKind::Comma: {
                                    consume_token();

                                    while(true) {
                                        expect(pre_token, peek_token());

                                        bool is_constant;
                                        if(pre_token.kind == TokenKind::Dollar) {
                                            consume_token();

                                            is_constant = true;
                                        } else {
                                            is_constant = false;
                                        }

                                        expect(identifier, expect_identifier());

                                        expect(parameter, parse_function_parameter_second_half(identifier, is_constant));

                                        parameters.append(parameter);

                                        expect(token, peek_token());

                                        auto done = false;
                                        switch(token.kind) {
                                            case TokenKind::Comma: {
                                                consume_token();
                                            } break;

                                            case TokenKind::CloseRoundBracket: {
                                                consume_token();

                                                done = true;
                                            } break;

                                            default: {
                                                error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                    consume_token();

                                    last_range = token_range(token);
                                } break;

                                default: {
                                    error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                    return err();
                                } break;
                            }

                            expect(post_token, peek_token());

                            Expression* return_type;
                            if(post_token.kind == TokenKind::Arrow) {
                                consume_token();

                                expect(expression, parse_expression(OperatorPrecedence::None));

                                return_type = expression;
                                last_range = expression->range;
                            } else {
                                return_type = nullptr;
                            }

                            expect(tags, parse_tags());

                            left_expression = new FunctionType(
                                span_range(first_range, last_range),
                                parameters,
                                return_type,
                                tags
                            );
                        } break;

                        case TokenKind::CloseRoundBracket: {
                            consume_token();

                            auto last_range = token_range(token);

                            expect(token, peek_token());

                            Expression* return_type;
                            if(token.kind == TokenKind::Arrow) {
                                consume_token();

                                expect(expression, parse_expression(OperatorPrecedence::None));

                                return_type = expression;
                                last_range = expression->range;
                            } else {
                                return_type = nullptr;
                            }

                            expect(tags, parse_tags());

                            left_expression = new FunctionType(
                                span_range(first_range, last_range),
                                Array<FunctionParameter>::empty(),
                                return_type,
                                tags
                            );
                        } break;

                        case TokenKind::Identifier: {
                            consume_token();

                            auto identifier = identifier_from_token(token);

                            expect(token, peek_token());

                            if(token.kind == TokenKind::Colon) {
                                List<FunctionParameter> parameters {};

                                expect(parameter, parse_function_parameter_second_half(identifier, false));

                                parameters.append(parameter);

                                expect(token, peek_token());

                                FileRange last_range;
                                switch(token.kind) {
                                    case TokenKind::Comma: {
                                        consume_token();

                                        while(true) {
                                            expect(pre_token, peek_token());

                                            bool is_constant;
                                            if(pre_token.kind == TokenKind::Dollar) {
                                                consume_token();

                                                is_constant = true;
                                            } else {
                                                is_constant = false;
                                            }

                                            expect(identifier, expect_identifier());

                                            expect(parameter, parse_function_parameter_second_half(identifier, is_constant));

                                            parameters.append(parameter);

                                            expect(token, peek_token());

                                            auto done = false;
                                            switch(token.kind) {
                                                case TokenKind::Comma: {
                                                    consume_token();
                                                } break;

                                                case TokenKind::CloseRoundBracket: {
                                                    consume_token();

                                                    done = true;
                                                } break;

                                                default: {
                                                    error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                        consume_token();

                                        last_range = token_range(token);
                                    } break;

                                    default: {
                                        error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                        return err();
                                    } break;
                                }

                                expect(post_token, peek_token());

                                Expression* return_type;
                                if(post_token.kind == TokenKind::Arrow) {
                                    consume_token();

                                    expect(expression, parse_expression(OperatorPrecedence::None));

                                    return_type = expression;
                                    last_range = expression->range;
                                } else {
                                    return_type = nullptr;
                                }

                                expect(tags, parse_tags());

                                left_expression = new FunctionType(
                                    span_range(first_range, last_range),
                                    parameters,
                                    return_type,
                                    tags
                                );
                            } else {
                                auto expression = named_reference_from_identifier(identifier);

                                expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, expression));

                                expect_void(expect_basic_token(TokenKind::CloseRoundBracket));

                                left_expression = right_expression;
                            }
                        } break;

                        default: {
                            expect(expression, parse_expression(OperatorPrecedence::None));

                            expect_void(expect_basic_token(TokenKind::CloseRoundBracket));

                            left_expression = expression;
                        } break;
                    }
                } break;

                case TokenKind::OpenCurlyBracket: {
                    consume_token();

                    auto first_range = token_range(token);

                    expect(token, peek_token());

                    switch(token.kind) {
                        case TokenKind::CloseCurlyBracket: {
                            consume_token();

                            left_expression = new ArrayLiteral(
                                span_range(first_range, token_range(token)),
                                {}
                            );
                        } break;

                        case TokenKind::Identifier: {
                            consume_token();

                            auto identifier = identifier_from_token(token);

                            expect(token, peek_token());

                            switch(token.kind) {
                                case TokenKind::Equals: {
                                    consume_token();

                                    expect(first_expression, parse_expression(OperatorPrecedence::None));

                                    List<StructLiteral::Member> members {};

                                    StructLiteral::Member first_member {};
                                    first_member.name = identifier;
                                    first_member.value = first_expression;

                                    members.append(first_member);

                                    expect(token, peek_token());

                                    FileRange last_range;
                                    switch(token.kind) {
                                        case TokenKind::Comma: {
                                            consume_token();

                                            while(true) {
                                                expect(identifier, expect_identifier());

                                                expect_void(expect_basic_token(TokenKind::Equals));

                                                expect(expression, parse_expression(OperatorPrecedence::None));

                                                StructLiteral::Member member {};
                                                member.name = identifier;
                                                member.value = expression;

                                                members.append(member);

                                                expect(token, peek_token());

                                                auto done = false;
                                                switch(token.kind) {
                                                    case TokenKind::Comma: {
                                                        consume_token();
                                                    } break;

                                                    case TokenKind::CloseCurlyBracket: {
                                                        consume_token();

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error("Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                            consume_token();

                                            last_range = token_range(token);
                                        } break;

                                        default: {
                                            error("Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                            return err();
                                        } break;
                                    }

                                    left_expression = new StructLiteral(
                                        span_range(first_range, last_range),
                                        members
                                    );
                                } break;

                                case TokenKind::CloseCurlyBracket: {
                                    consume_token();

                                    auto first_element = named_reference_from_identifier(identifier);

                                    left_expression = new ArrayLiteral(
                                        span_range(first_range, token_range(token)),
                                        Array(1, heapify(first_element))
                                    );
                                } break;

                                default: {
                                    auto sub_expression = named_reference_from_identifier(identifier);

                                    expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, sub_expression));

                                    List<Expression*> elements {};

                                    elements.append(right_expression);

                                    expect(token, peek_token());

                                    FileRange last_range;
                                    switch(token.kind) {
                                        case TokenKind::Comma: {
                                            consume_token();

                                            while(true) {
                                                expect(expression, parse_expression(OperatorPrecedence::None));

                                                elements.append(expression);

                                                expect(token, peek_token());

                                                auto done = false;
                                                switch(token.kind) {
                                                    case TokenKind::Comma: {
                                                        consume_token();
                                                    } break;

                                                    case TokenKind::CloseCurlyBracket: {
                                                        consume_token();

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error("Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                            consume_token();

                                            last_range = token_range(token);
                                        } break;

                                        default: {
                                            error("Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                            return err();
                                        }
                                    }

                                    left_expression = new ArrayLiteral(
                                        span_range(first_range, last_range),
                                        elements
                                    );
                                } break;
                            }
                        } break;

                        default: {
                            expect(first_expression, parse_expression(OperatorPrecedence::None));

                            List<Expression*> elements {};

                            elements.append(first_expression);

                            expect(token, peek_token());

                            FileRange last_range;
                            switch(token.kind) {
                                case TokenKind::Comma: {
                                    consume_token();

                                    while(true) {
                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                        elements.append(expression);

                                        expect(token, peek_token());

                                        auto done = false;
                                        switch(token.kind) {
                                            case TokenKind::Comma: {
                                                consume_token();
                                            } break;

                                            case TokenKind::CloseCurlyBracket: {
                                                consume_token();

                                                done = true;
                                            } break;

                                            default: {
                                                error("Expected ',' or '}'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                    consume_token();

                                    last_range = token_range(token);
                                } break;
                            }

                            left_expression = new ArrayLiteral(
                                span_range(first_range, last_range),
                                elements
                            );
                        } break;
                    }
                } break;

                case TokenKind::OpenSquareBracket: {
                    consume_token();

                    expect(token, peek_token());

                    Expression* index;
                    FileRange last_range;
                    if(token.kind == TokenKind::CloseSquareBracket) {
                        consume_token();

                        index = nullptr;
                        last_range = token_range(token);
                    } else {
                        expect(expression, parse_expression(OperatorPrecedence::None));

                        expect(range, expect_basic_token_with_range(TokenKind::CloseSquareBracket));

                        index = expression;
                        last_range = range;
                    }

                    expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                    left_expression = new ArrayType(
                        span_range(token_range(token), expression->range),
                        expression,
                        index
                    );
                } break;

                default: {
                    error("Expected an expression. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                    return err();
                } break;
            }

            expect(final_expression, parse_expression_continuation(minimum_precedence, left_expression));

            return ok(final_expression);
        }

        profiled_function(Result<Expression*>, parse_expression_continuation, (
            OperatorPrecedence minimum_precedence,
            Expression* expression
        ), (
            minimum_precedence,
            expression
        )) {
            auto current_expression = expression;

            auto done = false;
            while(!done) {
                expect(token, peek_token());

                auto done = false;
                switch(token.kind) {
                    case TokenKind::Dot: {
                        if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(identifier, expect_identifier());

                        current_expression = new MemberReference(
                            span_range(current_expression->range, identifier.range),
                            current_expression,
                            identifier
                        );
                    } break;

                    case TokenKind::Plus: {
                        if(OperatorPrecedence::Additive <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Additive));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Addition,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::Dash: {
                        if(OperatorPrecedence::Additive <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Additive));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Subtraction,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::Asterisk: {
                        if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Multiplicitive));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Multiplication,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::ForwardSlash: {
                        if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Multiplicitive));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Division,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::Percent: {
                        if(OperatorPrecedence::Multiplicitive <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Multiplicitive));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Modulo,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::Ampersand: {
                        if(OperatorPrecedence::BitwiseAnd <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BitwiseAnd));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::BitwiseAnd,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::DoubleAmpersand: {
                        if(OperatorPrecedence::BooleanAnd <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BooleanAnd));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::BooleanAnd,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::Pipe: {
                        if(OperatorPrecedence::BitwiseOr <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BitwiseOr));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::BitwiseOr,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::DoublePipe: {
                        if(OperatorPrecedence::BooleanOr <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BooleanOr));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::BooleanOr,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::DoubleEquals: {
                        if(OperatorPrecedence::Comparison <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Comparison));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::Equal,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::BangEquals: {
                        if(OperatorPrecedence::Comparison <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Comparison));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::NotEqual,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::LeftArrow: {
                        if(OperatorPrecedence::Comparison <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Comparison));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::LessThan,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::DoubleLeftArrow: {
                        if(OperatorPrecedence::BitwiseShift <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BitwiseShift));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::LeftShift,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::RightArrow: {
                        if(OperatorPrecedence::Comparison <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::Comparison));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::GreaterThan,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::DoubleRightArrow: {
                        if(OperatorPrecedence::BitwiseShift <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        expect(expression, parse_expression(OperatorPrecedence::BitwiseShift));

                        current_expression = new BinaryOperation(
                            span_range(current_expression->range, expression->range),
                            BinaryOperation::Operator::RightShift,
                            current_expression,
                            expression
                        );
                    } break;

                    case TokenKind::OpenRoundBracket: {
                        if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        List<Expression*> parameters {};

                        expect(token, peek_token());

                        FileRange last_range;
                        if(token.kind == TokenKind::CloseRoundBracket) {
                            consume_token();

                            last_range = token_range(token);
                        } else {
                            while(true) {
                                expect(expression, parse_expression(OperatorPrecedence::None));

                                parameters.append(expression);

                                expect(token, peek_token());

                                auto done = false;
                                switch(token.kind) {
                                    case TokenKind::Comma: {
                                        consume_token();
                                    } break;

                                    case TokenKind::CloseRoundBracket: {
                                        consume_token();

                                        done = true;
                                    } break;

                                    default: {
                                        error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                        return err();
                                    } break;
                                }

                                if(done) {
                                    last_range = token_range(token);

                                    break;
                                }
                            }
                        }

                        current_expression = new FunctionCall(
                            span_range(current_expression->range, last_range),
                            current_expression,
                            parameters
                        );
                    } break;

                    case TokenKind::OpenSquareBracket: {
                        if(OperatorPrecedence::PostfixUnary <= minimum_precedence) {
                            done = true;

                            break;
                        }

                        consume_token();

                        auto first_range = token_range(token);

                        expect(index, parse_expression(OperatorPrecedence::None));

                        expect(last_range, expect_basic_token_with_range(TokenKind::CloseSquareBracket));

                        current_expression = new IndexReference(
                            span_range(current_expression->range, last_range),
                            current_expression,
                            index
                        );
                    } break;

                    case TokenKind::Identifier: {
                        if(token.identifier == "as"_S) {
                            if(OperatorPrecedence::Cast <= minimum_precedence) {
                                done = true;

                                break;
                            }

                            consume_token();

                            expect(expression, parse_expression(OperatorPrecedence::Cast));

                            current_expression = new Cast(
                                span_range(current_expression->range, expression->range),
                                current_expression,
                                expression
                            );
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

        Result<FunctionParameter> parse_function_parameter_second_half(Identifier name, bool is_constant) {
            expect_void(expect_basic_token(TokenKind::Colon));

            FunctionParameter parameter;
            parameter.name = name;
            parameter.is_constant = is_constant;

            expect(token, peek_token());

            switch(token.kind) {
                case TokenKind::Dollar: {
                    consume_token();

                    expect(name, expect_identifier());

                    parameter.is_polymorphic_determiner = true;
                    parameter.polymorphic_determiner = name;
                } break;

                default: {
                    expect(expression, parse_expression(OperatorPrecedence::None));

                    parameter.is_polymorphic_determiner = false;
                    parameter.type = expression;
                } break;
            }

            return ok(parameter);
        }

        Result<Array<Tag>> parse_tags() {
            expect(token, peek_token());

            if(token.kind != TokenKind::Hash) {
                return ok(Array<Tag> {});
            }

            auto first_range = token_range(token);

            consume_token();

            List<Tag> tags {};
            while(true) {
                expect(name, expect_identifier());

                auto last_range = name.range;

                expect(token, peek_token());

                List<Expression*> parameters {};
                if(token.kind == TokenKind::OpenRoundBracket) {
                    consume_token();

                    expect(token, peek_token());

                    if(token.kind == TokenKind::CloseRoundBracket) {
                        consume_token();

                        last_range = token_range(token);
                    } else {
                        while(true) {
                            expect(expression, parse_expression(OperatorPrecedence::None));

                            parameters.append(expression);

                            expect(token, peek_token());

                            auto done = false;
                            switch(token.kind) {
                                case TokenKind::Comma: {
                                    consume_token();
                                } break;

                                case TokenKind::CloseRoundBracket: {
                                    consume_token();

                                    done = true;
                                } break;

                                default: {
                                    error("Expected ',' or ')'. Got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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

                expect(post_token, peek_token());

                if(post_token.kind == TokenKind::Hash) {
                    consume_token();

                    first_range = token_range(post_token);
                } else {
                    break;
                }
            }

            return ok((Array<Tag>)tags);
        }

        Result<Statement*> continue_parsing_function_declaration(
            Identifier name,
            Array<FunctionParameter> parameters,
            FileRange parameters_range
        ) {
            auto last_range = parameters_range;

            expect(pre_token, peek_token());

            Expression* return_type;
            switch(pre_token.kind) {
                case TokenKind::Arrow: {
                    consume_token();

                    last_range = token_range(pre_token);

                    expect(expression, parse_expression(OperatorPrecedence::None));

                    return_type = expression;
                } break;

                default: {
                    return_type = nullptr;
                } break;
            }

            expect(tags, parse_tags());

            expect(token, peek_token());

            switch(token.kind) {
                case TokenKind::OpenCurlyBracket: {
                    consume_token();

                    List<Statement*> statements {};

                    while(true) {
                        expect(token, peek_token());

                        if(token.kind == TokenKind::CloseCurlyBracket) {
                            consume_token();

                            last_range = token_range(token);

                            break;
                        } else {
                            expect(statement, parse_statement());

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
                    consume_token();

                    return ok((Statement*)new FunctionDeclaration(
                        span_range(name.range, last_range),
                        name,
                        parameters,
                        return_type,
                        tags
                    ));
                } break;

                default: {
                    error("Expected '->', '{', ';', or '#', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                    return err();
                } break;
            }
        }

        profiled_function(Result<Statement*>, parse_statement, (), ()) {
            expect(token, peek_token());

            auto first_range = token_range(token);

            switch(token.kind) {
                case TokenKind::Hash: {
                    consume_token();

                    expect(token, peek_token());

                    if(token.kind != TokenKind::Identifier) {
                        error("Expected 'import', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                        return err();
                    }

                    consume_token();

                    if(token.identifier == "import"_S) {
                        expect(string, expect_string());

                        expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                        auto source_file_directory = path_get_directory_component(path);

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
                        expect(expression, parse_expression(OperatorPrecedence::None));

                        expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));

                        List<Statement*> statements {};

                        FileRange last_range;
                        while(true) {
                            expect(token, peek_token());

                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                consume_token();

                                last_range = token_range(token);

                                break;
                            } else {
                                expect(statement, parse_statement());

                                statements.append(statement);
                            }
                        }

                        return ok((Statement*)new StaticIf(
                            span_range(first_range, last_range),
                            expression,
                            statements
                        ));
                    } else if(token.identifier == "bake"_S) {
                        expect(expression, parse_expression(OperatorPrecedence::PrefixUnary));

                        if(expression->kind != ExpressionKind::FunctionCall) {
                            ::error(path, expression->range, "Expected a function call");

                            return err();
                        }

                        expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                        auto function_call = (FunctionCall*)expression;

                        auto bake = new Bake(
                            span_range(first_range, function_call->range),
                            function_call
                        );

                        return ok((Statement*)new ExpressionStatement(
                            span_range(first_range, last_range),
                            bake
                        ));
                    } else {
                        error("Expected 'import', 'if' or 'bake', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                        return err();
                    }
                } break;

                case TokenKind::Identifier: {
                    consume_token();

                    if(token.identifier == "if"_S) {
                        expect(expression, parse_expression(OperatorPrecedence::None));

                        expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));

                        List<Statement*> statements {};

                        FileRange last_range;
                        while(true) {
                            expect(token, peek_token());

                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                consume_token();

                                last_range = token_range(token);

                                break;
                            } else {
                                expect(statement, parse_statement());

                                statements.append(statement);
                            }
                        }

                        List<IfStatement::ElseIf> else_ifs {};

                        auto has_else = false;
                        List<Statement*> else_statements {};
                        while(true) {
                            expect(token, peek_token());

                            if(token.kind == TokenKind::Identifier && token.identifier == "else"_S) {
                                consume_token();

                                expect(token, peek_token());

                                switch(token.kind) {
                                    case TokenKind::OpenCurlyBracket: {
                                        consume_token();

                                        while(true) {
                                            expect(token, peek_token());

                                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                                consume_token();

                                                last_range = token_range(token);

                                                break;
                                            } else {
                                                expect(statement, parse_statement());

                                                statements.append(statement);
                                            }
                                        }
                                    } break;

                                    case TokenKind::Identifier: {
                                        if(token.identifier != "if"_S) {
                                            error("Expected '{' or 'if', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                            return err();
                                        }

                                        consume_token();

                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                        expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));
                                        
                                        List<Statement*> statements {};

                                        while(true) {
                                            expect(token, peek_token());

                                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                                consume_token();

                                                last_range = token_range(token);

                                                break;
                                            } else {
                                                expect(statement, parse_statement());

                                                statements.append(statement);
                                            }
                                        }

                                        IfStatement::ElseIf else_if {};
                                        else_if.condition = expression;
                                        else_if.statements = statements;

                                        else_ifs.append(else_if);
                                    } break;

                                    default: {
                                        error("Expected '{' or 'if', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                        expect(expression, parse_expression(OperatorPrecedence::None));

                        expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));

                        List<Statement*> statements {};

                        FileRange last_range;
                        while(true) {
                            expect(token, peek_token());

                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                consume_token();

                                last_range = token_range(token);

                                break;
                            } else {
                                expect(statement, parse_statement());

                                statements.append(statement);
                            }
                        }

                        return ok((Statement*)new WhileLoop(
                            span_range(first_range, last_range),
                            expression,
                            statements
                        ));
                    } else if(token.identifier == "for"_S) {
                        expect(token, peek_token());

                        bool has_index_name;
                        Identifier index_name;
                        Expression* from;
                        if(token.kind == TokenKind::Identifier) {
                            consume_token();

                            auto identifier = identifier_from_token(token);

                            expect(token, peek_token());

                            if(token.kind == TokenKind::Colon) {
                                consume_token();

                                expect(expression, parse_expression(OperatorPrecedence::None));

                                has_index_name = true;
                                index_name = identifier;

                                from = expression;
                            } else {
                                auto named_reference = named_reference_from_identifier(identifier);

                                expect(expression, parse_expression_continuation(OperatorPrecedence::None, named_reference));

                                has_index_name = false;

                                from = expression;
                            }
                        } else {
                            expect(expression, parse_expression(OperatorPrecedence::None));

                            has_index_name = false;

                            from = expression;
                        }

                        expect_void(expect_basic_token(TokenKind::DoubleDot));

                        expect(to, parse_expression(OperatorPrecedence::None));

                        { expect_void(expect_basic_token(TokenKind::OpenCurlyBracket)); }

                        List<Statement*> statements {};

                        FileRange last_range;
                        while(true) {
                            expect(token, peek_token());

                            if(token.kind == TokenKind::CloseCurlyBracket) {
                                consume_token();

                                last_range = token_range(token);

                                break;
                            } else {
                                expect(statement, parse_statement());

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
                        expect(token, peek_token());

                        Expression* value;
                        FileRange last_range;
                        if(token.kind == TokenKind::Semicolon) {
                            consume_token();

                            last_range = token_range(token);

                            value = nullptr;
                        } else {
                            expect(expression, parse_expression(OperatorPrecedence::None));

                            expect(range, expect_basic_token_with_range(TokenKind::Semicolon));

                            last_range = range;

                            value = expression;
                        }

                        return ok((Statement*)new ReturnStatement(
                            span_range(first_range, last_range),
                            value
                        ));
                    } else if(token.identifier == "break"_S) {
                        expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                        return ok((Statement*)new BreakStatement(
                            span_range(first_range, last_range)
                        ));
                    } else if(token.identifier == "using"_S) {
                        expect(expression, parse_expression(OperatorPrecedence::None));

                        expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                        return ok((Statement*)new UsingStatement(
                            span_range(first_range, last_range),
                            expression
                        ));
                    } else {
                        auto identifier = identifier_from_token(token);

                        expect(token, peek_token());

                        if(token.kind == TokenKind::Colon) {
                            consume_token();

                            expect(token, peek_token());

                            switch(token.kind) {
                                case TokenKind::Colon: {
                                    consume_token();

                                    expect(token, peek_token());

                                    switch(token.kind) {
                                        case TokenKind::OpenRoundBracket: {
                                            consume_token();

                                            auto parameters_first_range = token_range(token);

                                            expect(token, peek_token());

                                            if(token.kind == TokenKind::Dollar) {
                                                consume_token();

                                                expect(name, expect_identifier());

                                                List<FunctionParameter> parameters {};

                                                expect(parameter, parse_function_parameter_second_half(name, true));

                                                parameters.append(parameter);

                                                expect(token, peek_token());

                                                FileRange last_range;
                                                switch(token.kind) {
                                                    case TokenKind::Comma: {
                                                        consume_token();

                                                        while(true) {
                                                            expect(pre_token, peek_token());

                                                            bool is_constant;
                                                            if(pre_token.kind == TokenKind::Dollar) {
                                                                consume_token();

                                                                is_constant = true;
                                                            } else {
                                                                is_constant = false;
                                                            }

                                                            expect(identifier, expect_identifier());

                                                            expect(parameter, parse_function_parameter_second_half(identifier, is_constant));

                                                            parameters.append(parameter);

                                                            expect(token, peek_token());

                                                            auto done = false;
                                                            switch(token.kind) {
                                                                case TokenKind::Comma: {
                                                                    consume_token();
                                                                } break;

                                                                case TokenKind::CloseRoundBracket: {
                                                                    consume_token();

                                                                    done = true;
                                                                } break;

                                                                default: {
                                                                    error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                                        consume_token();

                                                        last_range = token_range(token);
                                                    } break;

                                                    default: {
                                                        error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                        return err();
                                                    } break;
                                                }

                                                return continue_parsing_function_declaration(
                                                    identifier,
                                                    parameters,
                                                    span_range(parameters_first_range, last_range)
                                                );
                                            } else if(token.kind == TokenKind::CloseRoundBracket) {
                                                consume_token();

                                                return continue_parsing_function_declaration(
                                                    identifier,
                                                    Array<FunctionParameter>::empty(),
                                                    span_range(parameters_first_range, token_range(token))
                                                );
                                            } else if(token.kind == TokenKind::Identifier) {
                                                consume_token();

                                                auto first_identifier = identifier_from_token(token);

                                                expect(token, peek_token());

                                                if(token.kind == TokenKind::Colon) {
                                                    List<FunctionParameter> parameters {};

                                                    expect(parameter, parse_function_parameter_second_half(first_identifier, false));

                                                    parameters.append(parameter);

                                                    expect(token, peek_token());

                                                    FileRange last_range;
                                                    switch(token.kind) {
                                                        case TokenKind::Comma: {
                                                            consume_token();

                                                            while(true) {
                                                                expect(pre_token, peek_token());

                                                                bool is_constant;
                                                                if(pre_token.kind == TokenKind::Dollar) {
                                                                    consume_token();

                                                                    is_constant = true;
                                                                } else {
                                                                    is_constant = false;
                                                                }

                                                                expect(identifier, expect_identifier());

                                                                expect(parameter, parse_function_parameter_second_half(identifier, is_constant));

                                                                parameters.append(parameter);

                                                                expect(token, peek_token());

                                                                auto done = false;
                                                                switch(token.kind) {
                                                                    case TokenKind::Comma: {
                                                                        consume_token();
                                                                    } break;

                                                                    case TokenKind::CloseRoundBracket: {
                                                                        consume_token();

                                                                        done = true;
                                                                    } break;

                                                                    default: {
                                                                        error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                                            consume_token();

                                                            last_range = token_range(token);
                                                        } break;

                                                        default: {
                                                            error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                            return err();
                                                        } break;
                                                    }

                                                    return continue_parsing_function_declaration(
                                                        identifier,
                                                        parameters,
                                                        span_range(parameters_first_range, last_range)
                                                    );
                                                } else {
                                                    auto expression = named_reference_from_identifier(first_identifier);

                                                    expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, expression));

                                                    expect_void(expect_basic_token(TokenKind::CloseRoundBracket));

                                                    expect(outer_right_expression, parse_expression_continuation(OperatorPrecedence::None, right_expression));

                                                    expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                                                    return ok((Statement*)new ConstantDefinition(
                                                        span_range(first_range, last_range),
                                                        identifier,
                                                        outer_right_expression
                                                    ));
                                                }
                                            } else {
                                                expect(expression, parse_expression(OperatorPrecedence::None));

                                                expect_void(expect_basic_token(TokenKind::CloseRoundBracket));

                                                expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, expression));

                                                expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                                                return ok((Statement*)new ConstantDefinition(
                                                    span_range(first_range, last_range),
                                                    identifier,
                                                    right_expression
                                                ));
                                            }
                                        } break;

                                        case TokenKind::Identifier: {
                                            consume_token();

                                            if(token.identifier == "struct"_S) {
                                                expect(maybe_parameter_token, peek_token());

                                                List<StructDefinition::Parameter> parameters {};

                                                if(maybe_parameter_token.kind == TokenKind::OpenRoundBracket) {
                                                    consume_token();

                                                    expect(token, peek_token());

                                                    if(token.kind == TokenKind::CloseRoundBracket) {
                                                        consume_token();
                                                    } else {
                                                        while(true) {
                                                            expect(name, expect_identifier());

                                                            expect_void(expect_basic_token(TokenKind::Colon));

                                                            expect(type, parse_expression(OperatorPrecedence::None));

                                                            StructDefinition::Parameter parameter {};
                                                            parameter.name = name;
                                                            parameter.type = type;

                                                            parameters.append(parameter);

                                                            expect(token, peek_token());

                                                            auto done = false;
                                                            switch(token.kind) {
                                                                case TokenKind::Comma: {
                                                                    consume_token();
                                                                } break;

                                                                case TokenKind::CloseRoundBracket: {
                                                                    consume_token();

                                                                    done = true;
                                                                } break;

                                                                default: {
                                                                    error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));
                                                                } break;
                                                            }

                                                            if(done) {
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }

                                                expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));

                                                List<StructDefinition::Member> members {};

                                                expect(token, peek_token());

                                                FileRange last_range;
                                                if(token.kind == TokenKind::CloseCurlyBracket) {
                                                    consume_token();

                                                    last_range = token_range(token);
                                                } else {
                                                    while(true) {
                                                        expect(identifier, expect_identifier());

                                                        expect_void(expect_basic_token(TokenKind::Colon));

                                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                                        StructDefinition::Member member {};
                                                        member.name = identifier;
                                                        member.type = expression;

                                                        members.append(member);

                                                        expect(token, peek_token());

                                                        auto done = false;
                                                        switch(token.kind) {
                                                            case TokenKind::Comma: {
                                                                consume_token();
                                                            } break;

                                                            case TokenKind::CloseCurlyBracket: {
                                                                consume_token();

                                                                done = true;
                                                            } break;

                                                            default: {
                                                                error("Expected ',' or '}', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

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
                                                    parameters,
                                                    members
                                                ));
                                            } else if(token.identifier == "union"_S) {
                                                expect(maybe_parameter_token, peek_token());

                                                List<UnionDefinition::Parameter> parameters {};

                                                if(maybe_parameter_token.kind == TokenKind::OpenRoundBracket) {
                                                    consume_token();

                                                    expect(token, peek_token());

                                                    if(token.kind == TokenKind::CloseRoundBracket) {
                                                        consume_token();
                                                    } else {
                                                        while(true) {
                                                            expect(name, expect_identifier());

                                                            expect_void(expect_basic_token(TokenKind::Colon));

                                                            expect(type, parse_expression(OperatorPrecedence::None));

                                                            UnionDefinition::Parameter parameter {};
                                                            parameter.name = name;
                                                            parameter.type = type;

                                                            parameters.append(parameter);

                                                            expect(token, peek_token());

                                                            auto done = false;
                                                            switch(token.kind) {
                                                                case TokenKind::Comma: {
                                                                    consume_token();
                                                                } break;

                                                                case TokenKind::CloseRoundBracket: {
                                                                    consume_token();

                                                                    done = true;
                                                                } break;

                                                                default: {
                                                                    error("Expected ',' or ')', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));
                                                                } break;
                                                            }

                                                            if(done) {
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }

                                                expect_void(expect_basic_token(TokenKind::OpenCurlyBracket));

                                                List<UnionDefinition::Member> members {};

                                                expect(token, peek_token());

                                                FileRange last_range;
                                                if(token.kind == TokenKind::CloseCurlyBracket) {
                                                    consume_token();

                                                    last_range = token_range(token);
                                                } else {
                                                    while(true) {
                                                        expect(identifier, expect_identifier());

                                                        expect_void(expect_basic_token(TokenKind::Colon));

                                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                                        UnionDefinition::Member member {};
                                                        member.name = identifier;
                                                        member.type = expression;

                                                        members.append(member);

                                                        expect(token, peek_token());

                                                        auto done = false;
                                                        switch(token.kind) {
                                                            case TokenKind::Comma: {
                                                                consume_token();
                                                            } break;

                                                            case TokenKind::CloseCurlyBracket: {
                                                                consume_token();

                                                                done = true;
                                                            } break;

                                                            default: {
                                                                error("Expected ',' or '}', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                                                return err();
                                                            } break;
                                                        }

                                                        if(done) {
                                                            last_range = token_range(token);

                                                            break;
                                                        }
                                                    }
                                                }

                                                return ok((Statement*)new UnionDefinition(
                                                    span_range(first_range, token_range(token)),
                                                    identifier,
                                                    parameters,
                                                    members
                                                ));
                                            } else {
                                                auto sub_identifier = identifier_from_token(token);

                                                auto expression = named_reference_from_identifier(sub_identifier);

                                                expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, expression));

                                                expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                                                return ok((Statement*)new ConstantDefinition(
                                                    span_range(first_range, last_range),
                                                    identifier,
                                                    right_expression
                                                ));
                                            }
                                        } break;

                                        default: {
                                            expect(expression, parse_expression(OperatorPrecedence::None));

                                            expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

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
                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                        type = expression;
                                    }

                                    expect(pre_token, peek_token());

                                    Expression* initializer = nullptr;
                                    if(pre_token.kind == TokenKind::Equals || !type) {
                                        expect_void(expect_basic_token(TokenKind::Equals));

                                        expect(expression, parse_expression(OperatorPrecedence::None));

                                        initializer = expression;
                                    }

                                    expect(tags, parse_tags());

                                    expect_void(expect_basic_token(TokenKind::Semicolon));

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

                            expect(right_expression, parse_expression_continuation(OperatorPrecedence::None, expression));

                            expect(token, peek_token());

                            switch(token.kind) {
                                case TokenKind::Semicolon: {
                                    consume_token();

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
                                            consume_token();

                                            is_binary_operation_assignment = false;
                                        } break;

                                        case TokenKind::PlusEquals: {
                                            consume_token();

                                            is_binary_operation_assignment = true;
                                            binary_operator = BinaryOperation::Operator::Addition;
                                        } break;

                                        case TokenKind::DashEquals: {
                                            consume_token();

                                            is_binary_operation_assignment = true;
                                            binary_operator = BinaryOperation::Operator::Subtraction;
                                        } break;

                                        case TokenKind::AsteriskEquals: {
                                            consume_token();

                                            is_binary_operation_assignment = true;
                                            binary_operator = BinaryOperation::Operator::Multiplication;
                                        } break;

                                        case TokenKind::ForwardSlashEquals: {
                                            consume_token();

                                            is_binary_operation_assignment = true;
                                            binary_operator = BinaryOperation::Operator::Division;
                                        } break;

                                        case TokenKind::PercentEquals: {
                                            consume_token();

                                            is_binary_operation_assignment = true;
                                            binary_operator = BinaryOperation::Operator::Modulo;
                                        } break;

                                        default: {
                                            error("Expected '=', '+=', '-=', '*=', '/=', '%=' or ';', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                                            return err();
                                        } break;
                                    }

                                    expect(expression, parse_expression(OperatorPrecedence::None));

                                    expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

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
                    expect(expression, parse_expression(OperatorPrecedence::None));

                    expect(token, peek_token());

                    switch(token.kind) {
                        case TokenKind::Semicolon: {
                            consume_token();

                            return ok((Statement*)new ExpressionStatement(
                                span_range(first_range, token_range(token)),
                                expression
                            ));
                        } break;

                        case TokenKind::Equals: {
                            consume_token();

                            expect(value_expression, parse_expression(OperatorPrecedence::None));

                            expect(last_range, expect_basic_token_with_range(TokenKind::Semicolon));

                            return ok((Statement*)new Assignment(
                                span_range(first_range, last_range),
                                expression,
                                value_expression
                            ));
                        } break;

                        default: {
                            error("Expected '=' or ';', got '%.*s'", STRING_PRINTF_ARGUMENTS(token.get_text()));

                            return err();
                        } break;
                    }
                } break;
            }
        }
    };
};

profiled_function(Result<Array<Statement*>>, parse_tokens, (String path, Array<Token> tokens), (path, tokens)) {
    Parser parser {};
    parser.path = path;
    parser.tokens = tokens;

    List<Statement*> statements {};

    while(parser.next_token_index < tokens.length) {
        expect(statement, parser.parse_statement());

        statements.append(statement);
    }

    return ok((Array<Statement*>)statements);
}