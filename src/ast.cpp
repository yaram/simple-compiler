#include "ast.h"
#include <stdio.h>

void indent(unsigned int level) {
    for(auto i = 0; i < level; i += 1) {
        printf("    ");
    }
}

void debug_print_expression_indent(Expression expression, unsigned int indentation_level) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            printf("NamedReference: %s", expression.named_reference);
        } break;
    }
}

void debug_print_expression(Expression expression) {
    debug_print_expression_indent(expression, 0);
}

void debug_print_statement_indent(Statement statement, unsigned int indentation_level) {
    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            printf("FunctionDeclaration {\n");

            indent(indentation_level + 1);
            printf("name: %s\n", statement.function_declaration.name);

            indent(indentation_level + 1);
            printf("statements: [\n");

            for(auto i = 0; i < statement.function_declaration.statement_count; i += 1) {
                indent(indentation_level + 2);
                debug_print_statement_indent(statement.function_declaration.statements[i], indentation_level + 2);

                if(i != statement.function_declaration.statement_count - 1) {
                    printf(",");
                }

                printf("\n");
            }

            indent(indentation_level + 1);
            printf("]\n");

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::Expression: {
            printf("Expression: ");

            debug_print_expression_indent(statement.expression, indentation_level + 1);
            
            printf("\n");
        } break;
    }
}

void debug_print_statement(Statement statement) {
    debug_print_statement_indent(statement, 0);
}