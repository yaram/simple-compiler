#pragma once

enum struct StatementType {
    FunctionDeclaration
};

struct Statement {
    StatementType type;

    union {
        struct {
            const char *name;

            Statement *statements;
            size_t statement_count;
        } function_declaration;
    };
};

void debug_print_statement(Statement statement);