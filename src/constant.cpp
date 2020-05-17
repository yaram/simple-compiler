#include "constant.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "profiler.h"
#include "path.h"
#include "util.h"
#include "jobs.h"
#include "types.h"

ConstantValue void_constant_singleton { ConstantValueKind::VoidConstant };

void error(ConstantScope *scope, FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    auto current_scope = scope;
    while(!current_scope->is_top_level) {
        current_scope = current_scope->parent;
    }

    error(current_scope->file_path, range, format, arguments);

    va_end(arguments);
}

bool check_undetermined_integer_to_integer_coercion(ConstantScope *scope, FileRange range, Integer *target_type, int64_t value, bool probing) {
    bool in_range;
    if(target_type->is_signed) {
        int64_t min;
        int64_t max;
        switch(target_type->size) {
            case RegisterSize::Size8: {
                min = INT8_MIN;
                max = INT8_MAX;
            } break;

            case RegisterSize::Size16: {
                min = INT16_MIN;
                max = INT16_MAX;
            } break;

            case RegisterSize::Size32: {
                min = INT32_MIN;
                max = INT32_MAX;
            } break;

            case RegisterSize::Size64: {
                min = INT64_MIN;
                max = INT64_MAX;
            } break;

            default: {
                abort();
            } break;
        }

        in_range = value >= min && value <= max;
    } else {
        if(value < 0) {
            in_range = false;
        } else {
            uint64_t max;
            switch(target_type->size) {
                case RegisterSize::Size8: {
                    max = UINT8_MAX;
                } break;

                case RegisterSize::Size16: {
                    max = UINT16_MAX;
                } break;

                case RegisterSize::Size32: {
                    max = UINT32_MAX;
                } break;

                case RegisterSize::Size64: {
                    max = UINT64_MAX;
                } break;

                default: {
                    abort();
                } break;
            }

            in_range = (uint64_t)value <= max;
        }
    }

    if(!in_range) {
        if(!probing) {
            error(scope, range, "Constant '%zd' cannot fit in '%s'. You must cast explicitly", value, type_description(target_type));
        }

        return false;
    }

    return true;
}

Result<IntegerConstant*> coerce_constant_to_integer_type(
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Integer *target_type,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        if(integer->size != target_type->size || integer->size != target_type->size) {
            if(!probing) {
                error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(integer), type_description(target_type));
            }

            return err;
        }

        auto integer_value = (IntegerConstant*)value;

        return ok(integer_value);
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value->value, probing)) {
            return err;
        }

        return ok(integer_value);
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
        }

        return err;
    }
}

static Result<IntegerConstant*> coerce_constant_to_undetermined_integer(
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    bool probing
) {
    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;
        auto integer_value = (IntegerConstant*)value;

        switch(integer->size) {
            case RegisterSize::Size8: {
                return ok(new IntegerConstant {
                    (uint8_t)integer_value->value
                });
            } break;

            case RegisterSize::Size16: {
                return ok(new IntegerConstant {
                    (uint16_t)integer_value->value
                });
            } break;

            case RegisterSize::Size32: {
                return ok(new IntegerConstant {
                    (uint32_t)integer_value->value
                });
            } break;

            case RegisterSize::Size64: {
                return ok(new IntegerConstant {
                    integer_value->value
                });
            } break;

            default: {
                abort();
            } break;
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        return ok(integer_value);
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%s' to '{integer}'", type_description(type));
        }

        return err;
    }
}

static Result<PointerConstant*> coerce_constant_to_pointer_type(
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Pointer target_type,
    bool probing
) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (IntegerConstant*)value;

        return ok(new PointerConstant {
            integer_value->value
        });
    } else if(type->kind == TypeKind::Pointer) {
        auto pointer = (Pointer*)type;

        if(types_equal(pointer->type, target_type.type)) {
            auto pointer_value = (PointerConstant*)value;

            return ok(pointer_value);
        }
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(&target_type));
    }

    return err;
}

Result<ConstantValue*> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope *scope,
    FileRange range,
    Type *type,
    ConstantValue *value,
    Type *target_type,
    bool probing
) {
    if(target_type->kind == TypeKind::Integer) {
        auto integer = (Integer*)target_type;

        expect(integer_value, coerce_constant_to_integer_type(scope, range, type, value, integer, probing));

        return ok(integer_value);
    } else if(target_type->kind == TypeKind::UndeterminedInteger) {
        expect(integer_value, coerce_constant_to_undetermined_integer(scope, range, type, value, probing));

        return ok(integer_value);
    } else if(target_type->kind == TypeKind::FloatType) {
        auto target_float_type = (FloatType*)target_type;

        if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            return ok(new FloatConstant {
                (double)integer_value->value
            });
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            if(target_float_type->size == float_type->size) {
                return ok(value);
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            return ok(value);
        }
    } else if(target_type->kind == TypeKind::UndeterminedFloat) {
        if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            return ok(new FloatConstant {
                value
            });
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            return ok(value);
        }
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        expect(pointer_value, coerce_constant_to_pointer_type(scope, range, type, value, *target_pointer, probing));

        return ok(pointer_value);
    } else if(target_type->kind == TypeKind::ArrayTypeType) {
        auto target_array_type = (ArrayTypeType*)target_type;

        if(type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)type;
            if(types_equal(target_array_type->element_type, array_type->element_type)) {
                return ok(value);
            }
        } else if(type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)type;
            if(
                undetermined_struct->members.count == 2 &&
                strcmp(undetermined_struct->members[0].name, "pointer") == 0 &&
                strcmp(undetermined_struct->members[1].name, "length") == 0
            ) {
                auto undetermined_struct_value = (StructConstant*)value;

                auto pointer_result = coerce_constant_to_pointer_type(
                    scope,
                    range,
                    undetermined_struct->members[0].type,
                    undetermined_struct_value->members[0],
                    {
                        target_array_type->element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_constant_to_integer_type(
                        scope,
                        range,
                        undetermined_struct->members[1].type,
                        undetermined_struct_value->members[1],
                        heapify<Integer>({
                            info.address_integer_size,
                            false
                        }),
                        true
                    );

                    if(length_result.status) {
                        return ok(new ArrayConstant {
                            pointer_result.value->value,
                            length_result.value->value,
                        });
                    }
                }
            }
        }
    } else if(types_equal(type, target_type)) {
        return ok(value);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%s' to '%s'", type_description(type), type_description(target_type));
    }

    return err;
}

Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope *scope,
    Type *type,
    ConstantValue *value,
    FileRange range,
    Type *index_type,
    ConstantValue *index_value,
    FileRange index_range
) {
    expect(index, coerce_constant_to_integer_type(
        scope,
        index_range,
        index_type,
        index_value,
        heapify<Integer>({
            info.address_integer_size,
            false
        }),
        false
    ));

    if(type->kind == TypeKind::StaticArray) {
        auto static_array = (StaticArray*)type;
        if(index->value >= static_array->length) {
            error(scope, index_range, "Array index %zu out of bounds", index);

            return err;
        }

        auto static_array_value = (StaticArrayConstant*)value;

        return ok({
            static_array->element_type,
            static_array_value->elements[index->value]
        });
    } else {
        error(scope, range, "Cannot index %s", type_description(type));

        return err;
    }
}

Result<Type*> determine_binary_operation_type(ConstantScope *scope, FileRange range, Type *left, Type *right) {
    if(left->kind == TypeKind::Boolean || right->kind == TypeKind::Boolean) {
        return ok(left);
    } else if(left->kind == TypeKind::Pointer) {
        return ok(left);
    } else if(right->kind == TypeKind::Pointer) {
        return ok(right);
    } else if(left->kind == TypeKind::Integer && right->kind == TypeKind::Integer) {
        auto left_integer = (Integer*)left;
        auto right_integer = (Integer*)right;

        RegisterSize largest_size;
        if(left_integer->size > right_integer->size) {
            largest_size = left_integer->size;
        } else {
            largest_size = right_integer->size;
        }

        auto is_either_signed = left_integer->is_signed || right_integer->is_signed;

        return ok(new Integer {
            largest_size,
            is_either_signed
        });
    } else if(left->kind == TypeKind::FloatType && right->kind == TypeKind::FloatType) {
        auto left_float = (FloatType*)left;
        auto right_float = (FloatType*)right;

        RegisterSize largest_size;
        if(left_float->size > right_float->size) {
            largest_size = left_float->size;
        } else {
            largest_size = right_float->size;
        }

        return ok(new FloatType {
            largest_size
        });
    } else if(left->kind == TypeKind::FloatType) {
        return ok(left);
    } else if(right->kind == TypeKind::FloatType) {
        return ok(right);
    } else if(left->kind == TypeKind::UndeterminedFloat || right->kind == TypeKind::UndeterminedFloat) {
        return ok(left);
    } else if(left->kind == TypeKind::Integer) {
        return ok(left);
    } else if(right->kind == TypeKind::Integer) {
        return ok(right);
    } else if(left->kind == TypeKind::UndeterminedInteger || right->kind == TypeKind::UndeterminedInteger) {
        return ok(left);
    } else {
        error(scope, range, "Mismatched types '%s' and '%s'", type_description(left), type_description(right));

        return err;
    }
}

Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope *scope,
    FileRange range,
    BinaryOperation::Operator binary_operator,
    FileRange left_range,
    Type *left_type,
    ConstantValue *left_value,
    FileRange right_range,
    Type *right_type,
    ConstantValue *right_value
) {
    expect(type, determine_binary_operation_type(scope, range, left_type, right_type));

    expect(coerced_left_value, coerce_constant_to_type(info, scope, left_range, left_type, left_value, type, false));

    expect(coerced_right_value, coerce_constant_to_type(info, scope, right_range, right_type, right_value, type, false));

    if(type->kind == TypeKind::Integer) {
        auto integer = (Integer*)type;

        auto left = (IntegerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::IntegerConstant);

        auto right = (IntegerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::IntegerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok({
                    integer,
                    new IntegerConstant {
                        left->value + right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok({
                    integer,
                    new IntegerConstant {
                        left->value - right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Multiplication: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value * (int64_t)right->value;
                } else {
                    result = left->value * right->value;
                }

                return ok({
                    integer,
                    new IntegerConstant {
                        result
                    }
                });
            } break;

            case BinaryOperation::Operator::Division: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value / (int64_t)right->value;
                } else {
                    result = left->value / right->value;
                }

                return ok({
                    integer,
                    new IntegerConstant {
                        result
                    }
                });
            } break;

            case BinaryOperation::Operator::Modulo: {
                uint64_t result;
                if(integer->is_signed) {
                    result = (int64_t)left->value % (int64_t)right->value;
                } else {
                    result = left->value % right->value;
                }

                return ok({
                    integer,
                    new IntegerConstant {
                        result
                    }
                });
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return ok({
                    integer,
                    new IntegerConstant {
                        left->value & right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return ok({
                    integer,
                    new IntegerConstant {
                        left->value | right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value == right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value != right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::LessThan: {
                bool result;
                if(integer->is_signed) {
                    result = (int64_t)left->value < (int64_t)right->value;
                } else {
                    result = left->value < right->value;
                }

                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        result
                    }
                });
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                bool result;
                if(integer->is_signed) {
                    result = (int64_t)left->value > (int64_t)right->value;
                } else {
                    result = left->value > right->value;
                }

                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        result
                    }
                });
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return err;
            } break;
        }
    } else if(type->kind == TypeKind::UndeterminedInteger) {
        auto left = (IntegerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::IntegerConstant);

        auto right = (IntegerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::IntegerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        left->value + right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        left->value - right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        (uint64_t)((int64_t)left->value * (int64_t)right->value)
                    }
                });
            } break;

            case BinaryOperation::Operator::Division: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        (uint64_t)((int64_t)left->value / (int64_t)right->value)
                    }
                });
            } break;

            case BinaryOperation::Operator::Modulo: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        (uint64_t)((int64_t)left->value % (int64_t)right->value)
                    }
                });
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        left->value & right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return ok({
                    &undetermined_integer_singleton,
                    new IntegerConstant {
                        left->value | right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value == right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value != right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::LessThan: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        (int64_t)left->value < (int64_t)right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        (int64_t)left->value > (int64_t)right->value
                    }
                });
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return err;
            } break;
        }
    } else if(type->kind == TypeKind::Boolean) {
        auto left = (BooleanConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::BooleanConstant);

        auto right = (BooleanConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::BooleanConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value && right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::BooleanOr: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value || right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value == right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value != right->value
                    }
                });
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on booleans");

                return err;
            } break;
        }
    } else if(type->kind == TypeKind::FloatType || type->kind == TypeKind::UndeterminedFloat) {
        auto left = (FloatConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::FloatConstant);

        auto right = (FloatConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::FloatConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok({
                    type,
                    new FloatConstant {
                        left->value + right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok({
                    type,
                    new FloatConstant {
                        left->value - right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return ok({
                    type,
                    new FloatConstant {
                        left->value * right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Division: {
                return ok({
                    type,
                    new FloatConstant {
                        left->value / right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value == right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value != right->value
                    }
                });
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return err;
            } break;
        }
    } else if(type->kind == TypeKind::Pointer) {
        auto left = (PointerConstant*)coerced_left_value;
        assert(coerced_left_value->kind == ConstantValueKind::PointerConstant);

        auto right = (PointerConstant*)coerced_right_value;
        assert(coerced_right_value->kind == ConstantValueKind::PointerConstant);

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value == right->value
                    }
                });
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok({
                    &boolean_singleton,
                    new BooleanConstant {
                        left->value != right->value
                    }
                });
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return err;
            } break;
        }
    } else {
        abort();
    }
}

Result<ConstantValue*> evaluate_constant_cast(
    GlobalInfo info,
    ConstantScope *scope,
    Type *type,
    ConstantValue *value,
    FileRange value_range,
    Type *target_type,
    FileRange target_range,
    bool probing
) {
    auto coerce_result = coerce_constant_to_type(
        info,
        scope,
        value_range,
        type,
        value,
        target_type,
        true
    );

    if(coerce_result.status) {
        return ok(coerce_result.value);
    }

    if(target_type->kind == TypeKind::Integer) {
        auto target_integer = (Integer*)target_type;

        uint64_t result;

        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            auto integer_value = (IntegerConstant*)value;

            if(integer->is_signed) {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }        
        } else if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            result = integer_value->value;
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double from_value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            if(target_integer->is_signed) {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)from_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)from_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)from_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (int64_t)from_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)from_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)from_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)from_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (uint64_t)from_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            auto float_value = (FloatConstant*)value;

            if(target_integer->is_signed) {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)float_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)float_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)float_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (int64_t)float_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(target_integer->size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)float_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)float_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)float_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (uint64_t)float_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            if(target_integer->size == info.address_integer_size && !target_integer->is_signed) {
                auto pointer_value = (PointerConstant*)value;

                result = pointer_value->value;
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(pointer), type_description(target_integer));
                }

                return err;
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_integer));
            }

            return err;
        }

        return ok(new IntegerConstant {
            result
        });
    } else if(target_type->kind == TypeKind::FloatType) {
        auto target_float_type = (FloatType*)target_type;

        double result;
        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            auto integer_value = (IntegerConstant*)value;

            double from_value;
            if(integer->is_signed) {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(int8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(int16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(int32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)(int64_t)integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer->size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(uint8_t)integer_value->value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(uint16_t)integer_value->value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(uint32_t)integer_value->value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)integer_value->value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)from_value;
                } break;

                case RegisterSize::Size64: {
                    result = from_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::UndeterminedInteger) {
            auto integer_value = (IntegerConstant*)value;

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)(int64_t)integer_value->value;
                } break;

                case RegisterSize::Size64: {
                    result = (double)(int64_t)integer_value->value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::FloatType) {
            auto float_type = (FloatType*)type;
            auto float_value = (FloatConstant*)value;

            double from_value;
            switch(float_type->size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)from_value;
                } break;

                case RegisterSize::Size64: {
                    result = from_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type->kind == TypeKind::UndeterminedFloat) {
            auto float_value = (FloatConstant*)value;

            switch(target_float_type->size) {
                case RegisterSize::Size32: {
                    result = (double)(float)float_value->value;
                } break;

                case RegisterSize::Size64: {
                    result = float_value->value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_float_type));
            }

            return err;
        }

        return ok(new FloatConstant {
            result
        });
    } else if(target_type->kind == TypeKind::Pointer) {
        auto target_pointer = (Pointer*)target_type;

        uint64_t result;

        if(type->kind == TypeKind::Integer) {
            auto integer = (Integer*)type;
            if(integer->size == info.address_integer_size && !integer->is_signed) {
                auto integer_value = (IntegerConstant*)value;

                result = integer_value->value;
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(integer), type_description(target_pointer));
                }

                return err;
            }
        } else if(type->kind == TypeKind::Pointer) {
            auto pointer = (Pointer*)type;

            auto pointer_value = (PointerConstant*)value;

            result = pointer_value->value;
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_pointer));
            }

            return err;
        }

        return ok(new PointerConstant {
            result
        });
    } else {
        if(!probing) {
            error(scope, value_range, "Cannot cast from '%s' to '%s'", type_description(type), type_description(target_type));
        }

        return err;
    }
}

Result<Type*> coerce_to_default_type(GlobalInfo info, ConstantScope *scope, FileRange range, Type *type) {
    if(type->kind == TypeKind::UndeterminedInteger) {
        return ok(new Integer {
            info.default_integer_size,
            true
        });
    } else if(type->kind == TypeKind::UndeterminedFloat) {
        return ok(new FloatType {
            info.default_integer_size
        });
    } else if(type->kind == TypeKind::UndeterminedStruct) {
        error(scope, range, "Undetermined struct types cannot exist at runtime");

        return err;
    } else {
        return ok(type);
    }
}

bool match_public_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        declaration_name = function_declaration->name.text;
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        declaration_name = constant_definition->name.text;
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        declaration_name = struct_definition->name.text;
    } else {
        return false;
    }

    return strcmp(declaration_name, name) == 0;
}

bool match_declaration(Statement *statement, const char *name) {
    const char *declaration_name;
    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        declaration_name = function_declaration->name.text;
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        declaration_name = constant_definition->name.text;
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        declaration_name = struct_definition->name.text;
    } else if(statement->kind == StatementKind::Import) {
        auto import = (Import*)statement;

        declaration_name = path_get_file_component(import->path);
    } else {
        return false;
    }

    return strcmp(declaration_name, name) == 0;
}

DelayedResult<TypedConstantValue> get_simple_resolved_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *declaration
) {
    switch(declaration->kind) {
        case StatementKind::FunctionDeclaration: {
            auto function_declaration = (FunctionDeclaration*)declaration;

            for(auto parameter : function_declaration->parameters) {
                if(parameter.is_constant || parameter.is_polymorphic_determiner) {
                    return has({
                        &polymorphic_function_singleton,
                        new PolymorphicFunctionConstant {
                            function_declaration,
                            scope
                        }
                    });
                }
            }

            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolveFunctionDeclaration) {
                    auto resolve_function_declaration = (ResolveFunctionDeclaration*)job;

                    if(resolve_function_declaration->declaration == function_declaration) {
                        if(resolve_function_declaration->done) {
                            return has({
                                resolve_function_declaration->type,
                                resolve_function_declaration->value
                            });
                        } else {
                            return wait(resolve_function_declaration);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::ConstantDefinition: {
            auto constant_definition = (ConstantDefinition*)declaration;

            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolveConstantDefinition) {
                    auto resolve_constant_definition = (ResolveConstantDefinition*)job;

                    if(resolve_constant_definition->definition == constant_definition) {
                        if(resolve_constant_definition->done) {
                            return has({
                                resolve_constant_definition->type,
                                resolve_constant_definition->value
                            });
                        } else {
                            return wait(resolve_constant_definition);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::StructDefinition: {
            auto struct_definition = (StructDefinition*)declaration;

            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolveStructDefinition) {
                    auto resolve_struct_definition = (ResolveStructDefinition*)job;

                    if(resolve_struct_definition->definition == struct_definition) {
                        if(resolve_struct_definition->done) {
                            return has({
                                &type_type_singleton,
                                new TypeConstant {
                                    resolve_struct_definition->type,
                                }
                            });
                        } else {
                            return wait(resolve_struct_definition);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::Import: {
            auto import = (Import*)declaration;

            auto current_scope = scope;
            while(!current_scope->is_top_level) {
                current_scope = current_scope->parent;
            }

            auto source_file_directory = path_get_directory_component(current_scope->file_path);

            StringBuffer import_file_path {};

            string_buffer_append(&import_file_path, source_file_directory);
            string_buffer_append(&import_file_path, import->path);

            expect(import_file_path_absolute, path_relative_to_absolute(import_file_path.data));

            auto job_already_added = false;
            for(auto job : *jobs) {
                if(job->kind == JobKind::ParseFile) {
                    auto parse_file = (ParseFile*)job;

                    if(strcmp(parse_file->path, import_file_path_absolute) == 0) {
                        if(parse_file->done) {
                            return has({
                                &file_module_singleton,
                                new FileModuleConstant {
                                    parse_file->scope
                                }
                            });
                        } else {
                            return wait(parse_file);
                        }
                    }
                }
            }

            abort();
        } break;

        default: abort();
    }
}

bool constant_values_equal(Type *type, ConstantValue *a, ConstantValue *b) {
    switch(type->kind) {
        case TypeKind::FunctionTypeType: {
            auto function_value_a = extract_constant_value(FunctionConstant, a);
            auto function_value_b = extract_constant_value(FunctionConstant, b);

            return function_value_a->declaration == function_value_b->declaration;
        } break;

        case TypeKind::PolymorphicFunction: {
            auto function_value_a = extract_constant_value(FunctionConstant, a);
            auto function_value_b = extract_constant_value(FunctionConstant, b);

            return function_value_a->declaration == function_value_b->declaration;
        } break;

        case TypeKind::BuiltinFunction: {
            auto builtin_function_value_a = extract_constant_value(BuiltinFunctionConstant, a);
            auto builtin_function_value_b = extract_constant_value(BuiltinFunctionConstant, b);

            return strcmp(builtin_function_value_a->name , builtin_function_value_b->name) == 0;
        } break;

        case TypeKind::Integer:
        case TypeKind::UndeterminedInteger: {
            auto integer_value_a = extract_constant_value(IntegerConstant, a);
            auto integer_value_b = extract_constant_value(IntegerConstant, b);

            return integer_value_a->value == integer_value_b->value;
        } break;

        case TypeKind::Boolean: {
            auto boolean_value_a = extract_constant_value(BooleanConstant, a);
            auto boolean_value_b = extract_constant_value(BooleanConstant, b);

            return boolean_value_a->value == boolean_value_b->value;
        } break;

        case TypeKind::FloatType:
        case TypeKind::UndeterminedFloat: {
            auto float_value_a = extract_constant_value(FloatConstant, a);
            auto float_value_b = extract_constant_value(FloatConstant, b);

            return float_value_a->value == float_value_b->value;
        } break;

        case TypeKind::TypeType: {
            auto type_value_a = extract_constant_value(TypeConstant, a);
            auto type_value_b = extract_constant_value(TypeConstant, b);

            return types_equal(type_value_a->type, type_value_b->type);
        } break;

        case TypeKind::Void: {
            return true;
        } break;

        case TypeKind::Pointer: {
            auto pointer_value_a = extract_constant_value(PointerConstant, a);
            auto pointer_value_b = extract_constant_value(PointerConstant, b);

            return pointer_value_a->value == pointer_value_b->value;
        } break;

        case TypeKind::ArrayTypeType: {
            auto array_value_a = extract_constant_value(ArrayConstant, a);
            auto array_value_b = extract_constant_value(ArrayConstant, b);

            return array_value_a->length == array_value_b->length && array_value_a->pointer == array_value_b->pointer;
        } break;

        case TypeKind::StaticArray: {
            auto static_array_type = (StaticArray*)type;

            auto static_array_value_a = extract_constant_value(StaticArrayConstant, a);
            auto static_array_value_b = extract_constant_value(StaticArrayConstant, b);

            for(size_t i = 0; i < static_array_type->length; i += 1) {
                if(!constant_values_equal(static_array_type->element_type, static_array_value_a->elements[i], static_array_value_b->elements[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeKind::StructType: {
            auto struct_type = (StructType*)type;

            assert(!struct_type->definition->is_union);

            auto struct_value_a = extract_constant_value(StructConstant, a);
            auto struct_value_b = extract_constant_value(StructConstant, b);

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(!constant_values_equal(struct_type->members[i].type, struct_value_a->members[i], struct_value_b->members[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case TypeKind::PolymorphicStruct: abort();

        case TypeKind::UndeterminedStruct: {
            return false;
        } break;

        case TypeKind::FileModule: {
            auto file_module_value_a = extract_constant_value(FileModuleConstant, a);
            auto file_module_value_b = extract_constant_value(FileModuleConstant, b);

            return file_module_value_a == file_module_value_b;
        } break;

        default: abort();
    }
}

struct DeclarationSearchValue {
    bool found;

    Type *type;
    ConstantValue *value;
};

static DelayedResult<DeclarationSearchValue> search_for_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    const char *name,
    ConstantScope *scope,
    Array<Statement*> statements,
    bool external,
    Statement *ignore
) {
    for(auto statement : statements) {
        if(statement == ignore) {
            continue;
        }

        bool matching;
        if(external) {
            matching = match_public_declaration(statement, name);
        } else {
            matching = match_declaration(statement, name);
        }

        if(matching) {
            expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, statement));

            return has({
                true,
                value.type,
                value.value
            });
        } else if(statement->kind == StatementKind::UsingStatement) {
            if(!external) {
                auto using_statement = (UsingStatement*)statement;

                expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, using_statement, using_statement->module));

                if(expression_value.type->kind != TypeKind::FileModule) {
                    error(scope, using_statement->range, "Expected a module, got '%s'", type_description(expression_value.type));

                    return err;
                }

                auto file_module = (FileModuleConstant*)expression_value.value;
                assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

                expect_delayed(search_value, search_for_declaration(info, jobs, name, file_module->scope, file_module->scope->statements, true, nullptr));

                if(search_value.found) {
                    return has({
                        true,
                        search_value.type,
                        search_value.value
                    });
                }
            }
        } else if(statement->kind == StatementKind::StaticIf) {
            auto static_if = (StaticIf*)statement;

            auto found = false;
            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolveStaticIf) {
                    auto resolve_static_if = (ResolveStaticIf*)job;

                    if(
                        resolve_static_if->static_if == static_if &&
                        resolve_static_if->scope == scope
                    ) {
                        found = true;

                        if(resolve_static_if->done) {
                            if(resolve_static_if->condition) {
                                expect_delayed(search_value, search_for_declaration(info, jobs, name, scope, static_if->statements, false, nullptr));

                                if(search_value.found) {
                                    return has({
                                        true,
                                        search_value.type,
                                        search_value.value
                                    });
                                }
                            }
                        } else {
                            auto have_to_wait = false;
                            for(auto statement : static_if->statements) {
                                bool matching;
                                if(external) {
                                    matching = match_public_declaration(statement, name);
                                } else {
                                    matching = match_declaration(statement, name);
                                }

                                if(matching) {
                                    have_to_wait = true;
                                } else if(statement->kind == StatementKind::UsingStatement) {
                                    if(!external) {
                                        have_to_wait = true;
                                    }
                                } else if(statement->kind == StatementKind::StaticIf) {
                                    have_to_wait = true;
                                }
                            }

                            if(have_to_wait) {
                                return wait(resolve_static_if);
                            }
                        }
                    }
                }
            }

            assert(found);
        }
    }

    for(auto scope_constant : scope->scope_constants) {
        if(strcmp(scope_constant.name, name) == 0) {
            return has({
                true,
                scope_constant.type,
                scope_constant.value
            });
        }
    }

    return has({
        false
    });
}

profiled_function(DelayedResult<TypedConstantValue>, evaluate_constant_expression, (
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *ignore_statement,
    Expression *expression
), (
    info,
    jobs,
    scope,
    ignore_statement,
    expression,
)) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        auto current_scope = scope;
        while(true) {
            expect_delayed(search_value, search_for_declaration(info, jobs, named_reference->name.text, current_scope, current_scope->statements, false, ignore_statement));

            if(search_value.found) {
                return has({
                    search_value.type,
                    search_value.value
                });
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(strcmp(named_reference->name.text, global_constant.name) == 0) {
                return has({
                    global_constant.type,
                    global_constant.value
                });
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %s", named_reference->name.text);

        return err;
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, member_reference->expression));

        if(expression_value.type->kind == TypeKind::ArrayTypeType) {
            auto array_type = (ArrayTypeType*)expression_value.type;
            auto array_value = (ArrayConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::ArrayConstant);

            if(strcmp(member_reference->name.text, "length") == 0) {
                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        array_value->length
                    }
                });
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                return has({
                    new Pointer {
                        array_type->element_type
                    },
                    new PointerConstant {
                        array_value->pointer
                    }
                });
            } else {
                error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return err;
            }
        } else if(expression_value.type->kind == TypeKind::StaticArray) {
            auto static_array = (StaticArray*)expression_value.type;
            if(strcmp(member_reference->name.text, "length") == 0) {
                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        static_array->length
                    }
                });
            } else if(strcmp(member_reference->name.text, "pointer") == 0) {
                error(scope, member_reference->name.range, "Cannot take pointer to static array in constant context", member_reference->name.text);

                return err;
            } else {
                error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

                return err;
            }
        } else if(expression_value.type->kind == TypeKind::StructType) {
            auto struct_type = (StructType*)expression_value.type;
            auto struct_value = (StructConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::StructConstant);

            for(size_t i = 0; i < struct_type->members.count; i += 1) {
                if(strcmp(member_reference->name.text, struct_type->members[i].name) == 0) {
                    return has({
                        struct_type->members[i].type,
                        struct_value->members[i]
                    });
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return err;
        } else if(expression_value.type->kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = (UndeterminedStruct*)expression_value.type;
            auto undetermined_struct_value = (StructConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::StructConstant);

            for(size_t i = 0; i < undetermined_struct->members.count; i += 1) {
                if(strcmp(member_reference->name.text, undetermined_struct->members[i].name) == 0) {
                    return has({
                        undetermined_struct->members[i].type,
                        undetermined_struct_value->members[i]
                    });
                }
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return err;
        } else if(expression_value.type->kind == TypeKind::FileModule) {
            auto file_module_value = (FileModuleConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::FileModuleConstant);

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                member_reference->name.text,
                file_module_value->scope,
                file_module_value->scope->statements,
                true,
                nullptr
            ));

            if(search_value.found) {
                return has({
                    search_value.type,
                    search_value.value
                });
            }

            error(scope, member_reference->name.range, "No member with name '%s'", member_reference->name.text);

            return err;
        } else {
            error(scope, member_reference->expression->range, "Type '%s' has no members", type_description(expression_value.type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, index_reference->expression));

        expect_delayed(index, evaluate_constant_expression(info, jobs, scope, ignore_statement, index_reference->index));

        expect(value, evaluate_constant_index(
            info,
            scope,
            expression_value.type,
            expression_value.value,
            index_reference->expression->range,
            index.type,
            index.value,
            index_reference->index->range
        ));

        return has(value);
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return has({
            &undetermined_integer_singleton,
            new IntegerConstant {
                integer_literal->value
            }
        });
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return has({
            &undetermined_float_singleton,
            new FloatConstant {
                float_literal->value
            }
        });
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.count;

        auto characters = allocate<ConstantValue*>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = new IntegerConstant {
                (uint64_t)string_literal->characters[i]
            };
        }

        return has({
            new StaticArray {
                character_count,
                new Integer {
                    RegisterSize::Size8,
                    false
                }
            },
            new StaticArrayConstant {
                characters
            }
        });
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.count;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err;
        }

        expect_delayed(first_element, evaluate_constant_expression(info, jobs, scope, ignore_statement, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!is_runtime_type(determined_element_type)) {
            error(scope, array_literal->range, "Arrays cannot be of type '%s'", type_description(determined_element_type));

            return err;
        }

        auto elements = allocate<ConstantValue*>(element_count);
        elements[0] = first_element.value;

        for(size_t i = 1; i < element_count; i += 1) {
            expect_delayed(element, evaluate_constant_expression(info, jobs, scope, ignore_statement, array_literal->elements[i]));

            expect(element_value, coerce_constant_to_type(
                info,
                scope,
                array_literal->elements[i]->range,
                element.type,
                element.value,
                determined_element_type,
                false
            ));

            elements[i] = element_value;
        }

        return has({
            new StaticArray {
                element_count,
                determined_element_type
            },
            new StaticArrayConstant {
                elements
            }
        });
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        auto member_count = struct_literal->members.count;

        if(member_count == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err;
        }

        auto members = allocate<UndeterminedStruct::Member>(member_count);
        auto member_values = allocate<ConstantValue*>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            auto member_name = struct_literal->members[i].name;

            for(size_t j = 0; j < member_count; j += 1) {
                if(j != i && strcmp(member_name.text, struct_literal->members[j].name.text) == 0) {
                    error(scope, member_name.range, "Duplicate struct member %s", member_name.text);

                    return err;
                }
            }

            expect_delayed(member, evaluate_constant_expression(info, jobs, scope, ignore_statement, struct_literal->members[i].value));

            members[i] = {
                member_name.text,
                member.type
            };

            member_values[i] = member.value;
        }

        return has({
            new UndeterminedStruct {
                {
                    member_count,
                    members
                }
            },
            new StructConstant {
                member_values
            }
        });
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->expression));

        if(expression_value.type->kind == TypeKind::FunctionTypeType) {
            auto function = (FunctionTypeType*)expression_value.type;
            error(scope, function_call->range, "Function calls not allowed in global context");

            return err;
        } else if(expression_value.type->kind == TypeKind::BuiltinFunction) {
            auto builtin_function_value = (BuiltinFunctionConstant*)expression_value.value;
            assert(expression_value.value->kind == ConstantValueKind::BuiltinFunctionConstant);

            if(strcmp(builtin_function_value->name, "size_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return err;
                }

                expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[0]));

                Type *type;
                if(parameter_value.type->kind == TypeKind::TypeType) {
                    type = extract_constant_value(TypeConstant, parameter_value.value)->type;
                } else {
                    type = parameter_value.type;
                }

                if(!is_runtime_type(type)) {
                    error(scope, function_call->parameters[0]->range, "'%s'' has no size", type_description(parameter_value.type));

                    return err;
                }

                auto size = get_type_size(info, type);

                return has({
                    new Integer {
                        info.address_integer_size,
                        false
                    },
                    new IntegerConstant {
                        size
                    }
                });
            } else if(strcmp(builtin_function_value->name, "type_of") == 0) {
                if(function_call->parameters.count != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.count);

                    return err;
                }

                expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[0]));

                return has({
                    &type_type_singleton,
                    new TypeConstant { parameter_value.type }
                });
            } else if(strcmp(builtin_function_value->name, "memcpy") == 0) {
                error(scope, function_call->range, "'memcpy' cannot be called in a constant context");

                return err;
            } else {
                abort();
            }
        } else if(expression_value.type->kind == TypeKind::TypeType) {
            auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

            if(type->kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = (PolymorphicStruct*)type;
                auto definition = polymorphic_struct->definition;

                auto parameter_count = definition->parameters.count;

                if(function_call->parameters.count != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.count);

                    return err;
                }

                auto parameters = allocate<ConstantValue*>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.type,
                        parameter.value,
                        polymorphic_struct->parameter_types[i],
                        false
                    ));

                    parameters[i] = {
                        parameter_value
                    };
                }

                for(auto job : *jobs) {
                    if(job->kind == JobKind::ResolvePolymorphicStruct) {
                        auto resolve_polymorphic_struct = (ResolvePolymorphicStruct*)job;

                        if(resolve_polymorphic_struct->definition == definition) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(polymorphic_struct->parameter_types[i], parameters[i], resolve_polymorphic_struct->parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(resolve_polymorphic_struct->done) {
                                    return has({
                                        &type_type_singleton,
                                        new TypeConstant {
                                            resolve_polymorphic_struct->type
                                        }
                                    });
                                } else {
                                    return wait(resolve_polymorphic_struct);
                                }
                            }
                        }
                    }
                }

                auto resolve_polymorphic_struct = new ResolvePolymorphicStruct;
                resolve_polymorphic_struct->done = false;
                resolve_polymorphic_struct->waiting_for = nullptr;
                resolve_polymorphic_struct->definition = definition;
                resolve_polymorphic_struct->parameters = parameters;
                resolve_polymorphic_struct->scope = polymorphic_struct->parent;

                append(jobs, (Job*)resolve_polymorphic_struct);

                return wait(resolve_polymorphic_struct);
            } else {
                error(scope, function_call->expression->range, "Type '%s' is not polymorphic", type_description(type));

                return err;
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call non-function '%s'", type_description(expression_value.type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        expect_delayed(left, evaluate_constant_expression(info, jobs, scope, ignore_statement, binary_operation->left));

        expect_delayed(right, evaluate_constant_expression(info, jobs, scope, ignore_statement, binary_operation->right));

        expect(value, evaluate_constant_binary_operation(
            info,
            scope,
            binary_operation->range,
            binary_operation->binary_operator,
            binary_operation->left->range,
            left.type,
            left.value,
            binary_operation->right->range,
            right.type,
            right.value
        ));

        return has(value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                if(expression_value.type->kind == TypeKind::TypeType) {
                    auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

                    if(
                        !is_runtime_type(type) &&
                        type->kind != TypeKind::Void &&
                        type->kind != TypeKind::FunctionTypeType
                    ) {
                        error(scope, unary_operation->expression->range, "Cannot create pointers to type '%s'", type_description(type));

                        return err;
                    }

                    return has({
                        &type_type_singleton,
                        new TypeConstant {
                            new Pointer {
                                type
                            }
                        }
                    });
                } else {
                    error(scope, unary_operation->range, "Cannot take pointers at constant time");

                    return err;
                }
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type->kind == TypeKind::Boolean) {
                    auto boolean_value = (BooleanConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::BooleanConstant);

                    return has({
                        &boolean_singleton,
                        new BooleanConstant {
                            !boolean_value->value
                        }
                    });
                } else {
                    error(scope, unary_operation->expression->range, "Expected a boolean, got '%s'", type_description(expression_value.type));

                    return err;
                }
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type->kind == TypeKind::Integer || expression_value.type->kind == TypeKind::UndeterminedInteger) {
                    auto integer_value = (IntegerConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::IntegerConstant);

                    return has({
                        expression_value.type,
                        new IntegerConstant {
                            -integer_value->value
                        }
                    });
                } else if(expression_value.type->kind == TypeKind::FloatType || expression_value.type->kind == TypeKind::UndeterminedFloat) {
                    auto float_value = (FloatConstant*)expression_value.value;
                    assert(expression_value.value->kind == ConstantValueKind::FloatConstant);

                    return has({
                        expression_value.type,
                        new FloatConstant {
                            -float_value->value
                        }
                    });
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%s'", type_description(expression_value.type));

                    return err;
                }
            } break;

            default: {
                abort();
            } break;
        }
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, cast->expression));

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, ignore_statement, cast->type));

        expect(value, evaluate_constant_cast(
            info,
            scope,
            expression_value.type,
            expression_value.value,
            cast->expression->range,
            type,
            cast->type->range,
            false
        ));

        return has({
            type,
            value
        });
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->expression));

        auto call_parameter_count = function_call->parameters.count;

        auto parameters = allocate<TypedConstantValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[i]));

            parameters[i] = parameter_value;
        }

        if(expression_value.type->kind == TypeKind::PolymorphicFunction) {
            auto polymorphic_function_value = extract_constant_value(PolymorphicFunctionConstant, expression_value.value);

            auto declaration_parameters = polymorphic_function_value->declaration->parameters;
            auto declaration_parameter_count = declaration_parameters.count;

            if(call_parameter_count != declaration_parameter_count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    declaration_parameter_count,
                    call_parameter_count
                );

                return err;
            }

            auto found = false;
            for(auto job : *jobs) {
                if(job->kind == JobKind::ResolvePolymorphicFunction) {
                    auto resolve_polymorphic_function = (ResolvePolymorphicFunction*)job;

                    if(
                        resolve_polymorphic_function->declaration == polymorphic_function_value->declaration &&
                        resolve_polymorphic_function->scope == polymorphic_function_value->scope
                    ) {
                        auto matching_polymorphic_parameters = true;
                        for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                            auto declaration_parameter = declaration_parameters[i];
                            auto call_parameter = parameters[i];
                            auto job_parameter = resolve_polymorphic_function->parameters[i];

                            if(
                                (declaration_parameter.is_polymorphic_determiner || declaration_parameter.is_constant) &&
                                !types_equal(job_parameter.type, call_parameter.type)
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }

                            if(
                                declaration_parameter.is_constant &&
                                !constant_values_equal( call_parameter.type, call_parameter.value, job_parameter.value)
                            ) {
                                matching_polymorphic_parameters = false;
                                break;
                            }
                        }

                        if(!matching_polymorphic_parameters) {
                            continue;
                        }

                        if(resolve_polymorphic_function->done) {
                            found = true;

                            return has({
                                resolve_polymorphic_function->type,
                                resolve_polymorphic_function->value
                            });
                        } else {
                            return wait(resolve_polymorphic_function);
                        }  
                    }
                }
            }

            if(!found) {
                auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

                for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                    call_parameter_ranges[i] = function_call->parameters[i]->range;
                }

                auto resolve_polymorphic_function = new ResolvePolymorphicFunction;
                resolve_polymorphic_function->done = false;
                resolve_polymorphic_function->waiting_for = nullptr;
                resolve_polymorphic_function->declaration = polymorphic_function_value->declaration;
                resolve_polymorphic_function->parameters = parameters;
                resolve_polymorphic_function->scope = polymorphic_function_value->scope;
                resolve_polymorphic_function->call_scope = scope;
                resolve_polymorphic_function->call_parameter_ranges = call_parameter_ranges;

                append(jobs, (Job*)resolve_polymorphic_function);

                return wait(resolve_polymorphic_function);
            }
        } else if(expression_value.type->kind == TypeKind::FunctionTypeType) {
            auto function_type = (FunctionTypeType*)expression_value.type;
            auto function_value = extract_constant_value(FunctionConstant, expression_value.value);

            if(call_parameter_count != function_type->parameters.count) {
                error(
                    scope,
                    function_call->range,
                    "Incorrect number of parameters. Expected %zu, got %zu",
                    function_type->parameters.count,
                    call_parameter_count
                );

                return err;
            }

            return has({
                function_type,
                function_value
            });
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%s'", type_description(expression_value.type));

            return err;
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, ignore_statement, array_type->expression));

        if(!is_runtime_type(type)) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%s'", type_description(type));

            return err;
        }

        if(array_type->index != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                heapify<Integer>({
                    info.address_integer_size,
                    false
                }),
                false
            ));

            return has({
                &type_type_singleton,
                new TypeConstant {
                    new StaticArray {
                        length->value,
                        type
                    }
                }
            });
        } else {
            return has({
                &type_type_singleton,
                new TypeConstant {
                    new ArrayTypeType {
                        type
                    }
                }
            });
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.count;

        auto parameters = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err;
            }

            expect_delayed(type, evaluate_type_expression(info, jobs, scope, ignore_statement, parameter.type));

            if(!is_runtime_type(type)) {
                error(scope, parameter.type->range, "Function parameters cannot be of type '%s'", type_description(type));

                return err;
            }

            parameters[i] = type;
        }

        Type *return_type;
        if(function_type->return_type == nullptr) {
            return_type = &void_singleton;
        } else {
            expect_delayed(return_type_value, evaluate_type_expression(info, jobs, scope, ignore_statement, function_type->return_type));

            if(!is_runtime_type(return_type_value)) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%s'", type_description(return_type_value));

                return err;
            }

            return_type = return_type_value;
        }

        return has({
            &type_type_singleton,
            new TypeConstant {
                new FunctionTypeType {
                    {
                        parameter_count,
                        parameters
                    },
                    return_type
                }
            }
        });
    } else {
        abort();
    }
}

DelayedResult<Type*> evaluate_type_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope *scope,
    Statement *ignore_statement,
    Expression *expression
) {
    expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, expression));

    if(expression_value.type->kind == TypeKind::TypeType) {
        auto type = extract_constant_value(TypeConstant, expression_value.value)->type;

        return has(type);
    } else {
        error(scope, expression->range, "Expected a type, got %s", type_description(expression_value.type));

        return err;
    }
}

DelayedResult<bool> do_resolve_static_if(GlobalInfo info, List<Job*> *jobs, StaticIf *static_if, ConstantScope *scope) {
    expect_delayed(condition, evaluate_constant_expression(info, jobs, scope, static_if, static_if->condition));

    if(condition.type->kind != TypeKind::Boolean) {
        error(scope, static_if->condition->range, "Expected a boolean, got '%s'", type_description(condition.type));

        return err;
    }

    auto condition_value = extract_constant_value(BooleanConstant, condition.value)->value;

    if(condition_value) {
        if(!process_scope(jobs, scope, static_if->statements, nullptr, true)) {
            return err;
        }
    }

    return has(condition_value);
}

DelayedResult<FunctionResolutionValue> do_resolve_function_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    ConstantScope *scope
) {
    auto parameter_count = declaration->parameters.count;

    auto parameter_types = allocate<Type*>(parameter_count);
    for(size_t i = 0; i < parameter_count; i += 1) {
        assert(!declaration->parameters[i].is_constant);
        assert(!declaration->parameters[i].is_polymorphic_determiner);

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->parameters[i].type));

        if(!is_runtime_type(type)) {
            error(scope, declaration->parameters[i].type->range, "Function parameters cannot be of type '%s'", type_description(type));

            return err;
        }

        parameter_types[i] = type;
    }

    Type *return_type;
    if(declaration->return_type) {
        expect_delayed(return_type_value, evaluate_type_expression(info, jobs, scope, nullptr, declaration->return_type));

        if(!is_runtime_type(return_type_value)) {
            error(scope, declaration->return_type->range, "Function parameters cannot be of type '%s'", type_description(return_type_value));

            return err;
        }

        return_type = return_type_value;
    } else {
        return_type = &void_singleton;
    }
    
    auto body_scope = new ConstantScope;
    body_scope->statements = {};
    body_scope->scope_constants = {};
    body_scope->is_top_level = false;
    body_scope->parent = scope;

    List<ConstantScope*> child_scopes {};
    if(!declaration->is_external) {
        body_scope->statements = declaration->statements;

        if(!process_scope(jobs, body_scope, body_scope->statements, &child_scopes, false)) {
            return err;
        }
    }

    return has({
        new FunctionTypeType {
            {
                parameter_count,
                parameter_types
            },
            return_type
        },
        new FunctionConstant {
            declaration,
            body_scope,
            to_array(child_scopes)
        }
    });
}

DelayedResult<FunctionResolutionValue> do_resolve_polymorphic_function(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    TypedConstantValue *parameters,
    ConstantScope *scope,
    ConstantScope *call_scope,
    FileRange *call_parameter_ranges
) {
    auto original_parameter_count = declaration->parameters.count;

    auto parameter_types = allocate<Type*>(original_parameter_count);

    List<ScopeConstant> polymorphic_determiners {};

    size_t polymorphic_determiner_index = 0;
    size_t runtime_parameter_count = 0;
    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];

        if(!declaration_parameter.is_constant) {
            runtime_parameter_count += 1;
        }

        if(declaration_parameter.is_polymorphic_determiner) {
            Type *type;
            if(declaration_parameter.is_constant) {
                type = parameters[i].type;
            } else {
                expect(determined_type, coerce_to_default_type(info, call_scope, call_parameter_ranges[i], parameters[i].type));

                type = determined_type;
            }

            parameter_types[i] = type;

            append(&polymorphic_determiners, {
                declaration->parameters[i].polymorphic_determiner.text,
                &type_type_singleton,
                new TypeConstant {
                    type
                }
            });

            polymorphic_determiner_index += 1;
        }
    }

    ConstantScope signature_scope;
    signature_scope.statements = {};
    signature_scope.scope_constants = to_array(polymorphic_determiners);
    signature_scope.is_top_level = false;
    signature_scope.parent = scope;

    List<ScopeConstant> scope_constants {};

    for(auto polymorphic_determiner : polymorphic_determiners) {
        append(&scope_constants, polymorphic_determiner);
    }

    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];
        auto call_parameter = parameters[i];

        if(declaration_parameter.is_constant) {
            if(!declaration_parameter.is_polymorphic_determiner) {
                expect_delayed(parameter_type, evaluate_type_expression(info, jobs, &signature_scope, nullptr, declaration_parameter.type));

                parameter_types[i] = parameter_type;
            }

            expect(coerced_constant_value, coerce_constant_to_type(
                info,
                call_scope,
                call_parameter_ranges[i],
                call_parameter.type,
                call_parameter.value,
                parameter_types[i],
                false
            ));

            append(&scope_constants, {
                declaration_parameter.name.text,
                parameter_types[i],
                coerced_constant_value
            });
        }
    }

    signature_scope.scope_constants = to_array(scope_constants);

    auto runtime_parameter_types = allocate<Type*>(runtime_parameter_count);

    size_t runtime_parameter_index = 0;
    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];

        if(!declaration_parameter.is_constant) {
            if(!declaration_parameter.is_polymorphic_determiner) {
                expect_delayed(parameter_type, evaluate_type_expression(info, jobs, &signature_scope, nullptr, declaration_parameter.type));

                if(!is_runtime_type(parameter_type)) {
                    error(scope,
                        declaration_parameter.type->range,
                        "Non-constant function parameters cannot be of type '%s'",
                        type_description(parameter_type)
                    );

                    error(call_scope, call_parameter_ranges[i], "Polymorphic function paremter here");

                    return err;
                }

                parameter_types[i] = parameter_type;
            }

            runtime_parameter_types[runtime_parameter_index] = parameter_types[i];

            runtime_parameter_index += 1;
        }
    }

    assert(runtime_parameter_index == runtime_parameter_count);

    Type *return_type;
    if(declaration->return_type) {
        expect_delayed(return_type_value, evaluate_type_expression(info, jobs, &signature_scope, nullptr, declaration->return_type));

        if(!is_runtime_type(return_type_value)) {
            error(
                scope,
                declaration->return_type->range,
                "Function returns cannot be of type '%s'",
                type_description(return_type_value)
            );

            return err;
        }

        return_type = return_type_value;
    } else {
        return_type = &void_singleton;
    }

    List<ConstantScope*> child_scopes {};
    ConstantScope *body_scope;
    if(declaration->is_external) {
        body_scope = nullptr;
    } else {
        body_scope = new ConstantScope;
        body_scope->statements = declaration->statements;
        body_scope->scope_constants = to_array(scope_constants);
        body_scope->is_top_level = false;
        body_scope->parent = scope;

        if(!process_scope(jobs, body_scope, body_scope->statements, &child_scopes, false)) {
            return err;
        }
    }

    return has({
        new FunctionTypeType {
            {
                runtime_parameter_count,
                runtime_parameter_types
            },
            return_type
        },
        new FunctionConstant {
            declaration,
            body_scope,
            to_array(child_scopes)
        }
    });
}

DelayedResult<Type*> do_resolve_struct_definition(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantScope *scope
) {
    auto parameter_count = struct_definition->parameters.count;

    if(struct_definition->parameters.count > 0) {
        auto parameter_types = allocate<Type*>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, struct_definition->parameters[i].type));

            parameter_types[i] = type;
        }

        return has(new PolymorphicStruct {
            struct_definition,
            parameter_types,
            scope
        });
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = {};
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.count;

    auto members = allocate<StructType::Member>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, evaluate_type_expression(
            info,
            jobs,
            &member_scope,
            nullptr,
            struct_definition->members[i].type
        ));

        expect(actual_member_type, coerce_to_default_type(info, &member_scope, struct_definition->members[i].type->range, member_type));

        if(!is_runtime_type(actual_member_type)) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(actual_member_type));

            return err;
        }

        members[i] = {
            struct_definition->members[i].name.text,
            actual_member_type
        };
    }

    return has(new StructType {
        struct_definition,
        {
            member_count,
            members
        }
    });
}

DelayedResult<Type*> do_resolve_polymorphic_struct(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantValue **parameters,
    ConstantScope *scope
) {
    auto parameter_count = struct_definition->parameters.count;
    assert(parameter_count > 0);

    auto constant_parameters = allocate<ScopeConstant>(parameter_count);

    for(size_t i = 0; i < parameter_count; i += 1) {
        expect_delayed(parameter_type, evaluate_type_expression(info, jobs, scope, nullptr, struct_definition->parameters[i].type));

        constant_parameters[i] = {
            struct_definition->parameters[i].name.text,
            parameter_type,
            parameters[i]
        };
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = { parameter_count, constant_parameters };
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.count;

    auto members = allocate<StructType::Member>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, evaluate_type_expression(
            info,
            jobs,
            &member_scope,
            nullptr,
            struct_definition->members[i].type
        ));

        expect(actual_member_type, coerce_to_default_type(info, &member_scope, struct_definition->members[i].type->range, member_type));

        if(!is_runtime_type(actual_member_type)) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%s'", type_description(actual_member_type));

            return err;
        }

        members[i] = {
            struct_definition->members[i].name.text,
            actual_member_type
        };
    }

    return has(new StructType {
        struct_definition,
        {
            member_count,
            members
        }
    });
}

profiled_function(bool, process_scope, (
    List<Job*> *jobs,
    ConstantScope *scope,
    Array<Statement*> statements,
    List<ConstantScope*> *child_scopes,
    bool is_top_level
), (
    jobs,
    scope,
    statements,
    child_scopes,
    is_top_level
)) {
    for(auto statement : statements) {
        switch(statement->kind) {
            case StatementKind::FunctionDeclaration: {
                auto function_declaration = (FunctionDeclaration*)statement;

                auto is_polymorphic = false;
                for(auto parameter : function_declaration->parameters) {
                    if(parameter.is_constant || parameter.is_polymorphic_determiner) {
                        is_polymorphic = true;
                        break;
                    }
                }

                if(!is_polymorphic) {
                    auto resolve_function_declaration = new ResolveFunctionDeclaration;
                    resolve_function_declaration->done = false;
                    resolve_function_declaration->waiting_for = nullptr;
                    resolve_function_declaration->declaration = function_declaration;
                    resolve_function_declaration->scope = scope;

                    append(jobs, (Job*)resolve_function_declaration);
                }
            } break;

            case StatementKind::ConstantDefinition: {
                auto constant_definition = (ConstantDefinition*)statement;

                auto resolve_constant_definition = new ResolveConstantDefinition;
                resolve_constant_definition->done = false;
                resolve_constant_definition->waiting_for = nullptr;
                resolve_constant_definition->definition = constant_definition;
                resolve_constant_definition->scope = scope;

                append(jobs, (Job*)resolve_constant_definition);
            } break;

            case StatementKind::StructDefinition: {
                auto struct_definition = (StructDefinition*)statement;

                auto resolve_struct_definition = new ResolveStructDefinition;
                resolve_struct_definition->done = false;
                resolve_struct_definition->waiting_for = nullptr;
                resolve_struct_definition->definition = struct_definition;
                resolve_struct_definition->scope = scope;

                append(jobs, (Job*)resolve_struct_definition);
            } break;

            case StatementKind::VariableDeclaration: {
                if(is_top_level) {
                    auto variable_declaration = (VariableDeclaration*)statement;

                    auto generate_static_variable = new GenerateStaticVariable;
                    generate_static_variable->done = false;
                    generate_static_variable->waiting_for = nullptr;
                    generate_static_variable->declaration = variable_declaration;
                    generate_static_variable->scope = scope;

                    append(jobs, (Job*)generate_static_variable);
                }
            } break;

            case StatementKind::IfStatement: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return false;
                }

                auto if_statement = (IfStatement*)statement;

                auto if_scope = new ConstantScope;
                if_scope->statements = if_statement->statements;
                if_scope->scope_constants = {};
                if_scope->is_top_level = false;
                if_scope->parent = scope;

                append(child_scopes, if_scope);

                process_scope(jobs, if_scope, if_statement->statements, child_scopes, false);

                for(auto else_if : if_statement->else_ifs) {
                    auto else_if_scope = new ConstantScope;
                    else_if_scope->statements = else_if.statements;
                    else_if_scope->scope_constants = {};
                    else_if_scope->is_top_level = false;
                    else_if_scope->parent = scope;

                    append(child_scopes, else_if_scope);

                    process_scope(jobs, else_if_scope, else_if.statements, child_scopes, false);
                }

                if(if_statement->else_statements.count != 0) {
                    auto else_scope = new ConstantScope;
                    else_scope->statements = if_statement->else_statements;
                    else_scope->scope_constants = {};
                    else_scope->is_top_level = false;
                    else_scope->parent = scope;

                    append(child_scopes, else_scope);

                    process_scope(jobs, else_scope, if_statement->else_statements, child_scopes, false);
                }
            } break;

            case StatementKind::WhileLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return false;
                }

                auto while_loop = (WhileLoop*)statement;

                auto while_scope = new ConstantScope;
                while_scope->statements = while_loop->statements;
                while_scope->scope_constants = {};
                while_scope->is_top_level = false;
                while_scope->parent = scope;

                append(child_scopes, while_scope);

                process_scope(jobs, while_scope, while_loop->statements, child_scopes, false);
            } break;

            case StatementKind::ForLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return false;
                }

                auto for_loop = (ForLoop*)statement;

                auto for_scope = new ConstantScope;
                for_scope->statements = for_loop->statements;
                for_scope->scope_constants = {};
                for_scope->is_top_level = false;
                for_scope->parent = scope;

                append(child_scopes, for_scope);

                process_scope(jobs, for_scope, for_loop->statements, child_scopes, false);
            } break;

            case StatementKind::Import: {
                auto import = (Import*)statement;

                auto source_file_directory = path_get_directory_component(scope->file_path);

                StringBuffer import_file_path {};

                string_buffer_append(&import_file_path, source_file_directory);
                string_buffer_append(&import_file_path, import->path);

                expect(import_file_path_absolute, path_relative_to_absolute(import_file_path.data));

                auto job_already_added = false;
                for(auto job : *jobs) {
                    if(job->kind == JobKind::ParseFile) {
                        auto parse_file = (ParseFile*)job;

                        if(strcmp(parse_file->path, import_file_path_absolute) == 0) {
                            job_already_added = true;
                            break;
                        }
                    }
                }

                if(!job_already_added) {
                    auto parse_file = new ParseFile;
                    parse_file->done = false;
                    parse_file->waiting_for = nullptr;
                    parse_file->path = import_file_path_absolute;

                    append(jobs, (Job*)parse_file);
                }
            } break;

            case StatementKind::UsingStatement: break;

            case StatementKind::StaticIf: {
                auto static_if = (StaticIf*)statement;

                auto resolve_static_if = new ResolveStaticIf;
                resolve_static_if->done = false;
                resolve_static_if->waiting_for = nullptr;
                resolve_static_if->static_if = static_if;
                resolve_static_if->scope = scope;

                append(jobs, (Job*)resolve_static_if);
            } break;

            default: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return false;
                }
            } break;
        }
    }

    return true;
}