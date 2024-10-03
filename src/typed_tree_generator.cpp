#include "typed_tree_generator.h"
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "profiler.h"
#include "list.h"
#include "util.h"
#include "string.h"
#include "constant.h"
#include "types.h"
#include "jobs.h"

struct AnyRuntimeValue;

struct UndeterminedStructValue {
    inline UndeterminedStructValue() = default;
    explicit inline UndeterminedStructValue(Array<AnyRuntimeValue> members) : members(members) {}

    Array<AnyRuntimeValue> members;
};

enum struct RuntimeValueKind {
    ConstantValue,
    RegisterValue,
    AddressedValue,
    UndeterminedStructValue
};

struct AnyRuntimeValue {
    RuntimeValueKind kind;

    union {
        AnyConstantValue constant;
        UndeterminedStructValue undetermined_struct;
    };

    inline AnyRuntimeValue() = default;
    explicit inline AnyRuntimeValue(AnyConstantValue constant) : kind(RuntimeValueKind::ConstantValue), constant(constant) {}
    explicit inline AnyRuntimeValue(UndeterminedStructValue undetermined_struct) : kind(RuntimeValueKind::UndeterminedStructValue), undetermined_struct(undetermined_struct) {}

    static inline AnyRuntimeValue create_register_value() {
        AnyRuntimeValue result;
        result.kind = RuntimeValueKind::RegisterValue;

        return result;
    }

    static inline AnyRuntimeValue create_addressed_value() {
        AnyRuntimeValue result;
        result.kind = RuntimeValueKind::AddressedValue;

        return result;
    }

    inline AnyConstantValue unwrap_constant_value() {
        assert(kind == RuntimeValueKind::ConstantValue);

        return constant;
    }

    inline UndeterminedStructValue unwrap_undetermined_struct_value() {
        assert(kind == RuntimeValueKind::UndeterminedStructValue);

        return undetermined_struct;
    }
};

struct Variable {
    Identifier name;

    AnyType type;
};

struct VariableScope {
    ConstantScope* constant_scope;

    List<Variable> variables;
};

struct TypingContext {
    Arena* arena;

    Array<AnyType> return_types;

    Array<ConstantScope*> child_scopes;
    size_t next_child_scope_index;

    bool in_breakable_scope;

    List<VariableScope> variable_scope_stack;
};

static Result<void> add_new_variable(TypingContext* context, Identifier name, AnyType type) {
    assert(context->variable_scope_stack.length != 0);
    auto variable_scope = &(context->variable_scope_stack[context->variable_scope_stack.length - 1]);

    for(auto variable : variable_scope->variables) {
        if(variable.name.text == name.text) {
            error(variable_scope->constant_scope, name.range, "Duplicate variable name %.*s", STRING_PRINTF_ARGUMENTS(name.text));
            error(variable_scope->constant_scope, variable.name.range, "Original declared here");

            return err();
        }
    }

    Variable variable {};
    variable.name = name;
    variable.type = type;

    variable_scope->variables.append(variable);

    return ok();
}

struct TypedRuntimeValue {
    inline TypedRuntimeValue() = default;
    explicit inline TypedRuntimeValue(AnyType type, AnyRuntimeValue value) : type(type), value(value) {}

    AnyType type;

    AnyRuntimeValue value;
};

static Result<void> coerce_to_integer_register_value(
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Integer target_type,
    bool probing
) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        if(integer.size == target_type.size && integer.is_signed == target_type.is_signed) {
            return ok();
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = value.unwrap_constant_value().unwrap_integer();

        expect_void(check_undetermined_integer_to_integer_coercion(context->arena, scope, range, target_type, (int64_t)integer_value, probing));

        return ok();
    } else if(type.kind == TypeKind::Enum) {
        auto enum_ = type.enum_;

        if(enum_.backing_type->is_signed == target_type.is_signed && enum_.backing_type->size == target_type.size) {
            return ok();
        }
    } else if(type.kind == TypeKind::Undef) {
        return ok();
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena)));
    }

    return err();
}

static Result<void> coerce_to_float_register_value(
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    FloatType target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        return ok();
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        if(target_type.size == float_type.size) {
            return ok();
        }
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        return ok();
    } else if(type.kind == TypeKind::Undef) {
        return ok();
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena)));
    }

    return err();
}

static Result<void> coerce_to_pointer_register_value(
    GlobalInfo info,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    Pointer target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        return ok();
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        if(*pointer.pointed_to_type == *target_type.pointed_to_type) {
            return ok();
        }
    } else if(type.kind == TypeKind::Undef) {
        return ok();
    }

    if (!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena)));
    }

    return err();
}


static Result<void> coerce_to_type_register(
    GlobalInfo info,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyRuntimeValue value,
    AnyType target_type,
    bool probing
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer = target_type.integer;

        expect_void(coerce_to_integer_register_value(
            scope,
            context,
            range,
            type,
            value,
            integer,
            probing
        ));

        return ok();
    } else if(target_type.kind == TypeKind::Boolean) {
        if(type.kind == TypeKind::Boolean) {
            return ok();
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else if(target_type.kind == TypeKind::FloatType) {
        auto float_type = target_type.float_;

        expect_void(coerce_to_float_register_value(
            scope,
            context,
            range,
            type,
            value,
            float_type,
            probing
        ));

        return ok();
    } else if(target_type.kind == TypeKind::Pointer) {
        auto pointer = target_type.pointer;

        expect_void(coerce_to_pointer_register_value(
            info,
            scope,
            context,
            range,
            type,
            value,
            pointer,
            probing
        ));

        return ok();
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;
            if(*target_array.element_type == *array_type.element_type) {
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    if(value.constant.kind == ConstantValueKind::ArrayConstant) {
                        return ok();
                    }
                } else if(value.kind == RuntimeValueKind::RegisterValue) {
                    return ok();
                } else if(value.kind == RuntimeValueKind::AddressedValue) {
                    return ok();
                } else {
                    abort();
                }
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_array.element_type == *static_array.element_type) {
                if(value.kind == RuntimeValueKind::AddressedValue) {
                    return ok();
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == u8"length"_S &&
                undetermined_struct.members[1].name == u8"pointer"_S
            ) {
                if(value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = value.constant;

                    auto undetermined_struct_value = constant_value.unwrap_struct();

                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        AnyRuntimeValue(undetermined_struct_value.members[0]),
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer_register_value(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            AnyRuntimeValue(undetermined_struct_value.members[1]),
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            return ok();
                        }
                    }
                } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                    auto undetermined_struct_value = value.undetermined_struct;

                    auto length_result = coerce_to_integer_register_value(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        undetermined_struct_value.members[0],
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer_register_value(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            undetermined_struct_value.members[1],
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            return ok();
                        }
                    }
                } else {
                    abort();
                }
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_static_array.element_type == *static_array.element_type && target_static_array.length == static_array.length) {
                return ok();
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else if(target_type.kind == TypeKind::StructType) {
        auto target_struct_type = target_type.struct_;

        if(type.kind == TypeKind::StructType) {
            auto struct_type = type.struct_;

            if(target_struct_type.definition == struct_type.definition && target_struct_type.members.length == struct_type.members.length) {
                auto same_members = true;
                for(size_t i = 0; i < struct_type.members.length; i += 1) {
                    if(
                        target_struct_type.members[i].name != struct_type.members[i].name ||
                        target_struct_type.members[i].type != struct_type.members[i].type
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    return ok();
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                AnyRuntimeValue(undetermined_struct_value.members[i]),
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok();
                        }
                    }
                }
            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = value.undetermined_struct;

                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                undetermined_struct_value.members[i],
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok();
                        }
                    }
                }
            } else {
                abort();
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else if(target_type.kind == TypeKind::UnionType) {
        auto target_union_type = target_type.union_;

        if(type.kind == TypeKind::UnionType) {
            auto union_type = type.union_;

            if(target_union_type.definition == union_type.definition && target_union_type.members.length == union_type.members.length) {
                auto same_members = true;
                for(size_t i = 0; i < union_type.members.length; i += 1) {
                    if(
                        target_union_type.members[i].name != union_type.members[i].name ||
                        target_union_type.members[i].type != union_type.members[i].type
                    ) {
                        same_members = false;

                        break;
                    }
                }

                if(same_members) {
                    return ok();
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                AnyRuntimeValue(undetermined_struct_value.members[0]),
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                return ok();
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else if(value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = value.undetermined_struct;

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto result = coerce_to_type_register(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                undetermined_struct_value.members[0],
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                return ok();
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else {
                abort();
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else if(target_type.kind == TypeKind::Enum) {
        auto target_enum = target_type.enum_;

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            if(integer.size == target_enum.backing_type->size && integer.is_signed == target_enum.backing_type->is_signed) {
                return ok();
            }
        } else if(type.kind == TypeKind::UndeterminedInteger) {
            auto integer_value = value.unwrap_constant_value().unwrap_integer();

            expect_void(check_undetermined_integer_to_integer_coercion(
                context->arena,
                scope,
                range,
                *target_enum.backing_type,
                (int64_t)integer_value, probing
            ));

            return ok();
        } else if(type.kind == TypeKind::Enum) {
            auto enum_ = type.enum_;

            if(target_enum.definition == enum_.definition) {
                return ok();
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok();
        }
    } else {
        abort();
    }

    if(!probing) {
        if(value.kind == RuntimeValueKind::ConstantValue) {
            error(scope, range, "Cannot implicitly convert constant '%.*s' (%.*s) to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(value.constant.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena)));
        } else if(value.kind == RuntimeValueKind::RegisterValue) {
            error(scope, range, "Cannot implicitly convert anonymous '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena)));
        } else {
            error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena)));
        }
    }

    return err();
}

struct GenerateExpressionResult {
    TypedExpression typed_expression;
    AnyRuntimeValue value;
};

static DelayedResult<GenerateExpressionResult> generate_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
);

struct EvaluateTypeExpressionResult {
    TypedExpression typed_expression;

    AnyType type;
};

static DelayedResult<EvaluateTypeExpressionResult> evaluate_type_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
) {
    expect_delayed(expression_value, generate_expression(info, jobs, scope, context, expression));

    if(expression_value.typed_expression.type.kind == TypeKind::Type) {
        auto constant_value = expression_value.value.unwrap_constant_value();

        EvaluateTypeExpressionResult result {};
        result.typed_expression = expression_value.typed_expression;
        result.type = constant_value.unwrap_type();

        return ok(result);
    } else {
        error(scope, expression->range, "Expected a type, got %.*s", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

        return err();
    }
}

struct EvaluateConstantExpressionResult {
    TypedExpression typed_expression;

    AnyConstantValue value;
};

static DelayedResult<EvaluateConstantExpressionResult> evaluate_constant_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
) {
    expect_delayed(expression_value, generate_expression(info, jobs, scope, context, expression));

    if(expression_value.value.kind != RuntimeValueKind::ConstantValue) {
        auto constant_value = expression_value.value.unwrap_constant_value();

        EvaluateConstantExpressionResult result {};
        result.typed_expression = expression_value.typed_expression;
        result.value = constant_value;

        return ok(result);
    } else {
        error(scope, expression->range, "Expected a constant value");

        return err();
    }
}

static DelayedResult<GenerateExpressionResult> generate_binary_operation(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    Expression* left_expression,
    Expression* right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect_delayed(left, generate_expression(info, jobs, scope, context, left_expression));

    expect_delayed(right, generate_expression(info, jobs, scope, context, right_expression));

    auto children = context->arena->allocate<TypedExpression>(2);
    children[0] = left.typed_expression;
    children[1] = right.typed_expression;

    TypedExpression typed_expression {};
    typed_expression.range = range;
    typed_expression.children = Array(2, children);

    if(left.value.kind == RuntimeValueKind::ConstantValue && right.value.kind == RuntimeValueKind::ConstantValue) {
        expect(constant, evaluate_constant_binary_operation(
            context->arena,
            info,
            scope,
            range,
            binary_operator,
            left_expression->range,
            left.typed_expression.type,
            left.value.constant,
            right_expression->range,
            right.typed_expression.type,
            right.value.constant
        ));

        typed_expression.type = constant.type;

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue(constant.value);

        return ok(result);
    }

    expect(type, determine_binary_operation_type(context->arena, scope, range, left.typed_expression.type, right.typed_expression.type));

    expect(determined_type, coerce_to_default_type(info, scope, range, type));

    if(determined_type.kind == TypeKind::Integer) {
        auto integer = determined_type.integer;

        expect_void(coerce_to_integer_register_value(
            scope,
            context,
            left_expression->range,
            left.typed_expression.type,
            left.value,
            integer,
            false
        ));

        expect_void(coerce_to_integer_register_value(
            scope,
            context,
            right_expression->range,
            right.typed_expression.type,
            right.value,
            integer,
            false
        ));

        auto is_arithmetic = true;
        switch(binary_operator) {
            case BinaryOperation::Operator::Addition:
            case BinaryOperation::Operator::Subtraction:
            case BinaryOperation::Operator::Multiplication:
            case BinaryOperation::Operator::Division:
            case BinaryOperation::Operator::Modulo:
            case BinaryOperation::Operator::BitwiseAnd:
            case BinaryOperation::Operator::BitwiseOr:
            case BinaryOperation::Operator::LeftShift:
            case BinaryOperation::Operator::RightShift: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        AnyType result_type;
        if(is_arithmetic) {
            result_type = AnyType(integer);
        } else {
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal:
                case BinaryOperation::Operator::NotEqual:
                case BinaryOperation::Operator::LessThan:
                case BinaryOperation::Operator::GreaterThan: break;

                default: {
                    error(scope, range, "Cannot perform that operation on integers");

                    return err();
                } break;
            }

            result_type = AnyType::create_boolean();
        }

        typed_expression.type = result_type;

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_register_value();

        return ok(result);
    } else if(determined_type.kind == TypeKind::Boolean) {
        if(left.typed_expression.type.kind != TypeKind::Boolean) {
            error(scope, left_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(left.typed_expression.type.get_description(context->arena)));

            return err();
        }

        if(right.typed_expression.type.kind != TypeKind::Boolean) {
            error(scope, right_expression->range, "Expected 'bool', got '%.*s'", STRING_PRINTF_ARGUMENTS(right.typed_expression.type.get_description(context->arena)));

            return err();
        }

        auto is_arithmetic = true;
        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd:
            case BinaryOperation::Operator::BooleanOr: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(!is_arithmetic) {
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal:
                case BinaryOperation::Operator::NotEqual: break;

                default: {
                    error(scope, range, "Cannot perform that operation on 'bool'");

                    return err();
                } break;
            }
        }

        typed_expression.type = AnyType::create_boolean();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_register_value();

        return ok(result);
    } else if(determined_type.kind == TypeKind::FloatType) {
        auto float_type = determined_type.float_;

        expect_void(coerce_to_float_register_value(
            scope,
            context,
            left_expression->range,
            left.typed_expression.type,
            left.value,
            float_type,
            false
        ));

        expect_void(coerce_to_float_register_value(
            scope,
            context,
            right_expression->range,
            right.typed_expression.type,
            right.value,
            float_type,
            false
        ));

        auto is_arithmetic = true;
        switch(binary_operator) {
            case BinaryOperation::Operator::Addition:
            case BinaryOperation::Operator::Subtraction:
            case BinaryOperation::Operator::Multiplication:
            case BinaryOperation::Operator::Division:
            case BinaryOperation::Operator::Modulo: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        AnyType result_type;
        if(is_arithmetic) {
            result_type = AnyType(float_type);
        } else {
            switch(binary_operator) {
                case BinaryOperation::Operator::Equal:
                case BinaryOperation::Operator::NotEqual:
                case BinaryOperation::Operator::LessThan:
                case BinaryOperation::Operator::GreaterThan: break;

                default: {
                    error(scope, range, "Cannot perform that operation on floats");

                    return err();
                } break;
            }

            result_type = AnyType::create_boolean();
        }

        typed_expression.type = result_type;

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_register_value();

        return ok(result);
    } else if(determined_type.kind == TypeKind::Pointer) {
        auto pointer = determined_type.pointer;

        expect_void(coerce_to_pointer_register_value(
            info,
            scope,
            context,
            left_expression->range,
            left.typed_expression.type,
            left.value,
            pointer,
            false
        ));

        expect_void(coerce_to_pointer_register_value(
            info,
            scope,
            context,
            right_expression->range,
            right.typed_expression.type,
            right.value,
            pointer,
            false
        ));

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal:
            case BinaryOperation::Operator::NotEqual: break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(AnyType(pointer).get_description(context->arena)));

                return err();
            } break;
        }

        typed_expression.type = AnyType::create_boolean();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_register_value();

        return ok(result);
    } else if(determined_type.kind == TypeKind::Enum) {
        auto pointer = determined_type.pointer;

        expect_void(coerce_to_type_register(
            info,
            scope,
            context,
            left_expression->range,
            left.typed_expression.type,
            left.value,
            determined_type,
            false
        ));

        expect_void(coerce_to_type_register(
            info,
            scope,
            context,
            right_expression->range,
            right.typed_expression.type,
            right.value,
            determined_type,
            false
        ));

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal:
            case BinaryOperation::Operator::NotEqual: break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                return err();
            } break;
        }

        typed_expression.type = AnyType::create_boolean();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_register_value();

        return ok(result);
    } else {
        abort();
    }
}

struct RuntimeNameSearchResult {
    bool found;

    AnyType type;
    AnyRuntimeValue value;
};

static_profiled_function(DelayedResult<RuntimeNameSearchResult>, search_for_name, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    String name,
    uint32_t name_hash,
    ConstantScope* name_scope,
    FileRange name_range,
    Array<Statement*> statements,
    DeclarationHashTable declarations,
    bool external
), (
    info,
    jobs,
    scope,
    context,
    name,
    name_hash,
    name_scope,
    name_range,
    statements,
    declarations,
    external
)) {
    auto declaration = search_in_declaration_hash_table(declarations, name_hash, name);

    if(declaration != nullptr) {
        if(external && !is_declaration_public(declaration)) {
            RuntimeNameSearchResult result {};
            result.found = false;

            return ok(result);
        }

        expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, declaration));

        RuntimeNameSearchResult result {};
        result.found = true;
        result.type = value.type;
        result.value = AnyRuntimeValue(value.value);

        return ok(result);
    }

    for(auto statement : statements) {
        if(statement->kind == StatementKind::UsingStatement) {
            auto using_statement = (UsingStatement*)statement;

            if(!external || using_statement->export_) {
                expect_delayed(expression_value, evaluate_constant_expression(
                    info,
                    jobs,
                    scope,
                    context,
                    using_statement->value
                ));

                if(expression_value.typed_expression.type.kind == TypeKind::FileModule) {
                    auto file_module = expression_value.value.unwrap_file_module();

                    expect_delayed(search_value, search_for_name(
                        info,
                        jobs,
                        file_module.scope,
                        context,
                        name,
                        name_hash,
                        name_scope,
                        name_range,
                        file_module.scope->statements,
                        file_module.scope->declarations,
                        true
                    ));

                    if(search_value.found) {
                        RuntimeNameSearchResult result {};
                        result.found = true;
                        result.type = search_value.type;
                        result.value = search_value.value;

                        return ok(result);
                    }
                } else if(expression_value.typed_expression.type.kind == TypeKind::Type) {
                    auto type = expression_value.value.unwrap_type();

                    if(type.kind == TypeKind::Enum) {
                        auto enum_ = type.enum_;

                        for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                            if(enum_.definition->variants[i].name.text == name) {
                                RuntimeNameSearchResult result {};
                                result.found = true;
                                result.type = AnyType(*enum_.backing_type);
                                result.value = AnyRuntimeValue(AnyConstantValue(enum_.variant_values[i]));

                                return ok(result);
                            }
                        }
                    } else {
                        error(scope, using_statement->range, "Cannot apply 'using' with type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                        return err();
                    }
                } else {
                    error(scope, using_statement->range, "Cannot apply 'using' with type '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }
            }
        } else if(statement->kind == StatementKind::StaticIf) {
            auto static_if = (StaticIf*)statement;

            auto found = false;
            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolveStaticIf) {
                    auto resolve_static_if = job.resolve_static_if;

                    if(
                        resolve_static_if.static_if == static_if &&
                        resolve_static_if.scope == scope
                    ) {
                        found = true;

                        if(job.state == JobState::Done) {
                            if(resolve_static_if.condition) {
                                expect_delayed(search_value, search_for_name(
                                    info,
                                    jobs,
                                    scope,
                                    context,
                                    name,
                                    name_hash,
                                    name_scope,
                                    name_range,
                                    static_if->statements,
                                    resolve_static_if.declarations,
                                    false
                                ));

                                if(search_value.found) {
                                    RuntimeNameSearchResult result {};
                                    result.found = true;
                                    result.type = search_value.type;
                                    result.value = search_value.value;

                                    return ok(result);
                                }
                            }
                        } else {
                            bool could_have_declaration;
                            if(external) {
                                could_have_declaration = does_or_could_have_public_name(static_if, name);
                            } else {
                                could_have_declaration = does_or_could_have_name(static_if, name);
                            }

                            if(could_have_declaration) {
                                return wait(i);
                            }
                        }
                    }
                }
            }

            assert(found);
        } else if(statement->kind == StatementKind::VariableDeclaration) {
            if(scope->is_top_level) {
                auto variable_declaration = (VariableDeclaration*)statement;

                if(variable_declaration->name.text == name) {
                    for(size_t i = 0; i < jobs->length; i += 1) {
                        auto job = (*jobs)[i];

                        if(job.kind == JobKind::TypeStaticVariable) {
                            auto type_static_variable = job.type_static_variable;

                            if(type_static_variable.declaration == variable_declaration) {
                                if(job.state == JobState::Done) {
                                    RuntimeNameSearchResult result {};
                                    result.found = true;
                                    result.type = type_static_variable.type;
                                    result.value = AnyRuntimeValue::create_addressed_value();

                                    return ok(result);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }

                    abort();
                }
            }
        }
    }

    for(auto scope_constant : scope->scope_constants) {
        if(scope_constant.name == name) {
            RuntimeNameSearchResult result {};
            result.found = true;
            result.type = scope_constant.type;
            result.value = AnyRuntimeValue(scope_constant.value);

            return ok(result);
        }
    }

    RuntimeNameSearchResult result {};
    result.found = false;

    return ok(result);
}

static_profiled_function(DelayedResult<GenerateExpressionResult>, generate_expression, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
), (
    info,
    jobs,
    scope,
    context,
    expression
)) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        auto name_hash = calculate_string_hash(named_reference->name.text);

        assert(context->variable_scope_stack.length > 0);

        TypedExpression typed_expression {};
        typed_expression.range = named_reference->range;

        for(size_t i = 0; i < context->variable_scope_stack.length; i += 1) {
            auto current_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1 - i];

            for(auto variable : current_scope.variables) {
                if(variable.name.text == named_reference->name.text) {
                    typed_expression.type = variable.type;

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue::create_addressed_value();

                    return ok(result);
                }
            }

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_scope.constant_scope,
                context,
                named_reference->name.text,
                name_hash,
                scope,
                named_reference->name.range,
                current_scope.constant_scope->statements,
                current_scope.constant_scope->declarations,
                false
            ));

            if(search_value.found) {
                typed_expression.type = search_value.type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = search_value.value;

                return ok(result);
            }
        }

        assert(!context->variable_scope_stack[0].constant_scope->is_top_level);

        auto current_scope = context->variable_scope_stack[0].constant_scope->parent;
        while(true) {
            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_scope,
                context,
                named_reference->name.text,
                name_hash,
                scope,
                named_reference->name.range,
                current_scope->statements,
                current_scope->declarations,
                false
            ));

            if(search_value.found) {
                typed_expression.type = search_value.type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = search_value.value;

                return ok(result);
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(named_reference->name.text == global_constant.name) {
                typed_expression.type = global_constant.type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue(global_constant.value);

                return ok(result);
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %.*s", STRING_PRINTF_ARGUMENTS(named_reference->name.text));

        return err();
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, index_reference->expression));

        expect_delayed(index, generate_expression(info, jobs, scope, context, index_reference->index));

        auto children = context->arena->allocate<TypedExpression>(2);
        children[0] = expression_value.typed_expression;
        children[1] = index.typed_expression;

        TypedExpression typed_expression {};
        typed_expression.range = index_reference->range;
        typed_expression.children = Array(2, children);

        if(expression_value.value.kind == RuntimeValueKind::ConstantValue && index.value.kind == RuntimeValueKind::ConstantValue) {
             expect(constant, evaluate_constant_index(
                context->arena,
                info,
                scope,
                expression_value.typed_expression.type,
                expression_value.value.constant,
                index_reference->expression->range,
                index.typed_expression.type,
                index.value.constant,
                index_reference->index->range
            ));

            typed_expression.type = constant.type;

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = AnyRuntimeValue(constant.value);

            return ok(result);
        }

        expect_void(coerce_to_integer_register_value(
            scope,
            context,
            index_reference->index->range,
            index.typed_expression.type,
            index.value,
            Integer(
                info.architecture_sizes.address_size,
                false
            ),
            false
        ));

        AnyType element_type;
        if(expression_value.typed_expression.type.kind == TypeKind::ArrayTypeType) {
            auto array_type = expression_value.typed_expression.type.array;
            element_type = *array_type.element_type;

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                } else {
                    error(scope, index_reference->expression->range, "Cannot index array constant at runtime");

                    return err();
                }
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
            } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
            } else {
                abort();
            }
        } else if(expression_value.typed_expression.type.kind == TypeKind::StaticArray) {
            auto static_array = expression_value.typed_expression.type.static_array;
            element_type = *static_array.element_type;

            if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                error(scope, index_reference->expression->range, "Cannot index static array constant at runtime");

                return err();
            } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                error(scope, index_reference->expression->range, "Cannot index anonymous static array");

                return err();
            } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
            } else {
                abort();
            }
        } else {
            error(scope, index_reference->expression->range, "Cannot index '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

            return err();
        }

        typed_expression.type = element_type;

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue::create_addressed_value();

        return ok(result);
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, member_reference->expression));

        TypedExpression typed_expression {};
        typed_expression.range = member_reference->range;
        typed_expression.children = Array(1, context->arena->heapify(expression_value.typed_expression));

        AnyType actual_type;
        AnyRuntimeValue actual_value;
        if(expression_value.typed_expression.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.typed_expression.type.pointer;
            actual_type = *pointer.pointed_to_type;

            if(!actual_type.is_runtime_type()) {
                error(scope, member_reference->expression->range, "Cannot access members of '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

                return err();
            }

            actual_value = AnyRuntimeValue::create_addressed_value();
        } else {
            actual_type = expression_value.typed_expression.type;
            actual_value = expression_value.value;
        }

        if(actual_type.kind == TypeKind::ArrayTypeType) {
            auto array_type = actual_type.array;

            if(member_reference->name.text == u8"length"_S) {
                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = expression_value.value.constant.unwrap_array();

                        value = AnyRuntimeValue(AnyConstantValue(array_value.length));
                    } else if(expression_value.value.constant.kind == ConstantValueKind::StaticArrayConstant) {
                        auto static_array_value = expression_value.value.constant.unwrap_static_array();

                        value = AnyRuntimeValue(AnyConstantValue(static_array_value.elements.length));
                    } else {
                        assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                        error(scope, member_reference->range, "Cannot get length of undefined array constant");

                        return err();
                    }
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    value = AnyRuntimeValue::create_register_value();
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    value = AnyRuntimeValue::create_addressed_value();
                } else {
                    abort();
                }

                typed_expression.type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = value;

                return ok(result);
            } else if(member_reference->name.text == u8"pointer"_S) {
                AnyRuntimeValue value;
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = expression_value.value.constant.unwrap_array();

                        value = AnyRuntimeValue(AnyConstantValue(array_value.pointer));
                    } else {
                        error(scope, member_reference->range, "Cannot take pointer to contents of constant array");

                        return err();
                    }
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    value = AnyRuntimeValue::create_register_value();
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                    value = AnyRuntimeValue::create_addressed_value();
                } else {
                    abort();
                }

                typed_expression.type = AnyType(Pointer(array_type.element_type));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = value;

                return ok(result);
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StaticArray) {
            auto static_array = actual_type.static_array;

            if(member_reference->name.text == u8"length"_S) {
                typed_expression.type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue(AnyConstantValue(static_array.length));

                return ok(result);
            } else if(member_reference->name.text == u8"pointer"_S) {
                if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of constant static array");

                    return err();
                } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of r-value static array");

                    return err();
                } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                typed_expression.type = AnyType(Pointer(static_array.element_type));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_register_value();

                return ok(result);
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StructType) {
            auto struct_type = actual_type.struct_;

            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(struct_type.members[i].name == member_reference->name.text) {
                    auto member_type = struct_type.members[i].type;

                    AnyRuntimeValue value;
                    if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::StructConstant) {
                            auto struct_value = expression_value.value.constant.unwrap_struct();

                            value = AnyRuntimeValue(struct_value.members[i]);
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, member_reference->range, "Cannot access members of undefined array constant");

                            return err();
                        }
                    } else if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        value = AnyRuntimeValue::create_register_value();
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        value = AnyRuntimeValue::create_addressed_value();
                    } else {
                        abort();
                    }

                    typed_expression.type = member_type;

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = value;

                    return ok(result);
                }
            }

            error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(actual_type.kind == TypeKind::UnionType) {
            auto union_type = actual_type.union_;

            for(size_t i = 0; i < union_type.members.length; i += 1) {
                if(union_type.members[i].name == member_reference->name.text) {
                    auto member_type = union_type.members[i].type;

                    AnyRuntimeValue value;
                    if(actual_value.kind == RuntimeValueKind::RegisterValue) {
                        value = AnyRuntimeValue::create_register_value();
                    } else if(actual_value.kind == RuntimeValueKind::AddressedValue) {
                        value = AnyRuntimeValue::create_addressed_value();
                    } else {
                        abort();
                    }

                    typed_expression.type = member_type;

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = value;

                    return ok(result);
                }
            }

            error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(actual_type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = actual_type.undetermined_struct;

            if(actual_value.kind == RuntimeValueKind::ConstantValue) {
                auto constant_value = actual_value.constant;

                auto undetermined_struct_value = constant_value.unwrap_struct();

                for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                    if(undetermined_struct.members[i].name == member_reference->name.text) {
                        typed_expression.type = undetermined_struct.members[i].type;

                        GenerateExpressionResult result {};
                        result.typed_expression = typed_expression;
                        result.value = AnyRuntimeValue(undetermined_struct_value.members[i]);

                        return ok(result);
                    }
                }

                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            } else if(actual_value.kind == RuntimeValueKind::UndeterminedStructValue) {
                auto undetermined_struct_value = actual_value.undetermined_struct;

                for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                    if(undetermined_struct.members[i].name == member_reference->name.text) {
                        typed_expression.type = undetermined_struct.members[i].type;

                        GenerateExpressionResult result {};
                        result.typed_expression = typed_expression;
                        result.value = undetermined_struct_value.members[i];

                        return ok(result);
                    }
                }

                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            } else {
                abort();
            }
        } else if(actual_type.kind == TypeKind::FileModule) {
            auto file_module_value = expression_value.value.constant.unwrap_file_module();

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                file_module_value.scope,
                context,
                member_reference->name.text,
                calculate_string_hash(member_reference->name.text),
                scope,
                member_reference->name.range,
                file_module_value.scope->statements,
                file_module_value.scope->declarations,
                true
            ));

            if(search_value.found) {
                typed_expression.type = search_value.type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = search_value.value;

                return ok(result);
            }

            error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(expression_value.typed_expression.type.kind == TypeKind::Type) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto type = constant_value.type;

            if(type.kind == TypeKind::Enum) {
                auto enum_ = type.enum_;

                for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                    if(enum_.definition->variants[i].name.text == member_reference->name.text) {
                        typed_expression.type = type;

                        GenerateExpressionResult result {};
                        result.typed_expression = typed_expression;
                        result.value = AnyRuntimeValue(AnyConstantValue(enum_.variant_values[i]));

                        return ok(result);
                    }
                }

                error(
                    scope,
                    member_reference->name.range,
                    "Enum '%.*s' has no variant with name '%.*s'",
                    STRING_PRINTF_ARGUMENTS(enum_.definition->name.text),
                    STRING_PRINTF_ARGUMENTS(member_reference->name.text)
                );

                return err();
            } else {
                error(
                    scope,
                    member_reference->expression->range,
                    "Type '%.*s' has no members",
                    STRING_PRINTF_ARGUMENTS(type.get_description(context->arena))
                );

                return err();
            }
        } else {
            error(scope, member_reference->expression->range, "Type %.*s has no members", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        TypedExpression typed_expression {};
        typed_expression.range = integer_literal->range;
        typed_expression.type = AnyType::create_undetermined_integer();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue(AnyConstantValue(integer_literal->value));

        return ok(result);
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        TypedExpression typed_expression {};
        typed_expression.range = float_literal->range;
        typed_expression.type = AnyType::create_undetermined_float();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue(AnyConstantValue(float_literal->value));

        return ok(result);
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.length;

        auto characters = context->arena->allocate<AnyConstantValue>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = AnyConstantValue((uint64_t)string_literal->characters[i]);
        }

        TypedExpression typed_expression {};
        typed_expression.range = string_literal->range;
        typed_expression.type = AnyType(StaticArray(
            character_count,
            context->arena->heapify(AnyType(Integer(
                RegisterSize::Size8,
                false
            )))
        ));

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue(AnyConstantValue(StaticArrayConstant(
            Array(character_count, characters)
        )));

        return ok(result);
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.length;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err();
        }

        expect_delayed(first_element, generate_expression(info, jobs, scope, context, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(
            info,
            scope,
            array_literal->elements[0]->range,
            first_element.typed_expression.type
        ));

        if(!determined_element_type.is_runtime_type()) {
            error(scope, array_literal->range, "Arrays cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(determined_element_type.get_description(context->arena)));

            return err();
        }

        auto children = context->arena->allocate<TypedExpression>(element_count);
        children[0] = first_element.typed_expression;

        auto elements = context->arena->allocate<TypedRuntimeValue>(element_count);
        elements[0] = TypedRuntimeValue(first_element.typed_expression.type, first_element.value);

        auto all_constant = first_element.value.kind == RuntimeValueKind::ConstantValue;
        for(size_t i = 1; i < element_count; i += 1) {
            expect_delayed(element, generate_expression(info, jobs, scope, context, array_literal->elements[i]));

            children[i] = element.typed_expression;
            elements[i] = TypedRuntimeValue(element.typed_expression.type, element.value);

            if(element.value.kind != RuntimeValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyRuntimeValue value;
        if(all_constant) {
            auto element_values = context->arena->allocate<AnyConstantValue>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                expect(coerced_constant_value, coerce_constant_to_type(
                    context->arena,
                    info,
                    scope,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    elements[i].value.constant,
                    determined_element_type,
                    false
                ));

                element_values[i] = coerced_constant_value;
            }

            value = AnyRuntimeValue(AnyConstantValue(StaticArrayConstant(
                Array(element_count, element_values)
            )));
        } else {
            for(size_t i = 0; i < element_count; i += 1) {
                expect_void(coerce_to_type_register(
                    info,
                    scope,
                    context,
                    array_literal->elements[i]->range,
                    elements[i].type,
                    elements[i].value,
                    determined_element_type,
                    false
                ));
            }

            value = AnyRuntimeValue::create_register_value();
        }

        TypedExpression typed_expression {};
        typed_expression.range = array_literal->range;
        typed_expression.children = Array(element_count, children);
        typed_expression.type = AnyType(StaticArray(
            element_count,
            context->arena->heapify(determined_element_type)
        ));

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = value;

        return ok(result);
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        if(struct_literal->members.length == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err();
        }

        auto member_count = struct_literal->members.length;

        auto children = context->arena->allocate<TypedExpression>(member_count);
        auto type_members = context->arena->allocate<StructTypeMember>(member_count);
        auto member_values = context->arena->allocate<AnyRuntimeValue>(member_count);
        auto all_constant = true;

        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(struct_literal->members[i].name.text == type_members[j].name) {
                    error(scope, struct_literal->members[i].name.range, "Duplicate struct member %.*s", STRING_PRINTF_ARGUMENTS(struct_literal->members[i].name.text));

                    return err();
                }
            }

            expect_delayed(member, generate_expression(info, jobs, scope, context, struct_literal->members[i].value));

            children[i] = member.typed_expression;

            type_members[i] = {
                struct_literal->members[i].name.text,
                member.typed_expression.type
            };

            member_values[i] = member.value;

            if(member.value.kind != RuntimeValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyRuntimeValue value;
        if(all_constant) {
            auto constant_member_values = context->arena->allocate<AnyConstantValue>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                constant_member_values[i] = member_values[i].constant;
            }

            value = AnyRuntimeValue(AnyConstantValue(StructConstant(
                Array(member_count, constant_member_values)
            )));
        } else {
            value = AnyRuntimeValue(UndeterminedStructValue(
                Array(member_count, member_values)
            ));
        }

        TypedExpression typed_expression {};
        typed_expression.range = struct_literal->range;
        typed_expression.children = Array(member_count, children);
        typed_expression.type = AnyType(UndeterminedStruct(
                Array(member_count, type_members)
            ));

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = value;

        return ok(result);
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, function_call->expression));

        if(expression_value.typed_expression.type.kind == TypeKind::FunctionTypeType || expression_value.typed_expression.type.kind == TypeKind::PolymorphicFunction) {
            auto call_parameter_count = function_call->parameters.length;

            auto children = context->arena->allocate<TypedExpression>(call_parameter_count + 1);
            auto call_parameters = context->arena->allocate<TypedRuntimeValue>(call_parameter_count);
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

                children[i] = parameter_value.typed_expression;
                call_parameters[i] = TypedRuntimeValue(parameter_value.typed_expression.type, parameter_value.value);
            }

            children[call_parameter_count] = expression_value.typed_expression;

            FunctionTypeType function_type;
            FunctionConstant function_value;
            if(expression_value.typed_expression.type.kind == TypeKind::PolymorphicFunction) {
                auto constant_value = expression_value.value.unwrap_constant_value();

                auto polymorphic_function_value = constant_value.unwrap_polymorphic_function();

                auto declaration_parameters = polymorphic_function_value.declaration->parameters;
                auto declaration_parameter_count = declaration_parameters.length;

                if(call_parameter_count != declaration_parameter_count) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        declaration_parameter_count,
                        call_parameter_count
                    );

                    return err();
                }

                auto polymorphic_parameters = context->arena->allocate<TypedConstantValue>(declaration_parameter_count);

                for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                    auto declaration_parameter = declaration_parameters[i];

                    if(declaration_parameter.is_polymorphic_determiner) {
                        polymorphic_parameters[i].type = call_parameters[i].type;
                    }

                    if(declaration_parameter.is_constant) {
                        if(call_parameters[i].value.kind != RuntimeValueKind::ConstantValue) {
                            error(
                                scope,
                                function_call->parameters[i]->range,
                                "Non-constant value provided for constant parameter '%.*s'",
                                STRING_PRINTF_ARGUMENTS(declaration_parameter.name.text)
                            );

                            return err();
                        }

                        polymorphic_parameters[i] = TypedConstantValue(
                            call_parameters[i].type,
                            call_parameters[i].value.constant
                        );
                    }
                }

                auto found = false;
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicFunction) {
                        auto resolve_polymorphic_function = job.resolve_polymorphic_function;

                        if(
                            resolve_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                            resolve_polymorphic_function.scope == polymorphic_function_value.scope
                        ) {
                            auto matching_polymorphic_parameters = true;
                            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                                auto declaration_parameter = declaration_parameters[i];
                                auto call_parameter = polymorphic_parameters[i];
                                auto job_parameter = resolve_polymorphic_function.parameters[i];

                                if(
                                    (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                    job_parameter.type != call_parameter.type
                                ) {
                                    matching_polymorphic_parameters = false;
                                    break;
                                }

                                if(
                                    declaration_parameter.is_constant &&
                                    !constant_values_equal(call_parameter.value, job_parameter.value)
                                ) {
                                    matching_polymorphic_parameters = false;
                                    break;
                                }
                            }

                            if(!matching_polymorphic_parameters) {
                                continue;
                            }

                            if(job.state == JobState::Done) {
                                found = true;

                                function_type = resolve_polymorphic_function.type;
                                function_value = resolve_polymorphic_function.value;

                                break;
                            } else {
                                return wait(i);
                            }  
                        }
                    }
                }

                if(!found) {
                    auto call_parameter_ranges = context->arena->allocate<FileRange>(declaration_parameter_count);

                    for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                        call_parameter_ranges[i] = function_call->parameters[i]->range;
                    }

                    AnyJob job {};
                    job.kind = JobKind::ResolvePolymorphicFunction;
                    job.state = JobState::Working;
                    job.resolve_polymorphic_function.declaration = polymorphic_function_value.declaration;
                    job.resolve_polymorphic_function.parameters = polymorphic_parameters;
                    job.resolve_polymorphic_function.scope = polymorphic_function_value.scope;
                    job.resolve_polymorphic_function.call_scope = scope;
                    job.resolve_polymorphic_function.call_parameter_ranges = call_parameter_ranges;

                    auto job_index = jobs->append(job);

                    return wait(job_index);
                }
            } else {
                function_type = expression_value.typed_expression.type.function;

                auto constant_value = expression_value.value.unwrap_constant_value();

                function_value = constant_value.unwrap_function();

                if(call_parameter_count != function_type.parameters.length) {
                    error(
                        scope,
                        function_call->range,
                        "Incorrect number of parameters. Expected %zu, got %zu",
                        function_type.parameters.length,
                        call_parameter_count
                    );

                    return err();
                }
            }

            auto found = false;
            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::TypeFunctionBody) {
                    auto type_function_body = job.type_function_body;

                    if(
                        AnyType(type_function_body.type) == AnyType(function_type) &&
                        type_function_body.value.declaration == function_value.declaration &&
                        type_function_body.value.body_scope == function_value.body_scope
                    ) {
                        found = true;

                        break;
                    }
                }
            }

            if(!found) {
                AnyJob job {};
                job.kind = JobKind::TypeFunctionBody;
                job.state = JobState::Working;
                job.type_function_body.type = function_type;
                job.type_function_body.value = function_value;

                jobs->append(job);
            }

            size_t runtime_parameter_index = 0;
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                if(!function_value.declaration->parameters[i].is_constant) {
                    expect_void(coerce_to_type_register(
                        info,
                        scope,
                        context,
                        function_call->parameters[i]->range,
                        call_parameters[i].type,
                        call_parameters[i].value,
                        function_type.parameters[i],
                        false
                    ));

                    runtime_parameter_index += 1;
                }
            }

            assert(runtime_parameter_index == function_type.parameters.length);

            AnyType return_type;
            if(function_type.return_types.length == 0) {
                return_type = AnyType::create_void();
            } else if(function_type.return_types.length == 1) {
                return_type = function_type.return_types[0];
            } else {
                return_type = AnyType(MultiReturn(function_type.return_types));
            }

            assert(context->variable_scope_stack.length != 0);
            auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

            AnyRuntimeValue value;
            if(return_type.kind != TypeKind::Void) {
                value = AnyRuntimeValue::create_register_value();
            } else {
                value = AnyRuntimeValue(AnyConstantValue::create_void());
            }

            TypedExpression typed_expression {};
            typed_expression.range = function_call->range;
            typed_expression.children = Array(call_parameter_count + 1, children);
            typed_expression.type = return_type;

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = value;

            return ok(result);
        } else if(expression_value.typed_expression.type.kind == TypeKind::BuiltinFunction) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto builtin_function_value = constant_value.unwrap_builtin_function();

            if(builtin_function_value.name == u8"size_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                AnyType type;
                if(parameter_value.typed_expression.type.kind == TypeKind::Type) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    type = constant_value.unwrap_type();
                } else {
                    type = parameter_value.typed_expression.type;
                }

                if(!type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "'%.*s'' has no size", STRING_PRINTF_ARGUMENTS(parameter_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto size = type.get_size(info.architecture_sizes);

                TypedExpression typed_expression {};
                typed_expression.range = function_call->range;
                typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                typed_expression.type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue(AnyConstantValue(size));

                return ok(result);
            } else if(builtin_function_value.name == u8"type_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                TypedExpression typed_expression {};
                typed_expression.range = function_call->range;
                typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                typed_expression.type = AnyType::create_type_type();

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue(AnyConstantValue(parameter_value.typed_expression.type));

                return ok(result);
            } else if(builtin_function_value.name == u8"globalify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                expect(determined_type, coerce_to_default_type(info, scope, function_call->parameters[0]->range, parameter_value.typed_expression.type));

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description(context->arena)));

                    return err();
                }

                if(parameter_value.value.kind != RuntimeValueKind::ConstantValue) {
                    error(scope, function_call->parameters[0]->range, "Cannot globalify a non-constant value");

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_constant_to_type(
                    context->arena,
                    info,
                    scope,
                    function_call->parameters[0]->range,
                    parameter_value.typed_expression.type,
                    constant_value,
                    determined_type,
                    false
                ));

                TypedExpression typed_expression {};
                typed_expression.range = function_call->range;
                typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                typed_expression.type = determined_type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_addressed_value();

                return ok(result);
            } else if(builtin_function_value.name == u8"stackify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                expect(determined_type, coerce_to_default_type(info, scope, function_call->parameters[0]->range, parameter_value.typed_expression.type));

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description(context->arena)));

                    return err();
                }

                if(parameter_value.value.kind != RuntimeValueKind::ConstantValue) {
                    error(scope, function_call->parameters[0]->range, "Cannot stackify a non-constant value");

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_constant_to_type(
                    context->arena,
                    info,
                    scope,
                    function_call->parameters[0]->range,
                    parameter_value.typed_expression.type,
                    constant_value,
                    determined_type,
                    false
                ));

                TypedExpression typed_expression {};
                typed_expression.range = function_call->range;
                typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                typed_expression.type = determined_type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_addressed_value();

                return ok(result);
            } else if(builtin_function_value.name == u8"sqrt"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[0]));

                if(parameter_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    RegisterSize result_size;
                    double value;
                    if(parameter_value.typed_expression.type.kind == TypeKind::UndeterminedInteger) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        auto integer_value = constant_value.unwrap_integer();

                        result_size = info.architecture_sizes.default_float_size;
                        value = (double)integer_value;
                    } else if(parameter_value.typed_expression.type.kind == TypeKind::UndeterminedFloat) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = info.architecture_sizes.default_float_size;
                        value = constant_value.unwrap_float();
                    } else if(parameter_value.typed_expression.type.kind == TypeKind::FloatType) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = parameter_value.typed_expression.type.float_.size;
                        value = constant_value.unwrap_float();
                    } else {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.typed_expression.type.get_description(context->arena)));

                        return err();
                    }

                    auto result_value = sqrt(value);

                    TypedExpression typed_expression {};
                    typed_expression.range = function_call->range;
                    typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                    typed_expression.type = AnyType(FloatType(result_size));

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue(AnyConstantValue(result_value));

                    return ok(result);
                } else {
                    if(parameter_value.typed_expression.type.kind != TypeKind::FloatType) {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.typed_expression.type.get_description(context->arena)));

                        return err();
                    }

                    TypedExpression typed_expression {};
                    typed_expression.range = function_call->range;
                    typed_expression.children = Array(1, context->arena->heapify(parameter_value.typed_expression));
                    typed_expression.type = parameter_value.typed_expression.type;

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue::create_register_value();

                    return ok(result);
                }
            } else {
                abort();
            }
        } else if(expression_value.typed_expression.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.typed_expression.type.pointer;

            if(pointer.pointed_to_type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                return err();
            }

            auto function_type = pointer.pointed_to_type->function;
            auto parameter_count = function_type.parameters.length;

            if(function_call->parameters.length != parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    parameter_count,
                    function_call->parameters.length
                );

                return err();
            }

            auto children = context->arena->allocate<TypedExpression>(parameter_count + 1);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

                expect_void(coerce_to_type_register(
                    info,
                    scope,
                    context,
                    function_call->parameters[i]->range,
                    parameter_value.typed_expression.type,
                    parameter_value.value,
                    function_type.parameters[i],
                    false
                ));

                children[i] = parameter_value.typed_expression;
            }

            children[parameter_count] = expression_value.typed_expression;

            AnyType return_type;
            if(function_type.return_types.length == 0) {
                return_type = AnyType::create_void();
            } else if(function_type.return_types.length == 1) {
                return_type = function_type.return_types[0];
            } else {
                return_type = AnyType(MultiReturn(function_type.return_types));
            }

            AnyRuntimeValue value;
            if(return_type.kind != TypeKind::Void) {
                value = AnyRuntimeValue::create_register_value();
            } else {
                value = AnyRuntimeValue(AnyConstantValue::create_void());
            }

            TypedExpression typed_expression {};
            typed_expression.range = function_call->range;
            typed_expression.children = Array(parameter_count + 1, children);
            typed_expression.type = return_type;

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = value;

            return ok(result);
        } else if(expression_value.typed_expression.type.kind == TypeKind::Type) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto type = constant_value.unwrap_type();

            if(type.kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = type.polymorphic_struct;
                auto definition = polymorphic_struct.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto children = context->arena->allocate<TypedExpression>(parameter_count + 1);
                auto parameters = context->arena->allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, context, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        context->arena,
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.typed_expression.type,
                        parameter.value,
                        polymorphic_struct.parameter_types[i],
                        false
                    ));

                    children[i] = parameter.typed_expression;
                    parameters[i] = parameter_value;
                }

                children[parameter_count] = expression_value.typed_expression;

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicStruct) {
                        auto resolve_polymorphic_struct = job.resolve_polymorphic_struct;

                        if(resolve_polymorphic_struct.definition == definition && resolve_polymorphic_struct.parameters != nullptr) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameters[i], resolve_polymorphic_struct.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    TypedExpression typed_expression {};
                                    typed_expression.range = function_call->range;
                                    typed_expression.children = Array(parameter_count + 1, children);
                                    typed_expression.type = AnyType::create_type_type();

                                    GenerateExpressionResult result {};
                                    result.typed_expression = typed_expression;
                                    result.value = AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_struct.type));

                                    return ok(result);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job {};
                job.kind = JobKind::ResolvePolymorphicStruct;
                job.state = JobState::Working;
                job.resolve_polymorphic_struct.definition = definition;
                job.resolve_polymorphic_struct.parameters = parameters;
                job.resolve_polymorphic_struct.scope = polymorphic_struct.parent;

                auto job_index = jobs->append(job);

                return wait(job_index);
            } else if(type.kind == TypeKind::PolymorphicUnion) {
                auto polymorphic_union = type.polymorphic_union;
                auto definition = polymorphic_union.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect union parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto children = context->arena->allocate<TypedExpression>(parameter_count + 1);
                auto parameters = context->arena->allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, context, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        context->arena,
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.typed_expression.type,
                        parameter.value,
                        polymorphic_union.parameter_types[i],
                        false
                    ));

                    children[i] = parameter.typed_expression;
                    parameters[i] = parameter_value;
                }

                children[parameter_count] = expression_value.typed_expression;

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicUnion) {
                        auto resolve_polymorphic_union = job.resolve_polymorphic_union;

                        if(resolve_polymorphic_union.definition == definition && resolve_polymorphic_union.parameters != nullptr) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameters[i], resolve_polymorphic_union.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    TypedExpression typed_expression {};
                                    typed_expression.range = function_call->range;
                                    typed_expression.children = Array(parameter_count + 1, children);
                                    typed_expression.type = AnyType::create_type_type();

                                    GenerateExpressionResult result {};
                                    result.typed_expression = typed_expression;
                                    result.value = AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_union.type));

                                    return ok(result);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job {};
                job.kind = JobKind::ResolvePolymorphicUnion;
                job.state = JobState::Working;
                job.resolve_polymorphic_union.definition = definition;
                job.resolve_polymorphic_union.parameters = parameters;
                job.resolve_polymorphic_union.scope = polymorphic_union.parent;

                auto job_index = jobs->append(job);

                return wait(job_index);
            } else {
                error(scope, function_call->expression->range, "Type '%.*s' is not polymorphic", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                return err();
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        expect_delayed(result_value, generate_binary_operation(
            info,
            jobs,
            scope,
            context,
            binary_operation->range,
            binary_operation->left,
            binary_operation->right,
            binary_operation->binary_operator
        ));

        return ok(result_value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, unary_operation->expression));

        TypedExpression typed_expression {};
        typed_expression.range = unary_operation->range;
        typed_expression.children = Array(1, context->arena->heapify(expression_value.typed_expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    auto constant_value = expression_value.value.constant;

                    if(expression_value.typed_expression.type.kind == TypeKind::FunctionTypeType) {
                        auto function = expression_value.typed_expression.type.function;

                        auto function_value = constant_value.unwrap_function();

                        auto found = false;
                        for(size_t i = 0; i < jobs->length; i += 1) {
                            auto job = (*jobs)[i];

                            if(job.kind == JobKind::TypeFunctionBody) {
                                auto type_function_body = job.type_function_body;

                                if(
                                    AnyType(type_function_body.type) == AnyType(function) &&
                                    type_function_body.value.declaration == function_value.declaration &&
                                    type_function_body.value.body_scope == function_value.body_scope
                                ) {
                                    found = true;

                                    break;
                                }
                            }
                        }

                        if(!found) {
                            AnyJob job {};
                            job.kind = JobKind::TypeFunctionBody;
                            job.state = JobState::Working;
                            job.type_function_body.type = function;
                            job.type_function_body.value = function_value;

                            jobs->append(job);
                        }
                    } else if(expression_value.typed_expression.type.kind == TypeKind::Type) {
                        auto type = constant_value.unwrap_type();

                        if(!type.is_pointable_type()) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                            return err();
                        }

                        typed_expression.type = AnyType::create_type_type();

                        GenerateExpressionResult result {};
                        result.typed_expression = typed_expression;
                        result.value = AnyRuntimeValue(AnyConstantValue(AnyType(Pointer(context->arena->heapify(type)))));

                        return ok(result);
                    } else {
                        error(scope, unary_operation->expression->range, "Cannot take pointers to constants of type '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                        return err();
                    }
                } else if(
                    expression_value.value.kind == RuntimeValueKind::RegisterValue ||
                    expression_value.value.kind == RuntimeValueKind::UndeterminedStructValue
                ) {
                    error(scope, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return err();
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                typed_expression.type = AnyType(Pointer(context->arena->heapify(expression_value.typed_expression.type)));

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_register_value();

                return ok(result);
            } break;

            case UnaryOperation::Operator::PointerDereference: {
                if(expression_value.typed_expression.type.kind != TypeKind::Pointer) {
                    error(scope, unary_operation->expression->range, "Expected a pointer, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto pointed_to_type = *expression_value.typed_expression.type.pointer.pointed_to_type;

                if(!pointed_to_type.is_runtime_type()) {
                    error(scope, unary_operation->expression->range, "Cannot dereference pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(pointed_to_type.get_description(context->arena)));

                    return err();
                }

                typed_expression.type = pointed_to_type;

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_addressed_value();

                return ok(result);
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.typed_expression.type.kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::BooleanConstant) {
                        auto boolean_value = expression_value.value.constant.unwrap_boolean();

                        typed_expression.type = AnyType::create_boolean();

                        GenerateExpressionResult result {};
                        result.typed_expression = typed_expression;
                        result.value = AnyRuntimeValue(AnyConstantValue(!boolean_value));

                        return ok(result);
                    } else {
                        assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                        error(scope, unary_operation->expression->range, "Cannot invert an undefined boolean constant");

                        return err();
                    }
                } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                typed_expression.type = AnyType::create_boolean();

                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue::create_register_value();

                return ok(result);
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.typed_expression.type.kind == TypeKind::UndeterminedInteger) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto integer_value = constant_value.unwrap_integer();

                    typed_expression.type = AnyType::create_undetermined_integer();

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue(AnyConstantValue((uint64_t)-(int64_t)integer_value));

                    return ok(result);
                } else if(expression_value.typed_expression.type.kind == TypeKind::Integer) {
                    auto integer = expression_value.typed_expression.type.integer;

                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::IntegerConstant) {
                            auto integer_value = expression_value.value.constant.unwrap_integer();

                            typed_expression.type = AnyType::create_undetermined_integer();

                            GenerateExpressionResult result {};
                            result.typed_expression = typed_expression;
                            result.value = AnyRuntimeValue(AnyConstantValue((uint64_t)-(int64_t)integer_value));

                            return ok(result);
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined integer constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    } else {
                        abort();
                    }

                    typed_expression.type = AnyType(integer);

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue::create_register_value();

                    return ok(result);
                } else if(expression_value.typed_expression.type.kind == TypeKind::FloatType) {
                    auto float_type = expression_value.typed_expression.type.float_;

                    if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::FloatConstant) {
                            auto float_value = expression_value.value.constant.unwrap_float();

                            typed_expression.type = AnyType(float_type);

                            GenerateExpressionResult result {};
                            result.typed_expression = typed_expression;
                            result.value = AnyRuntimeValue(AnyConstantValue(-float_value));

                            return ok(result);
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined float constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    } else {
                        abort();
                    }

                    typed_expression.type = AnyType(float_type);

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue::create_register_value();

                    return ok(result);
                } else if(expression_value.typed_expression.type.kind == TypeKind::UndeterminedFloat) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto float_value = constant_value.unwrap_float();

                    typed_expression.type = AnyType::create_undetermined_float();

                    GenerateExpressionResult result {};
                    result.typed_expression = typed_expression;
                    result.value = AnyRuntimeValue(AnyConstantValue(-float_value));

                    return ok(result);
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, cast->expression));

        expect_delayed(target_type, evaluate_type_expression(info, jobs, scope, context, cast->type));

        auto children = context->arena->allocate<TypedExpression>(2);
        children[0] = expression_value.typed_expression;
        children[1] = target_type.typed_expression;

        TypedExpression typed_expression {};
        typed_expression.range = cast->range;
        typed_expression.children = Array(2, children);
        typed_expression.type = target_type.type;

        if(expression_value.value.kind == RuntimeValueKind::ConstantValue) {
            auto constant_cast_result = evaluate_constant_cast(
                context->arena,
                info,
                scope,
                expression_value.typed_expression.type,
                expression_value.value.constant,
                cast->expression->range,
                target_type.type,
                cast->type->range,
                true
            );

            if(constant_cast_result.status) {
                GenerateExpressionResult result {};
                result.typed_expression = typed_expression;
                result.value = AnyRuntimeValue(constant_cast_result.value);

                return ok(result);
            }
        }

        auto coercion_result = coerce_to_type_register(
            info,
            scope,
            context,
            cast->range,
            expression_value.typed_expression.type,
            expression_value.value,
            target_type.type,
            true
        );

        auto has_cast = false;
        if(coercion_result.status) {
            has_cast = true;
        } else if(target_type.type.kind == TypeKind::Integer) {
            auto target_integer = target_type.type.integer;

            if(expression_value.typed_expression.type.kind == TypeKind::Integer) {
                auto integer = expression_value.typed_expression.type.integer;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.typed_expression.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.typed_expression.type.float_;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.typed_expression.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.typed_expression.type.pointer;

                if(target_integer.size == info.architecture_sizes.address_size && !target_integer.is_signed) {
                    has_cast = true;

                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    } else {
                        abort();
                    }
                }
            }
        } else if(target_type.type.kind == TypeKind::FloatType) {
            auto target_float_type = target_type.type.float_;

            if(expression_value.typed_expression.type.kind == TypeKind::Integer) {
                auto integer = expression_value.typed_expression.type.integer;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.typed_expression.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.typed_expression.type.float_;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                has_cast = true;
            }
        } else if(target_type.type.kind == TypeKind::Pointer) {
            auto target_pointer = target_type.type.pointer;

            if(expression_value.typed_expression.type.kind == TypeKind::Integer) {
                auto integer = expression_value.typed_expression.type.integer;

                if(integer.size == info.architecture_sizes.address_size && !integer.is_signed) {
                    has_cast = true;

                    if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                    } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                    } else {
                        abort();
                    }
                }
            } else if(expression_value.typed_expression.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.typed_expression.type.pointer;
                has_cast = true;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }
            }
        } else if(target_type.type.kind == TypeKind::Enum) {
            auto target_enum = target_type.type.enum_;

            if(expression_value.typed_expression.type.kind == TypeKind::Integer) {
                auto integer = expression_value.typed_expression.type.integer;

                if(expression_value.value.kind == RuntimeValueKind::RegisterValue) {
                } else if(expression_value.value.kind == RuntimeValueKind::AddressedValue) {
                } else {
                    abort();
                }

                has_cast = true;
            }
        } else {
            abort();
        }

        if(has_cast) {
            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = AnyRuntimeValue::create_register_value();

            return ok(result);
        } else {
            error(scope, cast->range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)), STRING_PRINTF_ARGUMENTS(target_type.type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, generate_expression(info, jobs, scope, context, function_call->expression));

        auto call_parameter_count = function_call->parameters.length;

        auto children = context->arena->allocate<TypedExpression>(call_parameter_count + 1);
        auto call_parameters = context->arena->allocate<TypedRuntimeValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, generate_expression(info, jobs, scope, context, function_call->parameters[i]));

            children[i] = parameter_value.typed_expression;
            call_parameters[i] = TypedRuntimeValue(parameter_value.typed_expression.type, parameter_value.value);
        }

        children[call_parameter_count] = expression_value.typed_expression;

        if(expression_value.typed_expression.type.kind == TypeKind::PolymorphicFunction) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto polymorphic_function_value = constant_value.unwrap_polymorphic_function();

            auto declaration_parameters = polymorphic_function_value.declaration->parameters;
            auto declaration_parameter_count = declaration_parameters.length;

            if(call_parameter_count != declaration_parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    declaration_parameter_count,
                    call_parameter_count
                );

                return err();
            }

            auto polymorphic_parameters = context->arena->allocate<TypedConstantValue>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                auto declaration_parameter = declaration_parameters[i];

                if(declaration_parameter.is_polymorphic_determiner) {
                    polymorphic_parameters[i].type = call_parameters[i].type;
                }

                if(declaration_parameter.is_constant) {
                    if(call_parameters[i].value.kind != RuntimeValueKind::ConstantValue) {
                        error(
                            scope,
                            function_call->parameters[i]->range,
                            "Non-constant value provided for constant parameter '%.*s'",
                            STRING_PRINTF_ARGUMENTS(declaration_parameter.name.text)
                        );

                        return err();
                    }

                    polymorphic_parameters[i] = TypedConstantValue(
                        call_parameters[i].type,
                        call_parameters[i].value.constant
                    );
                }
            }

            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolvePolymorphicFunction) {
                    auto resolve_polymorphic_function = job.resolve_polymorphic_function;

                    if(
                        resolve_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                        resolve_polymorphic_function.scope == polymorphic_function_value.scope
                    ) {
                        auto matching_polymorphic_parameters = true;
                        for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                            auto declaration_parameter = declaration_parameters[i];
                            auto call_parameter = polymorphic_parameters[i];
                            auto job_parameter = resolve_polymorphic_function.parameters[i];

                            if(
                                (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                job_parameter.type != call_parameter.type
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }

                            if(
                                declaration_parameter.is_constant &&
                                !constant_values_equal( call_parameter.value, job_parameter.value)
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }
                        }

                        if(!matching_polymorphic_parameters) {
                            continue;
                        }

                        if(job.state == JobState::Done) {
                            TypedExpression typed_expression {};
                            typed_expression.range = function_call->range;
                            typed_expression.children = Array(call_parameter_count + 1, children);
                            typed_expression.type = AnyType(resolve_polymorphic_function.type);

                            GenerateExpressionResult result {};
                            result.typed_expression = typed_expression;
                            result.value = AnyRuntimeValue(AnyConstantValue(resolve_polymorphic_function.value));

                            return ok(result);
                        } else {
                            return wait(i);
                        }  
                    }
                }
            }

            auto call_parameter_ranges = context->arena->allocate<FileRange>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                call_parameter_ranges[i] = function_call->parameters[i]->range;
            }

            AnyJob job {};
            job.kind = JobKind::ResolvePolymorphicFunction;
            job.state = JobState::Working;
            job.resolve_polymorphic_function.declaration = polymorphic_function_value.declaration;
            job.resolve_polymorphic_function.parameters = polymorphic_parameters;
            job.resolve_polymorphic_function.scope = polymorphic_function_value.scope;
            job.resolve_polymorphic_function.call_scope = scope;
            job.resolve_polymorphic_function.call_parameter_ranges = call_parameter_ranges;

            auto job_index = jobs->append(job);

            return wait(job_index);
        } else if(expression_value.typed_expression.type.kind == TypeKind::FunctionTypeType) {
            auto function_type = expression_value.typed_expression.type.function;

            auto constant_value = expression_value.value.unwrap_constant_value();

            auto function_value = constant_value.unwrap_function();

            if(call_parameter_count != function_type.parameters.length) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    function_type.parameters.length,
                    call_parameter_count
                );

                return err();
            }

            TypedExpression typed_expression {};
            typed_expression.range = function_call->range;
            typed_expression.type = AnyType(function_type);

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = AnyRuntimeValue(AnyConstantValue(function_value));

            return ok(result);
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.typed_expression.type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type_value, evaluate_type_expression(info, jobs, scope, context, array_type->expression));

        if(!type_value.type.is_runtime_type()) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

            return err();
        }

        if(array_type->length != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, context, array_type->length));

            expect(length, coerce_constant_to_integer_type(
                context->arena,
                scope,
                array_type->length->range,
                index_value.typed_expression.type,
                index_value.value,
                Integer(
                    info.architecture_sizes.address_size,
                    false
                ),
                false
            ));

            if(length.kind == ConstantValueKind::UndefConstant) {
                error(scope, array_type->length->range, "Length cannot be undefined");

                return err();
            }

            auto length_integer = length.unwrap_integer();

            auto children = context->arena->allocate<TypedExpression>(2);
            children[0] = type_value.typed_expression;
            children[1] = index_value.typed_expression;

            TypedExpression typed_expression {};
            typed_expression.range = array_type->range;
            typed_expression.children = Array(2, children);
            typed_expression.type = AnyType::create_type_type();

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = AnyRuntimeValue(AnyConstantValue(AnyType(StaticArray(
                length_integer,
                context->arena->heapify(type_value.type)
            ))));

            return ok(result);
        } else {
            TypedExpression typed_expression {};
            typed_expression.range = array_type->range;
            typed_expression.children = Array(1, context->arena->heapify(type_value.typed_expression));
            typed_expression.type = AnyType::create_type_type();

            GenerateExpressionResult result {};
            result.typed_expression = typed_expression;
            result.value = AnyRuntimeValue(AnyConstantValue(AnyType(ArrayTypeType(
                context->arena->heapify(type_value.type)
            ))));

            return ok(result);
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.length;
        auto return_type_count = function_type->return_types.length;

        auto children = context->arena->allocate<TypedExpression>(parameter_count + return_type_count);
        auto parameters = context->arena->allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err();
            }

            expect_delayed(type_value, evaluate_type_expression(info, jobs, scope, context, parameter.type));

            if(!type_value.type.is_runtime_type()) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                return err();
            }

            children[i] = type_value.typed_expression;
            parameters[i] = type_value.type;
        }

        auto return_types = context->arena->allocate<AnyType>(return_type_count);

        for(size_t i = 0; i < return_type_count; i += 1) {
            auto expression = function_type->return_types[i];

            expect_delayed(type_value, evaluate_type_expression(info, jobs, scope, context, expression));

            if(!type_value.type.is_runtime_type()) {
                error(scope, expression->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                return err();
            }

            children[parameter_count + i] = type_value.typed_expression;
            return_types[i] = type_value.type;
        }

        auto is_calling_convention_specified = false;
        auto calling_convention = CallingConvention::Default;
        for(auto tag : function_type->tags) {
            if(tag.name.text == u8"extern"_S) {
                error(scope, tag.range, "Function types cannot be external");

                return err();
            } else if(tag.name.text == u8"no_mangle"_S) {
                error(scope, tag.range, "Function types cannot be no_mangle");

                return err();
            } else if(tag.name.text == u8"call_conv"_S) {
                if(is_calling_convention_specified) {
                    error(scope, tag.range, "Duplicate 'call_conv' tag");

                    return err();
                }

                if(tag.parameters.length != 1) {
                    error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                    return err();
                }

                expect_delayed(parameter, evaluate_constant_expression(context->arena, info, jobs, scope, nullptr, tag.parameters[0]));

                expect(calling_convention_name, array_to_string(context->arena, scope, tag.parameters[0]->range, parameter.type, parameter.value));

                if(calling_convention_name == u8"default"_S) {
                    calling_convention = CallingConvention::Default;
                } else if(calling_convention_name == u8"stdcall"_S) {
                    calling_convention = CallingConvention::StdCall;
                }

                is_calling_convention_specified = true;
            } else {
                error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                return err();
            }
        }

        TypedExpression typed_expression {};
        typed_expression.range = function_type->range;
        typed_expression.children = Array(parameter_count + return_type_count, children);
        typed_expression.type = AnyType::create_type_type();

        GenerateExpressionResult result {};
        result.typed_expression = typed_expression;
        result.value = AnyRuntimeValue(AnyConstantValue(AnyType(FunctionTypeType(
            Array(parameter_count, parameters),
            Array(return_type_count, return_types),
            calling_convention
        ))));

        return ok(result);
    } else {
        abort();
    }
}

static bool is_runtime_statement(Statement* statement) {
    return !(
        statement->kind == StatementKind::FunctionDeclaration ||
        statement->kind == StatementKind::ConstantDefinition ||
        statement->kind == StatementKind::StructDefinition ||
        statement->kind == StatementKind::UnionDefinition ||
        statement->kind == StatementKind::EnumDefinition ||
        statement->kind == StatementKind::StaticIf
    );
}

static_profiled_function(DelayedResult<Array<TypedStatement>>, generate_runtime_statements, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Array<Statement*> statements
), (
    info,
    jobs,
    scope,
    context,
    statements
)) {
    List<TypedStatement> typed_statements(context->arena);

    auto unreachable = false;
    for(auto statement : statements) {
        if(is_runtime_statement(statement)) {
            if(unreachable) {
                error(scope, statement->range, "Unreachable code");

                return err();
            }

            TypedStatement typed_statement {};
            typed_statement.range = statement->range;

            if(statement->kind == StatementKind::ExpressionStatement) {
                auto expression_statement = (ExpressionStatement*)statement;

                expect_delayed(value, generate_expression(info, jobs, scope, context, expression_statement->expression));

                typed_statement.expressions = Array(1, context->arena->heapify(value.typed_expression));
            } else if(statement->kind == StatementKind::VariableDeclaration) {
                auto variable_declaration = (VariableDeclaration*)statement;

                for(auto tag : variable_declaration->tags) {
                    if(tag.name.text == u8"extern"_S) {
                        error(scope, variable_declaration->range, "Local variables cannot be external");

                        return err();
                    } else if(tag.name.text == u8"no_mangle"_S) {
                        error(scope, variable_declaration->range, "Local variables cannot be no_mangle");

                        return err();
                    } else {
                        error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                        return err();
                    }
                }

                if(variable_declaration->initializer == nullptr) {
                    error(scope, variable_declaration->range, "Variable must be initialized");

                    return err();
                }

                AnyType type;
                if(variable_declaration->type != nullptr) {
                    expect_delayed(type_value, evaluate_type_expression(info, jobs, scope, context, variable_declaration->type));

                    if(!type_value.type.is_runtime_type()) {
                        error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                        return err();
                    }

                    type = type_value.type;

                    expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                    auto children = context->arena->allocate<TypedExpression>(2);
                    children[0] = type_value.typed_expression;
                    children[1] = initializer_value.typed_expression;

                    typed_statement.expressions = Array(2, children);
                } else {
                    expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                    expect(actual_type, coerce_to_default_type(info, scope, variable_declaration->initializer->range, initializer_value.typed_expression.type));

                    typed_statement.expressions = Array(1, context->arena->heapify(initializer_value.typed_expression));

                    if(!actual_type.is_runtime_type()) {
                        error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

                        return err();
                    }

                    type = actual_type;

                    expect_void(coerce_to_type_register(
                        info,
                        scope,
                        context,
                        variable_declaration->range,
                        initializer_value.typed_expression.type,
                        initializer_value.value,
                        type,
                        false
                    ));
                }

                if(
                    !add_new_variable(
                        context,
                        variable_declaration->name,
                        type
                    ).status
                ) {
                    return err();
                }
            } else if(statement->kind == StatementKind::MultiReturnVariableDeclaration) {
                auto variable_declaration = (MultiReturnVariableDeclaration*)statement;

                assert(variable_declaration->names.length > 1);

                expect_delayed(initializer_value, generate_expression(info, jobs, scope, context, variable_declaration->initializer));

                typed_statement.expressions = Array(1, context->arena->heapify(initializer_value.typed_expression));

                if(initializer_value.typed_expression.type.kind != TypeKind::MultiReturn) {
                    error(scope, variable_declaration->initializer->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(initializer_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto return_types = initializer_value.typed_expression.type.multi_return.types;

                if(return_types.length != variable_declaration->names.length) {
                    error(
                        scope,
                        variable_declaration->initializer->range,
                        "Incorrect number of return values. Expected %zu, got %zu",
                        variable_declaration->names.length,
                        return_types.length
                    );

                    return err();
                }

                for(size_t i = 0; i < return_types.length; i += 1) {
                    if(
                        !add_new_variable(
                            context,
                            variable_declaration->names[i],
                            return_types[i]
                        ).status
                    ) {
                        return err();
                    }
                }
            } else if(statement->kind == StatementKind::Assignment) {
                auto assignment = (Assignment*)statement;

                expect_delayed(target, generate_expression(info, jobs, scope, context, assignment->target));

                if(target.value.kind == RuntimeValueKind::AddressedValue){
                } else {
                    error(scope, assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(value, generate_expression(info, jobs, scope, context, assignment->value));

                auto children = context->arena->allocate<TypedExpression>(2);
                children[0] = target.typed_expression;
                children[1] = value.typed_expression;

                typed_statement.expressions = Array(2, children);

                expect_void(coerce_to_type_register(
                    info,
                    scope,
                    context,
                    assignment->range,
                    value.typed_expression.type,
                    value.value,
                    target.typed_expression.type,
                    false
                ));
            } else if(statement->kind == StatementKind::MultiReturnAssignment) {
                auto assignment = (MultiReturnAssignment*)statement;

                assert(assignment->targets.length > 1);

                expect_delayed(value, generate_expression(info, jobs, scope, context, assignment->value));

                if(value.typed_expression.type.kind != TypeKind::MultiReturn) {
                    error(scope, assignment->value->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto return_types = value.typed_expression.type.multi_return.types;

                if(return_types.length != assignment->targets.length) {
                    error(
                        scope,
                        assignment->value->range,
                        "Incorrect number of return values. Expected %zu, got %zu",
                        assignment->targets.length,
                        return_types.length
                    );

                    return err();
                }

                auto children = context->arena->allocate<TypedExpression>(return_types.length + 1);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    expect_delayed(target, generate_expression(info, jobs, scope, context, assignment->targets[i]));

                    if(target.value.kind == RuntimeValueKind::AddressedValue){
                    } else {
                        error(scope, assignment->targets[i]->range, "Value is not assignable");

                        return err();
                    }

                    expect_void(coerce_to_type_register(
                        info,
                        scope,
                        context,
                        assignment->range,
                        return_types[i],
                        AnyRuntimeValue::create_register_value(),
                        target.typed_expression.type,
                        false
                    ));

                    children[i] = target.typed_expression;
                }

                children[return_types.length] = value.typed_expression;

                typed_statement.expressions = Array(return_types.length + 1, children);
            } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
                auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

                expect_delayed(target, generate_expression(info, jobs, scope, context, binary_operation_assignment->target));

                if(target.value.kind == RuntimeValueKind::AddressedValue){
                } else {
                    error(scope, binary_operation_assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(value, generate_binary_operation(
                    info,
                    jobs,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    binary_operation_assignment->target,
                    binary_operation_assignment->value,
                    binary_operation_assignment->binary_operator
                ));

                auto children = context->arena->allocate<TypedExpression>(2);
                children[0] = target.typed_expression;
                children[1] = value.typed_expression;

                typed_statement.expressions = Array(2, children);

                expect_void(coerce_to_type_register(
                    info,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    value.typed_expression.type,
                    value.value,
                    target.typed_expression.type,
                    false
                ));
            } else if(statement->kind == StatementKind::IfStatement) {
                auto if_statement = (IfStatement*)statement;

                expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->condition));

                auto child_expression_count = if_statement->else_ifs.length + 1;

                auto child_expressions = context->arena->allocate<TypedExpression>(child_expression_count);

                typed_statement.expressions = Array(1, context->arena->heapify(condition.typed_expression));

                size_t child_statement_count = if_statement->statements.length;
                for(auto else_if : if_statement->else_ifs) {
                    child_statement_count += else_if.statements.length;
                }
                child_statement_count += if_statement->else_statements.length;

                auto child_statements = context->arena->allocate<TypedStatement>(child_statement_count);

                size_t child_statement_index = 0;

                if(condition.typed_expression.type.kind != TypeKind::Boolean) {
                    error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto if_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                VariableScope if_variable_scope {};
                if_variable_scope.constant_scope = if_scope;
                if_variable_scope.variables.arena = context->arena;

                context->variable_scope_stack.append(if_variable_scope);

                expect_delayed(main_child_statements, generate_runtime_statements(info, jobs, if_scope, context, if_statement->statements));
                for(auto typed_statement : main_child_statements) {
                    child_statements[child_statement_index] = typed_statement;
                    child_statement_index += 1;
                }

                context->variable_scope_stack.length -= 1;

                for(size_t i = 0; i < if_statement->else_ifs.length; i += 1) {
                    expect_delayed(condition, generate_expression(info, jobs, scope, context, if_statement->else_ifs[i].condition));

                    child_expressions[i] = condition.typed_expression;

                    if(condition.typed_expression.type.kind != TypeKind::Boolean) {
                        error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.typed_expression.type.get_description(context->arena)));

                        return err();
                    }

                    auto else_if_scope = context->child_scopes[context->next_child_scope_index];
                    context->next_child_scope_index += 1;
                    assert(context->next_child_scope_index <= context->child_scopes.length);

                    VariableScope else_if_variable_scope {};
                    else_if_variable_scope.constant_scope = else_if_scope;
                    else_if_variable_scope.variables.arena = context->arena;

                    context->variable_scope_stack.append(else_if_variable_scope);

                    expect_delayed(else_if_child_statements, generate_runtime_statements(info, jobs, if_scope, context, if_statement->else_ifs[i].statements));
                    for(auto typed_statement : else_if_child_statements) {
                        child_statements[child_statement_index] = typed_statement;
                        child_statement_index += 1;
                    }

                    context->variable_scope_stack.length -= 1;
                }

                if(if_statement->else_statements.length != 0) {
                    auto else_scope = context->child_scopes[context->next_child_scope_index];
                    context->next_child_scope_index += 1;
                    assert(context->next_child_scope_index <= context->child_scopes.length);

                    VariableScope else_variable_scope {};
                    else_variable_scope.constant_scope = else_scope;
                    else_variable_scope.variables.arena = context->arena;

                    context->variable_scope_stack.append(else_variable_scope);

                    expect_delayed(else_child_statements, generate_runtime_statements(info, jobs, else_scope, context, if_statement->else_statements));
                    for(auto typed_statement : else_child_statements) {
                        child_statements[child_statement_index] = typed_statement;
                        child_statement_index += 1;
                    }

                    context->variable_scope_stack.length -= 1;
                }

                child_expressions[child_expression_count - 1] = condition.typed_expression;

                assert(child_statement_index == child_statement_count);

                typed_statement.expressions = Array(child_expression_count, child_expressions);
                typed_statement.children = Array(child_statement_count, child_statements);
            } else if(statement->kind == StatementKind::WhileLoop) {
                auto while_loop = (WhileLoop*)statement;

                expect_delayed(condition, generate_expression(info, jobs, scope, context, while_loop->condition));

                typed_statement.expressions = Array(1, context->arena->heapify(condition.typed_expression));

                if(condition.typed_expression.type.kind != TypeKind::Boolean) {
                    error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                auto while_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                VariableScope while_variable_scope {};
                while_variable_scope.constant_scope = while_scope;
                while_variable_scope.variables.arena = context->arena;

                context->variable_scope_stack.append(while_variable_scope);

                auto old_in_breakable_scope = context->in_breakable_scope;

                context->in_breakable_scope = true;

                expect_delayed(child_statements, generate_runtime_statements(info, jobs, while_scope, context, while_loop->statements));

                typed_statement.children = child_statements;

                context->in_breakable_scope = old_in_breakable_scope;

                context->variable_scope_stack.length -= 1;
            } else if(statement->kind == StatementKind::ForLoop) {
                auto for_loop = (ForLoop*)statement;

                Identifier index_name {};
                if(for_loop->has_index_name) {
                    index_name = for_loop->index_name;
                } else {
                    index_name.text = u8"it"_S;
                    index_name.range = for_loop->range;
                }

                expect_delayed(from_value, generate_expression(info, jobs, scope, context, for_loop->from));

                expect_delayed(to_value, generate_expression(info, jobs, scope, context, for_loop->to));

                auto child_expressions = context->arena->allocate<TypedExpression>(2);
                child_expressions[0] = from_value.typed_expression;
                child_expressions[1] = to_value.typed_expression;

                Integer determined_index_type;
                if(from_value.typed_expression.type.kind == TypeKind::UndeterminedInteger && to_value.typed_expression.type.kind == TypeKind::UndeterminedInteger) {
                    determined_index_type = Integer(
                        info.architecture_sizes.default_integer_size,
                        true
                    );
                } else if(from_value.typed_expression.type.kind == TypeKind::Integer) {
                    determined_index_type = from_value.typed_expression.type.integer;
                } else if(to_value.typed_expression.type.kind == TypeKind::Integer) {
                    determined_index_type = to_value.typed_expression.type.integer;
                } else {
                    error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(from_value.typed_expression.type.get_description(context->arena)));

                    return err();
                }

                expect_void(coerce_to_integer_register_value(
                    scope,
                    context,
                    for_loop->from->range,
                    from_value.typed_expression.type,
                    from_value.value,
                    determined_index_type,
                    false
                ));

                expect_void(coerce_to_integer_register_value(
                    scope,
                    context,
                    for_loop->from->range,
                    to_value.typed_expression.type,
                    to_value.value,
                    determined_index_type,
                    false
                ));

                auto for_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                assert(context->variable_scope_stack.length != 0);
                auto parent_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                VariableScope for_variable_scope {};
                for_variable_scope.constant_scope = for_scope;
                for_variable_scope.variables.arena = context->arena;

                context->variable_scope_stack.append(for_variable_scope);

                auto old_in_breakable_scope = context->in_breakable_scope;

                context->in_breakable_scope = true;

                expect_void(add_new_variable(
                    context,
                    index_name,
                    AnyType(determined_index_type)
                ));

                expect_delayed(child_statements, generate_runtime_statements(info, jobs, for_scope, context, for_loop->statements));

                typed_statement.children = child_statements;

                context->in_breakable_scope = old_in_breakable_scope;

                context->variable_scope_stack.length -= 1;
            } else if(statement->kind == StatementKind::ReturnStatement) {
                auto return_statement = (ReturnStatement*)statement;

                unreachable = true;

                assert(context->variable_scope_stack.length != 0);
                auto current_variable_scope = context->variable_scope_stack[context->variable_scope_stack.length - 1];

                if(return_statement->values.length != context->return_types.length) {
                    error(
                        scope,
                        return_statement->range,
                        "Incorrect number of returns, expected %zu, got %zu",
                        context->return_types.length,
                        return_statement->values.length
                    );

                    return err();
                }

                auto return_type_count = context->return_types.length;

                auto child_expressions = context->arena->allocate<TypedExpression>(return_type_count);

                for(size_t i = 0; i < return_type_count; i += 1) {
                    expect_delayed(value, generate_expression(info, jobs, scope, context, return_statement->values[i]));

                    expect_void(coerce_to_type_register(
                        info,
                        scope,
                        context,
                        return_statement->values[i]->range,
                        value.typed_expression.type,
                        value.value,
                        context->return_types[i],
                        false
                    ));

                    child_expressions[i] = value.typed_expression;
                }

                typed_statement.expressions = Array(return_type_count, child_expressions);
            } else if(statement->kind == StatementKind::BreakStatement) {
                auto break_statement = (BreakStatement*)statement;

                unreachable = true;

                if(!context->in_breakable_scope) {
                    error(scope, break_statement->range, "Not in a break-able scope");

                    return err();
                }
            } else if(statement->kind == StatementKind::InlineAssembly) {
                auto inline_assembly = (InlineAssembly*)statement;

                auto child_expressions = context->arena->allocate<TypedExpression>(inline_assembly->bindings.length);

                for(size_t i = 0; i < inline_assembly->bindings.length; i += 1) {
                    auto binding = inline_assembly->bindings[i];

                    if(binding.constraint.length < 1) {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    }

                    expect(value, generate_expression(
                        info,
                        jobs,
                        scope,
                        context,
                        binding.value
                    ));

                    child_expressions[i] = value.typed_expression;

                    if(binding.constraint[0] == '=') {
                        if(binding.constraint.length < 2) {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(binding.constraint[1] == '*') {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(value.value.kind != RuntimeValueKind::AddressedValue) {
                            error(scope, binding.value->range, "Output binding value must be assignable");

                            return err();
                        }
                    } else if(binding.constraint[0] == '*') {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    } else {
                        expect(determined_value_type, coerce_to_default_type(info, scope, binding.value->range, value.typed_expression.type));

                        if(!determined_value_type.is_runtime_type()) {
                            error(scope, binding.value->range, "Value of type '%.*s' cannot be used as a binding", STRING_PRINTF_ARGUMENTS(determined_value_type.get_description(context->arena)));

                            return err();
                        }

                        expect_void(coerce_to_type_register(
                            info,
                            scope,
                            context,
                            binding.value->range,
                            value.typed_expression.type,
                            value.value,
                            determined_value_type,
                            false
                        ));
                    }
                }

                typed_statement.expressions = Array(inline_assembly->bindings.length, child_expressions);
            } else {
                abort();
            }

            typed_statements.append(typed_statement);
        }
    }

    return ok((Array<TypedStatement>)typed_statements);
}

profiled_function(DelayedResult<Array<TypedStatement>>, do_type_function_body, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    Arena* arena,
    FunctionTypeType type,
    FunctionConstant value
), (
    info,
    jobs,
    arena,
    type,
    value
)) {
    auto declaration = value.declaration;

    auto declaration_parameter_count = declaration->parameters.length;

    auto file_path = get_scope_file_path(*value.body_scope);

    auto runtime_parameter_count = type.parameters.length;

    if(value.is_external) {
        return ok(Array<TypedStatement>::empty());
    } else {
        TypingContext context {};
        context.arena = arena;
        context.variable_scope_stack.arena = arena;

        context.return_types = type.return_types;

        VariableScope body_variable_scope {};
        body_variable_scope.constant_scope = value.body_scope;
        body_variable_scope.variables.arena = context.arena;

        context.variable_scope_stack.append(body_variable_scope);

        context.child_scopes = value.child_scopes;

        size_t runtime_parameter_index = 0;
        for(size_t i = 0; i < declaration->parameters.length; i += 1) {
            if(!declaration->parameters[i].is_constant) {
                auto parameter_type = type.parameters[i];

                add_new_variable(
                    &context,
                    declaration->parameters[i].name,
                    parameter_type
                );

                runtime_parameter_index += 1;
            }
        }

        assert(runtime_parameter_index == runtime_parameter_count);

        expect_delayed(body_typed_statements, generate_runtime_statements(
            info,
            jobs,
            value.body_scope,
            &context,
            declaration->statements
        ));

        assert(context.next_child_scope_index == value.child_scopes.length);

        bool has_return_at_end;
        if(declaration->statements.length > 0) {
            auto last_statement = declaration->statements[declaration->statements.length - 1];

            has_return_at_end = last_statement->kind == StatementKind::ReturnStatement;
        } else {
            has_return_at_end = false;
        }

        if(!has_return_at_end) {
            if(type.return_types.length > 0) {
                error(value.body_scope, declaration->range, "Function '%.*s' must end with a return", STRING_PRINTF_ARGUMENTS(declaration->name.text));

                return err();
            }
        }

        return ok(body_typed_statements);
    }
}

profiled_function(DelayedResult<AnyType>, do_type_static_variable, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    Arena* arena,
    VariableDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    declaration,
    scope
)) {
    auto is_external = false;
    Array<String> external_libraries;
    auto is_no_mangle = false;
    for(auto tag : declaration->tags) {
        if(tag.name.text == u8"extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            List<String> libraries(arena);

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, evaluate_constant_expression(arena, info, jobs, scope, nullptr, tag.parameters[i]));

                if(parameter.type.kind == TypeKind::ArrayTypeType) {
                    auto array = parameter.type.array;

                    if(
                        array.element_type->kind == TypeKind::ArrayTypeType ||
                        array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::ArrayConstant) {
                            error(scope, tag.parameters[i]->range, "Cannot use an array with non-constant elements in a constant context");

                            return err();
                        } else {
                            auto static_array_value = parameter.value.unwrap_static_array();

                            for(auto element : static_array_value.elements) {
                                expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else if(parameter.type.kind == TypeKind::StaticArray) {
                    auto static_array = parameter.type.static_array;

                    if(
                        static_array.element_type->kind == TypeKind::ArrayTypeType ||
                        static_array.element_type->kind == TypeKind::StaticArray
                    ) {
                        auto static_array_value = parameter.value.unwrap_static_array();

                        assert(static_array.length == static_array_value.elements.length);

                        for(auto element : static_array_value.elements) {
                            expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.type.get_description(arena)));

                    return err();
                }
            }

            is_external = true;
            external_libraries = libraries;
        } else if(tag.name.text == u8"no_mangle"_S) {
            if(is_no_mangle) {
                error(scope, tag.range, "Duplicate 'no_mangle' tag");

                return err();
            }

            is_no_mangle = true;
        } else {
            error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

            return err();
        }
    }

    if(is_external && is_no_mangle) {
        error(scope, declaration->range, "External variables cannot be no_mangle");

        return err();
    }

    if(is_external) {
        if(declaration->initializer != nullptr) {
            error(scope, declaration->range, "External variables cannot have initializers");

            return err();
        }

        expect_delayed(type, evaluate_type_expression(arena, info, jobs, scope, (Statement*)nullptr, declaration->type));

        if(!type.is_runtime_type()) {
            error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

            return err();
        }

        return ok(type);
    } else {
        if(declaration->initializer == nullptr) {
            error(scope, declaration->range, "Variable must be initialized");

            return err();
        }

        if(declaration->type != nullptr) {
            expect_delayed(type, evaluate_type_expression(arena, info, jobs, scope, (Statement*)nullptr, declaration->type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

                return err();
            }

            expect_delayed(initial_value, evaluate_constant_expression(arena, info, jobs, scope, nullptr, declaration->initializer));

            expect(coerced_initial_value, coerce_constant_to_type(
                arena,
                info,
                scope,
                declaration->initializer->range,
                initial_value.type,
                initial_value.value,
                type,
                false
            ));

            return ok(type);
        } else {
            expect_delayed(initial_value, evaluate_constant_expression(arena, info, jobs, scope, nullptr, declaration->initializer));

            expect(type, coerce_to_default_type(info, scope, declaration->initializer->range, initial_value.type));

            if(!type.is_runtime_type()) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

                return err();
            }

            return ok(type);
        }
    }
}