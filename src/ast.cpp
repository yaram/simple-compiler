#include "ast.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

inline void indent(unsigned int level) {
    for(unsigned int i = 0; i < level; i += 1) {
        printf("  ");
    }
}

inline void print_range(FileRange range) {
    printf("(%u:%u)-(%u:%u)", range.first_line, range.first_column, range.last_line, range.last_column);
}

inline void print_identifier(Identifier identifier) {
    print_range(identifier.range);
    printf(": %.*s", STRING_PRINTF_ARGUMENTS(identifier.text));
}

static void print_binary_operator(BinaryOperation::Operator binary_operator) {
    switch(binary_operator) {
        case BinaryOperation::Operator::Addition: {
            printf("Addition");
        } break;

        case BinaryOperation::Operator::Subtraction: {
            printf("Subtraction");
        } break;

        case BinaryOperation::Operator::Multiplication: {
            printf("Multiplication");
        } break;

        case BinaryOperation::Operator::Division: {
            printf("Division");
        } break;

        case BinaryOperation::Operator::Modulo: {
            printf("Modulo");
        } break;

        case BinaryOperation::Operator::Equal: {
            printf("Equal");
        } break;

        case BinaryOperation::Operator::NotEqual: {
            printf("NotEqual");
        } break;

        case BinaryOperation::Operator::LessThan: {
            printf("LessThan");
        } break;

        case BinaryOperation::Operator::GreaterThan: {
            printf("GreaterThan");
        } break;

        case BinaryOperation::Operator::BitwiseAnd: {
            printf("BitwiseAnd");
        } break;

        case BinaryOperation::Operator::BitwiseOr: {
            printf("BitwiseOr");
        } break;

        case BinaryOperation::Operator::LeftShift: {
            printf("LeftShift");
        } break;

        case BinaryOperation::Operator::RightShift: {
            printf("RightShift");
        } break;

        case BinaryOperation::Operator::BooleanAnd: {
            printf("BooleanAnd");
        } break;

        case BinaryOperation::Operator::BooleanOr: {
            printf("BooleanOr");
        } break;

        default: {
            abort();
        } break;
    }
}

static void print_expression_internal(Expression* expression, unsigned int indentation_level) {
    print_range(expression->range);
    printf(": ");

    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        printf("NamedReference: ");
        print_identifier(named_reference->name);
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        printf("MemberReference: ");

        indent(indentation_level + 1);
        printf("expression = ");
        print_expression_internal(member_reference->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("name = ");
        print_identifier(member_reference->name);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        printf("IndexReference: ");

        indent(indentation_level + 1);
        printf("expression = ");
        print_expression_internal(index_reference->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("index = ");
        print_expression_internal(index_reference->index, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        printf("IntegerLiteral: %" PRIu64, integer_literal->value);
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        printf("IntegerLiteral: %f", float_literal->value);
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        printf("StringLiteral: \"%.*s\"", STRING_PRINTF_ARGUMENTS(string_literal->characters));
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        printf("ArrayLiteral: [");

        if(array_literal->elements.length != 0) {
            printf("\n");

            for(auto element_expression : array_literal->elements) {
                indent(indentation_level + 1);
                print_expression_internal(element_expression, indentation_level + 1);
                printf("\n");
            }

            indent(indentation_level);
        }

        printf("]");
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        printf("StructLiteral: {");

        if(struct_literal->members.length != 0) {
            printf("\n");

            for(auto member : struct_literal->members) {
                indent(indentation_level + 1);
                print_identifier(member.name);
                printf(": ");
                print_expression_internal(member.value, indentation_level + 1);
                printf("\n");
            }

            indent(indentation_level);
        }

        printf("}");
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        printf("FunctionCall: {");

        indent(indentation_level + 1);
        printf("expression: ");
        print_expression_internal(function_call->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("parameters: [");

        if(function_call->parameters.length != 0) {
            printf("\n");

            for(auto parameter : function_call->parameters) {
                indent(indentation_level + 2);
                print_expression_internal(parameter, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        printf("BinaryOperation: {\n");

        indent(indentation_level + 1);
        printf("binary_operator: ");
        print_binary_operator(binary_operation->binary_operator);
        printf("\n");

        indent(indentation_level + 1);
        printf("left: ");
        print_expression_internal(binary_operation->left, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("right: ");
        print_expression_internal(binary_operation->right, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        printf("UnaryOperation: {\n");

        indent(indentation_level + 1);
        printf("unary_operator: ");
        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                printf("Pointer");
            } break;

            case UnaryOperation::Operator::PointerDereference: {
                printf("PointerDereference");
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                printf("BooleanInvert");
            } break;

            case UnaryOperation::Operator::Negation: {
                printf("Negation");
            } break;

            default: {
                abort();
            } break;
        }
        printf("\n");

        indent(indentation_level + 1);
        printf("expression: ");
        print_expression_internal(unary_operation->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        printf("Cast: {\n");

        indent(indentation_level + 1);
        printf("expression: ");
        print_expression_internal(cast->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("type: ");
        print_expression_internal(cast->type, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        printf("Bake: ");
        print_expression_internal(bake->function_call, indentation_level + 1);
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        printf("ArrayType: {\n");

        indent(indentation_level + 1);
        printf("expression: ");
        print_expression_internal(array_type->expression, indentation_level + 1);
        printf("\n");

        if(array_type->length) {
            indent(indentation_level + 1);
            printf("index: ");
            print_expression_internal(array_type->length, indentation_level + 1);
            printf("\n");
        }

        indent(indentation_level);
        printf("}");
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        printf("FunctionType: {");

        indent(indentation_level + 1);
        printf("parameters: {");

        if(function_type->parameters.length != 0) {
            printf("\n");

            for(auto parameter : function_type->parameters) {
                indent(indentation_level + 2);
                print_identifier(parameter.name);
                printf(": {\n");

                indent(indentation_level + 3);
                printf("is_polymorphic_determiner: ");

                if(parameter.is_polymorphic_determiner) {
                    printf("true\n");

                    indent(indentation_level + 3);
                    printf("polymorphic_determiner: ");
                    print_identifier(parameter.polymorphic_determiner);
                } else {
                    printf("false\n");

                    indent(indentation_level + 3);
                    printf("type: ");
                    print_expression_internal(parameter.type, indentation_level + 3);
                }

                printf("\n");

                indent(indentation_level + 3);
                printf("is_constant: ");
                if(parameter.is_constant) {
                    printf("true\n");
                } else {
                    printf("false\n");
                }

                indent(indentation_level + 2);
                printf("}\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level + 1);
        printf("return_types: [");

        if(function_type->return_types.length != 0) {
            printf("\n");

            for(auto return_type : function_type->return_types) {
                indent(indentation_level + 2);
                print_expression_internal(return_type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("tags: {");

        if(function_type->tags.length != 0) {
            printf("\n");

            for(auto tag : function_type->tags) {
                indent(indentation_level + 2);
                print_identifier(tag.name);
                printf(": [");

                if(tag.parameters.length != 0) {
                    printf("\n");

                    for(auto parameter : tag.parameters) {
                        indent(indentation_level + 3);
                        print_expression_internal(parameter, indentation_level + 3);
                        printf("\n");
                    }

                    indent(indentation_level + 2);
                }

                printf("]\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level);
        printf("}");
    } else {
        abort();
    }
}

void Expression::print() {
    print_expression_internal(this, 0);
}

static void print_statement_internal(Statement* statement, unsigned int indentation_level) {
    print_range(statement->range);
    printf(": ");

    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        printf("FunctionDeclaration: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(function_declaration->name);
        printf("\n");

        indent(indentation_level + 1);
        printf("parameters: {");

        if(function_declaration->parameters.length != 0) {
            printf("\n");

            for(auto parameter : function_declaration->parameters) {
                indent(indentation_level + 2);
                print_identifier(parameter.name);
                printf(": {\n");

                indent(indentation_level + 3);
                printf("is_polymorphic_determiner: ");

                if(parameter.is_polymorphic_determiner) {
                    printf("true\n");

                    indent(indentation_level + 3);
                    printf("polymorphic_determiner: ");
                    print_identifier(parameter.polymorphic_determiner);
                } else {
                    printf("false\n");

                    indent(indentation_level + 3);
                    printf("type: ");
                    print_expression_internal(parameter.type, indentation_level + 3);
                }

                printf("\n");

                indent(indentation_level + 3);
                printf("is_constant: ");
                if(parameter.is_constant) {
                    printf("true\n");
                } else {
                    printf("false\n");
                }

                indent(indentation_level + 2);
                printf("}\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level + 1);
        printf("return_types: [");

        if(function_declaration->return_types.length != 0) {
            printf("\n");

            for(auto return_type : function_declaration->return_types) {
                indent(indentation_level + 2);
                print_expression_internal(return_type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("tags: {");

        if(function_declaration->tags.length != 0) {
            printf("\n");

            for(auto tag : function_declaration->tags) {
                indent(indentation_level + 2);
                print_identifier(tag.name);
                printf(": [");

                if(tag.parameters.length != 0) {
                    printf("\n");

                    for(auto parameter : tag.parameters) {
                        indent(indentation_level + 3);
                        print_expression_internal(parameter, indentation_level + 3);
                        printf("\n");
                    }

                    indent(indentation_level + 2);
                }

                printf("]\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level + 1);
        printf("has_body: ");
        if(function_declaration->has_body) {
            printf("true\n");

            indent(indentation_level + 1);
            printf("statements: {");

            if(function_declaration->statements.length != 0) {
                printf("\n");

                for(auto statement : function_declaration->statements) {
                    indent(indentation_level + 2);
                    print_statement_internal(statement, indentation_level + 2);
                    printf("\n");
                }

                indent(indentation_level + 1);
            }

            printf("}\n");
        } else {
            printf("false\n");
        }

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        printf("ConstantDefinition: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(constant_definition->name);
        printf("\n");

        indent(indentation_level + 1);
        printf("expression: ");
        print_expression_internal(constant_definition->expression, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        printf("StructDefinition: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(struct_definition->name);
        printf("\n");

        if(struct_definition->parameters.length != 0) {
            indent(indentation_level + 1);
            printf("parameters: {\n");

            for(auto parameter : struct_definition->parameters) {
                indent(indentation_level + 2);
                print_identifier(parameter.name);
                printf(": ");
                print_expression_internal(parameter.type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
            printf("}\n");
        }

        indent(indentation_level + 1);
        printf("members: {");

        if(struct_definition->members.length != 0) {
            printf("\n");

            for(auto member : struct_definition->members) {
                indent(indentation_level + 2);
                print_identifier(member.name);
                printf(": ");
                print_expression_internal(member.type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::UnionDefinition) {
        auto union_definition = (UnionDefinition*)statement;

        printf("UniontDefinition: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(union_definition->name);
        printf("\n");

        if(union_definition->parameters.length != 0) {
            indent(indentation_level + 1);
            printf("parameters: {\n");

            for(auto parameter : union_definition->parameters) {
                indent(indentation_level + 2);
                print_identifier(parameter.name);
                printf(": ");
                print_expression_internal(parameter.type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
            printf("}\n");
        }

        indent(indentation_level + 1);
        printf("members: {");

        if(union_definition->members.length != 0) {
            printf("\n");

            for(auto member : union_definition->members) {
                indent(indentation_level + 2);
                print_identifier(member.name);
                printf(": ");
                print_expression_internal(member.type, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::EnumDefinition) {
        auto enum_definition = (EnumDefinition*)statement;

        printf("EnumDefinition: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(enum_definition->name);
        printf("\n");

        if(enum_definition->backing_type != nullptr) {
            indent(indentation_level + 1);
            printf("backing_type: ");
            print_expression_internal(enum_definition->backing_type, indentation_level + 1);
            printf("\n");
        }

        indent(indentation_level + 1);
        printf("variants: {");

        if(enum_definition->variants.length != 0) {
            printf("\n");

            for(auto variant : enum_definition->variants) {
                indent(indentation_level + 2);

                print_identifier(variant.name);

                if(variant.value != nullptr) {
                    printf(" = ");
                    print_expression_internal(variant.value, indentation_level + 2);
                }

                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::ExpressionStatement) {
        auto expression_statement = (ExpressionStatement*)statement;

        printf("ExpressionStatement: ");
        print_expression_internal(expression_statement->expression, indentation_level);
    } else if(statement->kind == StatementKind::VariableDeclaration) {
        auto variable_declaration = (VariableDeclaration*)statement;

        printf("VariableDeclaration: {\n");

        indent(indentation_level + 1);
        printf("name: ");
        print_identifier(variable_declaration->name);
        printf("\n");

        if(variable_declaration->type) {
            indent(indentation_level + 1);
            printf("type: ");
            print_expression_internal(variable_declaration->type, indentation_level + 1);
            printf("\n");
        }

        if(variable_declaration->initializer) {
            indent(indentation_level + 1);
            printf("initializer: ");
            print_expression_internal(variable_declaration->initializer, indentation_level + 1);
            printf("\n");
        }

        indent(indentation_level + 1);
        printf("tags: {");

        if(variable_declaration->tags.length != 0) {
            printf("\n");

            for(auto tag : variable_declaration->tags) {
                indent(indentation_level + 2);
                print_identifier(tag.name);
                printf(": [");

                if(tag.parameters.length != 0) {
                    printf("\n");

                    for(auto parameter : tag.parameters) {
                        indent(indentation_level + 3);
                        print_expression_internal(parameter, indentation_level + 3);
                        printf("\n");
                    }

                    indent(indentation_level + 2);
                }

                printf("]\n");
            }

            indent(indentation_level + 1);
        }

        printf("}\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::MultiReturnVariableDeclaration) {
        auto variable_declaration = (MultiReturnVariableDeclaration*)statement;

        printf("MultiReturnVariableDeclaration: {\n");

        indent(indentation_level + 1);
        printf("names: [");

        if(variable_declaration->names.length != 0) {
            printf("\n");

            for(auto name : variable_declaration->names) {
                indent(indentation_level + 2);
                print_identifier(name);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("initializer: ");
        print_expression_internal(variable_declaration->initializer, indentation_level + 1);
        printf("\n");

        printf("]\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::Assignment) {
        auto assignment = (Assignment*)statement;

        printf("Assignment: {\n");

        indent(indentation_level + 1);
        printf("target: ");
        print_expression_internal(assignment->target, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("value: ");
        print_expression_internal(assignment->value, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
        auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

        printf("Assignment: {\n");

        indent(indentation_level + 1);
        printf("target: ");
        print_expression_internal(binary_operation_assignment->target, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("binary_operator: ");
        print_binary_operator(binary_operation_assignment->binary_operator);
        printf("\n");

        indent(indentation_level + 1);
        printf("value: ");
        print_expression_internal(binary_operation_assignment->value, indentation_level + 1);
        printf("\n");

        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::MultiReturnAssignment) {
        auto assignment = (MultiReturnAssignment*)statement;

        printf("MultiReturnAssignment: {\n");

        indent(indentation_level + 1);
        printf("targets: [");

        if(assignment->targets.length != 0) {
            printf("\n");

            for(auto target : assignment->targets) {
                indent(indentation_level + 2);
                print_expression_internal(target, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("value: ");
        print_expression_internal(assignment->value, indentation_level + 1);
        printf("\n");

        printf("]\n");
        indent(indentation_level);
        printf("}");
    } else if(statement->kind == StatementKind::IfStatement) {
        auto if_statement = (IfStatement*)statement;

        printf("IfStatement: {\n");

        indent(indentation_level + 1);
        printf("condition: ");
        print_expression_internal(if_statement->condition, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("statements: [");

        if(if_statement->statements.length != 0) {
            printf("\n");

            for(auto statement : if_statement->statements) {
                indent(indentation_level + 2);
                print_statement_internal(statement, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("else_ifs: [");

        if(if_statement->else_ifs.length != 0) {
            printf("\n");

            for(auto else_if : if_statement->else_ifs) {
                indent(indentation_level + 2);
                printf("{\n");

                indent(indentation_level + 3);
                printf("condition: ");
                print_expression_internal(else_if.condition, indentation_level + 3);
                printf("\n");

                indent(indentation_level + 3);
                printf("statements: [");

                if(else_if.statements.length != 0) {
                    printf("\n");

                    for(auto statement : else_if.statements) {
                        indent(indentation_level + 4);
                        print_statement_internal(statement, indentation_level + 4);
                        printf("\n");
                    }

                    indent(indentation_level + 3);
                }

                printf("]\n");

                indent(indentation_level + 2);
                printf("}\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        indent(indentation_level + 1);
        printf("else_statements: [");

        if(if_statement->else_statements.length != 0) {
            printf("\n");

            for(auto statement : if_statement->else_statements) {
                indent(indentation_level + 2);
                print_statement_internal(statement, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]\n");

        printf("}");
    } else if(statement->kind == StatementKind::WhileLoop) {
        auto while_loop = (WhileLoop*)statement;

        printf("WhileLoop: {\n");

        indent(indentation_level + 1);
        printf("condition: ");
        print_expression_internal(while_loop->condition, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("statements: [");

        if(while_loop->statements.length != 0) {
            printf("\n");

            for(auto statement : while_loop->statements) {
                indent(indentation_level + 2);
                print_statement_internal(statement, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}");
    } else if(statement->kind == StatementKind::ForLoop) {
        auto for_loop = (ForLoop*)statement;

        printf("ForLoop: {\n");

        indent(indentation_level + 1);
        printf("from: ");
        print_expression_internal(for_loop->from, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("to: ");
        print_expression_internal(for_loop->to, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("statements: [");

        if(for_loop->statements.length != 0) {
            printf("\n");

            for(auto statement : for_loop->statements) {
                indent(indentation_level + 2);
                print_statement_internal(statement, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}");
    } else if(statement->kind == StatementKind::ReturnStatement) {
        auto return_statement = (ReturnStatement*)statement;

        printf("ReturnStatement: [");

        if(return_statement->values.length != 0) {
            printf("\n");

            for(auto value : return_statement->values) {
                indent(indentation_level + 1);
                print_expression_internal(value, indentation_level + 1);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]");
    } else if(statement->kind == StatementKind::BreakStatement) {
        printf("BreakStatement");
    } else if(statement->kind == StatementKind::InlineAssembly) {
        auto inline_assembly = (InlineAssembly*)statement;

        printf("InlineAssembly: {\n");

        indent(indentation_level + 1);
        printf("assembly: \"%.*s\"\n", STRING_PRINTF_ARGUMENTS(inline_assembly->assembly));

        indent(indentation_level + 1);
        printf("bindings: [");

        if(inline_assembly->bindings.length != 0) {
            printf("\n");

            for(auto binding : inline_assembly->bindings) {
                indent(indentation_level + 2);
                printf("\"%.*s\" = ", STRING_PRINTF_ARGUMENTS(binding.constraint));
                print_expression_internal(binding.value, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("]");
    } else if(statement->kind == StatementKind::Import) {
        auto import = (Import*)statement;

        printf("Import: %.*s", STRING_PRINTF_ARGUMENTS(import->path));
    } else if(statement->kind == StatementKind::UsingStatement) {
        auto using_statement = (UsingStatement*)statement;

        printf("UsingStatement: {\n");

        indent(indentation_level + 1);
        printf("export: ");
        if(using_statement->export_) {
            printf("true");
        } else {
            printf("false");
        }
        printf("\n");

        indent(indentation_level + 1);
        printf("value: ");
        print_expression_internal(using_statement->value, indentation_level + 1);
        printf("\n");
    } else if(statement->kind == StatementKind::StaticIf) {
        auto static_if = (StaticIf*)statement;

        printf("StaticIf: {\n");

        indent(indentation_level + 1);
        printf("condition: ");
        print_expression_internal(static_if->condition, indentation_level + 1);
        printf("\n");

        indent(indentation_level + 1);
        printf("statements: [");

        if(static_if->statements.length != 0) {
            printf("\n");

            for(auto statement : static_if->statements) {
                indent(indentation_level + 2);
                print_statement_internal(statement, indentation_level + 2);
                printf("\n");
            }

            indent(indentation_level + 1);
        }

        printf("}");
    } else {
        abort();
    }
}

void Statement::print() {
    print_statement_internal(this, 0);
}