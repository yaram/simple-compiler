#include "ast.h"
#include <stdio.h>
#include <string.h>
#include "path.h"

static void indent(unsigned int level) {
    for(unsigned int i = 0; i < level; i += 1) {
        printf("    ");
    }
}

static void debug_print_position(FilePosition position) {
    const size_t max_path_length = 17;

    auto path_length = strlen(position.path);

    if(path_length <= max_path_length) {
        printf("%s", position.path);
    } else {
        char buffer[max_path_length + 1];

        printf("...%.*s", max_path_length, position.path + path_length - max_path_length);
    }

    printf("(%u:%u)", position.line, position.character);
}

static void debug_print_indentifier(Identifier identifier) {
    debug_print_position(identifier.position);

    printf("%s", identifier.text);
}

static void debug_print_expression_indent(Expression expression, unsigned int indentation_level) {
    debug_print_position(expression.position);

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
            printf("StringLiteral: %.*s", (int)expression.string_literal.count, expression.string_literal.elements);
        } break;

        case ExpressionType::ArrayLiteral: {
            printf("ArrayLiteral: [\n");

            for(auto element_expression : expression.array_literal) {
                indent(indentation_level);
                debug_print_expression_indent(element_expression, indentation_level);
                printf("\n");
            }

            indent(indentation_level);
            printf("]");
        } break;

        case ExpressionType::FunctionCall: {
            printf("FunctionCall: {\n");

            indent(indentation_level + 1);
            printf("expression: ");
            debug_print_expression_indent(*(expression.function_call.expression), indentation_level + 1);
            printf("\n");
            
            indent(indentation_level + 1);
            printf("parameters: [");

            if(expression.function_call.parameters.count != 0) {
                printf("\n");

                for(auto parameter : expression.function_call.parameters) {
                    indent(indentation_level + 2);
                    debug_print_expression_indent(parameter, indentation_level + 2);

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
            printf("operator: ");
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
                
                case BinaryOperator::Equal: {
                    printf("Equal");
                } break;

                case BinaryOperator::NotEqual: {
                    printf("NotEqual");
                } break;
                
                case BinaryOperator::BitwiseAnd: {
                    printf("BitwiseAnd");
                } break;

                case BinaryOperator::BitwiseOr: {
                    printf("BitwiseOr");
                } break;
                
                case BinaryOperator::BooleanAnd: {
                    printf("BooleanAnd");
                } break;

                case BinaryOperator::BooleanOr: {
                    printf("BooleanOr");
                } break;
            }
            printf("\n");

            indent(indentation_level + 1);
            printf("left: ");
            debug_print_expression_indent(*expression.binary_operation.left, indentation_level + 1);
            printf("\n");

            indent(indentation_level + 1);
            printf("right: ");
            debug_print_expression_indent(*expression.binary_operation.right, indentation_level + 1);
            printf("\n");
            
            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::UnaryOperation: {
            printf("UnaryOperation: {\n");

            indent(indentation_level + 1);
            printf("operation: ");
            switch(expression.unary_operation.unary_operator) {
                case UnaryOperator::Pointer: {
                    printf("Pointer");
                } break;
                
                case UnaryOperator::BooleanInvert: {
                    printf("BooleanInvert");
                } break;
                
                case UnaryOperator::Negation: {
                    printf("Negation");
                } break;
            }
            printf("\n");

            indent(indentation_level + 1);
            printf("expression: ");
            debug_print_expression_indent(*(expression.unary_operation.expression), indentation_level + 1);
            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;

        case ExpressionType::ArrayType: {
            printf("ArrayType: ");

            debug_print_expression_indent(*(expression.array_type), indentation_level);
        } break;

        case ExpressionType::FunctionType: {
            printf("FunctionDeclaration {\n");
            
            indent(indentation_level + 1);
            printf("parameters: {");

            if(expression.function_type.parameters.count != 0) {
                printf("\n");

                for(auto parameter : expression.function_type.parameters) {
                    indent(indentation_level + 2);
                    debug_print_indentifier(parameter.name);
                    printf(": ");

                    debug_print_expression_indent(parameter.type, indentation_level + 2);

                    printf("\n");
                }

                indent(indentation_level + 1);
            }
            
            printf("}\n");

            if(expression.function_type.return_type != nullptr) {
                indent(indentation_level + 1);
                printf("return_type: ");

                debug_print_expression_indent(*expression.function_type.return_type, indentation_level + 1);
            
                printf("\n");
            }
            
            indent(indentation_level);
            printf("}");
        } break;
    }
}

void debug_print_expression(Expression expression) {
    debug_print_expression_indent(expression, 0);
}

static void debug_print_statement_indent(Statement statement, unsigned int indentation_level) {
    debug_print_position(statement.position);

    switch(statement.type) {
        case StatementType::FunctionDeclaration: {
            printf("FunctionDeclaration {\n");

            indent(indentation_level + 1);
            printf("name: ");
            debug_print_indentifier(statement.function_declaration.name);
            printf("\n");
            
            indent(indentation_level + 1);
            printf("parameters: {");

            if(statement.function_declaration.parameters.count != 0) {
                printf("\n");

                for(auto parameter : statement.function_declaration.parameters) {
                    indent(indentation_level + 2);
                    debug_print_indentifier(parameter.name);
                    printf(": ");

                    debug_print_expression_indent(parameter.type, indentation_level + 2);

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

                    for(auto child_statement : statement.function_declaration.statements) {
                        indent(indentation_level + 2);
                        debug_print_statement_indent(child_statement, indentation_level + 2);

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
            printf("name: ");
            debug_print_indentifier(statement.constant_definition.name);
            printf("\n");

            indent(indentation_level + 1);
            printf("expression: ");

            debug_print_expression_indent(statement.constant_definition.expression, indentation_level + 2);

            printf("\n");

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::StructDefinition: {
            printf("StructDefinition: {\n");

            indent(indentation_level + 1);
            printf("name: ");
            debug_print_indentifier(statement.struct_definition.name);
            printf("\n");

            indent(indentation_level + 1);
            printf("members: [");

            if(statement.struct_definition.members.count != 0) {
                printf("\n");

                for(auto member : statement.struct_definition.members) {
                    indent(indentation_level + 2);
                    debug_print_indentifier(member.name);
                    printf(": ");

                    debug_print_expression_indent(member.type, indentation_level + 2);

                    printf("\n");
                }

                indent(indentation_level + 1);
            }

            printf("]\n");

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
            printf("\n");

            switch(statement.variable_declaration.type) {
                case VariableDeclarationType::Uninitialized: {
                    indent(indentation_level + 1);
                    printf("type: ");

                    debug_print_expression_indent(statement.variable_declaration.uninitialized, indentation_level + 2);

                    printf("\n");
                } break;
                
                case VariableDeclarationType::TypeElided: {
                    indent(indentation_level + 1);
                    printf("initializer: ");

                    debug_print_expression_indent(statement.variable_declaration.type_elided, indentation_level + 2);

                    printf("\n");
                } break;
                
                case VariableDeclarationType::FullySpecified: {
                    indent(indentation_level + 1);
                    printf("type: ");

                    debug_print_expression_indent(statement.variable_declaration.fully_specified.type, indentation_level + 2);

                    printf("\n");
                    
                    indent(indentation_level + 1);
                    printf("initializer: ");

                    debug_print_expression_indent(statement.variable_declaration.fully_specified.initializer, indentation_level + 2);

                    printf("\n");
                } break;
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

        case StatementType::LoneIf: {
            printf("LoneIf: {\n");

            indent(indentation_level + 1);
            printf("condition: ");

            debug_print_expression_indent(statement.lone_if.condition, indentation_level + 2);

            printf("\n");

            indent(indentation_level + 1);
            printf("statements: [");

            if(statement.lone_if.statements.count != 0) {
                printf("\n");

                for(auto child_statement : statement.lone_if.statements) {
                    indent(indentation_level + 2);

                    debug_print_statement_indent(child_statement, indentation_level + 2);

                    printf("\n");
                }

                indent(indentation_level + 1);
            }

            printf("]\n");

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::WhileLoop: {
            printf("WhileLoop: {\n");

            indent(indentation_level + 1);
            printf("condition: ");

            debug_print_expression_indent(statement.while_loop.condition, indentation_level + 2);

            printf("\n");

            indent(indentation_level + 1);
            printf("statements: [");

            if(statement.while_loop.statements.count != 0) {
                printf("\n");

                for(auto child_statement : statement.while_loop.statements) {
                    indent(indentation_level + 2);

                    debug_print_statement_indent(child_statement, indentation_level + 2);

                    printf("\n");
                }

                indent(indentation_level + 1);
            }

            printf("]\n");

            indent(indentation_level);
            printf("}");
        } break;

        case StatementType::Return: {
            printf("Return: ");

            debug_print_expression_indent(statement._return, indentation_level);
        } break;

        case StatementType::Import: {
            printf("Import: %s", statement.import);
        } break;
    }
}

void debug_print_statement(Statement statement) {
    debug_print_statement_indent(statement, 0);
}