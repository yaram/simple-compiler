#include "parser.h"
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include "list.h"
#include "util.h"
#include "path.h"

struct Context {
    const char *source_file_path;

    FILE *source_file;

    unsigned int line;
    unsigned int character;

    List<const char*> *remaining_files;
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
                auto next_character = fgetc(context->source_file);

                if(next_character == '/') {
                    context->character += 2;

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
                } else if(next_character == '*') {
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
                    ungetc(next_character, context->source_file);
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

static Identifier parse_identifier(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto last_line = context->line;
    auto last_character = context->character;

    List<char> buffer{};

    while(true) {
        auto character = fgetc(context->source_file);

        if(isalnum(character) || character == '_') {
            last_line = context->line;
            last_character = context->character;

            context->character += 1;

            append(&buffer, (char)character);
        } else {
            ungetc(character, context->source_file);

            break;
        }
    }

    append(&buffer, '\0');

    return {
        buffer.elements,
        {
            context->source_file_path,
            first_line,
            first_character,
            last_line,
            last_character
        }
    };
}

static Result<Identifier> expect_identifier(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;
    
    auto last_line = context->line;
    auto last_character = context->character;

    auto character = fgetc(context->source_file);

    if(isalpha(character) || character == '_') {
        context->character += 1;

        List<char> buffer{};

        append(&buffer, (char)character);

        while(true) {
            auto character = fgetc(context->source_file);

            if(isalnum(character) || character == '_') {
                last_line = context->line;
                last_character = context->character;

                context->character += 1;

                append(&buffer, (char)character);
            } else {
                ungetc(character, context->source_file);

                break;
            }
        }

        append(&buffer, '\0');

        return {
            true,
            {
                buffer.elements,
                {
                    context->source_file_path,
                    first_line,
                    first_character,
                    last_line,
                    last_character
                }
            }
        };
    } else if(character == EOF) {
        error(*context, "Unexpected End of File");

        return { false };
    } else {
        error(*context, "Expected a-z, A-Z or '_'. Got '%c'", character);

        return { false };
    }
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

enum struct OperationType {
    FunctionCall,
    MemberReference,
    IndexReference,
    ArrayType,

    Pointer,
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

    BitwiseAnd,

    BitwiseOr,

    BooleanAnd,

    BooleanOr
};

unsigned int operation_precedences[] = {
    1, 1, 1, 1,
    2, 2, 2,
    3,
    4, 4, 4,
    5, 5,
    8, 8,
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

        Expression index_reference;

        Array<Expression> function_call;
    };
};

static void apply_operation(List<Expression> *expression_stack, Operation operation) {
    Expression expression;
    expression.range = operation.range;

    switch(operation.type) {
        case OperationType::Addition:
        case OperationType::Subtraction:
        case OperationType::Multiplication:
        case OperationType::Division:
        case OperationType::Modulo:
        case OperationType::Equal:
        case OperationType::NotEqual:
        case OperationType::BitwiseAnd:
        case OperationType::BitwiseOr:
        case OperationType::BooleanAnd:
        case OperationType::BooleanOr: {
            expression.type = ExpressionType::BinaryOperation;

            expression.binary_operation.right = heapify(take_last(expression_stack));
            expression.binary_operation.left = heapify(take_last(expression_stack));

            switch(operation.type) {
                case OperationType::Addition: {
                    expression.binary_operation.binary_operator = BinaryOperator::Addition;
                } break;

                case OperationType::Subtraction: {
                    expression.binary_operation.binary_operator = BinaryOperator::Subtraction;
                } break;

                case OperationType::Multiplication: {
                    expression.binary_operation.binary_operator = BinaryOperator::Multiplication;
                } break;

                case OperationType::Division: {
                    expression.binary_operation.binary_operator = BinaryOperator::Division;
                } break;

                case OperationType::Modulo: {
                    expression.binary_operation.binary_operator = BinaryOperator::Modulo;
                } break;

                case OperationType::Equal: {
                    expression.binary_operation.binary_operator = BinaryOperator::Equal;
                } break;

                case OperationType::NotEqual: {
                    expression.binary_operation.binary_operator = BinaryOperator::NotEqual;
                } break;

                case OperationType::BitwiseAnd: {
                    expression.binary_operation.binary_operator = BinaryOperator::BitwiseAnd;
                } break;

                case OperationType::BitwiseOr: {
                    expression.binary_operation.binary_operator = BinaryOperator::BitwiseOr;
                } break;

                case OperationType::BooleanAnd: {
                    expression.binary_operation.binary_operator = BinaryOperator::BooleanAnd;
                } break;

                case OperationType::BooleanOr: {
                    expression.binary_operation.binary_operator = BinaryOperator::BooleanOr;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case OperationType::MemberReference: {
            expression.type = ExpressionType::MemberReference;

            expression.member_reference = {
                heapify(take_last(expression_stack)),
                operation.member_reference
            };
        } break;

        case OperationType::IndexReference: {
            expression.type = ExpressionType::IndexReference;

            expression.index_reference = {
                heapify(take_last(expression_stack)),
                heapify(operation.index_reference)
            };
        } break;

        case OperationType::FunctionCall: {
            expression.type = ExpressionType::FunctionCall;

            expression.function_call = {
                heapify(take_last(expression_stack)),
                operation.function_call
            };
        } break;

        case OperationType::Pointer: {
            expression.type = ExpressionType::UnaryOperation;

            expression.unary_operation = {
                UnaryOperator::Pointer,
                heapify(take_last(expression_stack))
            };
        } break;

        case OperationType::BooleanInvert: {
            expression.type = ExpressionType::UnaryOperation;

            expression.unary_operation = {
                UnaryOperator::BooleanInvert,
                heapify(take_last(expression_stack))
            };
        } break;

        case OperationType::Negation: {
            expression.type = ExpressionType::UnaryOperation;

            expression.unary_operation = {
                UnaryOperator::Negation,
                heapify(take_last(expression_stack))
            };
        } break;

        case OperationType::Cast: {
            expression.type = ExpressionType::Cast;

            auto type = heapify(take_last(expression_stack));
            auto expression_pointer = heapify(take_last(expression_stack));

            expression.cast = {
                expression_pointer,
                type
            };
        } break;

        case OperationType::ArrayType: {
            expression.type = ExpressionType::ArrayType;

            expression.array_type = heapify(take_last(expression_stack));
        } break;

        default: {
            abort();
        } break;
    }

    append(expression_stack, expression);
}

struct String {
    Array<char> text;

    FileRange range;
};

static Result<String> parse_string(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    if(!expect_character(context, '"')) {
        return { false };
    }

    List<char> buffer{};

    unsigned int last_line;
    unsigned int last_character;

    while(true) {
        last_line = context->line;
        last_character = context->character;

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

    return {
        true,
        {
            to_array(buffer),
            {
                context->source_file_path,
                first_line,
                first_character,
                last_line,
                last_character
            }
        }
    };
}

static Result<Expression> parse_expression(Context *context);

static Result<FunctionParameter> parse_function_parameter(Context *context) {
    expect(identifier, expect_identifier(context));

    skip_whitespace(context);

    if(!expect_character(context, ':')) {
        return { false };
    }

    skip_whitespace(context);

    auto character = fgetc(context->source_file);

    FunctionParameter parameter = {
        identifier
    };

    switch(character) {
        case '$': {
            context->character += 1;

            skip_whitespace(context);

            expect(name, expect_identifier(context));

            parameter.is_polymorphic_determiner = true;
            parameter.polymorphic_determiner = name;
        } break;

        case EOF: {
            error(*context, "Unexpected End of File");

            return { false };
        } break;

        default: {
            ungetc(character, context->source_file);

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

static Result<Expression> parse_right_expressions(Context *context, List<Operation> *operation_stack, List<Expression> *expression_stack, bool start_with_non_left_recursive) {
    auto expect_non_left_recursive = start_with_non_left_recursive;

    while(true) {
        if(expect_non_left_recursive) {
            skip_whitespace(context);

            auto first_line = context->line;
            auto first_character = context->character;

            auto character = fgetc(context->source_file);

            // Parse non-left-recursive expressions first
            if(isalpha(character) || character == '_') {
                ungetc(character, context->source_file);

                auto identifier = parse_identifier(context);

                Expression expression;
                expression.type = ExpressionType::NamedReference;
                expression.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    identifier.range.end_line,
                    identifier.range.end_character
                };
                expression.named_reference = identifier;

                append(expression_stack, expression);
            } else if(isdigit(character)){
                context->character += 1;

                auto radix = 10;

                if(character == '0') {
                    auto character = fgetc(context->source_file);

                    if(character == 'x' || character == 'X') {
                        context->character += 1;

                        radix = 16;
                    } else if(character == 'b' || character == 'B') {
                        context->character += 1;

                        radix = 2;
                    } else if(character == 'o' || character == 'O') {
                        context->character += 1;

                        radix = 8;
                    } else {
                        ungetc(character, context->source_file);
                    }
                }

                unsigned int end_line;
                unsigned int end_character;

                List<char> buffer{};

                append(&buffer, (char)character);

                while(true) {
                    end_character = context->character;
                    end_line = context->line;

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

                char *end;

                auto value = strtoll(buffer.elements, &end, radix);

                if(value == LLONG_MAX || value == LLONG_MIN) {
                    if(errno == ERANGE) {
                        error(*context, "Integer literal out of range");

                        return { false };
                    }
                } else if(value == 0) {
                    if(end != buffer.elements + buffer.count - 1) {
                        error(*context, "Invalid integer literal");

                        return { false };
                    }
                }

                Expression expression;
                expression.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    end_line,
                    end_character
                };
                expression.type = ExpressionType::IntegerLiteral;
                expression.integer_literal = value;

                append(expression_stack, expression);
            } else if(character == '*') {
                context->character += 1;

                Operation operation;
                operation.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    first_line,
                    first_character
                };
                operation.type = OperationType::Pointer;

                append(operation_stack, operation);

                continue;
            } else if(character == '!') {
                context->character += 1;

                Operation operation;
                operation.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    first_line,
                    first_character
                };
                operation.type = OperationType::BooleanInvert;

                append(operation_stack, operation);

                continue;
            } else if(character == '-') {
                context->character += 1;

                Operation operation;
                operation.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    first_line,
                    first_character
                };
                operation.type = OperationType::Negation;

                append(operation_stack, operation);

                continue;
            } else if(character == '"') {
                ungetc(character, context->source_file);

                expect(string, parse_string(context));

                Expression expression;
                expression.range = string.range;
                expression.type = ExpressionType::StringLiteral;
                expression.string_literal = string.text;

                append(expression_stack, expression);
            } else if(character == '(') {
                context->character += 1;

                skip_whitespace(context);

                auto character = fgetc(context->source_file);

                if(isalpha(character) || character == '_') {
                    ungetc(character, context->source_file);

                    auto identifier = parse_identifier(context);

                    skip_whitespace(context);

                    auto character = fgetc(context->source_file);

                    auto last_line = context->line;
                    auto last_character = context->character;

                    if(character == ')') {
                        context->character += 1;

                        Expression expression;
                        expression.type = ExpressionType::NamedReference;
                        expression.range = {
                            context->source_file_path,
                            first_line,
                            first_character,
                            last_line,
                            last_character
                        };
                        expression.named_reference = identifier;

                        append(expression_stack, expression);
                    } else if(character == ':') {
                        context->character += 1;

                        skip_whitespace(context);

                        auto character = fgetc(context->source_file);

                        FunctionParameter first_parameter = {
                            identifier
                        };

                        switch(character) {
                            context->character += 1;

                            case '$': {
                                skip_whitespace(context);

                                expect(name, expect_identifier(context));

                                first_parameter.is_polymorphic_determiner = true;
                                first_parameter.polymorphic_determiner = name;
                            } break;

                            case EOF: {
                                error(*context, "Unexpected End of File");

                                return { false };
                            } break;

                            default: {
                                ungetc(character, context->source_file);

                                expect(expression, parse_expression(context));

                                first_parameter.is_polymorphic_determiner = false;
                                first_parameter.type = expression;
                            } break;
                        }

                        skip_whitespace(context);

                        List<FunctionParameter> parameters{};

                        append(&parameters, first_parameter);

                        while(true) {
                            auto character = fgetc(context->source_file);

                            if(character == ',') {
                                context->character += 1;

                                skip_whitespace(context);
                            } else if(character == ')') {
                                last_line = context->line;
                                last_character = context->character;

                                context->character += 1;

                                break;
                            } else if(character == EOF) {
                                error(*context, "Unexpected End of File");

                                return { false };
                            } else {
                                error(*context, "Expected ',' or ')', got '%c'", character);

                                return { false };
                            }

                            expect(parameter, parse_function_parameter(context));

                            append(&parameters, parameter);
                        }

                        skip_whitespace(context);

                        character = fgetc(context->source_file);

                        Expression *return_type;
                        if(character == '-') {
                            context->character += 1;

                            if(!expect_character(context, '>')) {
                                return { false };
                            }

                            skip_whitespace(context);

                            expect(expression, parse_expression(context));

                            last_line = expression.range.end_line;
                            last_character = expression.range.end_character;

                            return_type = heapify(expression);
                        } else {
                            ungetc(character, context->source_file);

                            return_type = nullptr;
                        }

                        Expression expression;
                        expression.type = ExpressionType::FunctionType;
                        expression.range = {
                            context->source_file_path,
                            first_line,
                            first_character,
                            last_line,
                            last_character
                        };
                        expression.function_type = {
                            to_array(parameters),
                            return_type
                        };

                        append(expression_stack, expression);
                    } else {
                        ungetc(character, context->source_file);

                        Expression expression;
                        expression.type = ExpressionType::NamedReference;
                        expression.range = identifier.range;
                        expression.named_reference = identifier;

                        List<Operation> sub_operation_stack{};

                        List<Expression> sub_expression_stack{};

                        append(&sub_expression_stack, expression);

                        expect(right_expresion, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, false));

                        skip_whitespace(context);

                        if(!expect_character(context, ')')) {
                            return { false };
                        }

                        append(expression_stack, right_expresion);
                    }
                } else if(character == ')') {
                    auto last_line = context->line;
                    auto last_character = context->character;

                    context->character += 1;

                    skip_whitespace(context);

                    auto character = fgetc(context->source_file);

                    Expression *return_type;
                    if(character == '-') {
                        context->character += 1;

                        if(!expect_character(context, '>')) {
                            return { false };
                        }

                        skip_whitespace(context);

                        expect(expression, parse_expression(context));

                        last_line = expression.range.end_line;
                        last_character = expression.range.end_character;

                        return_type = heapify(expression);
                    } else {
                        ungetc(character, context->source_file);

                        return_type = nullptr;
                    }

                    Expression expression;
                    expression.type = ExpressionType::FunctionType;
                    expression.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };
                    expression.function_type = {
                        {},
                        return_type
                    };

                    append(expression_stack, expression);
                } else {
                    ungetc(character, context->source_file);

                    expect(expression, parse_expression(context));

                    skip_whitespace(context);

                    if(!expect_character(context, ')')) {
                        return { false };
                    }

                    append(expression_stack, expression);
                }
            } else if(character == '{') {
                context->character += 1;

                skip_whitespace(context);

                List<Expression> elements{};

                auto last_line = context->line;
                auto last_character = context->character;

                auto character = fgetc(context->source_file);

                if(character == '}') {
                    context->character += 1;
                } else {
                    ungetc(character, context->source_file);

                    while(true) {
                        expect(expression, parse_expression(context));

                        append(&elements, expression);

                        skip_whitespace(context);

                        last_line = context->line;
                        last_character = context->character;

                        auto character = fgetc(context->source_file);

                        if(character == ',') {
                            context->character += 1;

                            skip_whitespace(context);
                        } else if(character == '}') {
                            context->character += 1;

                            break;
                        } else if(character == EOF) {
                            error(*context, "Unexpected End of File");

                            return { false };
                        } else {
                            error(*context, "Expected ',' or ']'. Got '%c'", character);

                            return { false };
                        }
                    }
                }

                Expression expression;
                expression.type = ExpressionType::ArrayLiteral;
                expression.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    last_line,
                    last_character
                };
                expression.array_literal = to_array(elements);

                append(expression_stack, expression);
            } else if(character == EOF) {
                error(*context, "Unexpected End of File");

                return { false };
            } else {
                error(*context, "Expected a-z, A-Z, 0-9, '*', '!', '(', '{' or '\"'. Got '%c'", character);

                return { false };
            }
        }

        skip_whitespace(context);

        auto first_line = context->line;
        auto first_character = context->character;

        auto character = fgetc(context->source_file);

        Operation operation;
        operation.range = {
            context->source_file_path,
            first_line,
            first_character,
            first_line,
            first_character
        };

        // Parse left-recursive expressions (e.g. binary operators) after parsing all adjacent non-left-recursive expressions
        if(character == 'a') {
            context->character += 1;

            if(!expect_character(context, 's')) {
                return { false };
            }

            operation.type = OperationType::Cast;

            expect_non_left_recursive = true;
        } else if(character == '(') {
            context->character += 1;
            
            skip_whitespace(context);

            List<Expression> parameters{};

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            if(character == ')') {
                context->character += 1;
            } else {
                ungetc(character, context->source_file);

                while(true) {
                    expect(expression, parse_expression(context));

                    append(&parameters, expression);

                    skip_whitespace(context);

                    last_line = context->line;
                    last_character = context->character;

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

                        return { false };
                    } else {
                        error(*context, "Expected ',' or ')', got '%c'", character);

                        return { false };
                    }
                }
            }

            operation.type = OperationType::FunctionCall;
            operation.function_call = to_array(parameters);
            operation.range.end_line = last_line;
            operation.range.end_character = last_character;

            expect_non_left_recursive = false;
        } else if(character == '.') {
            context->character += 1;

            skip_whitespace(context);

            auto character = fgetc(context->source_file);

            if(isalnum(character) || character == '_') {
                ungetc(character, context->source_file);

                auto name = parse_identifier(context);

                operation.type = OperationType::MemberReference;
                operation.member_reference = name;
                operation.range.end_line = name.range.end_line;
                operation.range.end_character = name.range.end_character;

                expect_non_left_recursive = false;
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

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            if(character == ']') {
                context->character += 1;

                operation.type = OperationType::ArrayType;
            } else {
                ungetc(character, context->source_file);

                expect(expression, parse_expression(context));

                skip_whitespace(context);
                
                last_line = context->line;
                last_character = context->character;

                if(!expect_character(context, ']')) {
                    return { false };
                }

                operation.type = OperationType::IndexReference;
                operation.index_reference = expression;
            }

            operation.range.end_line = last_line;
            operation.range.end_character = last_character;

            expect_non_left_recursive = false;
        } else if(character == '+') {
            context->character += 1;

            operation.type = OperationType::Addition;

            expect_non_left_recursive = true;
        } else if(character == '-') {
            context->character += 1;

            operation.type = OperationType::Subtraction;

            expect_non_left_recursive = true;
        } else if(character == '*') {
            context->character += 1;

            operation.type = OperationType::Multiplication;

            expect_non_left_recursive = true;
        } else if(character == '/') {
            context->character += 1;

            operation.type = OperationType::Division;

            expect_non_left_recursive = true;
        } else if(character == '%') {
            context->character += 1;

            operation.type = OperationType::Modulo;

            expect_non_left_recursive = true;
        } else if(character == '=') {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            auto next_character = fgetc(context->source_file);

            if(next_character == '=') {
                context->character += 1;

                operation.type = OperationType::Equal;
                operation.range.end_line = last_line;
                operation.range.end_character = last_character;

                expect_non_left_recursive = true;
            } else {
                ungetc(next_character, context->source_file);

                context->character -= 1;

                ungetc(character, context->source_file);

                break;
            }
        } else if(character == '&') {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            if(character == '&') {
                context->character += 1;

                operation.type = OperationType::BooleanAnd;
                operation.range.end_line = last_line;
                operation.range.end_character = last_character;
            } else {
                ungetc(character, context->source_file);

                operation.type = OperationType::BitwiseAnd;
            }

            expect_non_left_recursive = true;
        } else if(character == '|') {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            if(character == '|') {
                context->character += 1;

                operation.type = OperationType::BooleanOr;
                operation.range.end_line = last_line;
                operation.range.end_character = last_character;
            } else {
                ungetc(character, context->source_file);

                operation.type = OperationType::BitwiseOr;
            }

            expect_non_left_recursive = true;
        } else if(character == '!') {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            if(!expect_character(context, '=')) {
                return { false };
            }

            operation.type = OperationType::NotEqual;
            operation.range.end_line = last_line;
            operation.range.end_character = last_character;

            expect_non_left_recursive = true;
        } else {
            ungetc(character, context->source_file);

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

    assert(expression_stack->count == 1);

    return {
        true,
        (*expression_stack)[0]
    };
}

static Result<Expression> parse_expression(Context *context) {
    List<Operation> operation_stack{};

    List<Expression> expression_stack{};

    return parse_right_expressions(context, &operation_stack, &expression_stack, true);
}

static Result<Statement> parse_expression_statement_or_variable_assignment(Context *context, Expression expression) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    switch(character) {
        case '=': {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            switch(character) {
                case '=': {
                    context->character += 1;

                    skip_whitespace(context);

                    List<Operation> operation_stack{};

                    Operation operation;
                    operation.type = OperationType::Equal;
                    operation.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };

                    append(&operation_stack, operation);

                    List<Expression> expression_stack{};

                    append(&expression_stack, expression);

                    expect(right_expressions, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                    skip_whitespace(context);

                    last_line = context->line;
                    last_character = context->character;

                    if(!expect_character(context, ';')) {
                        return { false };
                    }

                    Statement statement;
                    statement.type = StatementType::Expression;
                    statement.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };

                    statement.expression.type = ExpressionType::BinaryOperation;
                    statement.expression.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        right_expressions.range.end_line,
                        right_expressions.range.end_character
                    };
                    statement.expression.binary_operation.binary_operator = BinaryOperator::Equal;
                    statement.expression.binary_operation.left = heapify(expression);
                    statement.expression.binary_operation.right = heapify(right_expressions);

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

                    skip_whitespace(context);

                    expect(value_expression, parse_expression(context));

                    skip_whitespace(context);

                    auto last_line = context->line;
                    auto last_character = context->character;

                    if(!expect_character(context, ';')) {
                        return { false };
                    }

                    Statement statement;
                    statement.type = StatementType::Assignment;
                    statement.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };
                    statement.assignment.target = expression;
                    statement.assignment.value = value_expression;

                    return {
                        true,
                        statement
                    };
                } break;
            }
        } break;
        
        case ';': {
            context->character += 1;

            Statement statement;
            statement.type = StatementType::Expression;
            statement.range = {
                context->source_file_path,
                first_line,
                first_character,
                first_line,
                first_character
            };
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

static Result<Statement> parse_statement(Context *context);

static Result<Statement> continue_parsing_function_declaration(Context *context, Identifier name, Array<FunctionParameter> parameters, FileRange parameters_range) {
    auto character = fgetc(context->source_file);

    auto is_external = false;
    auto has_return_type = false;
    Expression return_type;
    if(character == '-') {
        context->character += 1;

        if(!expect_character(context, '>')) {
            return { false };
        }

        skip_whitespace(context);

        expect(expression, parse_expression(context));

        has_return_type = true;
        return_type = expression;

        skip_whitespace(context);
        
        character = fgetc(context->source_file);

        if(isalpha(character) || character == '_') {
            ungetc(character, context->source_file);

            auto identifier = parse_identifier(context);

            if(strcmp(identifier.text, "extern") != 0) {
                error(*context, "Expected 'extern', ';' or '{', got '%s'", identifier.text);

                return { false };
            }

            character = fgetc(context->source_file);

            is_external = true;
        }
    } else if(isalpha(character) || character == '_') {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        if(strcmp(identifier.text, "extern") != 0) {
            error(*context, "Expected 'extern', '-', ';' or '{', got '%s'", identifier.text);

            return { false };
        }

        skip_whitespace(context);

        character = fgetc(context->source_file);

        is_external = true;
    }

    if(character == '{') {
        context->character += 1;

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

                expect(statement, parse_statement(context));

                append(&statement_list, statement);

                skip_whitespace(context);
            }

            statements = to_array(statement_list);
        }

        Statement statement;
        statement.type = StatementType::FunctionDeclaration;
        statement.range = name.range;
        statement.function_declaration.name = name;
        statement.function_declaration.parameters = parameters;
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
    } else if(character == ';') {
        context->character += 1;

        if(is_external) {
            Statement statement;
            statement.type = StatementType::FunctionDeclaration;
            statement.range = name.range;
            statement.function_declaration.name = name;
            statement.function_declaration.parameters = parameters;
            statement.function_declaration.has_return_type = has_return_type;
            statement.function_declaration.is_external = true;

            if(has_return_type) {
                statement.function_declaration.return_type = return_type;
            }

            return {
                true,
                statement
            };
        } else {
            Expression expression;
            expression.type = ExpressionType::FunctionType;
            expression.range = parameters_range;
            expression.function_type.parameters = parameters;

            if(has_return_type) {
                expression.function_type.return_type = heapify(return_type);
            } else {
                expression.function_type.return_type = nullptr;
            }

            Statement statement;
            statement.type = StatementType::ConstantDefinition;
            statement.range = name.range;
            statement.constant_definition = {
                name,
                expression
            };

            return {
                true,
                statement
            };
        }
    } else if(character == EOF) {
        error(*context, "Unexpected End of File");

        return { false };
    } else {
        error(*context, "Expected 'extern', '-', ';' or '{', got '%c'", character);

        return { false };
    }
}

static Result<Statement> parse_statement(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    if(isalpha(character) || character == '_') {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        skip_whitespace(context);

        if(strcmp(identifier.text, "if") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);

            if(!expect_character(context, '{')) {
                return { false };
            }

            List<Statement> statements{};

            while(true) {
                skip_whitespace(context);

                auto character = fgetc(context->source_file);

                if(character == '}') {
                    break;
                } else {
                    ungetc(character, context->source_file);
                }

                expect(statment, parse_statement(context));

                append(&statements, statment);
            }

            Statement statement;
            statement.type = StatementType::LoneIf;
            statement.range = expression.range;
            statement.lone_if.condition = expression;
            statement.lone_if.statements = to_array(statements);

            return {
                true,
                statement
            };
        } else if(strcmp(identifier.text, "while") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);

            if(!expect_character(context, '{')) {
                return { false };
            }

            List<Statement> statements{};

            while(true) {
                skip_whitespace(context);

                auto character = fgetc(context->source_file);

                if(character == '}') {
                    break;
                } else {
                    ungetc(character, context->source_file);
                }

                expect(statement, parse_statement(context));

                append(&statements, statement);
            }

            Statement statement;
            statement.type = StatementType::WhileLoop;
            statement.range = expression.range;
            statement.while_loop.condition = expression;
            statement.while_loop.statements = to_array(statements);

            return {
                true,
                statement
            };
        } else if(strcmp(identifier.text, "return") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);
            
            auto last_line = context->line;
            auto last_character = context->character;

            if(!expect_character(context, ';')) {
                return { false };
            }

            Statement statement;
            statement.type = StatementType::Return;
            statement.range = {
                context->source_file_path,
                first_line,
                first_character,
                last_line,
                last_character
            };
            statement._return = expression;

            return {
                true,
                statement
            };
        } else {
            auto after_identifier_line = context->line;
            auto after_identifier_character = context->character;

            auto character = fgetc(context->source_file);

            switch(character) {
                case ':': {
                    context->character += 1;

                    auto character = fgetc(context->source_file);

                    if(character == ':') {
                        context->character += 1;

                        skip_whitespace(context);

                        auto character = fgetc(context->source_file);
            
                        auto value_line = context->line;
                        auto value_character = context->character;

                        if(isalpha(character) || character == '_'){
                            ungetc(character, context->source_file);

                            auto value_identifier = parse_identifier(context);

                            if(strcmp(value_identifier.text, "struct") == 0) {
                                skip_whitespace(context);

                                if(!expect_character(context, '{')) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                List<StructMember> members{};

                                auto character = fgetc(context->source_file);

                                if(character == '}') {
                                    context->character += 1;
                                } else {
                                    ungetc(character, context->source_file);

                                    while(true) {
                                        expect(name, expect_identifier(context));

                                        skip_whitespace(context);

                                        if(!expect_character(context, ':')) {
                                            return { false };
                                        }

                                        skip_whitespace(context);

                                        expect(type, parse_expression(context));

                                        append(&members, {
                                            name,
                                            type
                                        });

                                        skip_whitespace(context);

                                        auto character = fgetc(context->source_file);

                                        if(character == '}') {
                                            context->character += 1;

                                            break;
                                        } else if(character == ',') {
                                            context->character += 1;

                                            skip_whitespace(context);
                                        } else if(character == EOF) {

                                        } else if(character == EOF) {
                                            error(*context, "Unexpected End of File");

                                            return { false };
                                        } else {
                                            error(*context, "Expected ',' or ';'. Got '%c'", character);

                                            return { false };
                                        }
                                    }
                                }

                                Statement statement;
                                statement.type = StatementType::StructDefinition;
                                statement.range = identifier.range;
                                statement.struct_definition = {
                                    identifier,
                                    to_array(members)
                                };

                                return {
                                    true,
                                    statement
                                };
                            } else {
                                Expression expression;
                                expression.type = ExpressionType::NamedReference;
                                expression.range = value_identifier.range;
                                expression.named_reference = value_identifier;

                                List<Operation> operation_stack{};
                                
                                List<Expression> expression_stack{};

                                append(&expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                                skip_whitespace(context);

                                if(!expect_character(context, ';')) {
                                    return { false };
                                }

                                Statement statement;
                                statement.type = StatementType::ConstantDefinition;
                                statement.range = identifier.range;
                                statement.constant_definition = {
                                    identifier,
                                    right_expression
                                };

                                return {
                                    true,
                                    statement
                                };
                            }
                        } else if(character == '(') {
                            context->character += 1;

                            skip_whitespace(context);

                            auto last_line = context->line;
                            auto last_character = context->character;

                            auto character = fgetc(context->source_file);

                            if(isalpha(character) || character == '_') {
                                ungetc(character, context->source_file);

                                auto first_identifier = parse_identifier(context);

                                skip_whitespace(context);

                                auto character = fgetc(context->source_file);

                                if(character == ':') {
                                    context->character += 1;

                                    skip_whitespace(context);

                                    auto character = fgetc(context->source_file);

                                    FunctionParameter first_parameter = {
                                        first_identifier
                                    };

                                    switch(character) {
                                        context->character += 1;

                                        case '$': {
                                            skip_whitespace(context);

                                            expect(name, expect_identifier(context));

                                            first_parameter.is_polymorphic_determiner = true;
                                            first_parameter.polymorphic_determiner = name;
                                        } break;

                                        case EOF: {
                                            error(*context, "Unexpected End of File");

                                            return { false };
                                        } break;

                                        default: {
                                            ungetc(character, context->source_file);

                                            expect(expression, parse_expression(context));

                                            first_parameter.is_polymorphic_determiner = false;
                                            first_parameter.type = expression;
                                        } break;
                                    }

                                    skip_whitespace(context);

                                    List<FunctionParameter> parameters{};

                                    append(&parameters, first_parameter);
                                    
                                    unsigned int last_line;
                                    unsigned int last_character;

                                    while(true) {
                                        last_line = context->line;
                                        last_character = context->character;

                                        auto character = fgetc(context->source_file);

                                        if(character == ',') {
                                            context->character += 1;

                                            skip_whitespace(context);
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

                                        expect(parameter, parse_function_parameter(context));

                                        append(&parameters, parameter);
                                    }

                                    skip_whitespace(context);

                                    FileRange parameters_range {
                                        context->source_file_path,
                                        value_line,
                                        value_character,
                                        last_character,
                                        last_line
                                    };

                                    return continue_parsing_function_declaration(context, identifier, to_array(parameters), parameters_range);
                                } else {
                                    ungetc(character, context->source_file);

                                    skip_whitespace(context);

                                    Expression expression;
                                    expression.type = ExpressionType::NamedReference;
                                    expression.range = first_identifier.range;
                                    expression.named_reference = first_identifier;

                                    List<Operation> sub_operation_stack{};

                                    List<Expression> sub_expression_stack{};

                                    append(&sub_expression_stack, expression);

                                    expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, false));

                                    skip_whitespace(context);

                                    if(!expect_character(context, ')')) {
                                        return { false };
                                    }

                                    List<Operation> operation_stack{};

                                    List<Expression> expression_stack{};

                                    append(&expression_stack, right_expression);

                                    expect(outer_right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));
                                    
                                    auto last_line = context->line;
                                    auto last_character = context->character;

                                    if(!expect_character(context, ';')) {
                                        return { false };
                                    }

                                    Statement statement;
                                    statement.type = StatementType::ConstantDefinition;
                                    statement.range = {
                                        context->source_file_path,
                                        first_line,
                                        first_character,
                                        last_line,
                                        last_character
                                    };
                                    statement.constant_definition = {
                                        identifier,
                                        outer_right_expression
                                    };

                                    return {
                                        true,
                                        statement
                                    };
                                }
                            } else if(character == ')') {
                                context->character += 1;

                                skip_whitespace(context);

                                FileRange parameters_range {
                                    context->source_file_path,
                                    value_line,
                                    value_character,
                                    last_line,
                                    last_character
                                };

                                return continue_parsing_function_declaration(context, identifier, Array<FunctionParameter>{}, parameters_range);
                            } else {
                                ungetc(character, context->source_file);

                                skip_whitespace(context);

                                expect(expression, parse_expression(context));

                                if(!expect_character(context, ')')) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                List<Operation> operation_stack{};

                                List<Expression> expression_stack{};

                                append(&expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                                auto last_line = context->line;
                                auto last_character = context->character;

                                if(!expect_character(context, ';')) {
                                    return { false };
                                }

                                Statement statement;
                                statement.type = StatementType::ConstantDefinition;
                                statement.range = {
                                    context->source_file_path,
                                    first_line,
                                    first_character,
                                    last_line,
                                    last_character
                                };
                                statement.constant_definition = {
                                    identifier,
                                    right_expression
                                };

                                return {
                                    true,
                                    statement
                                };
                            }
                        } else {
                            ungetc(character, context->source_file);

                            expect(expression, parse_expression(context));
                            
                            auto last_line = context->line;
                            auto last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::ConstantDefinition;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };
                            statement.constant_definition = {
                                identifier,
                                expression
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

                            expect(expression, parse_expression(context));

                            has_type = true;
                            type = expression;

                            skip_whitespace(context);

                            character = fgetc(context->source_file);
                        } else {
                            has_type = false;
                        }

                        auto last_line = context->line;
                        auto last_character = context->character;

                        bool has_initializer;
                        Expression initializer;
                        if(character == '=') {
                            context->character += 1;

                            skip_whitespace(context);

                            expect(expression, parse_expression(context));

                            has_initializer = true;
                            initializer = expression;

                            skip_whitespace(context);
                            
                            last_line = context->line;
                            last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }
                        } else if(character == ';') {
                            context->character += 1;

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
                        statement.range = {
                            context->source_file_path,
                            first_line,
                            first_character,
                            last_line,
                            last_character
                        };
                        statement.variable_declaration.name = identifier;

                        if(has_type && has_initializer) {
                            statement.variable_declaration.type = VariableDeclarationType::FullySpecified;

                            statement.variable_declaration.fully_specified = {
                                type,
                                initializer
                            };
                        } else if(has_type) {
                            statement.variable_declaration.type = VariableDeclarationType::Uninitialized;

                            statement.variable_declaration.uninitialized = type;
                        } else if(has_initializer) {
                            statement.variable_declaration.type = VariableDeclarationType::TypeElided;

                            statement.variable_declaration.type_elided = initializer;
                        } else {
                            abort();
                        }

                        return {
                            true,
                            statement
                        };
                    }
                } break;

                case '=': {
                    context->character += 1;

                    Expression expression;
                    expression.type = ExpressionType::NamedReference;
                    expression.range = identifier.range;
                    expression.named_reference = identifier;
                    
                    auto last_line = context->line;
                    auto last_character = context->character;

                    auto character = fgetc(context->source_file);

                    switch(character) {
                        case '=': {
                            context->character += 1;

                            skip_whitespace(context);

                            List<Operation> operation_stack{};

                            Operation operation;
                            operation.type = OperationType::Equal;
                            operation.range = {
                                context->source_file_path,
                                after_identifier_line,
                                after_identifier_character,
                                last_line,
                                last_character
                            };

                            append(&operation_stack, operation);

                            List<Expression> expression_stack{};

                            append(&expression_stack, expression);

                            expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                            skip_whitespace(context);
                            
                            last_line = context->line;
                            last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::Expression;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };

                            statement.expression.type = ExpressionType::BinaryOperation;
                            statement.expression.range = {
                                context->source_file_path,
                                after_identifier_line,
                                after_identifier_character,
                                right_expression.range.end_line,
                                right_expression.range.end_character
                            };
                            statement.expression.binary_operation.binary_operator = BinaryOperator::Equal;
                            statement.expression.binary_operation.left = heapify(expression);
                            statement.expression.binary_operation.right = heapify(right_expression);

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

                            skip_whitespace(context);

                            expect(value_expression, parse_expression(context));

                            skip_whitespace(context);
                            
                            auto last_line = context->line;
                            auto last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::Assignment;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };
                            statement.assignment.target = expression;
                            statement.assignment.value = value_expression;

                            return {
                                true,
                                statement
                            };
                        } break;
                    }
                } break;

                case EOF: {
                    error(*context, "Unexpected End of File");

                    return { false };
                } break;

                default: {
                    ungetc(character, context->source_file);

                    Expression expression;
                    expression.range = identifier.range;
                    expression.type = ExpressionType::NamedReference;
                    expression.named_reference = identifier;

                    List<Operation> operation_stack{};

                    List<Expression> expression_stack{};

                    append(&expression_stack, expression);

                    expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                    skip_whitespace(context);

                    expect(statement, parse_expression_statement_or_variable_assignment(context, right_expression));

                    return {
                        true,
                        statement
                    };
                } break;
            }
        }
    } else if(character == '#') {
        context->character += 1;

        auto character = fgetc(context->source_file);

        if(isalpha(character) || character == '_') {
            ungetc(character, context->source_file);

            auto identifier = parse_identifier(context);

            if(strcmp(identifier.text, "import") == 0) {
                skip_whitespace(context);

                expect(string, parse_string(context));

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                auto import_path = allocate<char>(string.text.count + 1);
                memcpy(import_path, string.text.elements, string.text.count);
                import_path[string.text.count] = 0;

                auto source_file_directory = path_get_directory_component(context->source_file_path);

                auto import_path_relative = allocate<char>(strlen(source_file_directory) + string.text.count + 1);

                strcpy(import_path_relative, source_file_directory);
                strcat(import_path_relative, import_path);

                expect(absolute, path_relative_to_absolute(import_path_relative));

                append<const char *>(context->remaining_files, absolute);

                Statement statement;
                statement.type = StatementType::Import;
                statement.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    string.range.end_line,
                    string.range.end_character
                };
                statement.import = absolute;

                return {
                    true,
                    statement
                };
            } else if(strcmp(identifier.text, "library") == 0) {
                skip_whitespace(context);

                expect(string, parse_string(context));

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                auto library = allocate<char>(string.text.count + 1);
                memcpy(library, string.text.elements, string.text.count);
                library[string.text.count] = 0;

                Statement statement;
                statement.type = StatementType::Library;
                statement.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    string.range.end_line,
                    string.range.end_character
                };
                statement.library = library;

                return {
                    true,
                    statement
                };
            } else {
                error(*context, "Expected 'import' or 'library'. Got '%s'", identifier.text);

                return { false };
            }
        } else if(character == EOF) {
            error(*context, "Unexpected End of File");

            return { false };
        } else {
            error(*context, "Expected a-z or A-Z. Got '%c'", (char)character);

            return { false };
        }
    } else {
        ungetc(character, context->source_file);

        expect(expression, parse_expression(context));

        expect(statement, parse_expression_statement_or_variable_assignment(context, expression));

        return {
            true,
            statement
        };
    }
}

void set_statement_parents(Statement *statement) {
    switch(statement->type) {
        case StatementType::FunctionDeclaration: {
            if(!statement->function_declaration.is_external) {
                for(auto &child : statement->function_declaration.statements) {
                    child.is_top_level = false;
                    child.parent = statement;

                    set_statement_parents(&child);
                }
            }
        } break;

        case StatementType::LoneIf: {
            for(auto &child : statement->lone_if.statements) {
                child.is_top_level = false;
                child.parent = statement;

                set_statement_parents(&child);
            }
        } break;
       
        case StatementType::WhileLoop: {
            for(auto &child : statement->while_loop.statements) {
                child.is_top_level = false;
                child.parent = statement;

                set_statement_parents(&child);
            }
        } break;
    }
}

Result<Array<File>> parse_source(const char *source_file_path) {
    auto result = path_relative_to_absolute(source_file_path);

    if(!result.status) {
        fprintf(stderr, "Cannot find source file '%s'\n", source_file_path);

        return { false };
    }

    List<const char*> remaining_files{};

    append<const char *>(&remaining_files, result.value);

    List<File> files{};

    while(remaining_files.count > 0) {
        auto source_file_path = take_last(&remaining_files);

        auto already_parsed = false;
        for(auto file : files) {
            if(strcmp(file.path, source_file_path) == 0) {
                already_parsed = true;

                break;
            }
        }

        if(!already_parsed) {
            auto source_file = fopen(source_file_path, "rb");

            if(source_file == NULL) {
                fprintf(stderr, "Unable to read source file: %s (%s)\n", source_file_path, strerror(errno));

                return { false };
            }

            Context context {
                source_file_path,
                source_file,
                1, 1,
                &remaining_files
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

                expect(statement, parse_statement(&context));

                append(&top_level_statements, statement);

                skip_whitespace(&context);
            }

            append(&files, File {
                source_file_path,
                to_array(top_level_statements)
            });
        }
    }

    for(auto &file : files) {
        for(auto &statement : file.statements) {
            statement.is_top_level = true;
            statement.file = &file;

            set_statement_parents(&statement);
        }
    }

    return {
        true,
        to_array(files)
    };
}