#include "ast.h"
#include <stdio.h>

void next_line(unsigned int level) {
    printf("\n");

    for(auto i = 0; i < level; i += 1) {
        printf("    ");
    }
}

void debug_print_expression_indent(Expression expression, unsigned int indentation_level, bool multiline) {
    switch(expression.type) {
        case ExpressionType::NamedReference: {
            printf("NamedReference: %s", expression.named_reference);
        } break;
    }
}

void debug_print_expression(Expression expression, bool multiline) {
    debug_print_expression_indent(expression, 0, multiline);
}

void debug_print_statement_indent(Statement statement, unsigned int indentation_level, bool multiline) {
    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            printf("FunctionDeclaration {");
            if(multiline) {
                next_line(indentation_level + 1);
            } else {
                printf(" ");
            }

            printf("name: %s,", statement.function_declaration.name);
            if(multiline) {
                next_line(indentation_level + 1);
            } else {
                printf(" ");
            }

            printf("statements: [");
            if(multiline) {
                next_line(indentation_level + 2);
            } else {
                printf(" ");
            }

            for(auto i = 0; i < statement.function_declaration.statement_count; i += 1) {
                debug_print_statement_indent(statement.function_declaration.statements[i], indentation_level + 2, multiline);

                if(i != statement.function_declaration.statement_count - 1) {
                    printf(",");
                }

                if(multiline) {
                    if(i != statement.function_declaration.statement_count - 1) {
                        next_line(indentation_level + 2);
                    } else {
                        next_line(indentation_level + 1);
                    }
                } else {
                    printf(" ");
                }
            }

            printf("]");
            if(multiline) {
                next_line(indentation_level);
            } else {
                printf(" ");
            }

            printf("}");
        } break;

        case StatementType::Expression: {
            printf("Expression: ");

            debug_print_expression_indent(statement.expression, indentation_level + 1, multiline);
        } break;
    }
}

void debug_print_statement(Statement statement, bool multiline) {
    debug_print_statement_indent(statement, 0, multiline);
}