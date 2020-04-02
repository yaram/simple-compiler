#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
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

static Result<Token> next_token(Context context) {
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

static bool expect_basic_token(Context *context, TokenType type) {
    expect(token, next_token(*context));

    if(token.type != type) {
        Token expected_token;
        expected_token.type = type;

        error(*context, "Expected '%s', got '%s'", get_token_text(expected_token), get_token_text(token));

        return false;
    }

    context->next_token_index += 1;

    return true;
}

static Result<FileRange> expect_basic_token_with_range(Context *context, TokenType type) {
    expect(token, next_token(*context));

    if(token.type != type) {
        Token expected_token;
        expected_token.type = type;

        error(*context, "Expected '%s', got '%s'", get_token_text(expected_token), get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

    auto range = token_range(*context, token);

    return {
        true,
        range
    };
}

static Result<Array<char>> expect_string(Context *context) {
    expect(token, next_token(*context));

    if(token.type != TokenType::String) {
        error(*context, "Expected a string, got '%s'", get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

    return {
        true,
        token.string
    };
}

static Result<Identifier> expect_identifier(Context *context) {
    expect(token, next_token(*context));

    if(token.type != TokenType::Identifier) {
        error(*context, "Expected an identifier, got '%s'", get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

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

enum struct OperationType {
    FunctionCall,
    MemberReference,
    IndexReference,

    Pointer,
    ArrayType,
    BooleanInvert,
    Negation,

    Cast,

    Multiplication,
    Division,
    Modulo,

    Addition,
    Subtraction,

    Equal,
    NotEqual,
    LessThan,
    GreaterThan,

    BitwiseAnd,

    BitwiseOr,

    BooleanAnd,

    BooleanOr
};

unsigned int operation_precedences[] = {
    1, 1, 1,
    2, 2, 2, 2,
    3,
    4, 4, 4,
    5, 5,
    8, 8, 8, 8,
    9,
    11,
    12,
    13
};

struct Operation {
    OperationType type;

    FileRange range;

    union {
        Identifier member_reference;

        Expression *index_reference;

        Expression *array_type;

        Array<Expression*> function_call;
    };
};

static void apply_operation(List<Expression*> *expression_stack, Operation operation) {
    Expression *expression;

    switch(operation.type) {
        case OperationType::MemberReference: {
            auto sub_expression = take_last(expression_stack);

            expression = new MemberReference {
                span_range(sub_expression->range, operation.member_reference.range),
                sub_expression,
                operation.member_reference
            };
        } break;

        case OperationType::IndexReference: {
            auto sub_expression = take_last(expression_stack);

            expression = new IndexReference {
                span_range(sub_expression->range, operation.range),
                sub_expression,
                operation.index_reference
            };
        } break;

        case OperationType::FunctionCall: {
            auto sub_expression = take_last(expression_stack);

            expression = new FunctionCall {
                span_range(sub_expression->range, operation.range),
                sub_expression,
                operation.function_call
            };
        } break;

        case OperationType::Pointer: {
            auto sub_expression = take_last(expression_stack);

            expression = new UnaryOperation {
                span_range(operation.range, sub_expression->range),
                UnaryOperation::Operator::Pointer,
                sub_expression
            };
        } break;

        case OperationType::ArrayType: {
            auto sub_expression = take_last(expression_stack);

            expression = new ArrayType {
                span_range(operation.range, sub_expression->range),
                sub_expression,
                operation.array_type
            };
        } break;

        case OperationType::BooleanInvert: {
            auto sub_expression = take_last(expression_stack);

            expression = new UnaryOperation {
                span_range(operation.range, sub_expression->range),
                UnaryOperation::Operator::BooleanInvert,
                sub_expression
            };
        } break;

        case OperationType::Negation: {
            auto sub_expression = take_last(expression_stack);

            expression = new UnaryOperation {
                span_range(operation.range, sub_expression->range),
                UnaryOperation::Operator::Negation,
                sub_expression
            };
        } break;

        case OperationType::Cast: {
            auto type = take_last(expression_stack);
            auto sub_expression = take_last(expression_stack);

            expression = new Cast {
                span_range(sub_expression->range, type->range),
                sub_expression,
                type
            };
        } break;

        default: {
            auto right = take_last(expression_stack);
            auto left = take_last(expression_stack);

            BinaryOperation::Operator binary_operator;
            switch(operation.type) {
                case OperationType::Addition: {
                    binary_operator = BinaryOperation::Operator::Addition;
                } break;

                case OperationType::Subtraction: {
                    binary_operator = BinaryOperation::Operator::Subtraction;
                } break;

                case OperationType::Multiplication: {
                    binary_operator = BinaryOperation::Operator::Multiplication;
                } break;

                case OperationType::Division: {
                    binary_operator = BinaryOperation::Operator::Division;
                } break;

                case OperationType::Modulo: {
                    binary_operator = BinaryOperation::Operator::Modulo;
                } break;

                case OperationType::Equal: {
                    binary_operator = BinaryOperation::Operator::Equal;
                } break;

                case OperationType::NotEqual: {
                    binary_operator = BinaryOperation::Operator::NotEqual;
                } break;

                case OperationType::LessThan: {
                    binary_operator = BinaryOperation::Operator::LessThan;
                } break;

                case OperationType::GreaterThan: {
                    binary_operator = BinaryOperation::Operator::GreaterThan;
                } break;

                case OperationType::BitwiseAnd: {
                    binary_operator = BinaryOperation::Operator::BitwiseAnd;
                } break;

                case OperationType::BitwiseOr: {
                    binary_operator = BinaryOperation::Operator::BitwiseOr;
                } break;

                case OperationType::BooleanAnd: {
                    binary_operator = BinaryOperation::Operator::BooleanAnd;
                } break;

                case OperationType::BooleanOr: {
                    binary_operator = BinaryOperation::Operator::BooleanOr;
                } break;

                default: {
                    abort();
                } break;
            }

            expression = new BinaryOperation {
                span_range(left->range, right->range),
                binary_operator,
                left,
                right
            };
        } break;
    }

    append(expression_stack, expression);
}

static Result<Expression*> parse_expression(Context *context);

static Result<FunctionParameter> parse_function_parameter_second_half(Context *context, Identifier name, bool is_constant) {
    if(!expect_basic_token(context, TokenType::Colon)) {
        return { false };
    }

    FunctionParameter parameter;
    parameter.name = name;
    parameter.is_constant = is_constant;

    expect(token, next_token(*context));

    switch(token.type) {
        case TokenType::Dollar: {
            context->next_token_index += 1;

            expect(name, expect_identifier(context));

            parameter.is_polymorphic_determiner = true;
            parameter.polymorphic_determiner = name;
        } break;

        default: {
            expect(expression, parse_expression(context));

            parameter.is_polymorphic_determiner = false;
            parameter.type = expression;
        } break;
    }

    return {
        true,
        parameter
    };
}

static Result<Expression*> parse_right_expressions(Context *context, List<Operation> *operation_stack, List<Expression*> *expression_stack, bool start_with_left_recursive) {
    auto expect_left_recursive = start_with_left_recursive;

    while(true) {
        if(!expect_left_recursive) {
            expect(token, next_token(*context));

            // Parse non-left-recursive expressions first
            switch(token.type) {
                case TokenType::Identifier: {
                    context->next_token_index += 1;

                    auto identifier = identifier_from_token(*context, token);

                    auto expression = named_reference_from_identifier(identifier);

                    append(expression_stack, expression);
                } break;

                case TokenType::Integer: {
                    context->next_token_index += 1;

                    append(expression_stack, (Expression*)new IntegerLiteral {
                        token_range(*context, token),
                        token.integer
                    });
                } break;

                case TokenType::FloatingPoint: {
                    context->next_token_index += 1;

                    append(expression_stack, (Expression*)new FloatLiteral {
                        token_range(*context, token),
                        token.floating_point
                    });
                } break;

                case TokenType::Asterisk: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::Pointer;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::Bang: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::BooleanInvert;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::Dash: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::Negation;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::String: {
                    context->next_token_index += 1;

                    append(expression_stack, (Expression*)new StringLiteral {
                        token_range(*context, token),
                        token.string
                    });
                } break;

                case TokenType::OpenRoundBracket: {
                    context->next_token_index += 1;

                    auto first_range = token_range(*context, token);

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::Dollar: {
                            context->next_token_index += 1;

                            List<FunctionParameter> parameters{};

                            expect(name, expect_identifier(context));

                            expect(parameter, parse_function_parameter_second_half(context, name, true));

                            append(&parameters, parameter);

                            expect(token, next_token(*context));

                            FileRange last_range;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    context->next_token_index += 1;

                                    while(true) {
                                        expect(pre_token, next_token(*context));

                                        bool is_constant;
                                        if(pre_token.type == TokenType::Dollar) {
                                            context->next_token_index += 1;

                                            is_constant = true;
                                        } else {
                                            is_constant = false;
                                        }

                                        expect(identifier, expect_identifier(context));

                                        expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                        append(&parameters, parameter);

                                        expect(token, next_token(*context));

                                        auto done = false;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                context->next_token_index += 1;
                                            } break;

                                            case TokenType::CloseRoundBracket: {
                                                context->next_token_index += 1;

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
                                    context->next_token_index += 1;

                                    last_range = token_range(*context, token);
                                } break;

                                default: {
                                    error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                    return { false };
                                } break;
                            }

                            expect(post_token, next_token(*context));

                            Expression *return_type;
                            if(post_token.type == TokenType::Arrow) {
                                context->next_token_index += 1;

                                expect(expression, parse_expression(context));

                                return_type = expression;
                                last_range = expression->range;
                            } else {
                                return_type = nullptr;
                            }

                            append(expression_stack, (Expression*)new FunctionType {
                                span_range(first_range, last_range),
                                to_array(parameters),
                                return_type
                            });
                        } break;

                        case TokenType::CloseRoundBracket: {
                            context->next_token_index += 1;

                            auto last_range = token_range(*context, token);

                            expect(token, next_token(*context));

                            Expression* return_type;
                            if(token.type == TokenType::Arrow) {
                                context->next_token_index += 1;

                                expect(expression, parse_expression(context));

                                return_type = expression;
                                last_range = expression->range;
                            } else {
                                return_type = nullptr;
                            }

                            append(expression_stack, (Expression*)new FunctionType {
                                span_range(first_range, last_range),
                                {},
                                return_type
                            });
                        } break;

                        case TokenType::Identifier: {
                            context->next_token_index += 1;

                            auto identifier = identifier_from_token(*context, token);

                            expect(token, next_token(*context));

                            if(token.type == TokenType::Colon) {
                                List<FunctionParameter> parameters{};

                                expect(parameter, parse_function_parameter_second_half(context, identifier, false));

                                append(&parameters, parameter);

                                expect(token, next_token(*context));

                                FileRange last_range;
                                switch(token.type) {
                                    case TokenType::Comma: {
                                        context->next_token_index += 1;

                                        while(true) {
                                            expect(pre_token, next_token(*context));

                                            bool is_constant;
                                            if(pre_token.type == TokenType::Dollar) {
                                                context->next_token_index += 1;

                                                is_constant = true;
                                            } else {
                                                is_constant = false;
                                            }

                                            expect(identifier, expect_identifier(context));

                                            expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                            append(&parameters, parameter);

                                            expect(token, next_token(*context));

                                            auto done = false;
                                            switch(token.type) {
                                                case TokenType::Comma: {
                                                    context->next_token_index += 1;
                                                } break;

                                                case TokenType::CloseRoundBracket: {
                                                    context->next_token_index += 1;

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
                                        context->next_token_index += 1;

                                        last_range = token_range(*context, token);
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                        return { false };
                                    } break;
                                }

                                expect(post_token, next_token(*context));

                                Expression *return_type;
                                if(post_token.type == TokenType::Arrow) {
                                    context->next_token_index += 1;

                                    expect(expression, parse_expression(context));

                                    return_type = expression;
                                    last_range = expression->range;
                                } else {
                                    return_type = nullptr;
                                }

                                append(expression_stack, (Expression*)new FunctionType {
                                    span_range(first_range, last_range),
                                    to_array(parameters),
                                    return_type
                                });
                            } else {
                                auto expression = named_reference_from_identifier(identifier);

                                List<Operation> sub_operation_stack{};
                                List<Expression*> sub_expression_stack{};

                                append(&sub_expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, true));

                                if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                    return { false };
                                }

                                append(expression_stack, right_expression);
                            }
                        } break;

                        default: {
                            expect(expression, parse_expression(context));

                            if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                return { false };
                            }

                            append(expression_stack, expression);
                        } break;
                    }
                } break;

                case TokenType::OpenCurlyBracket: {
                    context->next_token_index += 1;

                    auto first_range = token_range(*context, token);

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::CloseCurlyBracket: {
                            context->next_token_index += 1;

                            append(expression_stack, (Expression*)new ArrayLiteral {
                                span_range(first_range, token_range(*context, token)),
                                {}
                            });
                        } break;

                        case TokenType::Identifier: {
                            context->next_token_index += 1;

                            auto identifier = identifier_from_token(*context, token);

                            expect(token, next_token(*context));

                            switch(token.type) {
                                case TokenType::Equals: {
                                    context->next_token_index += 1;

                                    expect(first_expression, parse_expression(context));

                                    List<StructLiteral::Member> members{};

                                    append(&members, {
                                        identifier,
                                        first_expression
                                    });

                                    expect(token, next_token(*context));

                                    FileRange last_range;
                                    switch(token.type) {
                                        case TokenType::Comma: {
                                            context->next_token_index += 1;

                                            while(true) {
                                                expect(identifier, expect_identifier(context));

                                                if(!expect_basic_token(context, TokenType::Equals)) {
                                                    return { false };
                                                }

                                                expect(expression, parse_expression(context));

                                                append(&members, {
                                                    identifier,
                                                    expression
                                                });

                                                expect(token, next_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        context->next_token_index += 1;
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        context->next_token_index += 1;

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
                                            context->next_token_index += 1;

                                            last_range = token_range(*context, token);
                                        } break;

                                        default: {
                                            error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                            return { false };
                                        } break;
                                    }

                                    append(expression_stack, (Expression*)new StructLiteral {
                                        span_range(first_range, last_range),
                                        to_array(members)
                                    });
                                } break;

                                case TokenType::CloseCurlyBracket: {
                                    context->next_token_index += 1;

                                    auto first_element = named_reference_from_identifier(identifier);

                                    append(expression_stack, (Expression*)new ArrayLiteral {
                                        span_range(first_range, token_range(*context, token)),
                                        {
                                            1,
                                            heapify(first_element)
                                        }
                                    });
                                } break;

                                default: {
                                    auto sub_expression = named_reference_from_identifier(identifier);

                                    List<Operation> sub_operation_stack{};
                                    List<Expression*> sub_expression_stack{};

                                    append(&sub_expression_stack, sub_expression);

                                    expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, true));

                                    List<Expression*> elements{};

                                    append(&elements, right_expression);

                                    expect(token, next_token(*context));

                                    FileRange last_range;
                                    switch(token.type) {
                                        case TokenType::Comma: {
                                            context->next_token_index += 1;

                                            while(true) {
                                                expect(expression, parse_expression(context));

                                                append(&elements, expression);

                                                expect(token, next_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        context->next_token_index += 1;
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        context->next_token_index += 1;

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
                                            context->next_token_index += 1;

                                            last_range = token_range(*context, token);
                                        } break;
                                    }

                                    append(expression_stack, (Expression*)new ArrayLiteral {
                                        span_range(first_range, last_range),
                                        to_array(elements)
                                    });
                                } break;
                            }
                        } break;

                        default: {
                            expect(first_expression, parse_expression(context));

                            List<Expression*> elements{};

                            append(&elements, first_expression);

                            expect(token, next_token(*context));

                            FileRange last_range;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    context->next_token_index += 1;

                                    while(true) {
                                        expect(expression, parse_expression(context));

                                        append(&elements, expression);

                                        expect(token, next_token(*context));

                                        auto done = false;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                context->next_token_index += 1;
                                            } break;

                                            case TokenType::CloseCurlyBracket: {
                                                context->next_token_index += 1;

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
                                    context->next_token_index += 1;

                                    last_range = token_range(*context, token);
                                } break;
                            }

                            append(expression_stack, (Expression*)new ArrayLiteral {
                                span_range(first_range, last_range),
                                to_array(elements)
                            });
                        } break;
                    }
                } break;

                case TokenType::OpenSquareBracket: {
                    context->next_token_index += 1;

                    expect(token, next_token(*context));

                    Expression *index;
                    FileRange last_range;
                    if(token.type == TokenType::CloseSquareBracket) {
                        context->next_token_index += 1;

                        index = nullptr;
                        last_range = token_range(*context, token);
                    } else {
                        expect(expression, parse_expression(context));

                        expect(range, expect_basic_token_with_range(context, TokenType::CloseSquareBracket));

                        index = expression;
                        last_range = range;
                    }

                    Operation operation;
                    operation.type = OperationType::ArrayType;
                    operation.range = token_range(*context, token);
                    operation.array_type = index;

                    append(operation_stack, operation);

                    continue;
                }

                default: {
                    error(*context, "Expected an expression. Got '%s'", get_token_text(token));

                    return { false };
                } break;
            }
        }

        if(context->next_token_index == context->tokens.count) {
            break;
        }

        auto token = context->tokens[context->next_token_index];

        auto first_range = token_range(*context, token);

        Operation operation;

        // Parse left-recursive expressions (e.g. binary operators) after parsing all adjacent non-left-recursive expressions
        auto done = false;
        switch(token.type) {
            case TokenType::Dot: {
                context->next_token_index += 1;

                expect(identifier, expect_identifier(context));

                operation.type = OperationType::MemberReference;
                operation.range = first_range;
                operation.member_reference = identifier;

                expect_left_recursive = true;
            } break;

            case TokenType::Plus: {
                context->next_token_index += 1;

                operation.type = OperationType::Addition;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Dash: {
                context->next_token_index += 1;

                operation.type = OperationType::Subtraction;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Asterisk: {
                context->next_token_index += 1;

                operation.type = OperationType::Multiplication;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::ForwardSlash: {
                context->next_token_index += 1;

                operation.type = OperationType::Division;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Percent: {
                context->next_token_index += 1;

                operation.type = OperationType::Modulo;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Ampersand: {
                context->next_token_index += 1;

                operation.type = OperationType::BitwiseAnd;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoubleAmpersand: {
                context->next_token_index += 1;

                operation.type = OperationType::BooleanAnd;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Pipe: {
                context->next_token_index += 1;

                operation.type = OperationType::BitwiseOr;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoublePipe: {
                context->next_token_index += 1;

                operation.type = OperationType::BooleanOr;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoubleEquals: {
                context->next_token_index += 1;

                operation.type = OperationType::Equal;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::BangEquals: {
                context->next_token_index += 1;

                operation.type = OperationType::NotEqual;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::LeftArrow: {
                context->next_token_index += 1;

                operation.type = OperationType::LessThan;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::RightArrow: {
                context->next_token_index += 1;

                operation.type = OperationType::GreaterThan;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::OpenRoundBracket: {
                context->next_token_index += 1;

                List<Expression*> parameters{};

                expect(token, next_token(*context));

                FileRange last_range;
                if(token.type == TokenType::CloseRoundBracket) {
                    context->next_token_index += 1;

                    last_range = token_range(*context, token);
                } else {
                    while(true) {
                        expect(expression, parse_expression(context));

                        append(&parameters, expression);

                        expect(token, next_token(*context));

                        auto done = false;
                        switch(token.type) {
                            case TokenType::Comma: {
                                context->next_token_index += 1;
                            } break;

                            case TokenType::CloseRoundBracket: {
                                context->next_token_index += 1;

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

                operation.type = OperationType::FunctionCall;
                operation.range = span_range(first_range, last_range);
                operation.function_call = to_array(parameters);

                expect_left_recursive = true;
            } break;

            case TokenType::OpenSquareBracket: {
                context->next_token_index += 1;

                expect(token, next_token(*context));

                if(token.type == TokenType::CloseSquareBracket) {
                    context->next_token_index += 1;

                    operation.type = OperationType::ArrayType;
                    operation.range = span_range(first_range, token_range(*context, token));

                    expect_left_recursive = true;
                } else {
                    expect(expression, parse_expression(context));

                    expect(last_range, expect_basic_token_with_range(context, TokenType::CloseSquareBracket));

                    operation.type = OperationType::IndexReference;
                    operation.range = span_range(first_range, last_range);
                    operation.index_reference = expression;

                    expect_left_recursive = true;
                }
            } break;

            case TokenType::Identifier: {
                if(strcmp(token.identifier, "as") == 0) {
                    context->next_token_index += 1;

                    operation.type = OperationType::Cast;
                    operation.range = token_range(*context, token);

                    expect_left_recursive = false;
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

        // Meat of the precedence sorting
        while(true) {
            if(operation_stack->count == 0) {
                append(operation_stack, operation);

                break;
            } else {
                auto last_operation = (*operation_stack)[operation_stack->count - 1];

                if(operation_precedences[(int)operation.type] < operation_precedences[(int)last_operation.type]) {
                    append(operation_stack, operation);

                    break;
                } else {
                    apply_operation(expression_stack, take_last(operation_stack));
                }
            }
        }
    }

    // Apply the remaining operations
    while(operation_stack->count != 0) {
        apply_operation(expression_stack, take_last(operation_stack));
    }

    return {
        true,
        (*expression_stack)[0]
    };
}

static Result<Expression*> parse_expression(Context *context) {
    List<Operation> operation_stack{};

    List<Expression*> expression_stack{};

    return parse_right_expressions(context, &operation_stack, &expression_stack, false);
}

static Result<Statement*> parse_statement(Context *context);

static Result<Statement*> continue_parsing_function_declaration_or_function_type_constant(Context *context, Identifier name, Array<FunctionParameter> parameters, FileRange parameters_range) {
    auto last_range = parameters_range;

    expect(pre_token, next_token(*context));

    Expression *return_type;
    switch(pre_token.type) {
        case TokenType::Arrow: {
            context->next_token_index += 1;

            last_range = token_range(*context, pre_token);

            expect(expression, parse_expression(context));

            return_type = expression;
        } break;

        default: {
            return_type = nullptr;
        } break;
    }

    expect(token, next_token(*context));

    switch(token.type) {
        case TokenType::OpenCurlyBracket: {
            context->next_token_index += 1;

            List<Statement*> statements{};

            while(true) {
                expect(token, next_token(*context));

                if(token.type == TokenType::CloseCurlyBracket) {
                    context->next_token_index += 1;

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
                to_array(statements)
            };

            return {
                true,
                function_declaration
            };
        } break;

        case TokenType::Semicolon: {
            context->next_token_index += 1;

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
            if(strcmp(token.identifier, "extern") != 0) {
                error(*context, "Expected '->', '{', ';' or 'extern', got '%s'", token.identifier);

                return { false };
            }

            context->next_token_index += 1;

            List<const char *> libraries{};

            expect(token, next_token(*context));

            FileRange last_range;
            switch(token.type) {
                case TokenType::Semicolon: {
                    context->next_token_index += 1;

                    last_range = token_range(*context, token);
                } break;

                case TokenType::String: {
                    while(true) {
                        expect(string, expect_string(context));

                        auto library = allocate<char>(string.count + 1);
                        memcpy(library, string.elements, string.count);
                        library[string.count] = 0;

                        append(&libraries, (const char *)library);

                        expect(token, next_token(*context));

                        auto done = false;
                        switch(token.type) {
                            case TokenType::Comma: {
                                context->next_token_index += 1;
                            } break;

                            case TokenType::Semicolon: {
                                context->next_token_index += 1;

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
            if(token.type != TokenType::String) {
                error(*context, "Expected a string, got '%s'", get_token_text(token));

                return { false };
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
        } break;

        default: {
            error(*context, "Expected '->', '{', ';' or 'extern', got '%s'", get_token_text(token));

            return { false };
        } break;
    }
}

static Result<Statement*> parse_statement(Context *context) {
    expect(token, next_token(*context));

    auto first_range = token_range(*context, token);

    switch(token.type) {
        case TokenType::Hash: {
            context->next_token_index += 1;

            expect(token, next_token(*context));

            if(token.type != TokenType::Identifier) {
                error(*context, "Expected 'import', got '%s'", get_token_text(token));

                return { false };
            }

            context->next_token_index += 1;

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
            context->next_token_index += 1;

            if(strcmp(token.identifier, "if") == 0) {
                expect(expression, parse_expression(context));

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, next_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        context->next_token_index += 1;

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
                    expect(token, next_token(*context));

                    if(token.type == TokenType::Identifier && strcmp(token.identifier, "else") == 0) {
                        context->next_token_index += 1;

                        expect(token, next_token(*context));

                        switch(token.type) {
                            case TokenType::OpenCurlyBracket: {
                                context->next_token_index += 1;

                                while(true) {
                                    expect(token, next_token(*context));

                                    if(token.type == TokenType::CloseCurlyBracket) {
                                        context->next_token_index += 1;

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

                                context->next_token_index += 1;

                                expect(expression, parse_expression(context));

                                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                                    return { false };
                                }
                                
                                List<Statement*> statements{};

                                while(true) {
                                    expect(token, next_token(*context));

                                    if(token.type == TokenType::CloseCurlyBracket) {
                                        context->next_token_index += 1;

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
                expect(expression, parse_expression(context));

                if(!expect_basic_token(context, TokenType::OpenCurlyBracket)) {
                    return { false };
                }

                List<Statement*> statements{};

                FileRange last_range;
                while(true) {
                    expect(token, next_token(*context));

                    if(token.type == TokenType::CloseCurlyBracket) {
                        context->next_token_index += 1;

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
            } else if(strcmp(token.identifier, "return") == 0) {
                expect(token, next_token(*context));

                Expression *value;
                FileRange last_range;
                if(token.type == TokenType::Semicolon) {
                    context->next_token_index += 1;

                    last_range = token_range(*context, token);

                    value = nullptr;
                } else {
                    expect(expression, parse_expression(context));

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
            } else if(strcmp(token.identifier, "using") == 0) {
                expect(expression, parse_expression(context));

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

                expect(token, next_token(*context));

                if(token.type == TokenType::Colon) {
                    context->next_token_index += 1;

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::Colon: {
                            context->next_token_index += 1;

                            expect(token, next_token(*context));

                            switch(token.type) {
                                case TokenType::OpenRoundBracket: {
                                    context->next_token_index += 1;

                                    auto parameters_first_range = token_range(*context, token);

                                    expect(token, next_token(*context));

                                    if(token.type == TokenType::Dollar) {
                                        context->next_token_index += 1;

                                        expect(name, expect_identifier(context));

                                        List<FunctionParameter> parameters{};

                                        expect(parameter, parse_function_parameter_second_half(context, name, true));

                                        append(&parameters, parameter);

                                        expect(token, next_token(*context));

                                        FileRange last_range;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                context->next_token_index += 1;

                                                while(true) {
                                                    expect(pre_token, next_token(*context));

                                                    bool is_constant;
                                                    if(pre_token.type == TokenType::Dollar) {
                                                        context->next_token_index += 1;

                                                        is_constant = true;
                                                    } else {
                                                        is_constant = false;
                                                    }

                                                    expect(identifier, expect_identifier(context));

                                                    expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                    append(&parameters, parameter);

                                                    expect(token, next_token(*context));

                                                    auto done = false;
                                                    switch(token.type) {
                                                        case TokenType::Comma: {
                                                            context->next_token_index += 1;
                                                        } break;

                                                        case TokenType::CloseRoundBracket: {
                                                            context->next_token_index += 1;

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
                                                context->next_token_index += 1;

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
                                        context->next_token_index += 1;

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
                                        context->next_token_index += 1;

                                        auto first_identifier = identifier_from_token(*context, token);

                                        expect(token, next_token(*context));

                                        if(token.type == TokenType::Colon) {
                                            List<FunctionParameter> parameters{};

                                            expect(parameter, parse_function_parameter_second_half(context, first_identifier, false));

                                            append(&parameters, parameter);

                                            expect(token, next_token(*context));

                                            FileRange last_range;
                                            switch(token.type) {
                                                case TokenType::Comma: {
                                                    context->next_token_index += 1;

                                                    while(true) {
                                                        expect(pre_token, next_token(*context));

                                                        bool is_constant;
                                                        if(pre_token.type == TokenType::Dollar) {
                                                            context->next_token_index += 1;

                                                            is_constant = true;
                                                        } else {
                                                            is_constant = false;
                                                        }

                                                        expect(identifier, expect_identifier(context));

                                                        expect(parameter, parse_function_parameter_second_half(context, identifier, is_constant));

                                                        append(&parameters, parameter);

                                                        expect(token, next_token(*context));

                                                        auto done = false;
                                                        switch(token.type) {
                                                            case TokenType::Comma: {
                                                                context->next_token_index += 1;
                                                            } break;

                                                            case TokenType::CloseRoundBracket: {
                                                                context->next_token_index += 1;

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
                                                    context->next_token_index += 1;

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

                                            List<Operation> operation_stack{};
                                            List<Expression*> expression_stack{};

                                            append(&expression_stack, expression);

                                            expect(right_expreession, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                                            if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                                return { false };
                                            }

                                            append(&expression_stack, right_expreession);

                                            expect(outer_right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

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
                                        expect(expression, parse_expression(context));

                                        if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                            return { false };
                                        }

                                        List<Operation> operation_stack{};
                                        List<Expression*> expression_stack{};

                                        append(&expression_stack, expression);

                                        expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

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
                                    context->next_token_index += 1;

                                    if(strcmp(token.identifier, "struct") == 0) {
                                        expect(maybe_union_token, next_token(*context));

                                        auto is_union = false;
                                        if(maybe_union_token.type == TokenType::Identifier && strcmp(maybe_union_token.identifier, "union") == 0) {
                                            context->next_token_index += 1;

                                            is_union = true;
                                        }

                                        expect(maybe_parameter_token, next_token(*context));

                                        List<StructDefinition::Parameter> parameters{};

                                        if(maybe_parameter_token.type == TokenType::OpenRoundBracket) {
                                            context->next_token_index += 1;

                                            expect(token, next_token(*context));

                                            if(token.type == TokenType::CloseRoundBracket) {
                                                context->next_token_index += 1;
                                            } else {
                                                while(true) {
                                                    expect(name, expect_identifier(context));

                                                    if(!expect_basic_token(context, TokenType::Colon)) {
                                                        return { false };
                                                    }

                                                    expect(type, parse_expression(context));

                                                    append(&parameters, {
                                                        name,
                                                        type
                                                    });

                                                    expect(token, next_token(*context));

                                                    auto done = false;
                                                    switch(token.type) {
                                                        case TokenType::Comma: {
                                                            context->next_token_index += 1;
                                                        } break;

                                                        case TokenType::CloseRoundBracket: {
                                                            context->next_token_index += 1;

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

                                        expect(token, next_token(*context));

                                        FileRange last_range;
                                        if(token.type == TokenType::CloseCurlyBracket) {
                                            context->next_token_index += 1;

                                            last_range = token_range(*context, token);
                                        } else {
                                            while(true) {
                                                expect(identifier, expect_identifier(context));

                                                if(!expect_basic_token(context, TokenType::Colon)) {
                                                    return { false };
                                                }

                                                expect(expression, parse_expression(context));

                                                append(&members, {
                                                    identifier,
                                                    expression
                                                });

                                                expect(token, next_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        context->next_token_index += 1;
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        context->next_token_index += 1;

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

                                        List<Operation> operation_stack{};
                                        List<Expression*> expression_stack{};

                                        append(&expression_stack, expression);

                                        expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

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
                                    expect(expression, parse_expression(context));

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

                        case TokenType::Equals: {
                            context->next_token_index += 1;

                            expect(expression, parse_expression(context));

                            expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                            auto variable_declaration = new VariableDeclaration {
                                span_range(first_range, last_range),
                                identifier,
                                nullptr,
                                expression
                            };

                            return {
                                true,
                                variable_declaration
                            };
                        } break;

                        default: {
                            expect(expression, parse_expression(context));

                            expect(token, next_token(*context));

                            switch(token.type) {
                                case TokenType::Semicolon: {
                                    context->next_token_index += 1;

                                    auto variable_declaration = new VariableDeclaration {
                                        span_range(first_range, token_range(*context, token)),
                                        identifier,
                                        expression,
                                        nullptr
                                    };

                                    return {
                                        true,
                                        variable_declaration
                                    };
                                } break;

                                case TokenType::Equals: {
                                    context->next_token_index += 1;

                                    expect(value_expression, parse_expression(context));

                                    expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                                    auto variable_declaration = new VariableDeclaration {
                                        span_range(first_range, last_range),
                                        identifier,
                                        expression,
                                        value_expression
                                    };

                                    return {
                                        true,
                                        variable_declaration
                                    };
                                } break;

                                default: {
                                    error(*context, "Expected '=' or ';', got '%s'", get_token_text(token));

                                    return { false };
                                } break;
                            }
                        } break;
                    }
                } else {
                    auto expression = named_reference_from_identifier(identifier);

                    List<Operation> operation_stack{};
                    List<Expression*> expression_stack{};

                    append(&expression_stack, expression);

                    expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::Semicolon: {
                            context->next_token_index += 1;

                            auto expression_statement = new ExpressionStatement {
                                span_range(first_range, token_range(*context, token)),
                                right_expression
                            };

                            return {
                                true,
                                expression_statement
                            };
                        } break;

                        case TokenType::Equals: {
                            context->next_token_index += 1;

                            expect(expression, parse_expression(context));

                            expect(last_range, expect_basic_token_with_range(context, TokenType::Semicolon));

                            auto assignment = new Assignment {
                                span_range(first_range, last_range),
                                right_expression,
                                expression
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
                }
            }
        } break;

        default: {
            expect(expression, parse_expression(context));

            expect(token, next_token(*context));

            switch(token.type) {
                case TokenType::Semicolon: {
                    context->next_token_index += 1;

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
                    context->next_token_index += 1;

                    expect(value_expression, parse_expression(context));

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

void set_statement_parents(Statement *statement) {
    if(auto function_declaration = dynamic_cast<FunctionDeclaration*>(statement)) {
        if(!function_declaration->is_external) {
            for(auto child : function_declaration->statements) {
                child->parent = statement;

                set_statement_parents(child);
            }
        }
    } else if(auto if_statement = dynamic_cast<IfStatement*>(statement)) {
        for(auto child : if_statement->statements) {
            child->parent = statement;

            set_statement_parents(child);
        }

        for(auto else_if : if_statement->else_ifs) {
            for(auto child : else_if.statements) {
                child->parent = statement;

                set_statement_parents(child);
            }
        }

        for(auto child : if_statement->else_statements) {
            child->parent = statement;

            set_statement_parents(child);
        }
    } else if(auto while_loop = dynamic_cast<WhileLoop*>(statement)) {
        for(auto child : while_loop->statements) {
            child->parent = statement;

            set_statement_parents(child);
        }
    }
}

Result<Array<Statement*>> parse_tokens(const char *path, Array<Token> tokens) {
    Context context {
        path,
        tokens
    };

    List<Statement*> statements{};

    while(context.next_token_index < tokens.count) {
        expect(statement, parse_statement(&context));

        append(&statements, statement);
    }

    for(auto statement : statements) {
        statement->parent = nullptr;

        set_statement_parents(statement);
    }

    return {
        true,
        to_array(statements)
    };
}