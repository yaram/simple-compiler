#include "ast.h"
#include <stdio.h>

static void indent(unsigned int level) {
    for(auto i = 0; i < level; i += 1) {
        printf("    ");
    }
}

static void debug_print_indentifier(Identifier identifier) {
    printf("%s(%u:%u): %s", identifier.source_file_path, identifier.line, identifier.character, identifier.text);
}

static void debug_print_expression_indent(Expression expression, unsigned int indentation_level) {
    printf("%s(%u:%u): ", expression.source_file_path, expression.line, expression.character);

    switch(expression.type) {
        case ExpressionType::NamedReference: {
            printf("NamedReference: ");
            debug_print_indentifier(expression.named_reference);
        } break;

        case ExpressionType::MemberReference: {
            printf("MemberReference: {\n");

            indent(indentation_level + 1);
            printf("expression: ");
            debug_print_expression_indent(*(expression.member_reference.expression), indentation_level + 1);
            printf("\n");

            indent(indentation_level + 1);
            printf("name: ");
            debug_print_indentifier(expression.member_reference.name);
            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::IndexReference: {
            printf("IndexReference: {\n");

            indent(indentation_level + 1);
            printf("expression: ");
            debug_print_expression_indent(*(expression.index_reference.expression), indentation_level + 1);
            printf("\n");

            indent(indentation_level + 1);
            printf("index: ");
            debug_print_expression_indent(*(expression.index_reference.index), indentation_level + 1);
            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::IntegerLiteral: {
            printf("IntegerLiteral: %lld", expression.integer_literal);
        } break;

        case ExpressionType::StringLiteral: {
            printf("StringLiteral: %.*s", expression.string_literal.count, expression.string_literal.elements);
        } break;

        case ExpressionType::FunctionCall: {
            printf("FunctionCall: {\n");

            indent(indentation_level + 1);
            printf("expression: ");
            debug_print_expression_indent(*(expression.function_call.expression), indentation_level + 1);
            printf(",\n");
            
            indent(indentation_level + 1);
            printf("parameters: [");

            if(expression.function_call.parameters.count != 0) {
                printf("\n");

                for(auto i = 0; i < expression.function_call.parameters.count; i += 1) {
                    auto parameter = expression.function_call.parameters[i];

                    indent(indentation_level + 2);
                    debug_print_expression_indent(parameter, indentation_level + 2);

                    if(i != expression.function_call.parameters.count - 1) {
                        printf(",");
                    }

                    printf("\n");
                }

                indent(indentation_level + 1); 
            }
            
            printf("]\n");

            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::BinaryOperation: {
            printf("BinaryOperation: {\n");

            indent(indentation_level + 1);
            printf("operation: ");
            switch(expression.binary_operation.binary_operator) {
                case BinaryOperator::Addition: {
                    printf("Addition");
                } break;
                
                case BinaryOperator::Subtraction: {
                    printf("Subtraction");
                } break;
                
                case BinaryOperator::Multiplication: {
                    printf("Multiplication");
                } break;
                
                case BinaryOperator::Division: {
                    printf("Division");
                } break;

                case BinaryOperator::Modulo: {
                    printf("Modulo");
                } break;
            }
            printf(",\n");

            indent(indentation_level + 1);
            printf("left: ");
            debug_print_expression_indent(*expression.binary_operation.left, indentation_level + 1);
            printf(",\n");

            indent(indentation_level + 1);
            printf("right: ");
            debug_print_expression_indent(*expression.binary_operation.right, indentation_level + 1);
            printf(",\n");
            
            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::Pointer: {
            printf("Pointer: ");

            debug_print_expression_indent(*(expression.pointer), indentation_level + 1);
        } break;

        case ExpressionType::ArrayType: {
            printf("ArrayType: ");

            debug_print_expression_indent(*(expression.array_type), indentation_level + 1);
        } break;
    }
}

void debug_print_expression(Expression expression) {
    debug_print_expression_indent(expression, 0);
}

static void debug_print_statement_indent(Statement statement, unsigned int indentation_level) {
    printf("%s(%u:%u): ", statement.source_file_path, statement.line, statement.character);

    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            printf("FunctionDeclaration {\n");

            indent(indentation_level + 1);
            printf("name: ");
            debug_print_indentifier(statement.function_declaration.name);
            printf(",\n");
            
            indent(indentation_level + 1);
            printf("parameters: {");

            if(statement.function_declaration.parameters.count != 0) {
                printf("\n");

                for(auto i = 0; i < statement.function_declaration.parameters.count; i += 1) {
                    auto parameter = statement.function_declaration.parameters[i];

                    indent(indentation_level + 2);
                    debug_print_indentifier(parameter.name);
                    printf(": ");

                    debug_print_expression_indent(parameter.type, indentation_level + 2);

                    if(i != statement.function_declaration.parameters.count - 1) {
                        printf(",");
                    }

                    printf("\n");
                }

                indent(indentation_level + 1);
            }
            
            printf("}\n");

            if(statement.function_declaration.has_return_type) {
                indent(indentation_level + 1);
                printf("return_type: ");

                debug_print_expression_indent(statement.function_declaration.return_type, indentation_level + 1);
            
                printf("\n");
            }

            indent(indentation_level + 1);
            printf("is_external: ");

            if(statement.function_declaration.is_external) {
                printf("true\n");
            } else {
                printf("false\n");

                indent(indentation_level + 1);
                printf("statements: [");

                if(statement.function_declaration.statements.count != 0) {
                    printf("\n");

                    for(auto i = 0; i < statement.function_declaration.statements.count; i += 1) {
                        indent(indentation_level + 2);
                        debug_print_statement_indent(statement.function_declaration.statements[i], indentation_level + 2);

                        if(i != statement.function_declaration.statements.count - 1) {
                            printf(",");
                        }

                        printf("\n");
                    }

                    indent(indentation_level + 1);
                }

                printf("]\n");
            }

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::ConstantDefinition: {
            printf("ConstantDefinition: {\n");

            indent(indentation_level + 1);
            printf("name: ", statement.constant_definition.name);
            debug_print_indentifier(statement.constant_definition.name);
            printf(",\n");

            indent(indentation_level + 1);
            printf("expression: ");

            debug_print_expression_indent(statement.constant_definition.expression, indentation_level + 2);

            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::Expression: {
            printf("Expression: ");

            debug_print_expression_indent(statement.expression, indentation_level);
        } break;

        case StatementType::VariableDeclaration: {
            printf("VariableDeclaration: {\n");

            indent(indentation_level + 1);
            printf("name: ");
            debug_print_indentifier(statement.variable_declaration.name);
            printf(",\n");

            if(statement.variable_declaration.has_type) {
                indent(indentation_level + 1);
                printf("type: ");

                debug_print_expression_indent(statement.variable_declaration.type, indentation_level + 2);

                printf("\n");
            }

            if(statement.variable_declaration.has_initializer) {
                indent(indentation_level + 1);
                printf("initializer: ");

                debug_print_expression_indent(statement.variable_declaration.initializer, indentation_level + 2);

                printf("\n");
            }

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::Assignment: {
            printf("Assignment: {\n");
            
            indent(indentation_level + 1);
            printf("target: ");

            debug_print_expression_indent(statement.assignment.target, indentation_level + 2);

            printf("\n");
            
            indent(indentation_level + 1);
            printf("value: ");

            debug_print_expression_indent(statement.assignment.value, indentation_level + 2);

            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;
    }
}

void debug_print_statement(Statement statement) {
    debug_print_statement_indent(statement, 0);
}