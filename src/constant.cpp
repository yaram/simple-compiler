#include "constant.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "profiler.h"
#include "util.h"
#include "jobs.h"
#include "types.h"

void error(ConstantScope* scope, FileRange range, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(get_scope_file_path(*scope), range, format, arguments);

    va_end(arguments);
}

String get_scope_file_path(ConstantScope scope) {
    auto current = scope;

    while(!current.is_top_level) {
        current =* current.parent;
    }

    return current.file_path;
}

bool check_undetermined_integer_to_integer_coercion(ConstantScope* scope, FileRange range, Integer target_type, int64_t value, bool probing) {
    bool in_range;
    if(target_type.is_signed) {
        int64_t min;
        int64_t max;
        switch(target_type.size) {
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
            switch(target_type.size) {
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
            error(scope, range, "Constant '%zd' cannot fit in '%.*s'. You must cast explicitly", value, STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_type).get_description()));
        }

        return false;
    }

    return true;
}

Result<uint64_t> coerce_constant_to_integer_type(
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    Integer target_type,
    bool probing
) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        if(integer.size != target_type.size || integer.is_signed != target_type.is_signed) {
            if(!probing) {
                error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(wrap_integer_type(integer).get_description()), STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_type).get_description()));
            }

            return err();
        }

        return ok(unwrap_integer_constant(value));
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = unwrap_integer_constant(value);

        if(!check_undetermined_integer_to_integer_coercion(scope, range, target_type, (int64_t)integer_value, probing)) {
            return err();
        }

        return ok(integer_value);
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_type).get_description()));
        }

        return err();
    }
}

static Result<uint64_t> coerce_constant_to_undetermined_integer(
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    bool probing
) {
    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        auto integer_value = unwrap_integer_constant(value);

        switch(integer.size) {
            case RegisterSize::Size8: {
                return ok((uint64_t)(uint8_t)integer_value);
            } break;

            case RegisterSize::Size16: {
                return ok((uint64_t)(uint16_t)integer_value);
            } break;

            case RegisterSize::Size32: {
                return ok((uint64_t)(uint32_t)integer_value);
            } break;

            case RegisterSize::Size64: {
                return ok(integer_value);
            } break;

            default: {
                abort();
            } break;
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        return ok(unwrap_integer_constant(value));
    } else {
        if(!probing) {
            error(scope, range, "Cannot implicitly convert '%.*s' to '{integer}'", STRING_PRINTF_ARGUMENTS(type.get_description()));
        }

        return err();
    }
}

static Result<uint64_t> coerce_constant_to_pointer_type(
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    Pointer target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        return ok(unwrap_integer_constant(value));
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        if(*pointer.type == *target_type.type) {
            return ok(unwrap_pointer_constant(value));
        }
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_pointer_type(target_type).get_description()));
    }

    return err();
}

Result<AnyConstantValue> coerce_constant_to_type(
    GlobalInfo info,
    ConstantScope* scope,
    FileRange range,
    AnyType type,
    AnyConstantValue value,
    AnyType target_type,
    bool probing
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer = target_type.integer;

        expect(integer_value, coerce_constant_to_integer_type(scope, range, type, value, integer, probing));

        return ok(wrap_integer_constant(integer_value));
    } else if(target_type.kind == TypeKind::UndeterminedInteger) {
        expect(integer_value, coerce_constant_to_undetermined_integer(scope, range, type, value, probing));

        return ok(wrap_integer_constant(integer_value));
    } else if(target_type.kind == TypeKind::FloatType) {
        auto target_float_type = target_type.float_;

        if(type.kind == TypeKind::UndeterminedInteger) {
            return ok(wrap_float_constant((double)unwrap_integer_constant(value)));
        } else if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;

            if(target_float_type.size == float_type.size) {
                return ok(wrap_float_constant(unwrap_float_constant(value)));
            }
        } else if(type.kind == TypeKind::UndeterminedFloat) {
            return ok(wrap_float_constant(unwrap_float_constant(value)));
        }
    } else if(target_type.kind == TypeKind::UndeterminedFloat) {
        if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;
            auto float_value = unwrap_float_constant(value);

            double value;
            switch(float_type.size) {
                case RegisterSize::Size32: {
                    value = (double)(float)float_value;
                } break;

                case RegisterSize::Size64: {
                    value = float_value;
                } break;

                default: {
                    abort();
                } break;
            }

            return ok(wrap_float_constant(value));
        } else if(type.kind == TypeKind::UndeterminedFloat) {
            return ok(wrap_float_constant(unwrap_float_constant(value)));
        }
    } else if(target_type.kind == TypeKind::Pointer) {
        auto target_pointer = target_type.pointer;

        expect(pointer_value, coerce_constant_to_pointer_type(scope, range, type, value, target_pointer, probing));

        return ok(wrap_pointer_constant(pointer_value));
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array_type = target_type.array;

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;

            if(*target_array_type.element_type == *array_type.element_type) {
                return ok(value);
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*static_array.element_type != *target_array_type.element_type) {
                if(!probing) {
                    error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
                }

                return err();
            }

            return ok(value);
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == "pointer"_S &&
                undetermined_struct.members[1].name == "length"_S
            ) {
                auto undetermined_struct_value = unwrap_struct_constant(value);

                auto pointer_result = coerce_constant_to_pointer_type(
                    scope,
                    range,
                    undetermined_struct.members[0].type,
                    undetermined_struct_value.members[0],
                    {
                        target_array_type.element_type
                    },
                    true
                );

                if(pointer_result.status) {
                    auto length_result = coerce_constant_to_integer_type(
                        scope,
                        range,
                        undetermined_struct.members[1].type,
                        undetermined_struct_value.members[1],
                        {
                            info.architecture_sizes.address_size,
                            false
                        },
                        true
                    );

                    if(length_result.status) {
                        ArrayConstant array_constant {};
                        array_constant.pointer = pointer_result.value;
                        array_constant.length = length_result.value;
                    }
                }
            }
        }
    } else if(type == target_type) {
        return ok(value);
    }

    if(!probing) {
        error(scope, range, "Cannot implicitly convert '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
    }

    return err();
}

Result<TypedConstantValue> evaluate_constant_index(
    GlobalInfo info,
    ConstantScope* scope,
    AnyType type,
    AnyConstantValue value,
    FileRange range,
    AnyType index_type,
    AnyConstantValue index_value,
    FileRange index_range
) {
    expect(index, coerce_constant_to_integer_type(
        scope,
        index_range,
        index_type,
        index_value,
        {
            info.architecture_sizes.address_size,
            false
        },
        false
    ));

    if(type.kind == TypeKind::ArrayTypeType) {
        auto array_type = type.array;

        if(value.kind == ConstantValueKind::StaticArrayConstant) {
            auto static_array_value = unwrap_static_array_constant(value);

            if(index >= static_array_value.elements.length) {
                error(scope, index_range, "Array index %zu out of bounds", index);

                return err();
            }

            return ok(TypedConstantValue(
                *array_type.element_type,
                static_array_value.elements[index]
            ));
        } else {
            assert(value.kind == ConstantValueKind::ArrayConstant);

            error(scope, range, "Cannot index an array with non-constant elements in a constant context");

            return err();
        }
    } else if(type.kind == TypeKind::StaticArray) {
        auto static_array = type.static_array;

        if(index >= static_array.length) {
            error(scope, index_range, "Array index %zu out of bounds", index);

            return err();
        }

        auto static_array_value = unwrap_static_array_constant(value);

        assert(static_array_value.elements.length == static_array.length);

        return ok(TypedConstantValue(
            *static_array.element_type,
            static_array_value.elements[index]
        ));
    } else {
        error(scope, range, "Cannot index %.*s", STRING_PRINTF_ARGUMENTS(type.get_description()));

        return err();
    }
}

Result<AnyType> determine_binary_operation_type(ConstantScope* scope, FileRange range, AnyType left, AnyType right) {
    if(left.kind == TypeKind::Boolean || right.kind == TypeKind::Boolean) {
        return ok(left);
    } else if(left.kind == TypeKind::Pointer) {
        return ok(left);
    } else if(right.kind == TypeKind::Pointer) {
        return ok(right);
    } else if(left.kind == TypeKind::Integer && right.kind == TypeKind::Integer) {
        auto left_integer = left.integer;
        auto right_integer = right.integer;

        RegisterSize largest_size;
        if(left_integer.size > right_integer.size) {
            largest_size = left_integer.size;
        } else {
            largest_size = right_integer.size;
        }

        auto is_either_signed = left_integer.is_signed || right_integer.is_signed;

        return ok(wrap_integer_type(Integer(
            largest_size,
            is_either_signed
        )));
    } else if(left.kind == TypeKind::FloatType && right.kind == TypeKind::FloatType) {
        auto left_float = left.float_;
        auto right_float = right.float_;

        RegisterSize largest_size;
        if(left_float.size > right_float.size) {
            largest_size = left_float.size;
        } else {
            largest_size = right_float.size;
        }

        return ok(wrap_float_type(FloatType(
            largest_size
        )));
    } else if(left.kind == TypeKind::FloatType) {
        return ok(left);
    } else if(right.kind == TypeKind::FloatType) {
        return ok(right);
    } else if(left.kind == TypeKind::UndeterminedFloat || right.kind == TypeKind::UndeterminedFloat) {
        return ok(left);
    } else if(left.kind == TypeKind::Integer) {
        return ok(left);
    } else if(right.kind == TypeKind::Integer) {
        return ok(right);
    } else if(left.kind == TypeKind::UndeterminedInteger || right.kind == TypeKind::UndeterminedInteger) {
        return ok(left);
    } else {
        error(scope, range, "Mismatched types '%.*s' and '%.*s'", STRING_PRINTF_ARGUMENTS(left.get_description()), STRING_PRINTF_ARGUMENTS(right.get_description()));

        return err();
    }
}

Result<TypedConstantValue> evaluate_constant_binary_operation(
    GlobalInfo info,
    ConstantScope* scope,
    FileRange range,
    BinaryOperation::Operator binary_operator,
    FileRange left_range,
    AnyType left_type,
    AnyConstantValue left_value,
    FileRange right_range,
    AnyType right_type,
    AnyConstantValue right_value
) {
    expect(type, determine_binary_operation_type(scope, range, left_type, right_type));

    expect(coerced_left_value, coerce_constant_to_type(info, scope, left_range, left_type, left_value, type, false));

    expect(coerced_right_value, coerce_constant_to_type(info, scope, right_range, right_type, right_value, type, false));

    if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        auto left = unwrap_integer_constant(coerced_left_value);

        auto right = unwrap_integer_constant(coerced_right_value);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(left + right)
                ));
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(left - right)
                ));
            } break;

            case BinaryOperation::Operator::Multiplication: {
                uint64_t result;
                if(integer.is_signed) {
                    result = (int64_t)left * (int64_t)right;
                } else {
                    result = left * right;
                }

                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(result)
                ));
            } break;

            case BinaryOperation::Operator::Division: {
                uint64_t result;
                if(integer.is_signed) {
                    result = (int64_t)left / (int64_t)right;
                } else {
                    result = left / right;
                }

                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(result)
                ));
            } break;

            case BinaryOperation::Operator::Modulo: {
                uint64_t result;
                if(integer.is_signed) {
                    result = (int64_t)left % (int64_t)right;
                } else {
                    result = left % right;
                }

                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(result)
                ));
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(left & right)
                ));
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return ok(TypedConstantValue(
                    wrap_integer_type(integer),
                    wrap_integer_constant(left | right)
                ));
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left == right)
                ));
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left != right)
                ));
            } break;

            case BinaryOperation::Operator::LessThan: {
                bool result;
                if(integer.is_signed) {
                    result = (int64_t)left < (int64_t)right;
                } else {
                    result = left < right;
                }

                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(result)
                ));
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                bool result;
                if(integer.is_signed) {
                    result = (int64_t)left > (int64_t)right;
                } else {
                    result = left > right;
                }

                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(result)
                ));
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return err();
            } break;
        }
    } else if(type.kind == TypeKind::UndeterminedInteger) {
        auto left = unwrap_integer_constant(coerced_left_value);

        auto right = unwrap_integer_constant(coerced_right_value);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant(left + right)
                ));
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant(left - right)
                ));
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant((uint64_t)((int64_t)left * (int64_t)right))
                ));
            } break;

            case BinaryOperation::Operator::Division: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant((uint64_t)((int64_t)left / (int64_t)right))
                ));
            } break;

            case BinaryOperation::Operator::Modulo: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant((uint64_t)((int64_t)left % (int64_t)right))
                ));
            } break;

            case BinaryOperation::Operator::BitwiseAnd: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant(left & right)
                ));
            } break;

            case BinaryOperation::Operator::BitwiseOr: {
                return ok(TypedConstantValue(
                    create_undetermined_integer_type(),
                    wrap_integer_constant(left | right)
                ));
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left == right)
                ));
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left != right)
                ));
            } break;

            case BinaryOperation::Operator::LessThan: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant((int64_t)left < (int64_t)right)
                ));
            } break;

            case BinaryOperation::Operator::GreaterThan: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant((int64_t)left > (int64_t)right)
                ));
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on integers");

                return err();
            } break;
        }
    } else if(type.kind == TypeKind::Boolean) {
        auto left = unwrap_boolean_constant(coerced_left_value);

        auto right = unwrap_boolean_constant(coerced_right_value);

        switch(binary_operator) {
            case BinaryOperation::Operator::BooleanAnd: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left && right)
                ));
            } break;

            case BinaryOperation::Operator::BooleanOr: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left || right)
                ));
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left == right)
                ));
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left != right)
                ));
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on booleans");

                return err();
            } break;
        }
    } else if(type.kind == TypeKind::FloatType || type.kind == TypeKind::UndeterminedFloat) {
        auto left = unwrap_float_constant(coerced_left_value);

        auto right = unwrap_float_constant(coerced_right_value);

        switch(binary_operator) {
            case BinaryOperation::Operator::Addition: {
                return ok(TypedConstantValue(
                    type,
                    wrap_float_constant(left + right)
                ));
            } break;

            case BinaryOperation::Operator::Subtraction: {
                return ok(TypedConstantValue(
                    type,
                    wrap_float_constant(left - right)
                ));
            } break;

            case BinaryOperation::Operator::Multiplication: {
                return ok(TypedConstantValue(
                    type,
                    wrap_float_constant(left * right)
                ));
            } break;

            case BinaryOperation::Operator::Division: {
                return ok(TypedConstantValue(
                    type,
                    wrap_float_constant(left / right)
                ));
            } break;

            case BinaryOperation::Operator::Equal: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left == right)
                ));
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left != right)
                ));
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return err();
            } break;
        }
    } else if(type.kind == TypeKind::Pointer) {
        auto left = unwrap_pointer_constant(coerced_left_value);

        auto right = unwrap_pointer_constant(coerced_right_value);

        switch(binary_operator) {
            case BinaryOperation::Operator::Equal: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left == right)
                ));
            } break;

            case BinaryOperation::Operator::NotEqual: {
                return ok(TypedConstantValue(
                    create_boolean_type(),
                    wrap_boolean_constant(left != right)
                ));
            } break;

            default: {
                error(scope, range, "Cannot perform that operation on pointers");

                return err();
            } break;
        }
    } else {
        abort();
    }
}

Result<AnyConstantValue> evaluate_constant_cast(
    GlobalInfo info,
    ConstantScope* scope,
    AnyType type,
    AnyConstantValue value,
    FileRange value_range,
    AnyType target_type,
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

    if(target_type.kind == TypeKind::Integer) {
        auto target_integer = target_type.integer;

        uint64_t result;

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto integer_value = unwrap_integer_constant(value);

            if(integer.is_signed) {
                switch(integer.size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)integer_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)integer_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)integer_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer.size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)integer_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)integer_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)integer_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = integer_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }        
        } else if(type.kind == TypeKind::UndeterminedInteger) {
            result = unwrap_integer_constant(value);
        } else if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;

            auto float_value = unwrap_float_constant(value);

            double from_value;
            switch(float_type.size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value;
                } break;

                default: {
                    abort();
                } break;
            }

            if(target_integer.is_signed) {
                switch(target_integer.size) {
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
                switch(target_integer.size) {
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
        } else if(type.kind == TypeKind::UndeterminedFloat) {
            auto float_value = unwrap_float_constant(value);

            if(target_integer.is_signed) {
                switch(target_integer.size) {
                    case RegisterSize::Size8: {
                        result = (int8_t)float_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (int16_t)float_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (int32_t)float_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (int64_t)float_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(target_integer.size) {
                    case RegisterSize::Size8: {
                        result = (uint8_t)float_value;
                    } break;

                    case RegisterSize::Size16: {
                        result = (uint16_t)float_value;
                    } break;

                    case RegisterSize::Size32: {
                        result = (uint32_t)float_value;
                    } break;

                    case RegisterSize::Size64: {
                        result = (uint64_t)float_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }
        } else if(type.kind == TypeKind::Pointer) {
            auto pointer = type.pointer;

            if(target_integer.size == info.architecture_sizes.address_size && !target_integer.is_signed) {
                result = unwrap_pointer_constant(value);
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(wrap_pointer_type(pointer).get_description()), STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_integer).get_description()));
                }

                return err();
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_integer_type(target_integer).get_description()));
            }

            return err();
        }

        return ok(wrap_integer_constant(result));
    } else if(target_type.kind == TypeKind::FloatType) {
        auto target_float_type = target_type.float_;

        double result;
        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            auto integer_value = unwrap_integer_constant(value);

            double from_value;
            if(integer.is_signed) {
                switch(integer.size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(int8_t)integer_value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(int16_t)integer_value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(int32_t)integer_value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)(int64_t)integer_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            } else {
                switch(integer.size) {
                    case RegisterSize::Size8: {
                        from_value = (double)(uint8_t)integer_value;
                    } break;

                    case RegisterSize::Size16: {
                        from_value = (double)(uint16_t)integer_value;
                    } break;

                    case RegisterSize::Size32: {
                        from_value = (double)(uint32_t)integer_value;
                    } break;

                    case RegisterSize::Size64: {
                        from_value = (double)integer_value;
                    } break;

                    default: {
                        abort();
                    } break;
                }
            }

            switch(target_float_type.size) {
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
        } else if(type.kind == TypeKind::UndeterminedInteger) {
            auto integer_value = unwrap_integer_constant(value);

            switch(target_float_type.size) {
                case RegisterSize::Size32: {
                    result = (double)(float)(int64_t)integer_value;
                } break;

                case RegisterSize::Size64: {
                    result = (double)(int64_t)integer_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else if(type.kind == TypeKind::FloatType) {
            auto float_type = type.float_;

            auto float_value = unwrap_float_constant(value);

            double from_value;
            switch(float_type.size) {
                case RegisterSize::Size32: {
                    from_value = (double)(float)float_value;
                } break;

                case RegisterSize::Size64: {
                    from_value = float_value;
                } break;

                default: {
                    abort();
                } break;
            }

            switch(target_float_type.size) {
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
        } else if(type.kind == TypeKind::UndeterminedFloat) {
            auto float_value = unwrap_float_constant(value);

            switch(target_float_type.size) {
                case RegisterSize::Size32: {
                    result = (double)(float)float_value;
                } break;

                case RegisterSize::Size64: {
                    result = float_value;
                } break;

                default: {
                    abort();
                } break;
            }
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_float_type(target_float_type).get_description()));
            }

            return err();
        }

        return ok(wrap_float_constant(result));
    } else if(target_type.kind == TypeKind::Pointer) {
        auto target_pointer = target_type.pointer;

        uint64_t result;

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;
            if(integer.size == info.architecture_sizes.address_size && !integer.is_signed) {
                result = unwrap_integer_constant(value);
            } else {
                if(!probing) {
                    error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(wrap_integer_type(integer).get_description()), STRING_PRINTF_ARGUMENTS(wrap_pointer_type(target_pointer).get_description()));
                }

                return err();
            }
        } else if(type.kind == TypeKind::Pointer) {
            auto pointer = type.pointer;

            result = unwrap_pointer_constant(value);
        } else {
            if(!probing) {
                error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(wrap_pointer_type(target_pointer).get_description()));
            }

            return err();
        }

        return ok(wrap_pointer_constant(result));
    } else {
        if(!probing) {
            error(scope, value_range, "Cannot cast from '%.*s' to '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()), STRING_PRINTF_ARGUMENTS(target_type.get_description()));
        }

        return err();
    }
}

Result<AnyType> coerce_to_default_type(GlobalInfo info, ConstantScope* scope, FileRange range, AnyType type) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        return ok(wrap_integer_type(Integer(
            info.architecture_sizes.default_integer_size,
            true
        )));
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        return ok(wrap_float_type(FloatType(
            info.architecture_sizes.default_float_size
        )));
    } else if(type.kind == TypeKind::UndeterminedStruct) {
        error(scope, range, "Undetermined struct types cannot exist at runtime");

        return err();
    } else {
        return ok(type);
    }
}

bool is_declaration_public(Statement* declaration) {
    if(declaration->kind == StatementKind::FunctionDeclaration) {
        return true;
    } else if(declaration->kind == StatementKind::ConstantDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::StructDefinition) {
        return true;
    } else {
        return false;
    }
}

bool match_public_declaration(Statement* statement, String name) {
    String declaration_name;
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

    return declaration_name == name;
}

bool match_declaration(Statement* statement, String name) {
    String declaration_name;
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

        declaration_name = import->name;
    } else {
        return false;
    }

    return declaration_name == name;
}

DelayedResult<TypedConstantValue> get_simple_resolved_declaration(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* declaration
) {
    switch(declaration->kind) {
        case StatementKind::FunctionDeclaration: {
            auto function_declaration = (FunctionDeclaration*)declaration;

            for(auto parameter : function_declaration->parameters) {
                if(parameter.is_constant || parameter.is_polymorphic_determiner) {
                    return ok(TypedConstantValue(
                        create_polymorphic_function_type(),
                        wrap_polymorphic_function_constant(PolymorphicFunctionConstant(
                            function_declaration,
                            scope
                        ))
                    ));
                }
            }

            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolveFunctionDeclaration) {
                    auto resolve_function_declaration = job.resolve_function_declaration;

                    if(resolve_function_declaration.declaration == function_declaration) {
                        if(job.state == JobState::Done) {
                            return ok(TypedConstantValue(
                                resolve_function_declaration.type,
                                resolve_function_declaration.value
                            ));
                        } else {
                            return wait(i);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::ConstantDefinition: {
            auto constant_definition = (ConstantDefinition*)declaration;

            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolveConstantDefinition) {
                    auto resolve_constant_definition = job.resolve_constant_definition;

                    if(resolve_constant_definition.definition == constant_definition) {
                        if(job.state == JobState::Done) {
                            return ok(TypedConstantValue(
                                resolve_constant_definition.type,
                                resolve_constant_definition.value
                            ));
                        } else {
                            return wait(i);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::StructDefinition: {
            auto struct_definition = (StructDefinition*)declaration;

            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ResolveStructDefinition) {
                    auto resolve_struct_definition = job.resolve_struct_definition;

                    if(resolve_struct_definition.definition == struct_definition) {
                        if(job.state == JobState::Done) {
                            return ok(TypedConstantValue(
                                create_type_type(),
                                wrap_type_constant(resolve_struct_definition.type)
                            ));
                        } else {
                            return wait(i);
                        }
                    }
                }
            }

            abort();
        } break;

        case StatementKind::Import: {
            auto import = (Import*)declaration;

            auto job_already_added = false;
            for(size_t i = 0; i < jobs->length; i += 1) {
                auto job = (*jobs)[i];

                if(job.kind == JobKind::ParseFile) {
                    auto parse_file = job.parse_file;

                    if(parse_file.path == import->absolute_path) {
                        if(job.state == JobState::Done) {
                            return ok(TypedConstantValue(
                                create_file_module_type(),
                                wrap_file_module_constant(FileModuleConstant(
                                    parse_file.scope
                                ))
                            ));
                        } else {
                            return wait(i);
                        }
                    }
                }
            }

            abort();
        } break;

        default: abort();
    }
}

bool constant_values_equal(AnyConstantValue a, AnyConstantValue b) {
    if(a.kind != b.kind) {
        return false;
    }

    switch(a.kind) {
        case ConstantValueKind::FunctionConstant: {
            return a.function.declaration == b.function.declaration;
        } break;

        case ConstantValueKind::PolymorphicFunctionConstant: {
            return a.polymorphic_function.declaration == b.polymorphic_function.declaration;
        } break;

        case ConstantValueKind::BuiltinFunctionConstant: {
            return a.builtin_function.name == b.builtin_function.name;
        } break;

        case ConstantValueKind::IntegerConstant: {
            return a.integer == b.integer;
        } break;

        case ConstantValueKind::BooleanConstant: {
            return a.boolean == b.boolean;
        } break;

        case ConstantValueKind::FloatConstant: {
            return a.float_ == b.float_;
        } break;

        case ConstantValueKind::TypeConstant: {
            return a.type == b.type;
        } break;

        case ConstantValueKind::VoidConstant: {
            return true;
        } break;

        case ConstantValueKind::PointerConstant: {
            return a.pointer == b.pointer;
        } break;

        case ConstantValueKind::ArrayConstant: {
            return a.array.length == b.array.length && a.array.pointer == b.array.pointer;
        } break;

        case ConstantValueKind::StaticArrayConstant: {
            if(a.static_array.elements.length != b.static_array.elements.length) {
                return false;
            }

            for(size_t i = 0; i < a.static_array.elements.length; i += 1) {
                if(!constant_values_equal(a.static_array.elements[i], b.static_array.elements[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case ConstantValueKind::StructConstant: {
            if(a.struct_.members.length != b.struct_.members.length) {
                return false;
            }

            for(size_t i = 0; i < a.struct_.members.length; i += 1) {
                if(!constant_values_equal(a.struct_.members[i], b.struct_.members[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case ConstantValueKind::FileModuleConstant: {
            return a.file_module.scope == b.file_module.scope;
        } break;

        default: abort();
    }
}

static Result<String> get_declaration_name(Statement* declaration) {
    if(declaration->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)declaration;

        return ok(function_declaration->name.text);
    } else if(declaration->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)declaration;

        return ok(constant_definition->name.text);
    } else if(declaration->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)declaration;

        return ok(struct_definition->name.text);
    } else if(declaration->kind == StatementKind::Import) {
        auto import = (Import*)declaration;

        return ok(import->name);
    } else {
        return err();
    }
}

uint32_t calculate_string_hash(String string) {
    uint32_t hash = 0;

    for(size_t i = 0; i < string.length; i += 1) {
        hash = (uint8_t)string.elements[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

DeclarationHashTable create_declaration_hash_table(Array<Statement*> statements) {
    DeclarationHashTable hash_table {};

    for(auto statement : statements) {
        auto result = get_declaration_name(statement);

        if(result.status) {
            auto declaration_name = result.value;

            auto hash = calculate_string_hash(declaration_name);

            auto bucket_index = hash % DECLARATION_HASH_TABLE_SIZE;

            hash_table.buckets[bucket_index].append(statement);
        }
    }

    return hash_table;
}

Statement* search_in_declaration_hash_table(DeclarationHashTable declaration_hash_table, uint32_t hash, String name) {
    auto bucket = declaration_hash_table.buckets[hash % DECLARATION_HASH_TABLE_SIZE];

    for(auto declaration : bucket) {
        auto result = get_declaration_name(declaration);

        assert(result.status);

        if(result.value == name) {
            return declaration;
        }
    }

    return nullptr;
}

profiled_function(DelayedResult<DeclarationSearchResult>, search_for_declaration, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    String name,
    uint32_t name_hash,
    ConstantScope* scope,
    Array<Statement*> statements,
    DeclarationHashTable declarations,
    bool external,
    Statement* ignore
), (
    info,
    jobs,
    name,
    name_hash,
    scope,
    statements,
    declarations,
    external,
    ignore
)) {
    auto declaration = search_in_declaration_hash_table(declarations, name_hash, name);

    if(declaration != nullptr && declaration != ignore) {
        if(external && !is_declaration_public(declaration)) {
            DeclarationSearchResult result {};
            result.found = false;

            return ok(result);
        }

        expect_delayed(value, get_simple_resolved_declaration(info, jobs, scope, declaration));

        DeclarationSearchResult result {};
        result.found = true;
        result.type = value.type;
        result.value = value.value;

        return ok(result);
    }

    for(auto statement : statements) {
        if(statement == ignore) {
            continue;
        }

        if(statement->kind == StatementKind::UsingStatement) {
            if(!external) {
                auto using_statement = (UsingStatement*)statement;

                expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, using_statement, using_statement->module));

                if(expression_value.type.kind != TypeKind::FileModule) {
                    error(scope, using_statement->range, "Expected a module, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }

                auto file_module = unwrap_file_module_constant(expression_value.value);

                expect_delayed(search_value, search_for_declaration(info, jobs, name, name_hash, file_module.scope, file_module.scope->statements, file_module.scope->declarations, true, nullptr));

                if(search_value.found) {
                    DeclarationSearchResult result {};
                    result.found = true;
                    result.type = search_value.type;
                    result.value = search_value.value;

                    return ok(result);
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
                                expect_delayed(search_value, search_for_declaration(info, jobs, name, name_hash, scope, static_if->statements, resolve_static_if.declarations, false, nullptr));

                                if(search_value.found) {
                                    DeclarationSearchResult result {};
                                    result.found = true;
                                    result.type = search_value.type;
                                    result.value = search_value.value;

                                    return ok(result);
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
                                return wait(i);
                            }
                        }
                    }
                }
            }

            assert(found);
        }
    }

    for(auto scope_constant : scope->scope_constants) {
        if(scope_constant.name == name) {
            DeclarationSearchResult result {};
            result.found = true;
            result.type = scope_constant.type;
            result.value = scope_constant.value;

            return ok(result);
        }
    }

    DeclarationSearchResult result {};
    result.found = false;

    return ok(result);
}

Result<String> array_to_string(ConstantScope* scope, FileRange range, AnyType type, AnyConstantValue value) {
    AnyType element_type;
    StaticArrayConstant static_array_value;
    if(type.kind == TypeKind::StaticArray) {
        element_type = *type.static_array.element_type;

        static_array_value = unwrap_static_array_constant(value);

        assert(static_array_value.elements.length == type.static_array.length);
    } else if(type.kind == TypeKind::ArrayTypeType) {
        element_type = *type.array.element_type;

        if(value.kind == ConstantValueKind::StaticArrayConstant) {
            static_array_value = value.static_array;
        } else {
            assert(value.kind == ConstantValueKind::ArrayConstant);

            error(scope, range, "Cannot use an array with non-constant elements in this context");
        }
    } else {
        error(scope, range, "Expected a string ([]u8), got '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));
    }

    if(
        element_type.kind != TypeKind::Integer ||
        element_type.integer.size != RegisterSize::Size8
    ) {
        error(scope, range, "Expected a string ([]u8), got '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

        return err();
    }

    auto data = allocate<char>(static_array_value.elements.length);
    for(size_t i = 0; i < static_array_value.elements.length; i += 1) {
        data[i] = (char)unwrap_integer_constant(static_array_value.elements[i]);
    }

    String string {};
    string.length = static_array_value.elements.length;
    string.elements = data;

    return ok(string);
}

profiled_function(DelayedResult<TypedConstantValue>, evaluate_constant_expression, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* ignore_statement,
    Expression* expression
), (
    info,
    jobs,
    scope,
    ignore_statement,
    expression
)) {
    if(expression->kind == ExpressionKind::NamedReference) {
        auto named_reference = (NamedReference*)expression;

        auto name_hash = calculate_string_hash(named_reference->name.text);

        auto current_scope = scope;
        while(true) {
            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                named_reference->name.text,
                name_hash,
                current_scope,
                current_scope->statements,
                current_scope->declarations,
                false,
                ignore_statement
            ));

            if(search_value.found) {
                return ok(TypedConstantValue(
                    search_value.type,
                    search_value.value
                ));
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(named_reference->name.text == global_constant.name) {
                return ok(TypedConstantValue(
                    global_constant.type,
                    global_constant.value
                ));
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %.*s", STRING_PRINTF_ARGUMENTS(named_reference->name.text));

        return err();
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, member_reference->expression));

        if(expression_value.type.kind == TypeKind::ArrayTypeType) {
            auto array_type = expression_value.type.array;

            if(expression_value.value.kind == ConstantValueKind::ArrayConstant) {
                auto array_value = unwrap_array_constant(expression_value.value);

                if(member_reference->name.text == "length"_S) {
                    return ok(TypedConstantValue(
                        wrap_integer_type(Integer(
                            info.architecture_sizes.address_size,
                            false
                        )),
                        wrap_integer_constant(array_value.length)
                    ));
                } else if(member_reference->name.text == "pointer"_S) {
                    return ok(TypedConstantValue(
                        wrap_pointer_type(Pointer(
                            array_type.element_type
                        )),
                        wrap_pointer_constant(array_value.pointer)
                    ));
                } else {
                    error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                    return err();
                }
            } else {
                auto static_array_value = unwrap_static_array_constant(expression_value.value);

                if(member_reference->name.text == "length"_S) {
                    return ok(TypedConstantValue(
                        wrap_integer_type(Integer(
                            info.architecture_sizes.address_size,
                            false
                        )),
                        wrap_integer_constant(static_array_value.elements.length)
                    ));
                } else if(member_reference->name.text == "pointer"_S) {
                    error(scope, member_reference->name.range, "Cannot take pointer to array with constant elements in constant context", member_reference->name.text);

                    return err();
                } else {
                    error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                    return err();
                }
            }
        } else if(expression_value.type.kind == TypeKind::StaticArray) {
            auto static_array = expression_value.type.static_array;

            if(member_reference->name.text == "length"_S) {
                return ok(TypedConstantValue(
                    wrap_integer_type(Integer(
                        info.architecture_sizes.address_size,
                        false
                    )),
                    wrap_integer_constant(static_array.length)
                ));
            } else if(member_reference->name.text == "pointer"_S) {
                error(scope, member_reference->name.range, "Cannot take pointer to static array in constant context", member_reference->name.text);

                return err();
            } else {
                error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(expression_value.type.kind == TypeKind::StructType) {
            auto struct_type = expression_value.type.struct_;

            auto struct_value = unwrap_struct_constant(expression_value.value);

            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(member_reference->name.text == struct_type.members[i].name) {
                    return ok(TypedConstantValue(
                        struct_type.members[i].type,
                        struct_value.members[i]
                    ));
                }
            }

            error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(expression_value.type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = expression_value.type.undetermined_struct;

            auto undetermined_struct_value = unwrap_struct_constant(expression_value.value);

            for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                if(member_reference->name.text == undetermined_struct.members[i].name) {
                    return ok(TypedConstantValue(
                        undetermined_struct.members[i].type,
                        undetermined_struct_value.members[i]
                    ));
                }
            }

            error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else if(expression_value.type.kind == TypeKind::FileModule) {
            auto file_module_value = unwrap_file_module_constant(expression_value.value);

            expect_delayed(search_value, search_for_declaration(
                info,
                jobs,
                member_reference->name.text,
                calculate_string_hash(member_reference->name.text),
                file_module_value.scope,
                file_module_value.scope->statements,
                file_module_value.scope->declarations,
                true,
                nullptr
            ));

            if(search_value.found) {
                return ok(TypedConstantValue(
                    search_value.type,
                    search_value.value
                ));
            }

            error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

            return err();
        } else {
            error(scope, member_reference->expression->range, "Type '%.*s' has no members", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
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

        return ok(value);
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        return ok(TypedConstantValue(
            create_undetermined_integer_type(),
            wrap_integer_constant(integer_literal->value)
        ));
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        return ok(TypedConstantValue(
            create_undetermined_float_type(),
            wrap_float_constant(float_literal->value)
        ));
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.length;

        auto characters = allocate<AnyConstantValue>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = wrap_integer_constant((uint64_t)string_literal->characters[i]);
        }

        return ok(TypedConstantValue(
            wrap_static_array_type(StaticArray(
                character_count,
                heapify(wrap_integer_type(Integer(
                    RegisterSize::Size8,
                    false
                )))
            )),
            wrap_static_array_constant(StaticArrayConstant(
                Array(character_count, characters)
            ))
        ));
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.length;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err();
        }

        expect_delayed(first_element, evaluate_constant_expression(info, jobs, scope, ignore_statement, array_literal->elements[0]));

        expect(determined_element_type, coerce_to_default_type(info, scope, array_literal->elements[0]->range, first_element.type));

        if(!determined_element_type.is_runtime_type()) {
            error(scope, array_literal->range, "Arrays cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(determined_element_type.get_description()));

            return err();
        }

        auto elements = allocate<AnyConstantValue>(element_count);
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

        return ok(TypedConstantValue(
            wrap_static_array_type(StaticArray(
                element_count,
                heapify(determined_element_type)
            )),
            wrap_static_array_constant(StaticArrayConstant(
                Array(element_count, elements)
            ))
        ));
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        auto member_count = struct_literal->members.length;

        if(member_count == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err();
        }

        auto members = allocate<StructTypeMember>(member_count);
        auto member_values = allocate<AnyConstantValue>(member_count);

        for(size_t i = 0; i < member_count; i += 1) {
            auto member_name = struct_literal->members[i].name;

            for(size_t j = 0; j < member_count; j += 1) {
                if(j != i && member_name.text == struct_literal->members[j].name.text) {
                    error(scope, member_name.range, "Duplicate struct member %.*s", STRING_PRINTF_ARGUMENTS(member_name.text));

                    return err();
                }
            }

            expect_delayed(member, evaluate_constant_expression(info, jobs, scope, ignore_statement, struct_literal->members[i].value));

            members[i] = {
                member_name.text,
                member.type
            };

            member_values[i] = member.value;
        }

        return ok(TypedConstantValue(
            wrap_undetermined_struct_type(UndeterminedStruct(
                Array(member_count, members)
            )),
            wrap_struct_constant(StructConstant(
                Array(member_count, member_values)
            ))
        ));
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->expression));

        if(expression_value.type.kind == TypeKind::FunctionTypeType) {
            error(scope, function_call->range, "Function calls not allowed in global context");

            return err();
        } else if(expression_value.type.kind == TypeKind::BuiltinFunction) {
            auto builtin_function_value = unwrap_builtin_function_constant(expression_value.value);

            if(builtin_function_value.name == "size_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[0]));

                AnyType type;
                if(parameter_value.type.kind == TypeKind::Type) {
                    type = unwrap_type_constant(parameter_value.value);
                } else {
                    type = parameter_value.type;
                }

                if(!type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "'%.*s'' has no size", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description()));

                    return err();
                }

                auto size = type.get_size(info.architecture_sizes);

                return ok(TypedConstantValue(
                    wrap_integer_type(Integer(
                        info.architecture_sizes.address_size,
                        false
                    )),
                    wrap_integer_constant(size)
                ));
            } else if(builtin_function_value.name == "type_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[0]));

                return ok(TypedConstantValue(
                    create_type_type(),
                    wrap_type_constant(parameter_value.type)
                ));
            } else if(builtin_function_value.name == "memcpy"_S) {
                error(scope, function_call->range, "'memcpy' cannot be called in a constant context");

                return err();
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::Type) {
            auto type = unwrap_type_constant(expression_value.value);

            if(type.kind == TypeKind::PolymorphicStruct) {
                auto polymorphic_struct = type.polymorphic_struct;

                auto definition = polymorphic_struct.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect struct parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto parameters = allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[i]));

                    expect(parameter_value, coerce_constant_to_type(
                        info,
                        scope,
                        function_call->parameters[i]->range,
                        parameter.type,
                        parameter.value,
                        polymorphic_struct.parameter_types[i],
                        false
                    ));

                    parameters[i] = parameter_value;
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ResolvePolymorphicStruct) {
                        auto resolve_polymorphic_struct = job.resolve_polymorphic_struct;

                        if(resolve_polymorphic_struct.definition == definition) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameters[i], resolve_polymorphic_struct.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job.state == JobState::Done) {
                                    return ok(TypedConstantValue(
                                        create_type_type(),
                                        wrap_type_constant(resolve_polymorphic_struct.type)
                                    ));
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job;
                job.kind = JobKind::ResolvePolymorphicStruct;
                job.state = JobState::Working;
                job.resolve_polymorphic_struct.definition = definition;
                job.resolve_polymorphic_struct.parameters = parameters;
                job.resolve_polymorphic_struct.scope = polymorphic_struct.parent;

                auto job_index = jobs->append(job);

                return wait(job_index);
            } else {
                error(scope, function_call->expression->range, "Type '%.*s' is not polymorphic", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call non-function '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
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

        return ok(value);
    } else if(expression->kind == ExpressionKind::UnaryOperation) {
        auto unary_operation = (UnaryOperation*)expression;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, unary_operation->expression));

        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: {
                if(expression_value.type.kind == TypeKind::Type) {
                    auto type = unwrap_type_constant(expression_value.value);

                    if(
                        !type.is_runtime_type() &&
                        type.kind != TypeKind::Void &&
                        type.kind != TypeKind::FunctionTypeType
                    ) {
                        error(scope, unary_operation->expression->range, "Cannot create pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                        return err();
                    }

                    return ok(TypedConstantValue(
                        create_type_type(),
                        wrap_type_constant(
                            wrap_pointer_type(Pointer(
                                heapify(type)
                            ))
                        )
                    ));
                } else {
                    error(scope, unary_operation->range, "Cannot take pointers at constant time");

                    return err();
                }
            } break;

            case UnaryOperation::Operator::BooleanInvert: {
                if(expression_value.type.kind == TypeKind::Boolean) {
                    auto boolean_value = unwrap_boolean_constant(expression_value.value);

                    return ok(TypedConstantValue(
                        create_boolean_type(),
                        wrap_boolean_constant(!boolean_value)
                    ));
                } else {
                    error(scope, unary_operation->expression->range, "Expected a boolean, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
                }
            } break;

            case UnaryOperation::Operator::Negation: {
                if(expression_value.type.kind == TypeKind::Integer || expression_value.type.kind == TypeKind::UndeterminedInteger) {
                    auto integer_value = unwrap_integer_constant(expression_value.value);

                    return ok(TypedConstantValue(
                        expression_value.type,
                        wrap_integer_constant((uint64_t)-(int64_t)integer_value)
                    ));
                } else if(expression_value.type.kind == TypeKind::FloatType || expression_value.type.kind == TypeKind::UndeterminedFloat) {
                    auto float_value = unwrap_float_constant(expression_value.value);

                    return ok(TypedConstantValue(
                        expression_value.type,
                        wrap_float_constant(-float_value)
                    ));
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

                    return err();
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

        return ok(TypedConstantValue(
            type,
            value
        ));
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->expression));

        auto call_parameter_count = function_call->parameters.length;

        auto parameters = allocate<TypedConstantValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, function_call->parameters[i]));

            parameters[i] = parameter_value;
        }

        if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto polymorphic_function_value = unwrap_polymorphic_function_constant(expression_value.value);

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
                            auto call_parameter = parameters[i];
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
                            return ok(TypedConstantValue(
                                wrap_function_type(resolve_polymorphic_function.type),
                                wrap_function_constant(resolve_polymorphic_function.value)
                            ));
                        } else {
                            return wait(i);
                        }  
                    }
                }
            }

            auto call_parameter_ranges = allocate<FileRange>(declaration_parameter_count);

            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                call_parameter_ranges[i] = function_call->parameters[i]->range;
            }

            AnyJob job;
            job.kind = JobKind::ResolvePolymorphicFunction;
            job.state = JobState::Working;
            job.resolve_polymorphic_function.declaration = polymorphic_function_value.declaration;
            job.resolve_polymorphic_function.parameters = parameters;
            job.resolve_polymorphic_function.scope = polymorphic_function_value.scope;
            job.resolve_polymorphic_function.call_scope = scope;
            job.resolve_polymorphic_function.call_parameter_ranges = call_parameter_ranges;

            auto job_index = jobs->append(job);

            return wait(job_index);
        } else if(expression_value.type.kind == TypeKind::FunctionTypeType) {
            auto function_type = expression_value.type.function;

            auto function_value = unwrap_function_constant(expression_value.value);

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

            return ok(TypedConstantValue(
                wrap_function_type(function_type),
                wrap_function_constant(function_value)
            ));
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

            return err();
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, ignore_statement, array_type->expression));

        if(!type.is_runtime_type()) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        if(array_type->index != nullptr) {
            expect_delayed(index_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, array_type->index));

            expect(length, coerce_constant_to_integer_type(
                scope,
                array_type->index->range,
                index_value.type,
                index_value.value,
                {
                    info.architecture_sizes.address_size,
                    false
                },
                false
            ));

            return ok(TypedConstantValue(
                create_type_type(),
                wrap_type_constant(
                    wrap_static_array_type(StaticArray(
                        length,
                        heapify(type)
                    ))
                )
            ));
        } else {
            return ok(TypedConstantValue(
                create_type_type(),
                wrap_type_constant(
                    wrap_array_type(ArrayTypeType(
                        heapify(type)
                    ))
                )
            ));
        }
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.length;

        auto parameters = allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err();
            }

            expect_delayed(type, evaluate_type_expression(info, jobs, scope, ignore_statement, parameter.type));

            if(!type.is_runtime_type()) {
                error(scope, parameter.type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

                return err();
            }

            parameters[i] = type;
        }

        auto is_calling_convention_specified = false;
        auto calling_convention = CallingConvention::Default;
        for(auto tag : function_type->tags) {
            if(tag.name.text == "extern"_S) {
                error(scope, tag.range, "Function types cannot be external");

                return err();
            } else if(tag.name.text == "no_mangle"_S) {
                error(scope, tag.range, "Function types cannot be no_mangle");

                return err();
            } else if(tag.name.text == "call_conv"_S) {
                if(is_calling_convention_specified) {
                    error(scope, tag.range, "Duplicate 'call_conv' tag");

                    return err();
                }

                if(tag.parameters.length != 1) {
                    error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                    return err();
                }

                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[0]));

                expect(calling_convention_name, array_to_string(scope, tag.parameters[0]->range, parameter.type, parameter.value));

                if(calling_convention_name == "default"_S) {
                    calling_convention = CallingConvention::Default;
                } else if(calling_convention_name == "stdcall"_S) {
                    calling_convention = CallingConvention::StdCall;
                }

                is_calling_convention_specified = true;
            } else {
                error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

                return err();
            }
        }

        AnyType return_type;
        if(function_type->return_type == nullptr) {
            return_type = create_void_type();
        } else {
            expect_delayed(return_type_value, evaluate_type_expression(info, jobs, scope, ignore_statement, function_type->return_type));

            if(!return_type_value.is_runtime_type()) {
                error(scope, function_type->return_type->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(return_type_value.get_description()));

                return err();
            }

            return_type = return_type_value;
        }

        return ok(TypedConstantValue(
            create_type_type(),
            wrap_type_constant(
                wrap_function_type(FunctionTypeType(
                    Array(parameter_count, parameters),
                    heapify(return_type),
                    calling_convention
                ))
            )
        ));
    } else {
        abort();
    }
}

DelayedResult<AnyType> evaluate_type_expression(
    GlobalInfo info,
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Statement* ignore_statement,
    Expression* expression
) {
    expect_delayed(expression_value, evaluate_constant_expression(info, jobs, scope, ignore_statement, expression));

    if(expression_value.type.kind == TypeKind::Type) {
        return ok(unwrap_type_constant(expression_value.value));
    } else {
        error(scope, expression->range, "Expected a type, got %.*s", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description()));

        return err();
    }
}

DelayedResult<StaticIfResolutionResult> do_resolve_static_if(GlobalInfo info, List<AnyJob>* jobs, StaticIf* static_if, ConstantScope* scope) {
    expect_delayed(condition, evaluate_constant_expression(info, jobs, scope, static_if, static_if->condition));

    if(condition.type.kind != TypeKind::Boolean) {
        error(scope, static_if->condition->range, "Expected a boolean, got '%.*s'", STRING_PRINTF_ARGUMENTS(condition.type.get_description()));

        return err();
    }

    auto condition_value = unwrap_boolean_constant(condition.value);

    if(condition_value) {
        expect_void(process_scope(jobs, scope, static_if->statements, nullptr, true));

        auto declarations = create_declaration_hash_table(static_if->statements);

        StaticIfResolutionResult result {};
        result.condition = true;
        result.declarations = declarations;

        return ok(result);
    }

    StaticIfResolutionResult result {};
    result.condition = false;

    return ok(result);
}

profiled_function(DelayedResult<TypedConstantValue>, do_resolve_function_declaration, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    declaration,
    scope
)) {
    auto parameter_count = declaration->parameters.length;

    auto parameter_types = allocate<AnyType>(parameter_count);
    for(size_t i = 0; i < parameter_count; i += 1) {
        assert(!declaration->parameters[i].is_constant);
        assert(!declaration->parameters[i].is_polymorphic_determiner);

        expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, declaration->parameters[i].type));

        if(!type.is_runtime_type()) {
            error(scope, declaration->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description()));

            return err();
        }

        parameter_types[i] = type;
    }

    auto is_external = false;
    Array<String> external_libraries;
    auto is_no_mangle = false;
    auto is_calling_convention_specified = false;
    auto calling_convention = CallingConvention::Default;
    for(auto tag : declaration->tags) {
        if(tag.name.text == "extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            List<String> libraries {};

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[i]));

                if(parameter.type.kind == TypeKind::ArrayTypeType) {
                    auto array = parameter.type.array;

                    if(
                        array.element_type->kind == TypeKind::ArrayTypeType ||
                        array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::ArrayConstant) {
                            error(scope, tag.parameters[i]->range, "Cannot use an array with non-constant elements in a constant context");
                        } else {
                            auto static_array_value = unwrap_static_array_constant(parameter.value);

                            for(auto element : static_array_value.elements) {
                                expect(library_path, array_to_string(scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        }
                    } else {
                        expect(library_path, array_to_string(scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else if(parameter.type.kind == TypeKind::StaticArray) {
                    auto static_array = parameter.type.static_array;

                    if(
                        static_array.element_type->kind == TypeKind::ArrayTypeType ||
                        static_array.element_type->kind == TypeKind::StaticArray
                    ) {
                        auto static_array_value = unwrap_static_array_constant(parameter.value);

                        assert(static_array.length == static_array_value.elements.length);

                        for(auto element : static_array_value.elements) {
                            expect(library_path, array_to_string(scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(scope, tag.parameters[i]->range, parameter.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.type.get_description()));
                }
            }

            is_external = true;
            external_libraries = libraries;
        } else if(tag.name.text == "no_mangle"_S) {
            if(is_no_mangle) {
                error(scope, tag.range, "Duplicate 'no_mangle' tag");

                return err();
            }

            is_no_mangle = true;
        } else if(tag.name.text == "call_conv"_S) {
            if(is_calling_convention_specified) {
                error(scope, tag.range, "Duplicate 'call_conv' tag");

                return err();
            }

            if(tag.parameters.length != 1) {
                error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                return err();
            }

            expect_delayed(parameter, evaluate_constant_expression(info, jobs, scope, nullptr, tag.parameters[0]));

            expect(calling_convention_name, array_to_string(scope, tag.parameters[0]->range, parameter.type, parameter.value));

            if(calling_convention_name == "default"_S) {
                calling_convention = CallingConvention::Default;
            } else if(calling_convention_name == "stdcall"_S) {
                calling_convention = CallingConvention::StdCall;
            }

            is_calling_convention_specified = true;
        } else {
            error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

            return err();
        }
    }

    AnyType return_type;
    if(declaration->return_type) {
        expect_delayed(return_type_value, evaluate_type_expression(info, jobs, scope, nullptr, declaration->return_type));

        if(!return_type_value.is_runtime_type()) {
            error(scope, declaration->return_type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(return_type_value.get_description()));

            return err();
        }

        return_type = return_type_value;
    } else {
        return_type = create_void_type();
    }

    if(is_external && is_no_mangle) {
        error(scope, declaration->range, "External functions cannot be no_mangle");

        return err();
    }

    if(!is_external && !declaration->has_body) {
        if(is_no_mangle) {
            error(scope, declaration->range, "Function types cannot be no_mangle");

            return err();
        }

        return ok(TypedConstantValue(
            create_type_type(),
            wrap_type_constant(
                wrap_function_type(FunctionTypeType(
                    Array(parameter_count, parameter_types),
                    heapify(return_type),
                    calling_convention
                ))
            )
        ));
    } else {
        auto body_scope = new ConstantScope;
        body_scope->scope_constants = {};
        body_scope->is_top_level = false;
        body_scope->parent = scope;

        List<ConstantScope*> child_scopes {};
        if(is_external) {
            if(declaration->has_body) {
                error(scope, declaration->range, "External functions cannot have a body");

                return err();
            }

            body_scope->statements = {};
            body_scope->declarations = {};
        } else {
            body_scope->statements = declaration->statements;
            body_scope->declarations = create_declaration_hash_table(declaration->statements);

            expect_void(process_scope(jobs, body_scope, body_scope->statements, &child_scopes, false));
        }

        FunctionConstant function_constant;
        if(is_external) {
            function_constant.declaration = declaration;
            function_constant.body_scope = body_scope;
            function_constant.is_external = true;
            function_constant.external_libraries = external_libraries;
            function_constant.child_scopes = child_scopes;
        } else {
            function_constant.declaration = declaration;
            function_constant.body_scope = body_scope;
            function_constant.is_external = false;
            function_constant.child_scopes = child_scopes;
            function_constant.is_no_mangle = is_no_mangle;
        }

        return ok(TypedConstantValue(
            wrap_function_type(FunctionTypeType(
                Array(parameter_count, parameter_types),
                heapify(return_type),
                calling_convention
            )),
            wrap_function_constant(function_constant)
        ));
    }
}

profiled_function(DelayedResult<FunctionResolutionResult>, do_resolve_polymorphic_function, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionDeclaration* declaration,
    TypedConstantValue* parameters,
    ConstantScope* scope,
    ConstantScope* call_scope,
    FileRange* call_parameter_ranges
), (
    info,
    jobs,
    declaration,
    parameters,
    scope,
    call_scope,
    call_parameter_ranges
)) {
    auto original_parameter_count = declaration->parameters.length;

    auto parameter_types = allocate<AnyType>(original_parameter_count);

    List<ScopeConstant> polymorphic_determiners {};

    size_t polymorphic_determiner_index = 0;
    size_t runtime_parameter_count = 0;
    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];

        if(!declaration_parameter.is_constant) {
            runtime_parameter_count += 1;
        }

        if(declaration_parameter.is_polymorphic_determiner) {
            AnyType type;
            if(declaration_parameter.is_constant) {
                type = parameters[i].type;
            } else {
                expect(determined_type, coerce_to_default_type(info, call_scope, call_parameter_ranges[i], parameters[i].type));

                type = determined_type;
            }

            parameter_types[i] = type;

            ScopeConstant constant {};
            constant.name = declaration->parameters[i].polymorphic_determiner.text;
            constant.type = create_type_type();
            constant.value = wrap_type_constant(type);

            polymorphic_determiners.append(constant);

            polymorphic_determiner_index += 1;
        }
    }

    ConstantScope signature_scope;
    signature_scope.statements = {};
    signature_scope.declarations = {};
    signature_scope.scope_constants = polymorphic_determiners;
    signature_scope.is_top_level = false;
    signature_scope.parent = scope;

    List<ScopeConstant> scope_constants {};

    for(auto polymorphic_determiner : polymorphic_determiners) {
        scope_constants.append(polymorphic_determiner);
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

            ScopeConstant constant {};
            constant.name = declaration_parameter.name.text;
            constant.type = parameter_types[i];
            constant.value = coerced_constant_value;

            scope_constants.append(constant);
        }
    }

    signature_scope.scope_constants = scope_constants;

    auto runtime_parameter_types = allocate<AnyType>(runtime_parameter_count);

    size_t runtime_parameter_index = 0;
    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];

        if(!declaration_parameter.is_constant) {
            if(!declaration_parameter.is_polymorphic_determiner) {
                expect_delayed(parameter_type, evaluate_type_expression(info, jobs, &signature_scope, nullptr, declaration_parameter.type));

                if(!parameter_type.is_runtime_type()) {
                    error(scope,
                        declaration_parameter.type->range,
                        "Non-constant function parameters cannot be of type '%.*s'",
                        STRING_PRINTF_ARGUMENTS(parameter_type.get_description())
                    );

                    error(call_scope, call_parameter_ranges[i], "Polymorphic function paremter here");

                    return err();
                }

                parameter_types[i] = parameter_type;
            }

            runtime_parameter_types[runtime_parameter_index] = parameter_types[i];

            runtime_parameter_index += 1;
        }
    }

    assert(runtime_parameter_index == runtime_parameter_count);

    AnyType return_type;
    if(declaration->return_type) {
        expect_delayed(return_type_value, evaluate_type_expression(info, jobs, &signature_scope, nullptr, declaration->return_type));

        if(!return_type_value.is_runtime_type()) {
            error(
                scope,
                declaration->return_type->range,
                "Function returns cannot be of type '%.*s'",
                STRING_PRINTF_ARGUMENTS(return_type_value.get_description())
            );

            return err();
        }

        return_type = return_type_value;
    } else {
        return_type = create_void_type();
    }

    for(auto tag : declaration->tags) {
        if(tag.name.text == "extern"_S) {
            error(scope, tag.range, "Polymorphic functions cannot be external");

            return err();
        } else if(tag.name.text == "no_mangle"_S) {
            error(scope, tag.range, "Polymorphic functions cannot be no_mangle");

            return err();
        } else if(tag.name.text == "call_conv"_S) {
            error(scope, tag.range, "Polymorphic functions cannot have their calling convention specified");

            return err();
        } else {
            error(scope, tag.name.range, "Unknown tag '%.*s'", STRING_PRINTF_ARGUMENTS(tag.name.text));

            return err();
        }
    }

    if(!declaration->has_body) {
        error(scope, declaration->range, "Polymorphic function missing a body");

        return err();
    }

    auto body_scope = new ConstantScope;
    body_scope->statements = declaration->statements;
    body_scope->declarations = create_declaration_hash_table(declaration->statements);
    body_scope->scope_constants = scope_constants;
    body_scope->is_top_level = false;
    body_scope->parent = scope;

    List<ConstantScope*> child_scopes {};
    expect_void(process_scope(jobs, body_scope, body_scope->statements, &child_scopes, false));

    FunctionConstant function_constant;
    function_constant.declaration = declaration;
    function_constant.body_scope = body_scope;
    function_constant.child_scopes = child_scopes;
    function_constant.is_external = false;
    function_constant.is_no_mangle = false;

    FunctionResolutionResult result {};
    result.type.parameters.length = runtime_parameter_count;
    result.type.parameters.elements = runtime_parameter_types;
    result.type.return_type = heapify(return_type);
    result.type.calling_convention = CallingConvention::Default;
    result.value = function_constant;

    return ok(result);
}

profiled_function(DelayedResult<AnyType>, do_resolve_struct_definition, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    StructDefinition* struct_definition,
    ConstantScope* scope
), (
    info,
    jobs,
    struct_definition,
    scope
)) {
    auto parameter_count = struct_definition->parameters.length;

    if(struct_definition->parameters.length > 0) {
        auto parameter_types = allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect_delayed(type, evaluate_type_expression(info, jobs, scope, nullptr, struct_definition->parameters[i].type));

            parameter_types[i] = type;
        }

        return ok(wrap_polymorphic_struct_type(PolymorphicStruct(
            struct_definition,
            parameter_types,
            scope
        )));
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.declarations = {};
    member_scope.scope_constants = {};
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.length;

    auto members = allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, evaluate_type_expression(
            info,
            jobs,
            &member_scope,
            nullptr,
            struct_definition->members[i].type
        ));

        expect(actual_member_type, coerce_to_default_type(info, &member_scope, struct_definition->members[i].type->range, member_type));

        if(!actual_member_type.is_runtime_type()) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_member_type.get_description()));

            return err();
        }

        members[i] = {
            struct_definition->members[i].name.text,
            actual_member_type
        };
    }

    return ok(wrap_struct_type(StructType(
        struct_definition,
        Array(member_count, members)
    )));
}

profiled_function(DelayedResult<AnyType>, do_resolve_polymorphic_struct, (
    GlobalInfo info,
    List<AnyJob>* jobs,
    StructDefinition* struct_definition,
    AnyConstantValue* parameters,
    ConstantScope* scope
), (
    info,
    jobs,
    struct_definition,
    parameters,
    scope
)) {
    auto parameter_count = struct_definition->parameters.length;
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
    member_scope.declarations = {};
    member_scope.scope_constants = Array(parameter_count, constant_parameters);
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.length;

    auto members = allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, evaluate_type_expression(
            info,
            jobs,
            &member_scope,
            nullptr,
            struct_definition->members[i].type
        ));

        expect(actual_member_type, coerce_to_default_type(info, &member_scope, struct_definition->members[i].type->range, member_type));

        if(!actual_member_type.is_runtime_type()) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_member_type.get_description()));

            return err();
        }

        members[i] = {
            struct_definition->members[i].name.text,
            actual_member_type
        };
    }

    return ok(wrap_struct_type(StructType(
        struct_definition,
        Array(member_count, members)
    )));
}

profiled_function(Result<void>, process_scope, (
    List<AnyJob>* jobs,
    ConstantScope* scope,
    Array<Statement*> statements,
    List<ConstantScope*>* child_scopes,
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
                    AnyJob job;
                    job.kind = JobKind::ResolveFunctionDeclaration;
                    job.state = JobState::Working;
                    job.resolve_function_declaration.declaration = function_declaration;
                    job.resolve_function_declaration.scope = scope;

                    jobs->append(job);
                }
            } break;

            case StatementKind::ConstantDefinition: {
                auto constant_definition = (ConstantDefinition*)statement;

                AnyJob job;
                job.kind = JobKind::ResolveConstantDefinition;
                job.state = JobState::Working;
                job.resolve_constant_definition.definition = constant_definition;
                job.resolve_constant_definition.scope = scope;

                jobs->append(job);
            } break;

            case StatementKind::StructDefinition: {
                auto struct_definition = (StructDefinition*)statement;

                AnyJob job;
                job.kind = JobKind::ResolveStructDefinition;
                job.state = JobState::Working;
                job.resolve_struct_definition.definition = struct_definition;
                job.resolve_struct_definition.scope = scope;

                jobs->append(job);
            } break;

            case StatementKind::VariableDeclaration: {
                if(is_top_level) {
                    auto variable_declaration = (VariableDeclaration*)statement;

                    AnyJob job;
                    job.kind = JobKind::GenerateStaticVariable;
                    job.state = JobState::Working;
                    job.generate_static_variable.declaration = variable_declaration;
                    job.generate_static_variable.scope = scope;

                    jobs->append(job);
                }
            } break;

            case StatementKind::IfStatement: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto if_statement = (IfStatement*)statement;

                auto if_scope = new ConstantScope;
                if_scope->statements = if_statement->statements;
                if_scope->declarations = create_declaration_hash_table(if_statement->statements);
                if_scope->scope_constants = {};
                if_scope->is_top_level = false;
                if_scope->parent = scope;

                child_scopes->append(if_scope);

                expect_void(process_scope(jobs, if_scope, if_statement->statements, child_scopes, false));

                for(auto else_if : if_statement->else_ifs) {
                    auto else_if_scope = new ConstantScope;
                    else_if_scope->statements = else_if.statements;
                    else_if_scope->declarations = create_declaration_hash_table(else_if.statements);
                    else_if_scope->scope_constants = {};
                    else_if_scope->is_top_level = false;
                    else_if_scope->parent = scope;

                    child_scopes->append(else_if_scope);

                    expect_void(process_scope(jobs, else_if_scope, else_if.statements, child_scopes, false));
                }

                if(if_statement->else_statements.length != 0) {
                    auto else_scope = new ConstantScope;
                    else_scope->statements = if_statement->else_statements;
                    else_scope->declarations = create_declaration_hash_table(if_statement->else_statements);
                    else_scope->scope_constants = {};
                    else_scope->is_top_level = false;
                    else_scope->parent = scope;

                    child_scopes->append(else_scope);

                    expect_void(process_scope(jobs, else_scope, if_statement->else_statements, child_scopes, false));
                }
            } break;

            case StatementKind::WhileLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto while_loop = (WhileLoop*)statement;

                auto while_scope = new ConstantScope;
                while_scope->statements = while_loop->statements;
                while_scope->declarations = create_declaration_hash_table(while_loop->statements);
                while_scope->scope_constants = {};
                while_scope->is_top_level = false;
                while_scope->parent = scope;

                child_scopes->append(while_scope);

                expect_void(process_scope(jobs, while_scope, while_loop->statements, child_scopes, false));
            } break;

            case StatementKind::ForLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto for_loop = (ForLoop*)statement;

                auto for_scope = new ConstantScope;
                for_scope->statements = for_loop->statements;
                for_scope->declarations = create_declaration_hash_table(for_loop->statements);
                for_scope->scope_constants = {};
                for_scope->is_top_level = false;
                for_scope->parent = scope;

                child_scopes->append(for_scope);

                expect_void(process_scope(jobs, for_scope, for_loop->statements, child_scopes, false));
            } break;

            case StatementKind::Import: {
                auto import = (Import*)statement;

                auto job_already_added = false;
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job.kind == JobKind::ParseFile) {
                        auto parse_file = job.parse_file;

                        if(parse_file.path == import->absolute_path) {
                            job_already_added = true;
                            break;
                        }
                    }
                }

                if(!job_already_added) {
                    AnyJob job;
                    job.kind = JobKind::ParseFile;
                    job.state = JobState::Working;
                    job.parse_file.path = import->absolute_path;

                    jobs->append(job);
                }
            } break;

            case StatementKind::UsingStatement: break;

            case StatementKind::StaticIf: {
                auto static_if = (StaticIf*)statement;

                AnyJob job;
                job.kind = JobKind::ResolveStaticIf;
                job.state = JobState::Working;
                job.resolve_static_if.static_if = static_if;
                job.resolve_static_if.scope = scope;

                jobs->append(job);
            } break;

            default: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }
            } break;
        }
    }

    return ok();
}