#include "typed_tree_generator.h"
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "profiler.h"
#include "list.h"
#include "util.h"
#include "string.h"
#include "types.h"
#include "jobs.h"

void error(ConstantScope* scope, FileRange range, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(scope->get_file_path(), range, format, arguments);

    va_end(arguments);
}

static bool constant_values_equal(AnyConstantValue a, AnyConstantValue b) {
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

        case ConstantValueKind::ArrayConstant: {
            return a.array.length == b.array.length && a.array.pointer == b.array.pointer;
        } break;

        case ConstantValueKind::AggregateConstant: {
            if(a.aggregate.values.length != b.aggregate.values.length) {
                return false;
            }

            for(size_t i = 0; i < a.aggregate.values.length; i += 1) {
                if(!constant_values_equal(a.aggregate.values[i], b.aggregate.values[i])) {
                    return false;
                }
            }

            return true;
        } break;

        case ConstantValueKind::FileModuleConstant: {
            return a.file_module.scope == b.file_module.scope;
        } break;

        case ConstantValueKind::UndefConstant: {
            return false; // Unsure if this is the right thing to do here? Reminiscent of NaN != NaN
        } break;

        default: abort();
    }
}

struct InProgressVariableScope {
    List<TypedVariable*> variables;
};

struct TypingContext {
    Arena* arena;
    Arena* global_arena;

    Array<AnyType> return_types;

    Array<ConstantScope*> child_scopes;
    size_t next_child_scope_index;

    bool in_breakable_scope;

    VariableScope* variable_scope;

    List<InProgressVariableScope> in_progress_variable_scope_stack;

    List<ConstantScope*> scope_search_stack;

    Statement* search_ignore_statement;
};

static Result<TypedVariable*> add_new_variable(TypingContext* context, Identifier name, AnyType type) {
    assert(context->variable_scope != nullptr);
    assert(context->in_progress_variable_scope_stack.length != 0);

    auto in_progress_variable_scope = &(context->in_progress_variable_scope_stack[context->in_progress_variable_scope_stack.length - 1]);

    for(auto variable : in_progress_variable_scope->variables) {
        if(variable->name.text == name.text) {
            error(context->variable_scope->constant_scope, name.range, "Duplicate variable name %.*s", STRING_PRINTF_ARGUMENTS(name.text));
            error(context->variable_scope->constant_scope, variable->name.range, "Original declared here");

            return err();
        }
    }

    auto variable = context->arena->allocate_and_construct<TypedVariable>();
    variable->name = name;
    variable->type = type;

    in_progress_variable_scope->variables.append(variable);

    return ok(variable);
}

struct TypedRuntimeValue {
    inline TypedRuntimeValue() = default;
    explicit inline TypedRuntimeValue(AnyType type, AnyValue value) : type(type), value(value) {}

    AnyType type;

    AnyValue value;
};

static Result<void> check_undetermined_integer_to_integer_coercion(
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    Integer target_type,
    int64_t value,
    bool probing
) {
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
            error(
                scope,
                range,
                "Constant '%" PRIi64 "' cannot fit in '%.*s'. You must cast explicitly",
                value,
                STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
            );
        }

        return err();
    }

    return ok();
}

static Result<void> coerce_to_integer(
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyValue value,
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

        expect_void(check_undetermined_integer_to_integer_coercion(scope, context, range, target_type, (int64_t)integer_value, probing));

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
        error(
            scope,
            range,
            "Cannot implicitly convert '%.*s' to '%.*s'",
            STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
            STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
        );
    }

    return err();
}


static Result<AnyValue> coerce_to_float(
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyValue value,
    FloatType target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (int64_t)value.unwrap_constant_value().unwrap_integer();

        double float_value;
        if(target_type.size == RegisterSize::Size32) {
            float_value = (double)(float)integer_value;
        } else if(target_type.size == RegisterSize::Size64) {
            float_value = (double)integer_value;
        } else {
            abort();
        }

        if((int64_t)float_value != integer_value) {
            if(!probing) {
                error(
                    scope,
                    range,
                    "Constant '" PRIi64 "' cannot be represented by '%.*s'. You must cast explicitly",
                    integer_value,
                    STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
                );
            }

            return err();
        }

        return ok(AnyValue(AnyConstantValue(float_value)));
    } else if(type.kind == TypeKind::FloatType) {
        auto float_type = type.float_;

        if(target_type.size == float_type.size) {
            return ok(value);
        }
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        auto float_value = value.unwrap_constant_value().unwrap_float();

        if(target_type.size == RegisterSize::Size32 && (double)(float)float_value != float_value) {
            if(!probing) {
                error(
                    scope,
                    range,
                    "Constant '%f' cannot be represented by '%.*s'. You must cast explicitly",
                    float_value,
                    STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
                );
            }

            return err();
        }

        return ok(value);
    } else if(type.kind == TypeKind::Undef) {
        return ok(value);
    }

    if(!probing) {
        error(
            scope,
            range,
            "Cannot implicitly convert '%.*s' to '%.*s'",
            STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
            STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
        );
    }

    return err();
}

static Result<void> coerce_to_pointer(
    GlobalInfo info,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyValue value,
    Pointer target_type,
    bool probing
) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        auto integer_value = (int64_t)value.unwrap_constant_value().unwrap_integer();

        if(integer_value != 0) {
            if(!probing) {
                error(
                    scope,
                    range,
                    "Cannot convert non-zero value '%" PRIi64 "' to '%.*s'. You must cast explicitly",
                    integer_value,
                    STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
                );
            }

            return err();
        }

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
        error(
            scope,
            range,
            "Cannot implicitly convert '%.*s' to '%.*s'",
            STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
            STRING_PRINTF_ARGUMENTS(AnyType(target_type).get_description(context->arena))
        );
    }

    return err();
}

static Result<AnyValue> coerce_to_type(
    GlobalInfo info,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    AnyType type,
    AnyValue value,
    AnyType target_type,
    bool probing
) {
    if(target_type.kind == TypeKind::Integer) {
        auto integer = target_type.integer;

        expect_void(coerce_to_integer(
            scope,
            context,
            range,
            type,
            value,
            integer,
            probing
        ));

        return ok(value);
    } else if(target_type.kind == TypeKind::Boolean) {
        if(type.kind == TypeKind::Boolean) {
            return ok(value);
        } else if(type.kind == TypeKind::Undef) {
            return ok(value);
        }
    } else if(target_type.kind == TypeKind::FloatType) {
        auto float_type = target_type.float_;

        expect_void(coerce_to_float(
            scope,
            context,
            range,
            type,
            value,
            float_type,
            probing
        ));

        return ok(value);
    } else if(target_type.kind == TypeKind::Pointer) {
        auto pointer = target_type.pointer;

        expect_void(coerce_to_pointer(
            info,
            scope,
            context,
            range,
            type,
            value,
            pointer,
            probing
        ));

        return ok(value);
    } else if(target_type.kind == TypeKind::ArrayTypeType) {
        auto target_array = target_type.array;

        if(type.kind == TypeKind::ArrayTypeType) {
            auto array_type = type.array;

            if(*target_array.element_type == *array_type.element_type) {
                return ok(value);
            }
        } else if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_array.element_type == *static_array.element_type) {
                if(value.kind == ValueKind::AssignableValue) {
                    return ok(AnyValue::create_anonymous_value());
                } else if(value.kind == ValueKind::ConstantValue) {
                    return ok(value);
                }
            }
        } else if(type.kind == TypeKind::UndeterminedArray) {
            auto undetermined_array = type.undetermined_array;

            if(value.kind == ValueKind::ConstantValue) {
                auto aggregate_value = value.constant.unwrap_aggregate();

                auto elements = context->arena->allocate<AnyConstantValue>(undetermined_array.elements.length);

                auto all_valid = true;
                for(size_t i = 0; i < undetermined_array.elements.length; i += 1) {
                    auto result = coerce_to_type(
                        info,
                        scope,
                        context,
                        range,
                        undetermined_array.elements.elements[i], 
                        AnyValue(aggregate_value.values[i]),
                        *target_array.element_type,
                        true
                    );

                    assert(result.value.kind == ValueKind::ConstantValue);

                    if(!result.status) {
                        all_valid = false;
                        break;
                    }

                    elements[i] = result.value.constant;
                }

                if(all_valid) {
                    return ok(AnyValue(AnyConstantValue(AggregateConstant(Array(undetermined_array.elements.length, elements)))));
                }
            } else if(value.kind == ValueKind::UndeterminedAggregateValue) {
                auto aggregate_value = value.unwrap_undetermined_aggregate_value();

                auto all_valid = true;
                for(size_t i = 0; i < undetermined_array.elements.length; i += 1) {
                    auto result = coerce_to_type(
                        info,
                        scope,
                        context,
                        range,
                        undetermined_array.elements.elements[i], 
                        aggregate_value.values[i],
                        *target_array.element_type,
                        true
                    );

                    if(!result.status) {
                        all_valid = false;
                        break;
                    }
                }

                if(all_valid) {
                    return ok(AnyValue::create_anonymous_value());
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(
                undetermined_struct.members.length == 2 &&
                undetermined_struct.members[0].name == u8"length"_S &&
                undetermined_struct.members[1].name == u8"pointer"_S
            ) {
                if(value.kind == ValueKind::ConstantValue) {
                    auto constant_value = value.constant;

                    auto aggregate_value = constant_value.unwrap_aggregate();

                    auto length_result = coerce_to_integer(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        AnyValue(aggregate_value.values[0]),
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            AnyValue(aggregate_value.values[1]),
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            return ok(AnyValue(AnyConstantValue(ArrayConstant())));
                        }
                    }
                } else if(value.kind == ValueKind::UndeterminedAggregateValue) {
                    auto aggregate_value = value.undetermined_aggregate;

                    auto length_result = coerce_to_integer(
                        scope,
                        context,
                        range,
                        undetermined_struct.members[0].type,
                        aggregate_value.values[0],
                        Integer(
                            info.architecture_sizes.address_size,
                            false
                        ),
                        true
                    );

                    if(length_result.status) {
                        auto pointer_result = coerce_to_pointer(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_struct.members[1].type,
                            aggregate_value.values[1],
                            Pointer(target_array.element_type),
                            true
                        );

                        if(pointer_result.status) {
                            return ok(AnyValue::create_anonymous_value());
                        }
                    }
                } else {
                    abort();
                }
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok(value);
        }
    } else if(target_type.kind == TypeKind::StaticArray) {
        auto target_static_array = target_type.static_array;

        if(type.kind == TypeKind::StaticArray) {
            auto static_array = type.static_array;

            if(*target_static_array.element_type == *static_array.element_type && target_static_array.length == static_array.length) {
                return ok(value);
            }
        } else if(type.kind == TypeKind::UndeterminedArray) {
            auto undetermined_array = type.undetermined_array;

            if(undetermined_array.elements.length == target_static_array.length) {
                if(value.kind == ValueKind::ConstantValue) {
                    auto aggregate_value = value.constant.unwrap_aggregate();

                    auto elements = context->arena->allocate<AnyConstantValue>(undetermined_array.elements.length);

                    auto all_valid = true;
                    for(size_t i = 0; i < undetermined_array.elements.length; i += 1) {
                        auto result = coerce_to_type(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_array.elements.elements[i], 
                            AnyValue(aggregate_value.values[i]),
                            *target_static_array.element_type,
                            true
                        );

                        assert(result.value.kind == ValueKind::ConstantValue);

                        if(!result.status) {
                            all_valid = false;
                            break;
                        }

                        elements[i] = result.value.constant;
                    }

                    if(all_valid) {
                        return ok(AnyValue(AnyConstantValue(AggregateConstant(Array(undetermined_array.elements.length, elements)))));
                    }
                } else if(value.kind == ValueKind::UndeterminedAggregateValue) {
                    auto aggregate_value = value.unwrap_undetermined_aggregate_value();

                    auto all_valid = true;
                    for(size_t i = 0; i < undetermined_array.elements.length; i += 1) {
                        auto result = coerce_to_type(
                            info,
                            scope,
                            context,
                            range,
                            undetermined_array.elements.elements[i], 
                            aggregate_value.values[i],
                            *target_static_array.element_type,
                            true
                        );

                        if(!result.status) {
                            all_valid = false;
                            break;
                        }
                    }

                    if(all_valid) {
                        return ok(AnyValue::create_anonymous_value());
                    }
                }
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok(value);
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
                    return ok(value);
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == ValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto aggregate_value = constant_value.unwrap_aggregate();

                if(target_struct_type.members.length == undetermined_struct.members.length) {
                    auto same_members = true;
                    for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                        if(target_struct_type.members[i].name != undetermined_struct.members[i].name) {
                            same_members = false;

                            break;
                        }
                    }

                    if(same_members) {
                        auto elements = context->arena->allocate<AnyConstantValue>(undetermined_struct.members.length);

                        auto success = true;
                        for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                            auto result = coerce_to_type(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                AnyValue(aggregate_value.values[i]),
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok(AnyValue(AnyConstantValue(AggregateConstant(Array(undetermined_struct.members.length, elements)))));
                        }
                    }
                }
            } else if(value.kind == ValueKind::UndeterminedAggregateValue) {
                auto aggregate_value = value.undetermined_aggregate;

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
                            auto result = coerce_to_type(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[i].type,
                                aggregate_value.values[i],
                                target_struct_type.members[i].type,
                                true
                            );

                            if(!result.status) {
                                success = false;

                                break;
                            }
                        }

                        if(success) {
                            return ok(AnyValue::create_anonymous_value());
                        }
                    }
                }
            } else {
                abort();
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok(value);
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
                    return ok(value);
                }
            }
        } else if(type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = type.undetermined_struct;

            if(value.kind == ValueKind::ConstantValue) {
                auto constant_value = value.constant;

                auto aggregate_value = constant_value.unwrap_aggregate();

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto result = coerce_to_type(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                AnyValue(aggregate_value.values[0]),
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                return ok(AnyValue::create_anonymous_value());
                            } else {
                                break;
                            }
                        }
                    }
                }
            } else if(value.kind == ValueKind::UndeterminedAggregateValue) {
                auto aggregate_value = value.undetermined_aggregate;

                if(undetermined_struct.members.length == 1) {
                    for(size_t i = 0; i < target_union_type.members.length; i += 1) {
                        if(target_union_type.members[i].name == undetermined_struct.members[0].name) {
                            auto result = coerce_to_type(
                                info,
                                scope,
                                context,
                                range,
                                undetermined_struct.members[0].type,
                                aggregate_value.values[0],
                                target_union_type.members[i].type,
                                true
                            );

                            if(result.status) {
                                return ok(AnyValue::create_anonymous_value());
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
            return ok(AnyValue::create_anonymous_value());
        }
    } else if(target_type.kind == TypeKind::Enum) {
        auto target_enum = target_type.enum_;

        if(type.kind == TypeKind::Integer) {
            auto integer = type.integer;

            if(integer.size == target_enum.backing_type->size && integer.is_signed == target_enum.backing_type->is_signed) {
                return ok(value);
            }
        } else if(type.kind == TypeKind::UndeterminedInteger) {
            auto integer_value = value.unwrap_constant_value().unwrap_integer();

            expect_void(check_undetermined_integer_to_integer_coercion(
                scope,
                context,
                range,
                *target_enum.backing_type,
                integer_value,
                probing
            ));

            return ok(value);
        } else if(type.kind == TypeKind::Enum) {
            auto enum_ = type.enum_;

            if(target_enum.definition == enum_.definition) {
                return ok(value);
            }
        } else if(type.kind == TypeKind::Undef) {
            return ok(value);
        }
    } else {
        abort();
    }

    if(!probing) {
        if(value.kind == ValueKind::ConstantValue) {
            error(
                scope,
                range,
                "Cannot implicitly convert constant '%.*s' (%.*s) to '%.*s'",
                STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
                STRING_PRINTF_ARGUMENTS(value.constant.get_description(context->arena)),
                STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena))
            );
        } else if(value.kind == ValueKind::AnonymousValue) {
            error(
                scope,
                range,
                "Cannot implicitly convert anonymous '%.*s' to '%.*s'",
                STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
                STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena))
            );
        } else {
            error(
                scope,
                range,
                "Cannot implicitly convert '%.*s' to '%.*s'",
                STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)),
                STRING_PRINTF_ARGUMENTS(target_type.get_description(context->arena))
            );
        }
    }

    return err();
}

static DelayedResult<TypedExpression> type_expression(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
);

struct ExpectTypeExpressionResult {
    TypedExpression typed_expression;

    AnyType type;
};

static DelayedResult<ExpectTypeExpressionResult> expect_type_expression(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
) {
    expect_delayed(expression_value, type_expression(info, jobs, scope, context, expression));

    if(expression_value.type.kind == TypeKind::Type) {
        auto constant_value = expression_value.value.unwrap_constant_value();

        ExpectTypeExpressionResult result {};
        result.typed_expression = expression_value;
        result.type = constant_value.unwrap_type();

        return ok(result);
    } else {
        error(scope, expression->range, "Expected a type, got %.*s", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

        return err();
    }
}

struct ExpectConstantExpressionResult {
    TypedExpression typed_expression;

    AnyConstantValue value;
};

static DelayedResult<ExpectConstantExpressionResult> expect_constant_expression(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    Expression* expression
) {
    expect_delayed(expression_value, type_expression(info, jobs, scope, context, expression));

    if(expression_value.value.kind == ValueKind::ConstantValue) {
        auto constant_value = expression_value.value.unwrap_constant_value();

        ExpectConstantExpressionResult result {};
        result.typed_expression = expression_value;
        result.value = constant_value;

        return ok(result);
    } else {
        error(scope, expression->range, "Expected a constant value");

        return err();
    }
}

static AnyType get_default_type(GlobalInfo info, ConstantScope* scope, FileRange range, AnyType type) {
    if(type.kind == TypeKind::UndeterminedInteger) {
        return AnyType(Integer(
            info.architecture_sizes.default_integer_size,
            true
        ));
    } else if(type.kind == TypeKind::UndeterminedFloat) {
        return AnyType(FloatType(
            info.architecture_sizes.default_float_size
        ));
    } else {
        return type;
    }
}

static DelayedResult<TypedExpression> type_binary_operation(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    FileRange range,
    Expression* left_expression,
    Expression* right_expression,
    BinaryOperation::Operator binary_operator
) {
    expect_delayed(left, type_expression(info, jobs, scope, context, left_expression));

    expect_delayed(right, type_expression(info, jobs, scope, context, right_expression));

    AnyType type;
    if(left.type.kind == TypeKind::Boolean && right.type.kind == TypeKind::Boolean) {
        type = left.type;
    } else if(left.type.kind == TypeKind::Pointer) {
        type = left.type;
    } else if(right.type.kind == TypeKind::Pointer) {
        type = right.type;
    } else if(left.type.kind == TypeKind::Integer && right.type.kind == TypeKind::Integer) {
        auto left_integer = left.type.integer;
        auto right_integer = right.type.integer;

        RegisterSize largest_size;
        if(left_integer.size > right_integer.size) {
            largest_size = left_integer.size;
        } else {
            largest_size = right_integer.size;
        }

        auto is_either_signed = left_integer.is_signed || right_integer.is_signed;

        type = AnyType(Integer(largest_size, is_either_signed));
    } else if(left.type.kind == TypeKind::FloatType && right.type.kind == TypeKind::FloatType) {
        auto left_float = left.type.float_;
        auto right_float = right.type.float_;

        RegisterSize largest_size;
        if(left_float.size > right_float.size) {
            largest_size = left_float.size;
        } else {
            largest_size = right_float.size;
        }

        type = AnyType(FloatType(largest_size));
    } else if(left.type.kind == TypeKind::FloatType) {
        type = left.type;
    } else if(right.type.kind == TypeKind::FloatType) {
        type = right.type;
    } else if(left.type.kind == TypeKind::UndeterminedFloat && right.type.kind == TypeKind::UndeterminedFloat) {
        type = left.type;
    } else if(left.type.kind == TypeKind::Integer) {
        type = left.type;
    } else if(right.type.kind == TypeKind::Integer) {
        type = right.type;
    } else if(left.type.kind == TypeKind::UndeterminedInteger && right.type.kind == TypeKind::UndeterminedInteger) {
        type = left.type;
    } else if(left.type.kind == TypeKind::Enum) {
        type = left.type;
    } else if(right.type.kind == TypeKind::Enum) {
        type = right.type;
    } else {
        error(
            scope,
            range,
            "Cannot perform that operation on '%.*s' and '%.*s'",
            STRING_PRINTF_ARGUMENTS(left.type.get_description(context->arena)),
            STRING_PRINTF_ARGUMENTS(right.type.get_description(context->arena))
        );

        return err();
    }

    if(left.value.kind == ValueKind::ConstantValue && left.value.constant.kind == ConstantValueKind::UndefConstant) {
        error(scope, left_expression->range, "Value is undefined");

        return err();
    }

    if(right.value.kind == ValueKind::ConstantValue && right.value.constant.kind == ConstantValueKind::UndefConstant) {
        error(scope, right_expression->range, "Value is undefined");

        return err();
    }

    BinaryOperationKind kind;
    switch(binary_operator) {
        case BinaryOperation::Operator::Addition: kind = BinaryOperationKind::Addition; break;
        case BinaryOperation::Operator::Subtraction: kind = BinaryOperationKind::Subtraction; break;
        case BinaryOperation::Operator::Multiplication: kind = BinaryOperationKind::Multiplication; break;
        case BinaryOperation::Operator::Division: kind = BinaryOperationKind::Division; break;
        case BinaryOperation::Operator::Modulo: kind = BinaryOperationKind::Modulus; break;
        case BinaryOperation::Operator::Equal: kind = BinaryOperationKind::Equal; break;
        case BinaryOperation::Operator::NotEqual: kind = BinaryOperationKind::NotEqual; break;
        case BinaryOperation::Operator::LessThan: kind = BinaryOperationKind::LessThan; break;
        case BinaryOperation::Operator::GreaterThan: kind = BinaryOperationKind::GreaterThan; break;
        case BinaryOperation::Operator::BitwiseAnd: kind = BinaryOperationKind::BitwiseAnd; break;
        case BinaryOperation::Operator::BitwiseOr: kind = BinaryOperationKind::BitwiseOr; break;
        case BinaryOperation::Operator::LeftShift: kind = BinaryOperationKind::LeftShift; break;
        case BinaryOperation::Operator::RightShift: kind = BinaryOperationKind::RightShift; break;
        case BinaryOperation::Operator::BooleanAnd: kind = BinaryOperationKind::BooleanAnd; break;
        case BinaryOperation::Operator::BooleanOr: kind = BinaryOperationKind::BooleanOr; break;

        default: abort();
    }

    auto both_constant = left.value.kind == ValueKind::ConstantValue && right.value.kind == ValueKind::ConstantValue;

    AnyType result_type;
    AnyValue result_value;
    if(type.kind == TypeKind::UndeterminedInteger) {
        auto left_value = (int64_t)left.value.constant.unwrap_integer();

        auto right_value = (int64_t)right.value.constant.unwrap_integer();

        auto is_arithmetic = true;
        int64_t value;
        switch(kind) {
            case BinaryOperationKind::Addition: {
                value = left_value + right_value;
            } break;

            case BinaryOperationKind::Subtraction: {
                value = left_value - right_value;
            } break;

            case BinaryOperationKind::Multiplication: {
                value = left_value * right_value;
            } break;

            case BinaryOperationKind::Division: {
                value = left_value / right_value;
            } break;

            case BinaryOperationKind::Modulus: {
                value = left_value % right_value;
            } break;

            case BinaryOperationKind::BitwiseAnd: {
                value = left_value & right_value;
            } break;

            case BinaryOperationKind::BitwiseOr: {
                value = left_value | right_value;
            } break;

            case BinaryOperationKind::LeftShift: {
                value = left_value << right_value;
            } break;

            case BinaryOperationKind::RightShift: {
                value = left_value >> right_value;
            } break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            result_type = type;
            result_value = AnyValue(AnyConstantValue((uint64_t)value));
        } else {
            bool value;
            switch(kind) {
                case BinaryOperationKind::Equal: {
                    value = left_value == right_value;
                } break;

                case BinaryOperationKind::NotEqual: {
                    value = left_value != right_value;
                } break;

                case BinaryOperationKind::LessThan: {
                    value = left_value < right_value;
                } break;

                case BinaryOperationKind::GreaterThan: {
                    value = left_value > right_value;
                } break;

                default: {
                    error(scope, range, "Cannot perform that operation on integers");

                    return err();
                } break;
            }

            result_type = AnyType::create_boolean();
            result_value = AnyValue(AnyConstantValue(value));
        }
    } else if(type.kind == TypeKind::Integer) {
        auto integer = type.integer;

        expect_void(coerce_to_integer(
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            integer,
            false
        ));

        expect_void(coerce_to_integer(
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            integer,
            false
        ));

        auto is_arithmetic = true;
        switch(kind) {
            case BinaryOperationKind::Addition:
            case BinaryOperationKind::Subtraction:
            case BinaryOperationKind::Multiplication:
            case BinaryOperationKind::Division:
            case BinaryOperationKind::Modulus:
            case BinaryOperationKind::BitwiseAnd:
            case BinaryOperationKind::BitwiseOr:
            case BinaryOperationKind::LeftShift:
            case BinaryOperationKind::RightShift: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            result_type = type;

            if(both_constant) {
                auto left_value = left.value.constant.unwrap_integer();

                auto right_value = right.value.constant.unwrap_integer();

                uint64_t value;
                switch(kind) {
                    case BinaryOperationKind::Addition: {
                        value = left_value + right_value;
                    } break;

                    case BinaryOperationKind::Subtraction: {
                        value = left_value - right_value;
                    } break;

                    case BinaryOperationKind::Multiplication: {
                        if(integer.is_signed) {
                            value = (uint64_t)((int64_t)left_value * (int64_t)right_value);
                        } else {
                            value = left_value * right_value;
                        }
                    } break;

                    case BinaryOperationKind::Division: {
                        if(integer.is_signed) {
                            value = (uint64_t)((int64_t)left_value / (int64_t)right_value);
                        } else {
                            value = left_value / right_value;
                        }
                    } break;

                    case BinaryOperationKind::Modulus: {
                        if(integer.is_signed) {
                            value = (uint64_t)((int64_t)left_value % (int64_t)right_value);
                        } else {
                            value = left_value % right_value;
                        }
                    } break;

                    case BinaryOperationKind::BitwiseAnd: {
                        value = left_value & right_value;
                    } break;

                    case BinaryOperationKind::BitwiseOr: {
                        value = left_value | right_value;
                    } break;

                    case BinaryOperationKind::LeftShift: {
                        value = left_value << right_value;
                    } break;

                    case BinaryOperationKind::RightShift: {
                        if(integer.is_signed) {
                            value = (uint64_t)((int64_t)left_value >> (int64_t)right_value);
                        } else {
                            value = left_value >> right_value;
                        }
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        } else {
            switch(kind) {
                case BinaryOperationKind::Equal:
                case BinaryOperationKind::NotEqual:
                case BinaryOperationKind::LessThan:
                case BinaryOperationKind::GreaterThan: break;

                default: {
                    error(scope, range, "Cannot perform that operation on integers");

                    return err();
                } break;
            }

            result_type = AnyType::create_boolean();

            if(both_constant) {
                auto left_value = left.value.constant.unwrap_integer();

                auto right_value = right.value.constant.unwrap_integer();

                bool value;
                switch(kind) {
                    case BinaryOperationKind::Equal: {
                        value = left_value == right_value;
                    } break;

                    case BinaryOperationKind::NotEqual: {
                        value = left_value != right_value;
                    } break;

                    case BinaryOperationKind::LessThan: {
                        if(integer.is_signed) {
                            value = (int64_t)left_value < (int64_t)right_value;
                        } else {
                            value = left_value < right_value;
                        }
                    } break;

                    case BinaryOperationKind::GreaterThan: {
                        if(integer.is_signed) {
                            value = (int64_t)left_value > (int64_t)right_value;
                        } else {
                            value = left_value > right_value;
                        }
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        }
    } else if(type.kind == TypeKind::Boolean) {
        result_type = AnyType::create_boolean();

        auto is_arithmetic = true;
        switch(kind) {
            case BinaryOperationKind::BooleanAnd:
            case BinaryOperationKind::BooleanOr: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            if(both_constant) {
                auto left_value = left.value.constant.unwrap_boolean();

                auto right_value = right.value.constant.unwrap_boolean();

                bool value;
                switch(kind) {
                    case BinaryOperationKind::BooleanAnd: {
                        value = left_value && right_value;
                    } break;

                    case BinaryOperationKind::BooleanOr: {
                        value = left_value || right_value;
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        } else {
            switch(kind) {
                case BinaryOperationKind::Equal:
                case BinaryOperationKind::NotEqual: break;

                default: {
                    error(scope, range, "Cannot perform that operation on 'bool'");

                    return err();
                } break;
            }

            if(both_constant) {
                auto left_value = left.value.constant.unwrap_boolean();

                auto right_value = right.value.constant.unwrap_boolean();

                bool value;
                switch(kind) {
                    case BinaryOperationKind::BooleanAnd: {
                        value = left_value && right_value;
                    } break;

                    case BinaryOperationKind::BooleanOr: {
                        value = left_value || right_value;
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        }
    } else if(type.kind == TypeKind::FloatType) {
        auto float_ = type.float_;

        expect_void(coerce_to_float(
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            float_,
            false
        ));

        expect_void(coerce_to_float(
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            float_,
            false
        ));

        auto is_arithmetic = true;
        switch(kind) {
            case BinaryOperationKind::Addition:
            case BinaryOperationKind::Subtraction:
            case BinaryOperationKind::Multiplication:
            case BinaryOperationKind::Division:
            case BinaryOperationKind::Modulus:
            case BinaryOperationKind::BitwiseAnd:
            case BinaryOperationKind::BitwiseOr:
            case BinaryOperationKind::LeftShift:
            case BinaryOperationKind::RightShift: break;

            default: {
                is_arithmetic = false;
            } break;
        }

        if(is_arithmetic) {
            result_type = type;

            if(both_constant) {
                auto left_value = left.value.constant.unwrap_float();

                auto right_value = right.value.constant.unwrap_float();

                double value;
                switch(kind) {
                    case BinaryOperationKind::Addition: {
                        value = left_value + right_value;
                    } break;

                    case BinaryOperationKind::Subtraction: {
                        value = left_value - right_value;
                    } break;

                    case BinaryOperationKind::Multiplication: {
                        value = left_value * right_value;
                    } break;

                    case BinaryOperationKind::Division: {
                        value = left_value / right_value;
                    } break;

                    case BinaryOperationKind::Modulus: {
                        value = fmod(left_value, right_value);
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        } else {
            switch(kind) {
                case BinaryOperationKind::Equal:
                case BinaryOperationKind::NotEqual:
                case BinaryOperationKind::LessThan:
                case BinaryOperationKind::GreaterThan: break;

                default: {
                    error(scope, range, "Cannot perform that operation on float_s");

                    return err();
                } break;
            }

            result_type = AnyType::create_boolean();

            if(both_constant) {
                auto left_value = left.value.constant.unwrap_float();

                auto right_value = right.value.constant.unwrap_float();

                bool value;
                switch(kind) {
                    case BinaryOperationKind::Equal: {
                        value = left_value == right_value;
                    } break;

                    case BinaryOperationKind::NotEqual: {
                        value = left_value != right_value;
                    } break;

                    case BinaryOperationKind::LessThan: {
                        value = left_value < right_value;
                    } break;

                    case BinaryOperationKind::GreaterThan: {
                        value = left_value > right_value;
                    } break;

                    default: abort();
                }

                result_value = AnyValue(AnyConstantValue(value));
            } else {
                result_value = AnyValue::create_anonymous_value();
            }
        }
    } else if(type.kind == TypeKind::Pointer) {
        auto pointer = type.pointer;

        result_type = AnyType::create_boolean();

        expect_void(coerce_to_pointer(
            info,
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            pointer,
            false
        ));

        expect_void(coerce_to_pointer(
            info,
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            pointer,
            false
        ));

        switch(kind) {
            case BinaryOperationKind::Equal:
            case BinaryOperationKind::NotEqual: break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(AnyType(pointer).get_description(context->arena)));

                return err();
            } break;
        }

        if(both_constant) {
            auto left_value = left.value.constant.unwrap_integer();

            auto right_value = right.value.constant.unwrap_integer();

            bool value;
            switch(kind) {
                case BinaryOperationKind::Equal: {
                    value = left_value == right_value;
                } break;

                case BinaryOperationKind::NotEqual: {
                    value = left_value != right_value;
                } break;

                default: abort();
            }

            result_value = AnyValue(AnyConstantValue(value));
        } else {
            result_value = AnyValue::create_anonymous_value();
        }
    } else if(type.kind == TypeKind::Enum) {
        auto enum_ = type.enum_;

        result_type = AnyType::create_boolean();

        expect_void(coerce_to_type(
            info,
            scope,
            context,
            left_expression->range,
            left.type,
            left.value,
            type,
            false
        ));

        expect_void(coerce_to_type(
            info,
            scope,
            context,
            right_expression->range,
            right.type,
            right.value,
            type,
            false
        ));

        switch(kind) {
            case BinaryOperationKind::Equal:
            case BinaryOperationKind::NotEqual: break;

            default: {
                error(scope, range, "Cannot perform that operation on '%.*s'", STRING_PRINTF_ARGUMENTS(AnyType(enum_).get_description(context->arena)));

                return err();
            } break;
        }

        if(both_constant) {
            auto left_value = left.value.constant.unwrap_integer();

            auto right_value = right.value.constant.unwrap_integer();

            bool value;
            switch(kind) {
                case BinaryOperationKind::Equal: {
                    value = left_value == right_value;
                } break;

                case BinaryOperationKind::NotEqual: {
                    value = left_value != right_value;
                } break;

                default: abort();
            }

            result_value = AnyValue(AnyConstantValue(value));
        } else {
            result_value = AnyValue::create_anonymous_value();
        }
    } else {
        abort();
    }

    TypedExpression typed_expression {};
    typed_expression.kind = TypedExpressionKind::BinaryOperation;
    typed_expression.range = range;
    typed_expression.type = result_type;
    typed_expression.value = result_value;
    typed_expression.binary_operation.kind = kind;
    typed_expression.binary_operation.left = context->arena->heapify(left);
    typed_expression.binary_operation.right = context->arena->heapify(right);

    return ok(typed_expression);
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
    } else if(declaration->kind == StatementKind::UnionDefinition) {
        auto union_definition = (UnionDefinition*)declaration;

        return ok(union_definition->name.text);
    } else if(declaration->kind == StatementKind::EnumDefinition) {
        auto enum_definition = (EnumDefinition*)declaration;

        return ok(enum_definition->name.text);
    } else if(declaration->kind == StatementKind::Import) {
        auto import = (Import*)declaration;

        return ok(import->name);
    } else {
        return err();
    }
}

static bool is_declaration_public(Statement* declaration) {
    if(declaration->kind == StatementKind::FunctionDeclaration) {
        return true;
    } else if(declaration->kind == StatementKind::ConstantDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::StructDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::UnionDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::EnumDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::EnumDefinition) {
        return true;
    } else if(declaration->kind == StatementKind::Import) {
        return declaration;
    } else {
        return false;
    }
}

static bool does_or_could_have_name(Statement* statement, String name, bool external) {
    if(statement->kind == StatementKind::FunctionDeclaration) {
        auto function_declaration = (FunctionDeclaration*)statement;

        return name == function_declaration->name.text;
    } else if(statement->kind == StatementKind::ConstantDefinition) {
        auto constant_definition = (ConstantDefinition*)statement;

        return name == constant_definition->name.text;
    } else if(statement->kind == StatementKind::StructDefinition) {
        auto struct_definition = (StructDefinition*)statement;

        return name == struct_definition->name.text;
    } else if(statement->kind == StatementKind::UnionDefinition) {
        auto union_definition = (UnionDefinition*)statement;

        return name == union_definition->name.text;
    } else if(statement->kind == StatementKind::EnumDefinition) {
        auto enum_definition = (EnumDefinition*)statement;

        return name == enum_definition->name.text;
    } else if(statement->kind == StatementKind::Import) {
        if(!external) {
            auto import = (Import*)statement;

            return name == import->name;
        } else {
            return false;
        }
    } else if(statement->kind == StatementKind::StaticIf) {
        auto static_if = (StaticIf*)statement;

        for(auto statement : static_if->statements) {
            if(does_or_could_have_name(statement, name, external)) {
                return true;
            }
        }

        return false;
    } else if(statement->kind == StatementKind::UsingStatement) {
        auto using_statement = (UsingStatement*)statement;

        if(!external || using_statement->export_) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

struct NameSearchResult {
    bool found;

    AnyType type;

    bool is_static_variable;
    VariableDeclaration* static_variable_declaration;

    AnyConstantValue constant;
};

static_profiled_function(DelayedResult<NameSearchResult>, search_for_name, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    TypingContext* context,
    String name,
    ConstantScope* name_scope,
    FileRange name_range,
    Array<Statement*> statements,
    bool external
), (
    info,
    jobs,
    scope,
    context,
    name,
    name_scope,
    name_range,
    statements,
    declarations,
    external
)) {
    for(auto stack_scope : context->scope_search_stack) {
        if(stack_scope == scope) {
            NameSearchResult result {};
            result.found = false;

            return ok(result);
        }
    }

    context->scope_search_stack.append(scope);

    for(auto statement : statements) {
        if(statement == context->search_ignore_statement) {
            continue;
        }

        if(statement->kind == StatementKind::FunctionDeclaration) {
            auto function_declaration = (FunctionDeclaration*)statement;

            if(function_declaration->name.text == name) {
                for(auto parameter : function_declaration->parameters) {
                    if(parameter.is_constant || parameter.is_polymorphic_determiner) {
                        NameSearchResult result {};
                        result.found = true;
                        result.type = AnyType::create_polymorphic_function();
                        result.constant = AnyConstantValue(PolymorphicFunctionConstant(
                            function_declaration,
                            scope
                        ));

                        context->scope_search_stack.length -= 1;

                        return ok(result);
                    }
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypeFunctionDeclaration) {
                        auto type_function_declaration = job->type_function_declaration;

                        if(type_function_declaration.declaration == function_declaration) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = type_function_declaration.type;
                                result.constant = type_function_declaration.value;

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::ConstantDefinition) {
            auto constant_definition = (ConstantDefinition*)statement;

            if(constant_definition->name.text == name) {
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypeConstantDefinition) {
                        auto type_constant_definition = job->type_constant_definition;

                        if(type_constant_definition.definition == constant_definition) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = type_constant_definition.value.type;
                                result.constant = type_constant_definition.value.value.constant;

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::StructDefinition) {
            auto struct_definition = (StructDefinition*)statement;

            if(struct_definition->name.text == name) {
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypeStructDefinition) {
                        auto type_struct_definition = job->type_struct_definition;

                        if(type_struct_definition.definition == struct_definition) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = AnyType::create_type_type();
                                result.constant = AnyConstantValue(type_struct_definition.type);

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::UnionDefinition) {
            auto union_definition = (UnionDefinition*)statement;

            if(union_definition->name.text == name) {
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypeUnionDefinition) {
                        auto type_union_definition = job->type_union_definition;

                        if(type_union_definition.definition == union_definition) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = AnyType::create_type_type();
                                result.constant = AnyConstantValue(type_union_definition.type);

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::EnumDefinition) {
            auto enum_definition = (EnumDefinition*)statement;

            if(enum_definition->name.text == name) {
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypeEnumDefinition) {
                        auto type_enum_definition = job->type_enum_definition;

                        if(type_enum_definition.definition == enum_definition) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = AnyType::create_type_type();
                                result.constant = AnyConstantValue(AnyType(type_enum_definition.type));

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::Import) {
            auto import = (Import*)statement;

            if(!external && import->name == name) {
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::ParseFile) {
                        auto parse_file = job->parse_file;

                        if(parse_file.path == import->absolute_path) {
                            if(job->state == JobState::Done) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = AnyType::create_file_module();
                                result.constant = AnyConstantValue(FileModuleConstant(parse_file.scope));

                                context->scope_search_stack.length -= 1;

                                return ok(result);
                            } else {
                                return wait(i);
                            }
                        }
                    }
                }

                abort();
            }
        } else if(statement->kind == StatementKind::UsingStatement) {
            auto using_statement = (UsingStatement*)statement;

            if(!external || using_statement->export_) {
                expect_delayed(expression_value, expect_constant_expression(
                    info,
                    jobs,
                    scope,
                    context,
                    using_statement->value
                ));

                if(expression_value.typed_expression.type.kind == TypeKind::FileModule) {
                    auto file_module = expression_value.value.unwrap_file_module();

                    assert(file_module.scope->is_top_level);

                    expect_delayed(search_value, search_for_name(
                        info,
                        jobs,
                        file_module.scope,
                        context,
                        name,
                        name_scope,
                        name_range,
                        file_module.scope->statements,
                        true
                    ));

                    if(search_value.found) {
                        context->scope_search_stack.length -= 1;

                        return ok(search_value);
                    }
                } else if(expression_value.typed_expression.type.kind == TypeKind::Type) {
                    auto type = expression_value.value.unwrap_type();

                    if(type.kind == TypeKind::Enum) {
                        auto enum_ = type.enum_;

                        for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                            if(enum_.definition->variants[i].name.text == name) {
                                NameSearchResult result {};
                                result.found = true;
                                result.type = AnyType(*enum_.backing_type);
                                result.constant = AnyConstantValue(enum_.variant_values[i]);

                                context->scope_search_stack.length -= 1;

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

                if(job->kind == JobKind::TypeStaticIf) {
                    auto type_static_if = job->type_static_if;

                    if(
                        type_static_if.static_if == static_if &&
                        type_static_if.scope == scope
                    ) {
                        found = true;

                        if(job->state == JobState::Done) {
                            if(type_static_if.condition_value) {
                                expect_delayed(search_value, search_for_name(
                                    info,
                                    jobs,
                                    scope,
                                    context,
                                    name,
                                    name_scope,
                                    name_range,
                                    static_if->statements,
                                    false
                                ));

                                if(search_value.found) {
                                    context->scope_search_stack.length -= 1;

                                    return ok(search_value);
                                }
                            }
                        } else {
                            if(does_or_could_have_name(static_if, name, external)) {
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

                        if(job->kind == JobKind::TypeStaticVariable) {
                            auto type_static_variable = job->type_static_variable;

                            if(type_static_variable.declaration == variable_declaration) {
                                if(job->state == JobState::Done) {
                                    NameSearchResult result {};
                                    result.found = true;
                                    result.type = type_static_variable.actual_type;
                                    result.is_static_variable = true;
                                    result.static_variable_declaration = variable_declaration;

                                    context->scope_search_stack.length -= 1;

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
            NameSearchResult result {};
            result.found = true;
            result.type = scope_constant.type;
            result.constant = scope_constant.value;

            context->scope_search_stack.length -= 1;

            return ok(result);
        }
    }

    NameSearchResult result {};
    result.found = false;

    context->scope_search_stack.length -= 1;

    return ok(result);
}

static Result<String> array_to_string(Arena* arena, ConstantScope* scope, FileRange range, AnyType type, AnyConstantValue value) {
    AnyType element_type;
    AggregateConstant aggregate_value;
    if(type.kind == TypeKind::StaticArray) {
        element_type = *type.static_array.element_type;

        if(value.kind == ConstantValueKind::AggregateConstant) {
            assert(value.aggregate.values.length == type.static_array.length);

            aggregate_value = value.aggregate;
        } else {
            error(scope, range, "Cannot use an array with non-constant elements in this context");

            return err();
        }
    } else if(type.kind == TypeKind::ArrayTypeType) {
        element_type = *type.array.element_type;

        if(value.kind == ConstantValueKind::AggregateConstant) {
            aggregate_value = value.aggregate;
        } else {
            error(scope, range, "Cannot use an array with non-constant elements in this context");

            return err();
        }
    } else {
        error(scope, range, "Expected a string ([]u8), got '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

        return err();
    }

    if(
        element_type.kind != TypeKind::Integer ||
        element_type.integer.size != RegisterSize::Size8
    ) {
        error(scope, range, "Expected a string ([]u8), got '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(arena)));

        return err();
    }

    auto data = arena->allocate<uint8_t>(aggregate_value.values.length);
    for(size_t i = 0; i < aggregate_value.values.length; i += 1) {
        auto element_value = aggregate_value.values[i];

        if(element_value.kind == ConstantValueKind::UndefConstant) {
            error(scope, range, "String array is partially undefined, at element %zu", i);

            return err();
        }

        data[i] = (uint8_t)element_value.unwrap_integer();
    }

    if(!validate_utf8_string(data, aggregate_value.values.length).status) {
        error(scope, range, "String value is not valid UTF-8");

        return err();
    }

    String string {};
    string.length = aggregate_value.values.length;
    string.elements = (char8_t*)data;

    return ok(string);
}

static_profiled_function(DelayedResult<TypedExpression>, type_expression, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
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

        VariableScope* previous_variable_scope = nullptr;
        auto current_variable_scope = context->variable_scope;
        auto index = context->in_progress_variable_scope_stack.length - 1;
        while(current_variable_scope != nullptr) {
            auto in_progress_scope = context->in_progress_variable_scope_stack[index];

            for(auto variable : in_progress_scope.variables) {
                if(variable->name.text == named_reference->name.text) {
                    TypedExpression typed_expression {};
                    typed_expression.kind = TypedExpressionKind::VariableReference;
                    typed_expression.range = named_reference->range;
                    typed_expression.type = variable->type;
                    typed_expression.value = AnyValue::create_assignable_value();
                    typed_expression.variable_reference.variable = variable;

                    return ok(typed_expression);
                }
            }

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_variable_scope->constant_scope,
                context,
                named_reference->name.text,
                scope,
                named_reference->name.range,
                current_variable_scope->constant_scope->statements,
                false
            ));

            if(search_value.found) {
                TypedExpression typed_expression {};
                typed_expression.range = named_reference->range;
                typed_expression.type = search_value.type;

                if(search_value.is_static_variable) {
                    typed_expression.kind = TypedExpressionKind::StaticVariableReference;
                    typed_expression.value = AnyValue::create_assignable_value();
                    typed_expression.static_variable_reference.declaration = search_value.static_variable_declaration;
                } else {
                    typed_expression.kind = TypedExpressionKind::ConstantLiteral;
                    typed_expression.value = AnyValue(search_value.constant);
                }

                return ok(typed_expression);
            }

            previous_variable_scope = current_variable_scope;
            current_variable_scope = current_variable_scope->parent;
            index -= 1;
        }

        ConstantScope* current_scope;
        if(previous_variable_scope == nullptr) {
            current_scope = scope;
        } else {
            auto current_scope = previous_variable_scope->constant_scope->parent;
        }

        while(true) {
            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                current_scope,
                context,
                named_reference->name.text,
                scope,
                named_reference->name.range,
                current_scope->statements,
                false
            ));

            if(search_value.found) {
                TypedExpression typed_expression {};
                typed_expression.range = named_reference->range;
                typed_expression.type = search_value.type;

                if(search_value.is_static_variable) {
                    typed_expression.kind = TypedExpressionKind::StaticVariableReference;
                    typed_expression.value = AnyValue::create_assignable_value();
                    typed_expression.static_variable_reference.declaration = search_value.static_variable_declaration;
                } else {
                    typed_expression.kind = TypedExpressionKind::ConstantLiteral;
                    typed_expression.value = AnyValue(search_value.constant);
                }

                return ok(typed_expression);
            }

            if(current_scope->is_top_level) {
                break;
            } else {
                current_scope = current_scope->parent;
            }
        }

        for(auto global_constant : info.global_constants) {
            if(named_reference->name.text == global_constant.name) {
                TypedExpression typed_expression {};
                typed_expression.range = named_reference->range;
                typed_expression.type = global_constant.type;
                typed_expression.value = AnyValue(global_constant.value);
                typed_expression.kind = TypedExpressionKind::ConstantLiteral;

                return ok(typed_expression);
            }
        }

        error(scope, named_reference->name.range, "Cannot find named reference %.*s", STRING_PRINTF_ARGUMENTS(named_reference->name.text));

        return err();
    } else if(expression->kind == ExpressionKind::IndexReference) {
        auto index_reference = (IndexReference*)expression;

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, index_reference->expression));

        if(expression_value.value.kind == ValueKind::ConstantValue && expression_value.value.constant.kind == ConstantValueKind::UndefConstant) {
            error(scope, index_reference->expression->range, "Cannot index undefined value");

            return err();
        }

        expect_delayed(index, type_expression(info, jobs, scope, context, index_reference->index));

        if(index.value.kind == ValueKind::ConstantValue && index.value.constant.kind == ConstantValueKind::UndefConstant) {
            error(scope, index_reference->index->range, "Cannot index with an undefined index");

            return err();
        }

        expect_void(coerce_to_integer(
            scope,
            context,
            index_reference->index->range,
            index.type,
            index.value,
            Integer(info.architecture_sizes.address_size, false),
            false
        ));

        AnyType element_type;
        AnyValue element_value;
        if(expression_value.type.kind == TypeKind::ArrayTypeType) {
            auto array_type = expression_value.type.array;

            element_type = *array_type.element_type;

            if(expression_value.value.kind == ValueKind::ConstantValue) {
                auto constant_value = expression_value.value.constant;

                if(constant_value.kind == ConstantValueKind::ArrayConstant) {
                    element_value = AnyValue::create_assignable_value();
                } else if(constant_value.kind == ConstantValueKind::AggregateConstant) {
                    auto aggregate_value = constant_value.aggregate;

                    if(index.value.kind != ValueKind::ConstantValue) {
                        error(scope, index_reference->index->range, "Cannot index constant array with non-constant index");

                        return err();
                    }

                    auto index_integer = index.value.constant.unwrap_integer();

                    if(index_integer >= aggregate_value.values.length) {
                        error(scope, index_reference->index->range, "Array index %zu out of bounds", index_integer);

                        return err();
                    }

                    element_value = AnyValue(aggregate_value.values[index_integer]);
                } else {
                    abort();
                }
            } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                element_value = AnyValue::create_assignable_value();
            } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                element_value = AnyValue::create_assignable_value();
            } else if(expression_value.value.kind == ValueKind::UndeterminedAggregateValue) {
                auto aggregate_value = expression_value.value.undetermined_aggregate;

                if(index.value.kind != ValueKind::ConstantValue) {
                    error(scope, index_reference->index->range, "Cannot index undetermined array with non-constant index");

                    return err();
                }

                auto index_integer = index.value.constant.unwrap_integer();

                if(index_integer >= aggregate_value.values.length) {
                    error(scope, index_reference->index->range, "Array index %zu out of bounds", index_integer);

                    return err();
                }

                element_value = AnyValue(aggregate_value.values[index_integer]);
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::StaticArray) {
            auto static_array = expression_value.type.static_array;

            element_type = *static_array.element_type;

            if(expression_value.value.kind == ValueKind::ConstantValue) {
                auto aggregate_value = expression_value.value.constant.unwrap_aggregate();

                if(index.value.kind != ValueKind::ConstantValue) {
                    error(scope, index_reference->index->range, "Cannot index constant array with non-constant index");

                    return err();
                }

                auto index_integer = index.value.constant.unwrap_integer();

                if(index_integer >= aggregate_value.values.length) {
                    error(scope, index_reference->index->range, "Array index %zu out of bounds", index_integer);

                    return err();
                }

                element_value = AnyValue(aggregate_value.values[index_integer]);
            } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                if(index.value.kind != ValueKind::ConstantValue) {
                    error(scope, index_reference->index->range, "Cannot index anonymous array with non-constant index");

                    return err();
                }

                element_value = AnyValue::create_anonymous_value();
            } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                element_value = AnyValue::create_assignable_value();
            } else {
                abort();
            }
        } else {
            error(scope, index_reference->expression->range, "Cannot index '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

            return err();
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::IndexReference;
        typed_expression.range = index_reference->range;
        typed_expression.type = element_type;
        typed_expression.value = element_value;
        typed_expression.index_reference.value = context->arena->heapify(expression_value);
        typed_expression.index_reference.index = context->arena->heapify(index);

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::MemberReference) {
        auto member_reference = (MemberReference*)expression;

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, member_reference->expression));

        if(expression_value.value.kind == ValueKind::ConstantValue && expression_value.value.constant.kind == ConstantValueKind::UndefConstant) {
            error(scope, member_reference->expression->range, "Cannot access members of undefined value");

            return err();
        }

        AnyType actual_type;
        AnyValue actual_value;
        if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;
            actual_type = *pointer.pointed_to_type;

            if(!actual_type.is_runtime_type()) {
                error(scope, member_reference->expression->range, "Cannot access members of '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

                return err();
            }

            actual_value = AnyValue::create_assignable_value();
        } else {
            actual_type = expression_value.type;
            actual_value = expression_value.value;
        }

        AnyType member_type;
        AnyValue member_value;
        if(actual_type.kind == TypeKind::ArrayTypeType) {
            auto array_type = actual_type.array;

            if(member_reference->name.text == u8"length"_S) {
                member_type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));

                if(actual_value.kind == ValueKind::ConstantValue) {
                    auto constant_value = expression_value.value.constant;

                    if(constant_value.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = constant_value.unwrap_array();

                        member_value = AnyValue(AnyConstantValue(array_value.length));
                    } else if(constant_value.kind == ConstantValueKind::AggregateConstant) {
                        auto aggregate_value = constant_value.unwrap_aggregate();

                        member_value = AnyValue(AnyConstantValue(aggregate_value.values.length));
                    } else {
                        abort();
                    }
                } else if(actual_value.kind == ValueKind::AnonymousValue) {
                    member_value = AnyValue::create_anonymous_value();
                } else if(actual_value.kind == ValueKind::AssignableValue) {
                    member_value = AnyValue::create_assignable_value();
                } else {
                    abort();
                }
            } else if(member_reference->name.text == u8"pointer"_S) {
                member_type = AnyType(Pointer(array_type.element_type));

                if(actual_value.kind == ValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::ArrayConstant) {
                        auto array_value = expression_value.value.constant.unwrap_array();

                        member_value = AnyValue(AnyConstantValue(array_value.pointer));
                    } else {
                        error(scope, member_reference->range, "Cannot take pointer to contents of constant array");

                        return err();
                    }
                } else if(actual_value.kind == ValueKind::AnonymousValue) {
                    member_value = AnyValue::create_anonymous_value();
                } else if(actual_value.kind == ValueKind::AssignableValue) {
                    member_value = AnyValue::create_assignable_value();
                } else {
                    abort();
                }
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StaticArray) {
            auto static_array = actual_type.static_array;

            if(member_reference->name.text == u8"length"_S) {
                member_type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));

                member_value = AnyValue(AnyConstantValue(static_array.length));
            } else if(member_reference->name.text == u8"pointer"_S) {
                member_type = AnyType(Pointer(static_array.element_type));

                if(actual_value.kind == ValueKind::ConstantValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of constant static array");

                    return err();
                } else if(actual_value.kind == ValueKind::AnonymousValue) {
                    error(scope, member_reference->range, "Cannot take pointer to contents of anonymous static array");

                    return err();
                } else if(actual_value.kind == ValueKind::AssignableValue) {
                    member_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }
            } else {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::StructType) {
            auto struct_type = actual_type.struct_;

            auto found = false;
            for(size_t i = 0; i < struct_type.members.length; i += 1) {
                if(struct_type.members[i].name == member_reference->name.text) {
                    member_type = struct_type.members[i].type;

                    if(actual_value.kind == ValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::AggregateConstant) {
                            auto aggregate_value = expression_value.value.constant.unwrap_aggregate();

                            member_value = AnyValue(aggregate_value.values[i]);
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, member_reference->range, "Cannot access members of undefined array constant");

                            return err();
                        }
                    } else if(actual_value.kind == ValueKind::AnonymousValue) {
                        member_value = AnyValue::create_anonymous_value();
                    } else if(actual_value.kind == ValueKind::AssignableValue) {
                        member_value = AnyValue::create_assignable_value();
                    } else {
                        abort();
                    }

                    found = true;
                    break;
                }
            }

            if(!found) {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::UnionType) {
            auto union_type = actual_type.union_;

            auto found = false;
            for(size_t i = 0; i < union_type.members.length; i += 1) {
                if(union_type.members[i].name == member_reference->name.text) {
                    member_type = union_type.members[i].type;

                    if(actual_value.kind == ValueKind::AnonymousValue) {
                        member_value = AnyValue::create_anonymous_value();
                    } else if(actual_value.kind == ValueKind::AssignableValue) {
                        member_value = AnyValue::create_assignable_value();
                    } else {
                        abort();
                    }

                    found = true;
                    break;
                }
            }

            if(!found) {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::UndeterminedStruct) {
            auto undetermined_struct = actual_type.undetermined_struct;

            auto found = false;
            for(size_t i = 0; i < undetermined_struct.members.length; i += 1) {
                if(undetermined_struct.members[i].name == member_reference->name.text) {
                    member_type = undetermined_struct.members[i].type;

                    if(actual_value.kind == ValueKind::ConstantValue) {
                        auto aggregate_value = actual_value.constant.unwrap_aggregate();

                        member_value = AnyValue(aggregate_value.values[i]);
                    } else if(actual_value.kind == ValueKind::UndeterminedAggregateValue) {
                        auto aggregate_value = actual_value.undetermined_aggregate;

                        member_value = aggregate_value.values[i];
                    } else {
                        abort();
                    }

                    found = true;
                    break;
                }
            }

            if(!found) {
                error(scope, member_reference->name.range, "No member with name %.*s", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(actual_type.kind == TypeKind::FileModule) {
            auto file_module_value = expression_value.value.constant.unwrap_file_module();

            expect_delayed(search_value, search_for_name(
                info,
                jobs,
                file_module_value.scope,
                context,
                member_reference->name.text,
                scope,
                member_reference->name.range,
                file_module_value.scope->statements,
                true
            ));

            if(search_value.found) {
                member_type = search_value.type;

                if(search_value.is_static_variable) {
                    member_value = AnyValue::create_assignable_value();
                } else {
                    member_value = AnyValue(search_value.constant);
                }
            } else {
                error(scope, member_reference->name.range, "No member with name '%.*s'", STRING_PRINTF_ARGUMENTS(member_reference->name.text));

                return err();
            }
        } else if(expression_value.type.kind == TypeKind::Type) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto type = constant_value.type;

            if(type.kind == TypeKind::Enum) {
                auto enum_ = type.enum_;

                member_type = type;

                auto found = false;
                for(size_t i = 0; i < enum_.variant_values.length; i += 1) {
                    if(enum_.definition->variants[i].name.text == member_reference->name.text) {
                        member_value = AnyValue(AnyConstantValue(enum_.variant_values[i]));

                        found = true;
                        break;
                    }
                }

                if(!found) {
                    error(
                        scope,
                        member_reference->name.range,
                        "Enum '%.*s' has no variant with name '%.*s'",
                        STRING_PRINTF_ARGUMENTS(enum_.definition->name.text),
                        STRING_PRINTF_ARGUMENTS(member_reference->name.text)
                    );

                    return err();
                }
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

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::MemberReference;
        typed_expression.range = member_reference->range;
        typed_expression.type = member_type;
        typed_expression.value = member_value;
        typed_expression.member_reference.value = context->arena->heapify(expression_value);
        typed_expression.member_reference.name = member_reference->name;

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::IntegerLiteral) {
        auto integer_literal = (IntegerLiteral*)expression;

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::ConstantLiteral;
        typed_expression.range = integer_literal->range;
        typed_expression.type = AnyType::create_undetermined_integer();
        typed_expression.value = AnyValue(AnyConstantValue(integer_literal->value));

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::FloatLiteral) {
        auto float_literal = (FloatLiteral*)expression;

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::ConstantLiteral;
        typed_expression.range = float_literal->range;
        typed_expression.type = AnyType::create_undetermined_float();
        typed_expression.value = AnyValue(AnyConstantValue(float_literal->value));

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::StringLiteral) {
        auto string_literal = (StringLiteral*)expression;

        auto character_count = string_literal->characters.length;

        auto characters = context->arena->allocate<AnyConstantValue>(character_count);

        for(size_t i = 0; i < character_count; i += 1) {
            characters[i] = AnyConstantValue((uint64_t)string_literal->characters[i]);
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::ConstantLiteral;
        typed_expression.range = string_literal->range;
        typed_expression.type = AnyType(StaticArray(
            character_count,
            context->arena->heapify(AnyType(Integer(
                RegisterSize::Size8,
                false
            )))
        ));
        typed_expression.value = AnyValue(AnyConstantValue(AggregateConstant(
            Array(character_count, characters)
        )));

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::ArrayLiteral) {
        auto array_literal = (ArrayLiteral*)expression;

        auto element_count = array_literal->elements.length;

        if(element_count == 0) {
            error(scope, array_literal->range, "Empty array literal");

            return err();
        }

        auto elements = context->arena->allocate<TypedExpression>(element_count);
        auto element_types = context->arena->allocate<AnyType>(element_count);

        auto all_constant = true;
        for(size_t i = 0; i < element_count; i += 1) {
            expect_delayed(element, type_expression(info, jobs, scope, context, array_literal->elements[i]));

            elements[i] = element;
            element_types[i] = element.type;

            if(element.value.kind != ValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyValue value;
        if(all_constant) {
            auto element_values = context->arena->allocate<AnyConstantValue>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                element_values[i] = elements[i].value.constant;
            }

            value = AnyValue(AnyConstantValue(AggregateConstant(
                Array(element_count, element_values)
            )));
        } else {
            auto element_values = context->arena->allocate<AnyValue>(element_count);

            for(size_t i = 0; i < element_count; i += 1) {
                element_values[i] = elements[i].value;
            }

            value = AnyValue(UndeterminedAggregateValue(
                Array(element_count, element_values)
            ));
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::ArrayLiteral;
        typed_expression.range = array_literal->range;
        typed_expression.type = AnyType(UndeterminedArray(Array(
            element_count,
            element_types
        )));
        typed_expression.value = value;
        typed_expression.array_literal.elements = Array(element_count, elements);

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::StructLiteral) {
        auto struct_literal = (StructLiteral*)expression;

        if(struct_literal->members.length == 0) {
            error(scope, struct_literal->range, "Empty struct literal");

            return err();
        }

        auto member_count = struct_literal->members.length;

        auto members = context->arena->allocate<TypedStructMember>(member_count);
        auto type_members = context->arena->allocate<StructTypeMember>(member_count);

        auto all_constant = true;
        for(size_t i = 0; i < member_count; i += 1) {
            for(size_t j = 0; j < i; j += 1) {
                if(struct_literal->members[i].name.text == type_members[j].name) {
                    error(scope, struct_literal->members[i].name.range, "Duplicate struct member %.*s", STRING_PRINTF_ARGUMENTS(struct_literal->members[i].name.text));

                    return err();
                }
            }

            expect_delayed(member, type_expression(info, jobs, scope, context, struct_literal->members[i].value));

            TypedStructMember typed_member {};
            typed_member.member = member;
            typed_member.name = struct_literal->members[i].name;

            members[i] = typed_member;

            StructTypeMember type_member {};
            type_member.name = struct_literal->members[i].name.text;
            type_member.type = member.type;

            type_members[i] = type_member;

            if(member.value.kind != ValueKind::ConstantValue) {
                all_constant = false;
            }
        }

        AnyValue value;
        if(all_constant) {
            auto member_values = context->arena->allocate<AnyConstantValue>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                member_values[i] = members[i].member.value.constant;
            }

            value = AnyValue(AnyConstantValue(AggregateConstant(
                Array(member_count, member_values)
            )));
        } else {
            auto member_values = context->arena->allocate<AnyValue>(member_count);

            for(size_t i = 0; i < member_count; i += 1) {
                member_values[i] = members[i].member.value;
            }

            value = AnyValue(UndeterminedAggregateValue(
                Array(member_count, member_values)
            ));
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::StructLiteral;
        typed_expression.range = struct_literal->range;
        typed_expression.type = AnyType(UndeterminedStruct(
            Array(member_count, type_members)
        ));
        typed_expression.value = value;
        typed_expression.struct_literal.members = Array(member_count, members);

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::FunctionCall) {
        auto function_call = (FunctionCall*)expression;

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, function_call->expression));

        if(expression_value.type.kind == TypeKind::FunctionTypeType || expression_value.type.kind == TypeKind::PolymorphicFunction) {
            auto call_parameter_count = function_call->parameters.length;

            auto parameters = context->arena->allocate<TypedExpression>(call_parameter_count + 1);
            auto call_parameters = context->arena->allocate<TypedRuntimeValue>(call_parameter_count);
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[i]));

                parameters[i] = parameter_value;
                call_parameters[i] = TypedRuntimeValue(parameter_value.type, parameter_value.value);
            }

            FunctionTypeType function_type;
            FunctionConstant function_value;
            if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
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
                        if(call_parameters[i].value.kind != ValueKind::ConstantValue) {
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

                    if(job->kind == JobKind::TypePolymorphicFunction) {
                        auto type_polymorphic_function = job->type_polymorphic_function;

                        if(
                            type_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                            type_polymorphic_function.scope == polymorphic_function_value.scope
                        ) {
                            auto matching_polymorphic_parameters = true;
                            for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                                auto declaration_parameter = declaration_parameters[i];
                                auto call_parameter = polymorphic_parameters[i];
                                auto job_parameter = type_polymorphic_function.parameters[i];

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

                            if(job->state == JobState::Done) {
                                found = true;

                                function_type = type_polymorphic_function.type;
                                function_value = type_polymorphic_function.value;

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
                    job.kind = JobKind::TypePolymorphicFunction;
                    job.state = JobState::Working;
                    job.type_polymorphic_function.declaration = polymorphic_function_value.declaration;
                    job.type_polymorphic_function.parameters = Array(declaration_parameter_count, polymorphic_parameters);
                    job.type_polymorphic_function.scope = polymorphic_function_value.scope;
                    job.type_polymorphic_function.call_scope = scope;
                    job.type_polymorphic_function.call_parameter_ranges = Array(declaration_parameter_count, call_parameter_ranges);

                    auto job_index = jobs->append(context->global_arena->heapify(job));

                    return wait(job_index);
                }
            } else {
                function_type = expression_value.type.function;

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

                if(job->kind == JobKind::TypeFunctionBody) {
                    auto type_function_body = job->type_function_body;

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

                jobs->append(context->global_arena->heapify(job));
            }

            size_t runtime_parameter_index = 0;
            for(size_t i = 0; i < call_parameter_count; i += 1) {
                if(!function_value.declaration->parameters[i].is_constant) {
                    expect_void(coerce_to_type(
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

            AnyValue value;
            if(return_type.kind != TypeKind::Void) {
                value = AnyValue::create_anonymous_value();
            } else {
                value = AnyValue(AnyConstantValue::create_void());
            }

            TypedExpression typed_expression {};
            typed_expression.kind = TypedExpressionKind::FunctionCall;
            typed_expression.range = function_call->range;
            typed_expression.type = return_type;
            typed_expression.value = value;
            typed_expression.function_call.value = context->arena->heapify(expression_value);
            typed_expression.function_call.parameters = Array(call_parameter_count, parameters);

            return ok(typed_expression);
        } else if(expression_value.type.kind == TypeKind::BuiltinFunction) {
            auto constant_value = expression_value.value.unwrap_constant_value();

            auto builtin_function_value = constant_value.unwrap_builtin_function();

            if(builtin_function_value.name == u8"size_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[0]));

                AnyType type;
                if(parameter_value.type.kind == TypeKind::Type) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    type = constant_value.unwrap_type();
                } else {
                    type = parameter_value.type;
                }

                if(!type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "'%.*s'' has no size", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description(context->arena)));

                    return err();
                }

                auto size = type.get_size(info.architecture_sizes);

                TypedExpression typed_expression {};
                typed_expression.kind = TypedExpressionKind::FunctionCall;
                typed_expression.range = function_call->range;
                typed_expression.type = AnyType(Integer(
                    info.architecture_sizes.address_size,
                    false
                ));
                typed_expression.value = AnyValue(AnyConstantValue(size));
                typed_expression.function_call.value = context->arena->heapify(expression_value);
                typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                return ok(typed_expression);
            } else if(builtin_function_value.name == u8"type_of"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[0]));

                TypedExpression typed_expression {};
                typed_expression.kind = TypedExpressionKind::FunctionCall;
                typed_expression.range = function_call->range;
                typed_expression.type = AnyType::create_type_type();
                typed_expression.value = AnyValue(AnyConstantValue(parameter_value.type));
                typed_expression.function_call.value = context->arena->heapify(expression_value);
                typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                return ok(typed_expression);
            } else if(builtin_function_value.name == u8"globalify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[0]));

                auto determined_type = get_default_type(info, scope, function_call->parameters[0]->range, parameter_value.type);

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description(context->arena)));

                    return err();
                }

                if(parameter_value.value.kind != ValueKind::ConstantValue) {
                    error(scope, function_call->parameters[0]->range, "Cannot globalify a non-constant value");

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_to_type(
                    info,
                    scope,
                    context,
                    function_call->parameters[0]->range,
                    parameter_value.type,
                    AnyValue(constant_value),
                    determined_type,
                    false
                ));

                assert(coerced_value.kind == ValueKind::ConstantValue);

                TypedExpression typed_expression {};
                typed_expression.kind = TypedExpressionKind::FunctionCall;
                typed_expression.range = function_call->range;
                typed_expression.type = determined_type;
                typed_expression.value = AnyValue::create_assignable_value();
                typed_expression.function_call.value = context->arena->heapify(expression_value);
                typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                return ok(typed_expression);
            } else if(builtin_function_value.name == u8"stackify"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1, got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[0]));

                auto determined_type = get_default_type(info, scope, function_call->parameters[0]->range, parameter_value.type);

                if(!determined_type.is_runtime_type()) {
                    error(scope, function_call->parameters[0]->range, "Type '%.*s' cannot exist at runtime", STRING_PRINTF_ARGUMENTS(determined_type.get_description(context->arena)));

                    return err();
                }

                auto constant_value = parameter_value.value.constant;

                expect(coerced_value, coerce_to_type(
                    info,
                    scope,
                    context,
                    function_call->parameters[0]->range,
                    parameter_value.type,
                    AnyValue(constant_value),
                    determined_type,
                    false
                ));

                assert(coerced_value.kind == ValueKind::ConstantValue);

                TypedExpression typed_expression {};
                typed_expression.kind = TypedExpressionKind::FunctionCall;
                typed_expression.range = function_call->range;
                typed_expression.type = determined_type;
                typed_expression.value = AnyValue::create_assignable_value();
                typed_expression.function_call.value = context->arena->heapify(expression_value);
                typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                return ok(typed_expression);
            } else if(builtin_function_value.name == u8"sqrt"_S) {
                if(function_call->parameters.length != 1) {
                    error(scope, function_call->range, "Incorrect parameter count. Expected 1 got %zu", function_call->parameters.length);

                    return err();
                }

                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[0]));

                if(parameter_value.value.kind == ValueKind::ConstantValue) {
                    auto constant_value = parameter_value.value.unwrap_constant_value();

                    RegisterSize result_size;
                    double value;
                    if(parameter_value.type.kind == TypeKind::UndeterminedInteger) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        auto integer_value = constant_value.unwrap_integer();

                        result_size = info.architecture_sizes.default_float_size;
                        value = (double)integer_value;
                    } else if(parameter_value.type.kind == TypeKind::UndeterminedFloat) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = info.architecture_sizes.default_float_size;
                        value = constant_value.unwrap_float();
                    } else if(parameter_value.type.kind == TypeKind::FloatType) {
                        if(constant_value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, function_call->parameters[0]->range, "Value is undefined");

                            return err();
                        }

                        result_size = parameter_value.type.float_.size;
                        value = constant_value.unwrap_float();
                    } else {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description(context->arena)));

                        return err();
                    }

                    auto result_value = sqrt(value);

                    TypedExpression typed_expression {};
                    typed_expression.kind = TypedExpressionKind::FunctionCall;
                    typed_expression.range = function_call->range;
                    typed_expression.type = AnyType(FloatType(result_size));
                    typed_expression.value = AnyValue(AnyConstantValue(result_value));
                    typed_expression.function_call.value = context->arena->heapify(expression_value);
                    typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                    return ok(typed_expression);
                } else {
                    if(parameter_value.type.kind != TypeKind::FloatType) {
                        error(scope, function_call->parameters[0]->range, "Expected a float type, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter_value.type.get_description(context->arena)));

                        return err();
                    }

                    TypedExpression typed_expression {};
                    typed_expression.kind = TypedExpressionKind::FunctionCall;
                    typed_expression.range = function_call->range;
                    typed_expression.type = parameter_value.type;
                    typed_expression.value = AnyValue::create_anonymous_value();
                    typed_expression.function_call.value = context->arena->heapify(expression_value);
                    typed_expression.function_call.parameters = Array(1, context->arena->heapify(parameter_value));

                    return ok(typed_expression);
                }
            } else {
                abort();
            }
        } else if(expression_value.type.kind == TypeKind::Pointer) {
            auto pointer = expression_value.type.pointer;

            if(pointer.pointed_to_type->kind != TypeKind::FunctionTypeType) {
                error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

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

            auto parameters = context->arena->allocate<TypedExpression>(parameter_count);

            for(size_t i = 0; i < parameter_count; i += 1) {
                expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[i]));

                expect_void(coerce_to_type(
                    info,
                    scope,
                    context,
                    function_call->parameters[i]->range,
                    parameter_value.type,
                    parameter_value.value,
                    function_type.parameters[i],
                    false
                ));

                parameters[i] = parameter_value;
            }

            AnyType return_type;
            if(function_type.return_types.length == 0) {
                return_type = AnyType::create_void();
            } else if(function_type.return_types.length == 1) {
                return_type = function_type.return_types[0];
            } else {
                return_type = AnyType(MultiReturn(function_type.return_types));
            }

            AnyValue value;
            if(return_type.kind != TypeKind::Void) {
                value = AnyValue::create_anonymous_value();
            } else {
                value = AnyValue(AnyConstantValue::create_void());
            }

            TypedExpression typed_expression {};
            typed_expression.kind = TypedExpressionKind::FunctionCall;
            typed_expression.range = function_call->range;
            typed_expression.type = return_type;
            typed_expression.value = value;
            typed_expression.function_call.value = context->arena->heapify(expression_value);
            typed_expression.function_call.parameters = Array(parameter_count, parameters);

            return ok(typed_expression);
        } else if(expression_value.type.kind == TypeKind::Type) {
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

                auto parameters = context->arena->allocate<TypedExpression>(parameter_count);
                auto parameter_values = context->arena->allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, expect_constant_expression(
                        info,
                        jobs,
                        scope,
                        context,
                        function_call->parameters[i]
                    ));

                    expect(parameter_value, coerce_to_type(
                        info,
                        scope,
                        context,
                        function_call->parameters[i]->range,
                        parameter.typed_expression.type,
                        AnyValue(parameter.value),
                        polymorphic_struct.parameter_types[i],
                        false
                    ));

                    assert(parameter_value.kind == ValueKind::ConstantValue);

                    parameters[i] = parameter.typed_expression;
                    parameter_values[i] = parameter_value.constant;
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypePolymorphicStruct) {
                        auto type_polymorphic_struct = job->type_polymorphic_struct;

                        if(type_polymorphic_struct.definition == definition && type_polymorphic_struct.parameters.length != 0) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameter_values[i], type_polymorphic_struct.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job->state == JobState::Done) {
                                    TypedExpression typed_expression {};
                                    typed_expression.kind = TypedExpressionKind::FunctionCall;
                                    typed_expression.range = function_call->range;
                                    typed_expression.type = AnyType::create_type_type();
                                    typed_expression.value = AnyValue(AnyConstantValue(AnyType(type_polymorphic_struct.type)));
                                    typed_expression.function_call.value = context->arena->heapify(expression_value);
                                    typed_expression.function_call.parameters = Array(parameter_count, parameters);

                                    return ok(typed_expression);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job {};
                job.kind = JobKind::TypePolymorphicStruct;
                job.state = JobState::Working;
                job.type_polymorphic_struct.definition = definition;
                job.type_polymorphic_struct.parameters = Array(parameter_count, parameter_values);
                job.type_polymorphic_struct.scope = polymorphic_struct.parent;

                auto job_index = jobs->append(context->global_arena->heapify(job));

                return wait(job_index);
            } else if(type.kind == TypeKind::PolymorphicUnion) {
                auto polymorphic_union = type.polymorphic_union;
                auto definition = polymorphic_union.definition;

                auto parameter_count = definition->parameters.length;

                if(function_call->parameters.length != parameter_count) {
                    error(scope, function_call->range, "Incorrect union parameter count: expected %zu, got %zu", parameter_count, function_call->parameters.length);

                    return err();
                }

                auto parameters = context->arena->allocate<TypedExpression>(parameter_count);
                auto parameter_values = context->arena->allocate<AnyConstantValue>(parameter_count);

                for(size_t i = 0; i < parameter_count; i += 1) {
                    expect_delayed(parameter, expect_constant_expression(
                        info,
                        jobs,
                        scope,
                        context,
                        function_call->parameters[i]
                    ));

                    expect(parameter_value, coerce_to_type(
                        info,
                        scope,
                        context,
                        function_call->parameters[i]->range,
                        parameter.typed_expression.type,
                        AnyValue(parameter.value),
                        polymorphic_union.parameter_types[i],
                        false
                    ));

                    assert(parameter_value.kind == ValueKind::ConstantValue);

                    parameters[i] = parameter.typed_expression;
                    parameter_values[i] = parameter_value.constant;
                }

                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::TypePolymorphicUnion) {
                        auto type_polymorphic_union = job->type_polymorphic_union;

                        if(type_polymorphic_union.definition == definition && type_polymorphic_union.parameters.length != 0) {
                            auto same_parameters = true;
                            for(size_t i = 0; i < parameter_count; i += 1) {
                                if(!constant_values_equal(parameter_values[i], type_polymorphic_union.parameters[i])) {
                                    same_parameters = false;
                                    break;
                                }
                            }

                            if(same_parameters) {
                                if(job->state == JobState::Done) {
                                    TypedExpression typed_expression {};
                                    typed_expression.kind = TypedExpressionKind::FunctionCall;
                                    typed_expression.range = function_call->range;
                                    typed_expression.type = AnyType::create_type_type();
                                    typed_expression.value = AnyValue(AnyConstantValue(AnyType(type_polymorphic_union.type)));
                                    typed_expression.function_call.value = context->arena->heapify(expression_value);
                                    typed_expression.function_call.parameters = Array(parameter_count, parameters);

                                    return ok(typed_expression);
                                } else {
                                    return wait(i);
                                }
                            }
                        }
                    }
                }

                AnyJob job {};
                job.kind = JobKind::TypePolymorphicUnion;
                job.state = JobState::Working;
                job.type_polymorphic_union.definition = definition;
                job.type_polymorphic_union.parameters = Array(parameter_count, parameter_values);
                job.type_polymorphic_union.scope = polymorphic_union.parent;

                auto job_index = jobs->append(context->global_arena->heapify(job));

                return wait(job_index);
            } else {
                error(scope, function_call->expression->range, "Type '%.*s' is not polymorphic", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                return err();
            }
        } else {
            error(scope, function_call->expression->range, "Cannot call '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::BinaryOperation) {
        auto binary_operation = (BinaryOperation*)expression;

        expect_delayed(result_value, type_binary_operation(
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

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, unary_operation->expression));

        UnaryOperationKind kind;
        switch(unary_operation->unary_operator) {
            case UnaryOperation::Operator::Pointer: kind = UnaryOperationKind::Pointer; break;
            case UnaryOperation::Operator::PointerDereference: kind = UnaryOperationKind::PointerDereference; break;
            case UnaryOperation::Operator::BooleanInvert: kind = UnaryOperationKind::BooleanInvert; break;
            case UnaryOperation::Operator::Negation: kind = UnaryOperationKind::Negation; break;
            default: abort();
        }

        AnyType result_type;
        AnyValue result_value;
        switch(kind) {
            case UnaryOperationKind::Pointer: {
                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto constant_value = expression_value.value.constant;

                    if(expression_value.type.kind == TypeKind::FunctionTypeType) {
                        auto function = expression_value.type.function;

                        auto function_value = constant_value.unwrap_function();

                        auto found = false;
                        for(size_t i = 0; i < jobs->length; i += 1) {
                            auto job = (*jobs)[i];

                            if(job->kind == JobKind::TypeFunctionBody) {
                                auto type_function_body = job->type_function_body;

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

                            jobs->append(context->global_arena->heapify(job));
                        }

                        result_type = AnyType(Pointer(context->arena->heapify(expression_value.type)));
                        result_value = AnyValue::create_anonymous_value();
                    } else if(expression_value.type.kind == TypeKind::Type) {
                        auto type = constant_value.unwrap_type();

                        if(!type.is_pointable_type()) {
                            error(scope, unary_operation->expression->range, "Cannot create pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(type.get_description(context->arena)));

                            return err();
                        }

                        result_type = AnyType::create_type_type();
                        result_value = AnyValue(AnyConstantValue(AnyType(Pointer(context->arena->heapify(type)))));
                    } else {
                        error(scope, unary_operation->expression->range, "Cannot take pointers to constants of type '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

                        return err();
                    }
                } else if(
                    expression_value.value.kind == ValueKind::AnonymousValue ||
                    expression_value.value.kind == ValueKind::UndeterminedAggregateValue
                ) {
                    error(scope, unary_operation->expression->range, "Cannot take pointers to anonymous values");

                    return err();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_type = AnyType(Pointer(context->arena->heapify(expression_value.type)));
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }
            } break;

            case UnaryOperationKind::PointerDereference: {
                if(expression_value.type.kind != TypeKind::Pointer) {
                    error(scope, unary_operation->expression->range, "Expected a pointer, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

                    return err();
                }

                auto pointed_to_type = *expression_value.type.pointer.pointed_to_type;

                if(!pointed_to_type.is_runtime_type()) {
                    error(scope, unary_operation->expression->range, "Cannot dereference pointers to type '%.*s'", STRING_PRINTF_ARGUMENTS(pointed_to_type.get_description(context->arena)));

                    return err();
                }

                result_type = pointed_to_type;

                result_value = AnyValue::create_assignable_value();
            } break;

            case UnaryOperationKind::BooleanInvert: {
                if(expression_value.type.kind != TypeKind::Boolean) {
                    error(scope, unary_operation->expression->range, "Expected bool, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

                    return err();
                }

                result_type = AnyType::create_boolean();

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    if(expression_value.value.constant.kind == ConstantValueKind::BooleanConstant) {
                        auto boolean_value = expression_value.value.constant.unwrap_boolean();

                        result_value = AnyValue(AnyConstantValue(!boolean_value));
                    } else {
                        assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                        error(scope, unary_operation->expression->range, "Cannot invert an undefined boolean constant");

                        return err();
                    }
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }
            } break;

            case UnaryOperationKind::Negation: {
                if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto integer_value = constant_value.unwrap_integer();

                    result_type = AnyType::create_undetermined_integer();

                    result_value = AnyValue(AnyConstantValue((uint64_t)-(int64_t)integer_value));
                } else if(expression_value.type.kind == TypeKind::Integer) {
                    auto integer = expression_value.type.integer;

                    result_type = AnyType(integer);

                    if(expression_value.value.kind == ValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::IntegerConstant) {
                            auto integer_value = expression_value.value.constant.unwrap_integer();

                            result_value = AnyValue(AnyConstantValue((uint64_t)-(int64_t)integer_value));
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined integer constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else {
                        abort();
                    }
                } else if(expression_value.type.kind == TypeKind::FloatType) {
                    auto float_type = expression_value.type.float_;

                    result_type = AnyType(float_type);

                    if(expression_value.value.kind == ValueKind::ConstantValue) {
                        if(expression_value.value.constant.kind == ConstantValueKind::FloatConstant) {
                            auto float_value = expression_value.value.constant.unwrap_float();

                            result_value = AnyValue(AnyConstantValue(-float_value));
                        } else {
                            assert(expression_value.value.constant.kind == ConstantValueKind::UndefConstant);

                            error(scope, unary_operation->expression->range, "Cannot negate an undefined float constant");

                            return err();
                        }
                    } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else {
                        abort();
                    }
                } else if(expression_value.type.kind == TypeKind::UndeterminedFloat) {
                    auto constant_value = expression_value.value.unwrap_constant_value();

                    auto float_value = constant_value.unwrap_float();

                    result_type = AnyType::create_undetermined_float();

                    result_value = AnyValue(AnyConstantValue(-float_value));
                } else {
                    error(scope, unary_operation->expression->range, "Cannot negate '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

                    return err();
                }
            } break;

            default: {
                abort();
            } break;
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::UnaryOperation;
        typed_expression.range = unary_operation->range;
        typed_expression.type = result_type;
        typed_expression.value = result_value;
        typed_expression.unary_operation.kind = kind;
        typed_expression.unary_operation.value = context->arena->heapify(expression_value);

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::Cast) {
        auto cast = (Cast*)expression;

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, cast->expression));

        if(expression_value.value.kind == ValueKind::ConstantValue && expression_value.value.constant.kind == ConstantValueKind::UndefConstant) {
            error(scope, cast->expression->range, "Cannot cast an undefined value");

            return err();
        }

        expect_delayed(target_type, expect_type_expression(info, jobs, scope, context, cast->type));

        auto coercion_result = coerce_to_type(
            info,
            scope,
            context,
            cast->range,
            expression_value.type,
            expression_value.value,
            target_type.type,
            true
        );

        auto has_cast = false;
        AnyValue result_value;
        if(coercion_result.status) {
            has_cast = true;
            result_value = coercion_result.value;
        } else if(target_type.type.kind == TypeKind::Integer) {
            auto target_integer = target_type.type.integer;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto integer_value = expression_value.value.constant.unwrap_integer();

                    uint64_t result;
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

                    result_value = AnyValue(AnyConstantValue(result));
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                result_value = expression_value.value;
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto float_value = expression_value.value.constant.unwrap_float();

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

                    uint64_t result;
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

                    result_value = AnyValue(AnyConstantValue(result));
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::UndeterminedFloat) {
                auto float_value = expression_value.value.constant.unwrap_float();

                uint64_t result;
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

                result_value = AnyValue(AnyConstantValue(result));
            } else if(expression_value.type.kind == TypeKind::Enum) {
                auto enum_ = expression_value.type.enum_;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto integer_value = expression_value.value.constant.unwrap_integer();

                    uint64_t result;
                    if(enum_.backing_type->is_signed) {
                        switch(enum_.backing_type->size) {
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
                        switch(enum_.backing_type->size) {
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

                        result_value = AnyValue(AnyConstantValue(result));
                    }
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;

                if(target_integer.size == info.architecture_sizes.address_size && !target_integer.is_signed) {
                    has_cast = true;

                    if(expression_value.value.kind == ValueKind::ConstantValue) {
                        result_value = expression_value.value;
                    } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else {
                        abort();
                    }
                }
            }
        } else if(target_type.type.kind == TypeKind::FloatType) {
            auto target_float_type = target_type.type.float_;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto integer_value = expression_value.value.constant.unwrap_integer();

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

                    double result;
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

                    result_value = AnyValue(AnyConstantValue(result));
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                auto integer_value = expression_value.value.constant.unwrap_integer();

                double result;
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

                result_value = AnyValue(AnyConstantValue(result));
            } else if(expression_value.type.kind == TypeKind::FloatType) {
                auto float_type = expression_value.type.float_;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto float_value = expression_value.value.constant.unwrap_float();

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

                    double result;
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

                    result_value = AnyValue(AnyConstantValue(result));
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::UndeterminedFloat) {
                auto float_value = expression_value.value.constant.unwrap_float();

                double result;
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

                result_value = AnyValue(AnyConstantValue(result));
            }
        } else if(target_type.type.kind == TypeKind::Pointer) {
            auto target_pointer = target_type.type.pointer;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                if(integer.size == info.architecture_sizes.address_size && !integer.is_signed) {
                    has_cast = true;

                    if(expression_value.value.kind == ValueKind::ConstantValue) {
                        result_value = expression_value.value;
                    } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                        result_value = AnyValue::create_anonymous_value();
                    } else {
                        abort();
                    }
                }
            } else if(expression_value.type.kind == TypeKind::Pointer) {
                auto pointer = expression_value.type.pointer;

                has_cast = true;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    result_value = expression_value.value;
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }
            }
        } else if(target_type.type.kind == TypeKind::Enum) {
            auto target_enum = target_type.type.enum_;

            if(expression_value.type.kind == TypeKind::Integer) {
                auto integer = expression_value.type.integer;

                if(expression_value.value.kind == ValueKind::ConstantValue) {
                    auto integer_value = expression_value.value.constant.unwrap_integer();

                    uint64_t result;
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

                    result_value = AnyValue(AnyConstantValue(result));
                } else if(expression_value.value.kind == ValueKind::AnonymousValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else if(expression_value.value.kind == ValueKind::AssignableValue) {
                    result_value = AnyValue::create_anonymous_value();
                } else {
                    abort();
                }

                has_cast = true;
            } else if(expression_value.type.kind == TypeKind::UndeterminedInteger) {
                result_value = expression_value.value;
            }
        } else {
            abort();
        }

        if(has_cast) {
            TypedExpression type_expression {};
            type_expression.kind = TypedExpressionKind::Cast;
            type_expression.range = cast->range;
            type_expression.type = target_type.type;
            type_expression.value = result_value;
            type_expression.cast.value = context->arena->heapify(expression_value);
            type_expression.cast.type = context->arena->heapify(target_type.typed_expression);

            return ok(type_expression);
        } else {
            error(
                scope,
                cast->range,
                "Cannot cast from '%.*s' to '%.*s'",
                STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)),
                STRING_PRINTF_ARGUMENTS(target_type.type.get_description(context->arena))
            );

            return err();
        }
    } else if(expression->kind == ExpressionKind::Bake) {
        auto bake = (Bake*)expression;

        auto function_call = bake->function_call;

        expect_delayed(expression_value, type_expression(info, jobs, scope, context, function_call->expression));

        auto call_parameter_count = function_call->parameters.length;

        auto parameters = context->arena->allocate<TypedExpression>(call_parameter_count);
        auto call_parameters = context->arena->allocate<TypedRuntimeValue>(call_parameter_count);
        for(size_t i = 0; i < call_parameter_count; i += 1) {
            expect_delayed(parameter_value, type_expression(info, jobs, scope, context, function_call->parameters[i]));

            parameters[i] = parameter_value;
            call_parameters[i] = TypedRuntimeValue(parameter_value.type, parameter_value.value);
        }

        if(expression_value.type.kind == TypeKind::PolymorphicFunction) {
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
                    if(call_parameters[i].value.kind != ValueKind::ConstantValue) {
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

                if(job->kind == JobKind::TypePolymorphicFunction) {
                    auto type_polymorphic_function = job->type_polymorphic_function;

                    if(
                        type_polymorphic_function.declaration == polymorphic_function_value.declaration &&
                        type_polymorphic_function.scope == polymorphic_function_value.scope
                    ) {
                        auto matching_polymorphic_parameters = true;
                        for(size_t i = 0; i < declaration_parameter_count; i += 1) {
                            auto declaration_parameter = declaration_parameters[i];
                            auto call_parameter = polymorphic_parameters[i];
                            auto job_parameter = type_polymorphic_function.parameters[i];

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

                        if(job->state == JobState::Done) {
                            TypedExpression typed_expression {};
                            typed_expression.kind = TypedExpressionKind::Bake;
                            typed_expression.range = function_call->range;
                            typed_expression.type = AnyType(type_polymorphic_function.type);
                            typed_expression.value = AnyValue(AnyConstantValue(type_polymorphic_function.value));
                            typed_expression.bake.value = context->arena->heapify(expression_value);
                            typed_expression.bake.parameters = Array(call_parameter_count, parameters);

                            return ok(typed_expression);
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
            job.kind = JobKind::TypePolymorphicFunction;
            job.state = JobState::Working;
            job.type_polymorphic_function.declaration = polymorphic_function_value.declaration;
            job.type_polymorphic_function.parameters = Array(declaration_parameter_count, polymorphic_parameters);
            job.type_polymorphic_function.scope = polymorphic_function_value.scope;
            job.type_polymorphic_function.call_scope = scope;
            job.type_polymorphic_function.call_parameter_ranges = Array(declaration_parameter_count, call_parameter_ranges);

            auto job_index = jobs->append(context->global_arena->heapify(job));

            return wait(job_index);
        } else if(expression_value.type.kind == TypeKind::FunctionTypeType) {
            auto function_type = expression_value.type.function;

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
            typed_expression.kind = TypedExpressionKind::Bake;
            typed_expression.range = function_call->range;
            typed_expression.type = AnyType(function_type);
            typed_expression.value = AnyValue(AnyConstantValue(function_value));
            typed_expression.bake.value = context->arena->heapify(expression_value);
            typed_expression.bake.parameters = Array(call_parameter_count, parameters);

            return ok(typed_expression);
        } else {
            error(scope, function_call->expression->range, "Expected a function, got '%.*s'", STRING_PRINTF_ARGUMENTS(expression_value.type.get_description(context->arena)));

            return err();
        }
    } else if(expression->kind == ExpressionKind::ArrayType) {
        auto array_type = (ArrayType*)expression;

        expect_delayed(type_value, expect_type_expression(info, jobs, scope, context, array_type->expression));

        if(!type_value.type.is_runtime_type()) {
            error(scope, array_type->expression->range, "Cannot have arrays of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

            return err();
        }

        TypedExpression* length;
        AnyType result_type;
        if(array_type->length != nullptr) {
            expect_delayed(length_value, expect_constant_expression(
                info,
                jobs,
                scope,
                context,
                array_type->length
            ));

            expect_void(coerce_to_integer(
                scope,
                context,
                array_type->length->range,
                length_value.typed_expression.type,
                AnyValue(length_value.value),
                Integer(
                    info.architecture_sizes.address_size,
                    false
                ),
                false
            ));

            if(length_value.value.kind == ConstantValueKind::UndefConstant) {
                error(scope, array_type->length->range, "Length cannot be undefined");

                return err();
            }

            auto length_integer = length_value.value.unwrap_integer();

            length = context->arena->heapify(length_value.typed_expression);

            result_type = AnyType(StaticArray(
                length_integer,
                context->arena->heapify(type_value.type)
            ));
        } else {
            length = nullptr;

            result_type = AnyType(ArrayTypeType(
                context->arena->heapify(type_value.type)
            ));
        }

        TypedExpression typed_expression {};
        typed_expression.kind = TypedExpressionKind::ArrayType;
        typed_expression.range = array_type->range;
        typed_expression.type = AnyType::create_type_type();
        typed_expression.value = AnyValue(AnyConstantValue(result_type));
        typed_expression.array_type.length = length;
        typed_expression.array_type.element_type = context->arena->heapify(type_value.typed_expression);

        return ok(typed_expression);
    } else if(expression->kind == ExpressionKind::FunctionType) {
        auto function_type = (FunctionType*)expression;

        auto parameter_count = function_type->parameters.length;
        auto return_type_count = function_type->return_types.length;

        auto parameters = context->arena->allocate<TypedFunctionParameter>(parameter_count);
        auto parameter_types = context->arena->allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            auto parameter = function_type->parameters[i];

            if(parameter.is_polymorphic_determiner) {
                error(scope, parameter.polymorphic_determiner.range, "Function types cannot be polymorphic");

                return err();
            }

            expect_delayed(type_value, expect_type_expression(info, jobs, scope, context, parameter.type));

            if(!type_value.type.is_runtime_type()) {
                error(scope, function_type->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                return err();
            }

            TypedFunctionParameter typed_parameter {};
            typed_parameter.name = parameter.name;
            typed_parameter.type = type_value.typed_expression;

            parameters[i] = typed_parameter;
            parameter_types[i] = type_value.type;
        }

        auto typed_return_types = context->arena->allocate<TypedExpression>(return_type_count);
        auto return_types = context->arena->allocate<AnyType>(return_type_count);

        for(size_t i = 0; i < return_type_count; i += 1) {
            auto expression = function_type->return_types[i];

            expect_delayed(type_value, expect_type_expression(info, jobs, scope, context, expression));

            if(!type_value.type.is_runtime_type()) {
                error(scope, expression->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                return err();
            }

            typed_return_types[i] = type_value.typed_expression;
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

                expect_delayed(parameter, expect_constant_expression(
                    info,
                    jobs,
                    scope,
                    context,
                    tag.parameters[0]
                ));

                expect(calling_convention_name, array_to_string(context->arena, scope, tag.parameters[0]->range, parameter.typed_expression.type, parameter.value));

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
        typed_expression.kind = TypedExpressionKind::FunctionType;
        typed_expression.range = function_type->range;
        typed_expression.type = AnyType::create_type_type();
        typed_expression.value = AnyValue(AnyConstantValue(AnyType(FunctionTypeType(
            Array(parameter_count, parameter_types),
            Array(return_type_count, return_types),
            calling_convention
        ))));
        typed_expression.function_type.parameters = Array(parameter_count, parameters);
        typed_expression.function_type.return_types = Array(return_type_count, typed_return_types);

        return ok(typed_expression);
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
    List<AnyJob*>* jobs,
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

            if(statement->kind == StatementKind::ExpressionStatement) {
                auto expression_statement = (ExpressionStatement*)statement;

                expect_delayed(value, type_expression(info, jobs, scope, context, expression_statement->expression));

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::ExpressionStatement;
                typed_statement.range = statement->range;
                typed_statement.expression_statement.expression = value;

                typed_statements.append(typed_statement);
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
                    expect_delayed(type_value, expect_type_expression(info, jobs, scope, context, variable_declaration->type));

                    if(!type_value.type.is_runtime_type()) {
                        error(scope, variable_declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type_value.type.get_description(context->arena)));

                        return err();
                    }

                    type = type_value.type;

                    expect_delayed(initializer_value, type_expression(info, jobs, scope, context, variable_declaration->initializer));

                    TypedStatement typed_statement {};
                    typed_statement.kind = TypedStatementKind::VariableDeclaration;
                    typed_statement.range = statement->range;
                    typed_statement.variable_declaration.name = variable_declaration->name;
                    typed_statement.variable_declaration.has_type = true;
                    typed_statement.variable_declaration.type = type_value.typed_expression;
                    typed_statement.variable_declaration.has_initializer = true;
                    typed_statement.variable_declaration.initializer = initializer_value;
                    typed_statement.variable_declaration.actual_type = type;

                    typed_statements.append(typed_statement);
                } else {
                    expect_delayed(initializer_value, type_expression(info, jobs, scope, context, variable_declaration->initializer));

                    auto actual_type = get_default_type(info, scope, variable_declaration->initializer->range, initializer_value.type);

                    if(!actual_type.is_runtime_type()) {
                        error(scope, variable_declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(actual_type.get_description(context->arena)));

                        return err();
                    }

                    type = actual_type;

                    expect_void(coerce_to_type(
                        info,
                        scope,
                        context,
                        variable_declaration->range,
                        initializer_value.type,
                        initializer_value.value,
                        type,
                        false
                    ));

                    TypedStatement typed_statement {};
                    typed_statement.kind = TypedStatementKind::VariableDeclaration;
                    typed_statement.range = statement->range;
                    typed_statement.variable_declaration.name = variable_declaration->name;
                    typed_statement.variable_declaration.has_initializer = true;
                    typed_statement.variable_declaration.initializer = initializer_value;
                    typed_statement.variable_declaration.actual_type = type;

                    typed_statements.append(typed_statement);
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

                expect_delayed(initializer_value, type_expression(info, jobs, scope, context, variable_declaration->initializer));

                if(initializer_value.type.kind != TypeKind::MultiReturn) {
                    error(scope, variable_declaration->initializer->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(initializer_value.type.get_description(context->arena)));

                    return err();
                }

                auto return_types = initializer_value.type.multi_return.types;

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

                auto names = context->arena->allocate<TypedName>(return_types.length);

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

                    TypedName typed_name {};
                    typed_name.name = variable_declaration->names[i];
                    typed_name.type = return_types[i];

                    names[i] = typed_name;
                }

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::MultiReturnVariableDeclaration;
                typed_statement.range = statement->range;
                typed_statement.multi_return_variable_declaration.names = Array(return_types.length, names);
                typed_statement.multi_return_variable_declaration.initializer = initializer_value;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::Assignment) {
                auto assignment = (Assignment*)statement;

                expect_delayed(target, type_expression(info, jobs, scope, context, assignment->target));

                if(target.value.kind == ValueKind::AssignableValue){
                } else {
                    error(scope, assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(value, type_expression(info, jobs, scope, context, assignment->value));

                expect_void(coerce_to_type(
                    info,
                    scope,
                    context,
                    assignment->range,
                    value.type,
                    value.value,
                    target.type,
                    false
                ));

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::Assignment;
                typed_statement.range = statement->range;
                typed_statement.assignment.target = target;
                typed_statement.assignment.value = value;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::MultiReturnAssignment) {
                auto assignment = (MultiReturnAssignment*)statement;

                assert(assignment->targets.length > 1);

                expect_delayed(value, type_expression(info, jobs, scope, context, assignment->value));

                if(value.type.kind != TypeKind::MultiReturn) {
                    error(scope, assignment->value->range, "Expected multiple return values, got '%.*s'", STRING_PRINTF_ARGUMENTS(value.type.get_description(context->arena)));

                    return err();
                }

                auto return_types = value.type.multi_return.types;

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

                auto targets = context->arena->allocate<TypedExpression>(return_types.length);

                for(size_t i = 0; i < return_types.length; i += 1) {
                    expect_delayed(target, type_expression(info, jobs, scope, context, assignment->targets[i]));

                    if(target.value.kind != ValueKind::AssignableValue){
                        error(scope, assignment->targets[i]->range, "Value is not assignable");

                        return err();
                    }

                    expect_void(coerce_to_type(
                        info,
                        scope,
                        context,
                        assignment->range,
                        return_types[i],
                        AnyValue::create_anonymous_value(),
                        target.type,
                        false
                    ));

                    targets[i] = target;
                }

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::MultiReturnAssignment;
                typed_statement.range = statement->range;
                typed_statement.multi_return_assignment.targets = Array(return_types.length, targets);
                typed_statement.multi_return_assignment.value = value;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::BinaryOperationAssignment) {
                auto binary_operation_assignment = (BinaryOperationAssignment*)statement;

                expect_delayed(target, type_expression(info, jobs, scope, context, binary_operation_assignment->target));

                if(target.value.kind == ValueKind::AssignableValue){
                } else {
                    error(scope, binary_operation_assignment->target->range, "Value is not assignable");

                    return err();
                }

                expect_delayed(result_value, type_binary_operation(
                    info,
                    jobs,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    binary_operation_assignment->target,
                    binary_operation_assignment->value,
                    binary_operation_assignment->binary_operator
                ));

                expect_void(coerce_to_type(
                    info,
                    scope,
                    context,
                    binary_operation_assignment->range,
                    result_value.type,
                    result_value.value,
                    target.type,
                    false
                ));

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::BinaryOperationAssignment;
                typed_statement.range = statement->range;
                typed_statement.binary_operation_assignment.operation = result_value;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::IfStatement) {
                auto if_statement = (IfStatement*)statement;

                expect_delayed(condition, type_expression(info, jobs, scope, context, if_statement->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, if_statement->condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

                    return err();
                }

                auto if_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                auto if_variable_scope = context->arena->allocate_and_construct<VariableScope>();
                if_variable_scope->parent = context->variable_scope;
                if_variable_scope->constant_scope = if_scope;

                context->variable_scope = if_variable_scope;

                {
                    InProgressVariableScope scope {};
                    scope.variables.arena = context->arena;

                    context->in_progress_variable_scope_stack.append(scope);
                }

                expect_delayed(statements, generate_runtime_statements(info, jobs, if_scope, context, if_statement->statements));

                if_variable_scope->variables = context->in_progress_variable_scope_stack.take_last().variables;

                context->in_progress_variable_scope_stack.length -= 1;

                auto else_ifs = context->arena->allocate<TypedElseIf>(if_statement->else_ifs.length);

                for(size_t i = 0; i < if_statement->else_ifs.length; i += 1) {
                    expect_delayed(condition, type_expression(info, jobs, scope, context, if_statement->else_ifs[i].condition));

                    if(condition.type.kind != TypeKind::Boolean) {
                        error(scope, if_statement->else_ifs[i].condition->range, "Non-boolean if statement condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

                        return err();
                    }

                    auto else_if_scope = context->child_scopes[context->next_child_scope_index];
                    context->next_child_scope_index += 1;
                    assert(context->next_child_scope_index <= context->child_scopes.length);

                    auto else_if_variable_scope = context->arena->allocate_and_construct<VariableScope>();
                    else_if_variable_scope->parent = context->variable_scope;
                    else_if_variable_scope->constant_scope = else_if_scope;

                    context->variable_scope = else_if_variable_scope;

                    {
                        InProgressVariableScope scope {};
                        scope.variables.arena = context->arena;

                        context->in_progress_variable_scope_stack.append(scope);
                    }

                    expect_delayed(else_if_statements, generate_runtime_statements(info, jobs, if_scope, context, if_statement->else_ifs[i].statements));

                    else_if_variable_scope->variables = context->in_progress_variable_scope_stack.take_last().variables;

                    context->in_progress_variable_scope_stack.length -= 1;

                    TypedElseIf else_if {};
                    else_if.condition = condition;
                    else_if.scope = else_if_variable_scope;
                    else_if.statements = else_if_statements;

                    else_ifs[i] = else_if;
                }

                auto else_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                auto else_variable_scope = context->arena->allocate_and_construct<VariableScope>();
                else_variable_scope->parent = context->variable_scope;
                else_variable_scope->constant_scope = else_scope;

                context->variable_scope = else_variable_scope;

                {
                    InProgressVariableScope scope {};
                    scope.variables.arena = context->arena;

                    context->in_progress_variable_scope_stack.append(scope);
                }

                expect_delayed(else_statements, generate_runtime_statements(info, jobs, else_scope, context, if_statement->else_statements));

                else_variable_scope->variables = context->in_progress_variable_scope_stack.take_last().variables;

                context->in_progress_variable_scope_stack.length -= 1;

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::IfStatement;
                typed_statement.range = statement->range;
                typed_statement.if_statement.condition = condition;
                typed_statement.if_statement.scope = if_variable_scope;
                typed_statement.if_statement.statements = statements;
                typed_statement.if_statement.else_ifs = Array(if_statement->else_ifs.length, else_ifs);
                typed_statement.if_statement.else_scope = else_variable_scope;
                typed_statement.if_statement.else_statements = else_statements;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::WhileLoop) {
                auto while_loop = (WhileLoop*)statement;

                expect_delayed(condition, type_expression(info, jobs, scope, context, while_loop->condition));

                if(condition.type.kind != TypeKind::Boolean) {
                    error(scope, while_loop->condition->range, "Non-boolean while loop condition. Got %.*s", STRING_PRINTF_ARGUMENTS(condition.type.get_description(context->arena)));

                    return err();
                }

                auto while_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                auto while_variable_scope = context->arena->allocate_and_construct<VariableScope>();
                while_variable_scope->parent = context->variable_scope;
                while_variable_scope->constant_scope = while_scope;

                context->variable_scope = while_variable_scope;

                auto old_in_breakable_scope = context->in_breakable_scope;

                context->in_breakable_scope = true;

                expect_delayed(statements, generate_runtime_statements(info, jobs, while_scope, context, while_loop->statements));

                context->in_breakable_scope = old_in_breakable_scope;

                while_variable_scope->variables = context->in_progress_variable_scope_stack.take_last().variables;

                context->in_progress_variable_scope_stack.length -= 1;

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::WhileLoop;
                typed_statement.range = statement->range;
                typed_statement.while_loop.condition = condition;
                typed_statement.while_loop.scope = while_variable_scope;
                typed_statement.while_loop.statements = statements;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::ForLoop) {
                auto for_loop = (ForLoop*)statement;

                expect_delayed(from_value, type_expression(info, jobs, scope, context, for_loop->from));

                expect_delayed(to_value, type_expression(info, jobs, scope, context, for_loop->to));

                Integer determined_index_type;
                if(from_value.type.kind == TypeKind::UndeterminedInteger && to_value.type.kind == TypeKind::UndeterminedInteger) {
                    determined_index_type = Integer(
                        info.architecture_sizes.default_integer_size,
                        true
                    );
                } else if(from_value.type.kind == TypeKind::Integer) {
                    determined_index_type = from_value.type.integer;
                } else if(to_value.type.kind == TypeKind::Integer) {
                    determined_index_type = to_value.type.integer;
                } else {
                    error(scope, for_loop->range, "For loop index/range must be an integer. Got '%.*s'", STRING_PRINTF_ARGUMENTS(from_value.type.get_description(context->arena)));

                    return err();
                }

                TypedName typed_index_name;
                Identifier index_name;
                if(for_loop->has_index_name) {
                    index_name = for_loop->index_name;

                    typed_index_name = {};
                    typed_index_name.name = for_loop->index_name;
                    typed_index_name.type = AnyType(determined_index_type);
                } else {
                    index_name = {};
                    index_name.text = u8"it"_S;
                    index_name.range = for_loop->range;
                }

                expect_void(coerce_to_integer(
                    scope,
                    context,
                    for_loop->from->range,
                    from_value.type,
                    from_value.value,
                    determined_index_type,
                    false
                ));

                expect_void(coerce_to_integer(
                    scope,
                    context,
                    for_loop->from->range,
                    to_value.type,
                    to_value.value,
                    determined_index_type,
                    false
                ));

                auto for_scope = context->child_scopes[context->next_child_scope_index];
                context->next_child_scope_index += 1;
                assert(context->next_child_scope_index <= context->child_scopes.length);

                auto for_variable_scope = context->arena->allocate_and_construct<VariableScope>();
                for_variable_scope->parent = context->variable_scope;
                for_variable_scope->constant_scope = for_scope;

                context->variable_scope = for_variable_scope;

                auto old_in_breakable_scope = context->in_breakable_scope;

                context->in_breakable_scope = true;

                expect_void(add_new_variable(
                    context,
                    index_name,
                    AnyType(determined_index_type)
                ));

                expect_delayed(statements, generate_runtime_statements(info, jobs, for_scope, context, for_loop->statements));

                context->in_breakable_scope = old_in_breakable_scope;

                for_variable_scope->variables = context->in_progress_variable_scope_stack.take_last().variables;

                context->in_progress_variable_scope_stack.length -= 1;

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::ForLoop;
                typed_statement.range = statement->range;
                typed_statement.for_loop.from = from_value;
                typed_statement.for_loop.to = to_value;
                typed_statement.for_loop.has_index_name = for_loop->has_index_name;
                typed_statement.for_loop.index_name = typed_index_name;
                typed_statement.for_loop.scope = for_variable_scope;
                typed_statement.for_loop.statements = statements;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::ReturnStatement) {
                auto return_statement = (ReturnStatement*)statement;

                unreachable = true;

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

                auto values = context->arena->allocate<TypedExpression>(return_type_count);

                for(size_t i = 0; i < return_type_count; i += 1) {
                    expect_delayed(value, type_expression(info, jobs, scope, context, return_statement->values[i]));

                    expect_void(coerce_to_type(
                        info,
                        scope,
                        context,
                        return_statement->values[i]->range,
                        value.type,
                        value.value,
                        context->return_types[i],
                        false
                    ));

                    values[i] = value;
                }

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::Return;
                typed_statement.range = statement->range;
                typed_statement.return_.values = Array(return_type_count, values);

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::BreakStatement) {
                auto break_statement = (BreakStatement*)statement;

                unreachable = true;

                if(!context->in_breakable_scope) {
                    error(scope, break_statement->range, "Not in a break-able scope");

                    return err();
                }

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::Break;
                typed_statement.range = statement->range;

                typed_statements.append(typed_statement);
            } else if(statement->kind == StatementKind::InlineAssembly) {
                auto inline_assembly = (InlineAssembly*)statement;

                auto bindings = context->arena->allocate<TypedBinding>(inline_assembly->bindings.length);

                for(size_t i = 0; i < inline_assembly->bindings.length; i += 1) {
                    auto binding = inline_assembly->bindings[i];

                    if(binding.constraint.length < 1) {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    }

                    expect(value, type_expression(
                        info,
                        jobs,
                        scope,
                        context,
                        binding.value
                    ));

                    if(binding.constraint[0] == '=') {
                        if(binding.constraint.length < 2) {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(binding.constraint[1] == '*') {
                            error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                            return err();
                        }

                        if(value.value.kind != ValueKind::AssignableValue) {
                            error(scope, binding.value->range, "Output binding value must be assignable");

                            return err();
                        }
                    } else if(binding.constraint[0] == '*') {
                        error(scope, inline_assembly->range, "Binding \"%.*s\" is in an invalid form", STRING_PRINTF_ARGUMENTS(binding.constraint));

                        return err();
                    } else {
                        auto determined_value_type = get_default_type(info, scope, binding.value->range, value.type);

                        if(!determined_value_type.is_runtime_type()) {
                            error(scope, binding.value->range, "Value of type '%.*s' cannot be used as a binding", STRING_PRINTF_ARGUMENTS(determined_value_type.get_description(context->arena)));

                            return err();
                        }

                        expect_void(coerce_to_type(
                            info,
                            scope,
                            context,
                            binding.value->range,
                            value.type,
                            value.value,
                            determined_value_type,
                            false
                        ));
                    }

                    TypedBinding typed_binding {};
                    typed_binding.constraint = binding.constraint;
                    typed_binding.value = value;

                    bindings[i] = typed_binding;
                }

                TypedStatement typed_statement {};
                typed_statement.kind = TypedStatementKind::InlineAssembly;
                typed_statement.range = statement->range;
                typed_statement.inline_assembly.assembly = inline_assembly->assembly;
                typed_statement.inline_assembly.bindings = Array(inline_assembly->bindings.length, bindings);

                typed_statements.append(typed_statement);
            } else {
                abort();
            }
        }
    }

    return ok((Array<TypedStatement>)typed_statements);
}

DelayedResult<TypeStaticIfResult> do_type_static_if(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    StaticIf* static_if,
    ConstantScope* scope
) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = static_if;

    expect_delayed(condition, expect_constant_expression(
        info,
        jobs,
        scope,
        &context,
        static_if->condition
    ));

    assert(context.scope_search_stack.length == 0);

    if(condition.typed_expression.type.kind != TypeKind::Boolean) {
        error(scope, static_if->condition->range, "Expected a boolean, got '%.*s'", STRING_PRINTF_ARGUMENTS(condition.typed_expression.type.get_description(arena)));

        return err();
    }

    if(condition.value.kind == ConstantValueKind::UndefConstant) {
        error(scope, static_if->condition->range, "Condition cannot be undefined");

        return err();
    }

    auto condition_value = (condition.value.unwrap_boolean());

    if(condition_value) {
        expect_void(process_scope(global_arena, jobs, scope, static_if->statements, nullptr, true));
    }

    TypeStaticIfResult result {};
    result.condition = condition.typed_expression;
    result.condition_value = condition_value;

    return ok(result);
}

profiled_function(DelayedResult<TypeFunctionDeclarationResult>, do_type_function_declaration, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    FunctionDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    global_arena,
    arena,
    declaration,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = declaration;

    auto parameter_count = declaration->parameters.length;

    auto parameters = arena->allocate<TypedFunctionParameter>(parameter_count);
    auto parameter_types = arena->allocate<AnyType>(parameter_count);
    for(size_t i = 0; i < parameter_count; i += 1) {
        assert(!declaration->parameters[i].is_constant);
        assert(!declaration->parameters[i].is_polymorphic_determiner);

        expect_delayed(type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            declaration->parameters[i].type
        ));

        if(!type.type.is_runtime_type()) {
            error(scope, declaration->parameters[i].type->range, "Function parameters cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.type.get_description(arena)));

            return err();
        }

        TypedFunctionParameter parameter {};
        parameter.name = declaration->parameters[i].name;
        parameter.type = type.typed_expression;

        parameters[i] = parameter;
        parameter_types[i] = type.type;
    }

    auto return_type_count = declaration->return_types.length;

    auto return_types = arena->allocate<TypedExpression>(return_type_count);
    auto type_return_types = arena->allocate<AnyType>(return_type_count);

    for(size_t i = 0; i < return_type_count; i += 1) {
        auto expression = declaration->return_types[i];

        expect_delayed(type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            expression
        ));

        if(!type.type.is_runtime_type()) {
            error(scope, expression->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.type.get_description(arena)));

            return err();
        }

        return_types[i] = type.typed_expression;
        type_return_types[i] = type.type;
    }

    auto is_external = false;
    Array<String> external_libraries;
    auto is_no_mangle = false;
    auto is_calling_convention_specified = false;
    auto calling_convention = CallingConvention::Default;
    for(auto tag : declaration->tags) {
        if(tag.name.text == u8"extern"_S) {
            if(is_external) {
                error(scope, tag.range, "Duplicate 'extern' tag");

                return err();
            }

            List<String> libraries(arena);

            for(size_t i = 0; i < tag.parameters.length; i += 1) {
                expect_delayed(parameter, expect_constant_expression(
                    info,
                    jobs,
                    scope,
                    &context,
                    tag.parameters[i]
                ));

                if(parameter.typed_expression.type.kind == TypeKind::ArrayTypeType) {
                    auto array = parameter.typed_expression.type.array;

                    if(
                        array.element_type->kind == TypeKind::ArrayTypeType ||
                        array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::AggregateConstant) {
                            auto aggregate_value = parameter.value.unwrap_aggregate();

                            for(auto element : aggregate_value.values) {
                                expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        } else {
                            error(scope, tag.parameters[i]->range, "Array does not have constant members");

                            return err();
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.typed_expression.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else if(parameter.typed_expression.type.kind == TypeKind::StaticArray) {
                    auto static_array = parameter.typed_expression.type.static_array;

                    if(
                        static_array.element_type->kind == TypeKind::ArrayTypeType ||
                        static_array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::UndefConstant) {
                            error(scope, tag.parameters[i]->range, "External library list cannot be undefined");

                            return err();
                        }

                        auto aggregate_value = parameter.value.unwrap_aggregate();

                        assert(static_array.length == aggregate_value.values.length);

                        for(auto element : aggregate_value.values) {
                            expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.typed_expression.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.typed_expression.type.get_description(arena)));

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
        } else if(tag.name.text == u8"call_conv"_S) {
            if(is_calling_convention_specified) {
                error(scope, tag.range, "Duplicate 'call_conv' tag");

                return err();
            }

            if(tag.parameters.length != 1) {
                error(scope, tag.range, "Expected 1 parameter, got %zu", tag.parameters.length);

                return err();
            }

            expect_delayed(parameter, expect_constant_expression(
                info,
                jobs,
                scope,
                &context,
                tag.parameters[0]
            ));

            expect(calling_convention_name, array_to_string(arena, scope, tag.parameters[0]->range, parameter.typed_expression.type, parameter.value));

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

    assert(context.scope_search_stack.length == 0);

    if(is_external && is_no_mangle) {
        error(scope, declaration->range, "External functions cannot be no_mangle");

        return err();
    }

    if(!is_external && !declaration->has_body) {
        if(is_no_mangle) {
            error(scope, declaration->range, "Function types cannot be no_mangle");

            return err();
        }

        TypeFunctionDeclarationResult result {};
        result.parameters = Array(parameter_count, parameters);
        result.return_types = Array(return_type_count, return_types);
        result.type = AnyType::create_type_type();
        result.value = AnyConstantValue(
            AnyType(FunctionTypeType(
                Array(parameter_count, parameter_types),
                Array(return_type_count, type_return_types),
                calling_convention
            ))
        );

        return ok(result);
    } else {
        auto body_scope = global_arena->allocate_and_construct<ConstantScope>();
        body_scope->scope_constants = {};
        body_scope->is_top_level = false;
        body_scope->parent = scope;

        List<ConstantScope*> child_scopes(global_arena);
        if(is_external) {
            if(declaration->has_body) {
                error(scope, declaration->range, "External functions cannot have a body");

                return err();
            }

            body_scope->statements = {};
        } else {
            body_scope->statements = declaration->statements;

            expect_void(process_scope(global_arena, jobs, body_scope, body_scope->statements, &child_scopes, false));
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

        TypeFunctionDeclarationResult result {};
        result.parameters = Array(parameter_count, parameters);
        result.return_types = Array(return_type_count, return_types);
        result.type = AnyType(FunctionTypeType(
            Array(parameter_count, parameter_types),
            Array(return_type_count, type_return_types),
            calling_convention
        ));
        result.value = AnyConstantValue(function_constant);

        return ok(result);
    }
}

profiled_function(DelayedResult<TypePolymorphicFunctionResult>, do_type_polymorphic_function, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    FunctionDeclaration* declaration,
    Array<TypedConstantValue> parameters,
    ConstantScope* scope,
    ConstantScope* call_scope,
    Array<FileRange> call_parameter_ranges
), (
    info,
    jobs,
    global_arena,
    arena,
    declaration,
    parameters,
    scope,
    call_scope,
    call_parameter_ranges
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = declaration;

    auto original_parameter_count = declaration->parameters.length;

    auto parameter_types = arena->allocate<AnyType>(original_parameter_count);

    List<ScopeConstant> polymorphic_determiners(arena);

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
                auto determined_type = get_default_type(info, call_scope, call_parameter_ranges[i], parameters[i].type);

                type = determined_type;
            }

            parameter_types[i] = type;

            ScopeConstant constant {};
            constant.name = declaration->parameters[i].polymorphic_determiner.text;
            constant.type = AnyType::create_type_type();
            constant.value = AnyConstantValue(type);

            polymorphic_determiners.append(constant);

            polymorphic_determiner_index += 1;
        }
    }

    ConstantScope signature_scope;
    signature_scope.statements = {};
    signature_scope.scope_constants = polymorphic_determiners;
    signature_scope.is_top_level = false;
    signature_scope.parent = scope;

    List<ScopeConstant> scope_constants(arena);

    for(auto polymorphic_determiner : polymorphic_determiners) {
        scope_constants.append(polymorphic_determiner);
    }

    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];
        auto call_parameter = parameters[i];

        if(declaration_parameter.is_constant) {
            if(!declaration_parameter.is_polymorphic_determiner) {
                expect_delayed(parameter_type, expect_type_expression(
                    info,
                    jobs,
                    &signature_scope,
                    &context,
                    declaration_parameter.type
                ));

                parameter_types[i] = parameter_type.type;
            }

            expect(coerced_value, coerce_to_type(
                info,
                call_scope,
                &context,
                call_parameter_ranges[i],
                call_parameter.type,
                AnyValue(call_parameter.value),
                parameter_types[i],
                false
            ));

            assert(coerced_value.kind == ValueKind::ConstantValue);

            ScopeConstant constant {};
            constant.name = declaration_parameter.name.text;
            constant.type = parameter_types[i];
            constant.value = coerced_value.constant;

            scope_constants.append(constant);
        }
    }

    signature_scope.scope_constants = scope_constants;

    auto runtime_parameter_types = arena->allocate<AnyType>(runtime_parameter_count);

    size_t runtime_parameter_index = 0;
    for(size_t i = 0; i < original_parameter_count; i += 1) {
        auto declaration_parameter = declaration->parameters[i];

        if(!declaration_parameter.is_constant) {
            if(!declaration_parameter.is_polymorphic_determiner) {
                expect_delayed(parameter_type, expect_type_expression(
                    info,
                    jobs,
                    &signature_scope,
                    &context,
                    declaration_parameter.type
                ));

                if(!parameter_type.type.is_runtime_type()) {
                    error(scope,
                        declaration_parameter.type->range,
                        "Non-constant function parameters cannot be of type '%.*s'",
                        STRING_PRINTF_ARGUMENTS(parameter_type.type.get_description(arena))
                    );

                    error(call_scope, call_parameter_ranges[i], "Polymorphic function paremter here");

                    return err();
                }

                parameter_types[i] = parameter_type.type;
            }

            runtime_parameter_types[runtime_parameter_index] = parameter_types[i];

            runtime_parameter_index += 1;
        }
    }

    assert(runtime_parameter_index == runtime_parameter_count);

    auto return_type_count = declaration->return_types.length;

    auto return_types = arena->allocate<AnyType>(return_type_count);

    for(size_t i = 0; i < return_type_count; i += 1) {
        auto expression = declaration->return_types[i];

        expect_delayed(type, expect_type_expression(
            info,
            jobs,
            &signature_scope,
            &context,
            expression
        ));

        if(!type.type.is_runtime_type()) {
            error(scope, expression->range, "Function returns cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.type.get_description(arena)));

            return err();
        }

        return_types[i] = type.type;
    }

    assert(context.scope_search_stack.length == 0);

    for(auto tag : declaration->tags) {
        if(tag.name.text == u8"extern"_S) {
            error(scope, tag.range, "Polymorphic functions cannot be external");

            return err();
        } else if(tag.name.text == u8"no_mangle"_S) {
            error(scope, tag.range, "Polymorphic functions cannot be no_mangle");

            return err();
        } else if(tag.name.text == u8"call_conv"_S) {
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

    auto body_scope = global_arena->allocate_and_construct<ConstantScope>();
    body_scope->statements = declaration->statements;
    body_scope->scope_constants = scope_constants;
    body_scope->is_top_level = false;
    body_scope->parent = scope;

    List<ConstantScope*> child_scopes(global_arena);
    expect_void(process_scope(global_arena, jobs, body_scope, body_scope->statements, &child_scopes, false));

    FunctionConstant function_constant {};
    function_constant.declaration = declaration;
    function_constant.body_scope = body_scope;
    function_constant.child_scopes = child_scopes;

    FunctionTypeType type {};
    type.parameters = Array(runtime_parameter_count, runtime_parameter_types);
    type.return_types = Array(return_type_count, return_types);
    type.calling_convention = CallingConvention::Default;

    TypePolymorphicFunctionResult result {};
    result.type = type;
    result.value = function_constant;

    return ok(result);
}

DelayedResult<TypedExpression> do_type_constant_definition(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    ConstantDefinition* definition,
    ConstantScope* scope
) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = definition;

    expect(value, expect_constant_expression(
        info,
        jobs,
        scope,
        &context,
        definition->expression
    ));

    assert(context.scope_search_stack.length == 0);

    return ok(value.typed_expression);
}

profiled_function(DelayedResult<TypeStructDefinitionResult>, do_type_struct_definition, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    StructDefinition* struct_definition,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    struct_definition,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = struct_definition;

    auto parameter_count = struct_definition->parameters.length;

    if(struct_definition->parameters.length > 0) {
        auto parameter_types = arena->allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect_delayed(type, expect_type_expression(
                info,
                jobs,
                scope,
                &context,
                struct_definition->parameters[i].type
            ));

            parameter_types[i] = type.type;
        }

        TypeStructDefinitionResult result {};
        result.type = AnyType(PolymorphicStruct(
            scope->file_path,
            struct_definition,
            parameter_types,
            scope
        ));

        return ok(result);
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = {};
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.length;

    auto members = arena->allocate<TypedStructMember>(member_count);
    auto type_members = arena->allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, expect_type_expression(
            info,
            jobs,
            &member_scope,
            &context,
            struct_definition->members[i].type
        ));

        if(!member_type.type.is_runtime_type()) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(member_type.type.get_description(arena)));

            return err();
        }

        TypedStructMember member {};
        member.name = struct_definition->members[i].name;
        member.member = member_type.typed_expression;

        members[i] = member;

        StructTypeMember type_member {};
        type_member.name = struct_definition->members[i].name.text;
        type_member.type = member_type.type;
        
        type_members[i] = type_member;
    }

    assert(context.scope_search_stack.length == 0);

    TypeStructDefinitionResult result {};
    result.members = Array(member_count, members);
    result.type = AnyType(StructType(
        scope->file_path,
        struct_definition,
        Array(member_count, type_members)
    ));

    return ok(result);
}

profiled_function(DelayedResult<StructType>, do_type_polymorphic_struct, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    StructDefinition* struct_definition,
    Array<AnyConstantValue> parameters,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    struct_definition,
    parameters,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = struct_definition;

    auto parameter_count = struct_definition->parameters.length;
    assert(parameter_count > 0);

    auto constant_parameters = arena->allocate<ScopeConstant>(parameter_count);

    for(size_t i = 0; i < parameter_count; i += 1) {
        expect_delayed(parameter_type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            struct_definition->parameters[i].type
        ));

        ScopeConstant constant {};
        constant.name = struct_definition->parameters[i].name.text;
        constant.type = parameter_type.type;
        constant.value = parameters[i];
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = Array(parameter_count, constant_parameters);
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = struct_definition->members.length;

    auto members = arena->allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, expect_type_expression(
            info,
            jobs,
            &member_scope,
            &context,
            struct_definition->members[i].type
        ));

        if(!member_type.type.is_runtime_type()) {
            error(&member_scope, struct_definition->members[i].type->range, "Struct members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(member_type.type.get_description(arena)));

            return err();
        }

        members[i] = {
            struct_definition->members[i].name.text,
            member_type.type
        };
    }

    assert(context.scope_search_stack.length == 0);

    return ok(StructType(
        scope->file_path,
        struct_definition,
        Array(member_count, members)
    ));
}

profiled_function(DelayedResult<TypeUnionDefinitionResult>, do_type_union_definition, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    UnionDefinition* union_definition,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    union_definition,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = union_definition;

    auto parameter_count = union_definition->parameters.length;

    if(union_definition->parameters.length > 0) {
        auto parameter_types = arena->allocate<AnyType>(parameter_count);

        for(size_t i = 0; i < parameter_count; i += 1) {
            expect_delayed(type, expect_type_expression(
                info,
                jobs,
                scope,
                &context,
                union_definition->parameters[i].type
            ));

            parameter_types[i] = type.type;
        }

        TypeUnionDefinitionResult result {};
        result.type = AnyType(PolymorphicUnion(
            scope->file_path,
            union_definition,
            parameter_types,
            scope
        ));

        return ok(result);
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = {};
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = union_definition->members.length;

    auto members = arena->allocate<TypedStructMember>(member_count);
    auto type_members = arena->allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, expect_type_expression(
            info,
            jobs,
            &member_scope,
            &context,
            union_definition->members[i].type
        ));

        if(!member_type.type.is_runtime_type()) {
            error(&member_scope, union_definition->members[i].type->range, "Union members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(member_type.type.get_description(arena)));

            return err();
        }

        TypedStructMember member {};
        member.name = union_definition->members[i].name;
        member.member = member_type.typed_expression;

        members[i] = member;

        StructTypeMember type_member {};
        type_member.name = union_definition->members[i].name.text;
        type_member.type = member_type.type;

        type_members[i] = type_member;
    }

    assert(context.scope_search_stack.length == 0);

    TypeUnionDefinitionResult result {};
    result.members = Array(member_count, members);
    result.type = AnyType(UnionType(
        scope->file_path,
        union_definition,
        Array(member_count, type_members)
    ));

    return ok(result);
}

profiled_function(DelayedResult<UnionType>, do_type_polymorphic_union, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    UnionDefinition* union_definition,
    Array<AnyConstantValue> parameters,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    union_definition,
    parameters,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = union_definition;

    auto parameter_count = union_definition->parameters.length;
    assert(parameter_count > 0);

    auto constant_parameters = arena->allocate<ScopeConstant>(parameter_count);

    for(size_t i = 0; i < parameter_count; i += 1) {
        expect_delayed(parameter_type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            union_definition->parameters[i].type
        ));

        constant_parameters[i] = {
            union_definition->parameters[i].name.text,
            parameter_type.type,
            parameters[i]
        };
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = Array(parameter_count, constant_parameters);
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto member_count = union_definition->members.length;

    auto members = arena->allocate<StructTypeMember>(member_count);

    for(size_t i = 0; i < member_count; i += 1) {
        expect_delayed(member_type, expect_type_expression(
            info,
            jobs,
            &member_scope,
            &context,
            union_definition->members[i].type
        ));

        if(!member_type.type.is_runtime_type()) {
            error(&member_scope, union_definition->members[i].type->range, "Union members cannot be of type '%.*s'", STRING_PRINTF_ARGUMENTS(member_type.type.get_description(arena)));

            return err();
        }

        StructTypeMember member {};
        member.name = union_definition->members[i].name.text;
        member.type = member_type.type;

        members[i] = member;
    }

    assert(context.scope_search_stack.length == 0);

    return ok(UnionType(
        scope->file_path,
        union_definition,
        Array(member_count, members)
    ));
}

profiled_function(Result<void>, process_scope, (
    Arena* global_arena,
    List<AnyJob*>* jobs,
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
                    AnyJob job {};
                    job.kind = JobKind::TypeFunctionDeclaration;
                    job.state = JobState::Working;
                    job.type_function_declaration.declaration = function_declaration;
                    job.type_function_declaration.scope = scope;

                    jobs->append(global_arena->heapify(job));
                }
            } break;

            case StatementKind::ConstantDefinition: {
                auto constant_definition = (ConstantDefinition*)statement;

                AnyJob job {};
                job.kind = JobKind::TypeConstantDefinition;
                job.state = JobState::Working;
                job.type_constant_definition.definition = constant_definition;
                job.type_constant_definition.scope = scope;

                jobs->append(global_arena->heapify(job));
            } break;

            case StatementKind::StructDefinition: {
                auto struct_definition = (StructDefinition*)statement;

                AnyJob job {};
                job.kind = JobKind::TypeStructDefinition;
                job.state = JobState::Working;
                job.type_struct_definition.definition = struct_definition;
                job.type_struct_definition.scope = scope;

                jobs->append(global_arena->heapify(job));
            } break;

            case StatementKind::UnionDefinition: {
                auto union_definition = (UnionDefinition*)statement;

                AnyJob job {};
                job.kind = JobKind::TypeUnionDefinition;
                job.state = JobState::Working;
                job.type_union_definition.definition = union_definition;
                job.type_union_definition.scope = scope;

                jobs->append(global_arena->heapify(job));
            } break;

            case StatementKind::EnumDefinition: {
                auto enum_definition = (EnumDefinition*)statement;

                AnyJob job {};
                job.kind = JobKind::TypeEnumDefinition;
                job.state = JobState::Working;
                job.type_enum_definition.definition = enum_definition;
                job.type_enum_definition.scope = scope;

                jobs->append(global_arena->heapify(job));
            } break;

            case StatementKind::VariableDeclaration: {
                if(is_top_level) {
                    auto variable_declaration = (VariableDeclaration*)statement;

                    AnyJob job {};
                    job.kind = JobKind::TypeStaticVariable;
                    job.state = JobState::Working;
                    job.type_static_variable.declaration = variable_declaration;
                    job.type_static_variable.scope = scope;

                    jobs->append(global_arena->heapify(job));
                }
            } break;

            case StatementKind::IfStatement: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto if_statement = (IfStatement*)statement;

                auto if_scope = global_arena->allocate_and_construct<ConstantScope>();
                if_scope->statements = if_statement->statements;
                if_scope->scope_constants = {};
                if_scope->is_top_level = false;
                if_scope->parent = scope;

                child_scopes->append(if_scope);

                expect_void(process_scope(global_arena, jobs, if_scope, if_statement->statements, child_scopes, false));

                for(auto else_if : if_statement->else_ifs) {
                    auto else_if_scope = global_arena->allocate_and_construct<ConstantScope>();
                    else_if_scope->statements = else_if.statements;
                    else_if_scope->scope_constants = {};
                    else_if_scope->is_top_level = false;
                    else_if_scope->parent = scope;

                    child_scopes->append(else_if_scope);

                    expect_void(process_scope(global_arena, jobs, else_if_scope, else_if.statements, child_scopes, false));
                }

                if(if_statement->else_statements.length != 0) {
                    auto else_scope = global_arena->allocate_and_construct<ConstantScope>();
                    else_scope->statements = if_statement->else_statements;
                    else_scope->scope_constants = {};
                    else_scope->is_top_level = false;
                    else_scope->parent = scope;

                    child_scopes->append(else_scope);

                    expect_void(process_scope(global_arena, jobs, else_scope, if_statement->else_statements, child_scopes, false));
                }
            } break;

            case StatementKind::WhileLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto while_loop = (WhileLoop*)statement;

                auto while_scope = global_arena->allocate_and_construct<ConstantScope>();
                while_scope->statements = while_loop->statements;
                while_scope->scope_constants = {};
                while_scope->is_top_level = false;
                while_scope->parent = scope;

                child_scopes->append(while_scope);

                expect_void(process_scope(global_arena, jobs, while_scope, while_loop->statements, child_scopes, false));
            } break;

            case StatementKind::ForLoop: {
                if(is_top_level) {
                    error(scope, statement->range, "This kind of statement cannot be top-level");

                    return err();
                }

                auto for_loop = (ForLoop*)statement;

                auto for_scope = global_arena->allocate_and_construct<ConstantScope>();
                for_scope->statements = for_loop->statements;
                for_scope->scope_constants = {};
                for_scope->is_top_level = false;
                for_scope->parent = scope;

                child_scopes->append(for_scope);

                expect_void(process_scope(global_arena, jobs, for_scope, for_loop->statements, child_scopes, false));
            } break;

            case StatementKind::Import: {
                auto import = (Import*)statement;

                auto job_already_added = false;
                for(size_t i = 0; i < jobs->length; i += 1) {
                    auto job = (*jobs)[i];

                    if(job->kind == JobKind::ParseFile) {
                        auto parse_file = job->parse_file;

                        if(parse_file.path == import->absolute_path) {
                            job_already_added = true;
                            break;
                        }
                    }
                }

                if(!job_already_added) {
                    AnyJob job {};
                    job.kind = JobKind::ParseFile;
                    job.state = JobState::Working;
                    job.parse_file.path = import->absolute_path;
                    job.parse_file.has_source = false;

                    jobs->append(global_arena->heapify(job));
                }
            } break;

            case StatementKind::UsingStatement: break;

            case StatementKind::StaticIf: {
                auto static_if = (StaticIf*)statement;

                AnyJob job {};
                job.kind = JobKind::TypeStaticIf;
                job.state = JobState::Working;
                job.type_static_if.static_if = static_if;
                job.type_static_if.scope = scope;

                jobs->append(global_arena->heapify(job));
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

profiled_function(DelayedResult<TypeEnumDefinitionResult>, do_type_enum_definition, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    EnumDefinition* enum_definition,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    enum_definition,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = enum_definition;

    TypedExpression backing_type;
    Integer type_backing_type;
    if(enum_definition->backing_type != nullptr) {
        expect(type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            enum_definition->backing_type
        ));

        if(type.type.kind != TypeKind::Integer) {
            error(
                scope,
                enum_definition->backing_type->range,
                "Expected an integer type, got '%.*s'",
                STRING_PRINTF_ARGUMENTS(type.type.get_description(arena))
            );

            return err();
        }

        backing_type = type.typed_expression;
        type_backing_type = type.type.integer;
    } else {
        type_backing_type = {};
        type_backing_type.is_signed = true;
        type_backing_type.size = info.architecture_sizes.default_integer_size;
    }

    ConstantScope member_scope;
    member_scope.statements = {};
    member_scope.scope_constants = {};
    member_scope.is_top_level = false;
    member_scope.parent = scope;

    auto variant_count = enum_definition->variants.length;

    auto variants = arena->allocate<TypedEnumVariant>(variant_count);
    auto variant_values = arena->allocate<uint64_t>(variant_count);

    uint64_t next_value = 0;
    for(size_t i = 0; i < variant_count; i += 1) {
        TypedEnumVariant variant {};
        variant.name = enum_definition->variants[i].name;

        uint64_t value;
        if(enum_definition->variants[i].value != nullptr) {
            expect_delayed(variant_value, expect_constant_expression(
                info,
                jobs,
                &member_scope,
                &context,
                enum_definition->variants[i].value
            ));

            if(variant_value.value.kind == ConstantValueKind::UndefConstant) {
                error(scope, enum_definition->variants[i].value->range, "Enum variant cannot be undefined");

                return err();
            }

            expect_void(coerce_to_integer(
                &member_scope,
                &context,
                enum_definition->variants[i].value->range,
                variant_value.typed_expression.type,
                AnyValue(variant_value.value),
                type_backing_type,
                false
            ));

            variant.has_value = true;
            variant.value = variant_value.typed_expression;
            value = variant_value.value.unwrap_integer();
        } else {
            expect_void(check_undetermined_integer_to_integer_coercion(
                scope,
                &context,
                enum_definition->variants[i].name.range,
                type_backing_type,
                next_value,
                false
            ));

            value = next_value;
        }

        variants[i] =  variant;
        variant_values[i] = value;
        next_value = value + 1;
    }

    assert(context.scope_search_stack.length == 0);

    TypeEnumDefinitionResult result {};
    result.backing_type = backing_type;
    result.variants = Array(variant_count, variants);
    result.type = Enum(
        scope->file_path,
        enum_definition,
        arena->heapify(type_backing_type),
        Array(variant_count, variant_values)
    );

    return ok(result);
}

profiled_function(DelayedResult<TypeFunctionBodyResult>, do_type_function_body, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    FunctionTypeType type,
    FunctionConstant value
), (
    info,
    jobs,
    arena,
    global_arena,
    type,
    value
)) {
    auto declaration = value.declaration;

    auto declaration_parameter_count = declaration->parameters.length;

    auto file_path = value.body_scope->get_file_path();

    auto runtime_parameter_count = type.parameters.length;

    if(value.is_external) {
        TypeFunctionBodyResult result {};

        return ok(result);
    } else {
        TypingContext context {};
        context.arena = arena;
        context.global_arena = global_arena;
        context.in_progress_variable_scope_stack.arena = arena;
        context.scope_search_stack.arena = arena;

        context.return_types = type.return_types;

        auto body_variable_scope = arena->allocate_and_construct<VariableScope>();
        body_variable_scope->parent = nullptr;
        body_variable_scope->constant_scope = value.body_scope;

        context.variable_scope = body_variable_scope;

        {
            InProgressVariableScope scope {};
            scope.variables.arena = arena;

            context.in_progress_variable_scope_stack.append(scope);
        }

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

        expect_delayed(statements, generate_runtime_statements(
            info,
            jobs,
            value.body_scope,
            &context,
            declaration->statements
        ));

        assert(context.in_progress_variable_scope_stack.length == 1);

        body_variable_scope->variables = context.in_progress_variable_scope_stack.take_last().variables;

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

        TypeFunctionBodyResult result {};
        result.scope = body_variable_scope;
        result.statements = statements;

        return ok(result);
    }
}

profiled_function(DelayedResult<TypeStaticVariableResult>, do_type_static_variable, (
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    VariableDeclaration* declaration,
    ConstantScope* scope
), (
    info,
    jobs,
    arena,
    declaration,
    scope
)) {
    TypingContext context {};
    context.arena = arena;
    context.global_arena = global_arena;
    context.scope_search_stack.arena = arena;
    context.search_ignore_statement = declaration;

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
                expect_delayed(parameter, expect_constant_expression(
                    info,
                    jobs,
                    scope,
                    &context,
                    tag.parameters[i]
                ));

                if(parameter.typed_expression.type.kind == TypeKind::ArrayTypeType) {
                    auto array = parameter.typed_expression.type.array;

                    if(
                        array.element_type->kind == TypeKind::ArrayTypeType ||
                        array.element_type->kind == TypeKind::StaticArray
                    ) {
                        if(parameter.value.kind == ConstantValueKind::ArrayConstant) {
                            error(scope, tag.parameters[i]->range, "Cannot use an array with non-constant elements in a constant context");

                            return err();
                        } else {
                            auto aggregate_value = parameter.value.unwrap_aggregate();

                            for(auto element : aggregate_value.values) {
                                expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *array.element_type, element));

                                libraries.append(library_path);
                            }
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.typed_expression.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else if(parameter.typed_expression.type.kind == TypeKind::StaticArray) {
                    auto static_array = parameter.typed_expression.type.static_array;

                    if(
                        static_array.element_type->kind == TypeKind::ArrayTypeType ||
                        static_array.element_type->kind == TypeKind::StaticArray
                    ) {
                        auto aggregate_value = parameter.value.unwrap_aggregate();

                        assert(static_array.length == aggregate_value.values.length);

                        for(auto element : aggregate_value.values) {
                            expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, *static_array.element_type, element));

                            libraries.append(library_path);
                        }
                    } else {
                        expect(library_path, array_to_string(arena, scope, tag.parameters[i]->range, parameter.typed_expression.type, parameter.value));

                        libraries.append(library_path);
                    }
                } else {
                    error(scope, tag.parameters[i]->range, "Expected a string or array of strings, got '%.*s'", STRING_PRINTF_ARGUMENTS(parameter.typed_expression.type.get_description(arena)));

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

        expect_delayed(type, expect_type_expression(
            info,
            jobs,
            scope,
            &context,
            declaration->type
        ));

        if(!type.type.is_runtime_type()) {
            error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.type.get_description(arena)));

            return err();
        }

        TypeStaticVariableResult result {};
        result.is_external = true;
        result.type = type.typed_expression;
        result.actual_type = type.type;
        result.external_libraries = external_libraries;

        return ok(result);
    } else {
        if(declaration->initializer == nullptr) {
            error(scope, declaration->range, "Variable must be initialized");

            return err();
        }

        if(declaration->type != nullptr) {
            expect_delayed(type, expect_type_expression(
                info,
                jobs,
                scope,
                &context,
                declaration->type
            ));

            if(!type.type.is_runtime_type()) {
                error(scope, declaration->type->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(type.type.get_description(arena)));

                return err();
            }

            expect_delayed(initial_value, expect_constant_expression(
                info,
                jobs,
                scope,
                &context,
                declaration->initializer
            ));

            expect(coerced_initial_value, coerce_to_type(
                info,
                scope,
                &context,
                declaration->initializer->range,
                initial_value.typed_expression.type,
                AnyValue(initial_value.value),
                type.type,
                false
            ));

            assert(coerced_initial_value.kind == ValueKind::ConstantValue);

            assert(context.scope_search_stack.length == 0);

            TypeStaticVariableResult result {};
            result.type = type.typed_expression;
            result.initializer = initial_value.typed_expression;
            result.actual_type = type.type;

            return ok(result);
        } else {
            expect_delayed(initial_value, expect_constant_expression(
                info,
                jobs,
                scope,
                &context,
                declaration->initializer
            ));

            auto determined_type = get_default_type(info, scope, declaration->initializer->range, initial_value.typed_expression.type);

            if(!determined_type.is_runtime_type()) {
                error(scope, declaration->initializer->range, "Cannot create variables of type '%.*s'", STRING_PRINTF_ARGUMENTS(determined_type.get_description(arena)));

                return err();
            }

            expect(coerced_value, coerce_to_type(
                info,
                scope,
                &context,
                declaration->initializer->range,
                initial_value.typed_expression.type,
                AnyValue(initial_value.value),
                determined_type,
                false
            ));

            assert(coerced_value.kind == ValueKind::ConstantValue);

            assert(context.scope_search_stack.length == 0);

            TypeStaticVariableResult result {};
            result.initializer = initial_value.typed_expression;
            result.actual_type = determined_type;

            return ok(result);
        }
    }
}