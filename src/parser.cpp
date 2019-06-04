#include "parser.h"
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include "list.h"
#include "util.h"

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

static Identifier parse_identifier(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

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

    return {
        buffer.elements,
        context->source_file_path,
        first_line,
        first_character
    };
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
    Addition,
    Subtraction,
    Multiplication,
    Division,
    Modulo,
    MemberReference,
    IndexReference,
    FunctionCall,
    Pointer,
    ArrayType
};

unsigned int operation_precedences[] = {
    0,
    0,
    1,
    1,
    1,
    2,
    2,
    2,
    2,
    2
};

struct Operation {
    OperationType type;

    const char *source_file_path;

    unsigned int line;
    unsigned int character;

    union {
        Identifier member_reference;

        Expression index_reference;

        Array<Expression> function_call;
    };
};

static void apply_operation(List<Expression> *expression_stack, Operation operation) {
    Expression expression;
    expression.source_file_path = operation.source_file_path;
    expression.line = operation.line;
    expression.character = operation.character;

    switch(operation.type) {
        case OperationType::Addition:
        case OperationType::Subtraction:
        case OperationType::Multiplication:
        case OperationType::Division:
        case OperationType::Modulo: {
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
            expression.type = ExpressionType::Pointer;

            expression.pointer = heapify(take_last(expression_stack));
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

static Result<Expression> parse_expression(Context *context);

static Result<Expression> parse_right_expressions(Context *context, List<Operation> *operation_stack, List<Expression> *expression_stack) {
    auto expect_non_left_recursive = expression_stack->count == 0;

    while(true) {
        if(expect_non_left_recursive) {
            skip_whitespace(context);

            auto first_line = context->line;
            auto first_character = context->character;

            auto character = fgetc(context->source_file);

            // Parse non-left-recursive expressions first
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

                Expression expression;
                expression.type = ExpressionType::NamedReference;
                expression.source_file_path = context->source_file_path,
                expression.line = first_line;
                expression.character = first_character;
                expression.named_reference = {
                    buffer.elements,
                    context->source_file_path,
                    first_line,
                    first_character
                };

                append(expression_stack, expression);
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

                Expression expression;
                expression.source_file_path = context->source_file_path;
                expression.line = first_line;
                expression.character = first_character;

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
                    expression.source_file_path = context->source_file_path;
                    expression.line = first_line;
                    expression.character = first_character;
                    expression.named_reference = {
                        buffer.elements,
                        context->source_file_path,
                        first_line,
                        first_character
                    };
                }

                append(expression_stack, expression);
            } else if(character == '*') {
                context->character += 1;

                Operation operation;
                operation.source_file_path = context->source_file_path;
                operation.line = first_line;
                operation.character = first_character;
                operation.type = OperationType::Pointer;

                append(operation_stack, operation);

                // Unary operators are always followed by a non-left-recursive expression, so skip the left-recursive parsing below
                continue;
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

                Expression expression;
                expression.source_file_path = context->source_file_path;
                expression.line = first_line;
                expression.character = first_character;
                expression.type = ExpressionType::StringLiteral;
                expression.string_literal = to_array(buffer);

                append(expression_stack, expression);
            } else if(character == EOF) {
                error(*context, "Unexpected End of File");

                return { false };
            } else {
                error(*context, "Expected a-z, A-Z, 0-9, '-' or '*'. Got '%c'", character);

                return { false };
            }
        }

        skip_whitespace(context);

        auto first_line = context->line;
        auto first_character = context->character;

        auto character = fgetc(context->source_file);

        Operation operation;
        operation.source_file_path = context->source_file_path;
        operation.line = first_line;
        operation.character = first_character;

        // Parse left-recursive expressions (e.g. binary operators) after parsing all adjacent non-left-recursive expressions
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
                    auto result = parse_expression(context);

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

            operation.type = OperationType::FunctionCall;
            operation.function_call = to_array(parameters);

            expect_non_left_recursive = false;
        } else if(character == '.') {
            context->character += 1;

            skip_whitespace(context);

            auto character = fgetc(context->source_file);

            if(isalnum(character)) {
                ungetc(character, context->source_file);

                auto name = parse_identifier(context);

                operation.type = OperationType::MemberReference;
                operation.member_reference = name;

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

            auto character = fgetc(context->source_file);

            if(character == ']') {
                context->character += 1;

                operation.type = OperationType::ArrayType;
                expect_non_left_recursive = false;
            } else {
                ungetc(character, context->source_file);

                auto result = parse_expression(context);

                if(!result.status) {
                    return { false };
                }

                skip_whitespace(context);

                if(!expect_character(context, ']')) {
                    return { false };
                }

                operation.type = OperationType::IndexReference;
                operation.index_reference = result.value;

                expect_non_left_recursive = false;
            }
        } else if(character == '+') {
            operation.type = OperationType::Addition;

            expect_non_left_recursive = true;
        } else if(character == '-') {
            operation.type = OperationType::Subtraction;

            expect_non_left_recursive = true;
        } else if(character == '*') {
            operation.type = OperationType::Multiplication;

            expect_non_left_recursive = true;
        } else if(character == '/') {
            operation.type = OperationType::Division;

            expect_non_left_recursive = true;
        } else if(character == '%') {
            operation.type = OperationType::Modulo;

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

                if(operation_precedences[(int)operation.type] > operation_precedences[(int)last_operation.type]) {
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

    return parse_right_expressions(context, &operation_stack, &expression_stack);
}

static Result<Statement> parse_expression_statement_or_variable_assignment(Context *context, Expression expression) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    switch(character) {
        case '=': {
            context->character += 1;

            skip_whitespace(context);

            auto value_result = parse_expression(context);

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

                                auto result = parse_expression(context);

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

                            auto result = parse_expression(context);

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

                                if(strcmp(identifier.text, "extern") != 0) {
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

                            if(strcmp(identifier.text, "extern") != 0) {
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

                            is_external = false;
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

                        auto result = parse_expression(context);

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

                        auto result = parse_expression(context);

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

                        auto result = parse_expression(context);

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
                    statement.source_file_path = context->source_file_path;
                    statement.line = first_line;
                    statement.character = first_character;
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

                Expression target;
                target.type = ExpressionType::NamedReference;
                target.source_file_path = context->source_file_path;
                target.line = identifier.line;
                target.character = identifier.character;
                target.named_reference = identifier;

                skip_whitespace(context);

                auto result = parse_expression(context);

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

                List<Operation> operation_stack{};
                List<Expression> expression_stack{};

                append(&expression_stack, expression);

                auto result = parse_right_expressions(context, &operation_stack, &expression_stack);

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

        auto expression_result = parse_expression(context);

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