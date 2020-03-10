#pragma once

#include "array.h"
#include "tokens.h"
#include "generator.h"

enum struct JobKind {
    Parse,

    EvaluateConstantExpression,
    GenerateExpression,
    GenerateStatement,
    GenerateFunction,

    GenerateCSource
};

struct Job {
    JobKind kind;

    Array<size_t> dependencies;

    bool done;
    bool errored;

    union {
        struct {
            const char *path;

            Array<Token> tokens;
        } parse;

        struct {
            Expression expression;

            TypedConstantValue value;
        } evaluate_constant_expression;

        struct {
            Expression expression;

            TypedValue value;
            Array<size_t> patchups;
            Array<Instruction> instructions;
        } generate_expression;

        struct {
            Statement statement;

            Array<size_t> patchups;
            Array<Instruction> instructions;
        } generate_statement;

        struct {
            RuntimeFunction function;

            Function ir_function;
        } generate_function;

        struct {
            RuntimeFunction function;

            Function function;
        } generate_c_source;
    };
};