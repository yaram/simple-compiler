#include "ast.h"
#include <stdio.h>

void indent(unsigned int level) {
    for(auto i = 0; i < level; i += 1) {
        printf("    ");
    }
}

void print(Statement statement, unsigned int indentation_level) {
    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            indent(indentation_level);
            printf("FunctionDeclaration {\n");

            indent(indentation_level + 1);
            printf("name: %s\n", statement.function_declaration.name);

            indent(indentation_level + 1);
            printf("statements: [\n");

            for(auto i = 0; i < statement.function_declaration.statement_count; i += 1) {
                print(statement.function_declaration.statements[i], indentation_level + 1);
            }

            indent(indentation_level + 1);
            printf("]\n");

            indent(indentation_level);
            printf("}\n");
        } break;
    }
}

void debug_print_statement(Statement statement) {
    print(statement, 0);
}