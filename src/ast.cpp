#include "ast.h"
#include <stdio.h>

void next_line(unsigned int level) {
    printf("\n");

    for(auto i = 0; i < level; i += 1) {
        printf("    ");
    }
}

void debug_print_expression_indent(Expression expression, unsigned int indentation_level) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            printf("NamedReference: %s", expression.named_reference);
        } break;

        case ExpressionType::IntegerLiteral: {
            printf("IntegerLiteral: %llu", expression.integer_literal);
        } break;

        case ExpressionType::FunctionCall: {
            printf("FunctionCall: {");
            next_line(indentation_level + 1);

            printf("expression: ");
            debug_print_expression_indent(*(expression.function_call.expression), indentation_level + 1);
            next_line(indentation_level);

            printf("}");
        } break;
    }
}

void debug_print_expression(Expression expression) {
    debug_print_expression_indent(expression, 0);
}

void debug_print_statement_indent(Statement statement, unsigned int indentation_level) {
    switch(statement.type) {
        case StatementType::FunctionDefinition: {
            printf("FunctionDeclaration {");
            next_line(indentation_level + 1);

            printf("name: %s,", statement.function_definition.name);
            next_line(indentation_level + 1);

            printf("parameters: {");
            next_line(indentation_level + 2);

            for(auto i = 0; i < statement.function_definition.parameters.count; i += 1) {
                auto parameter = statement.function_definition.parameters[i];

                printf("%s: ", parameter.name);

                debug_print_expression_indent(parameter.type, indentation_level + 3);

                if(i != statement.function_definition.parameters.count - 1) {
                    printf(",");
                }
                
                if(i != statement.function_definition.parameters.count - 1) {
                    next_line(indentation_level + 2);
                } else {
                    next_line(indentation_level + 1);
                }
            }

            printf("}");
            next_line(indentation_level + 1);

            printf("statements: [");
            next_line(indentation_level + 2);

            for(auto i = 0; i < statement.function_definition.statements.count; i += 1) {
                debug_print_statement_indent(statement.function_definition.statements[i], indentation_level + 2);

                if(i != statement.function_definition.statements.count - 1) {
                    printf(",");
                }

                if(i != statement.function_definition.statements.count - 1) {
                    next_line(indentation_level + 2);
                } else {
                    next_line(indentation_level + 1);
                }
            }

            printf("]");
            next_line(indentation_level);

            printf("}");
        } break;

        case StatementType::Expression: {
            printf("Expression: ");

            debug_print_expression_indent(statement.expression, indentation_level);
        } break;
    }
}

void debug_print_statement(Statement statement) {
    debug_print_statement_indent(statement, 0);
}