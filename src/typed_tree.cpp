#include "typed_tree.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

String AnyConstantValue::get_description(Arena* arena) {
    if(kind == ConstantValueKind::FunctionConstant) {
        return function.declaration->name.text;
    } else if(kind == ConstantValueKind::BuiltinFunctionConstant) {
        return builtin_function.name;
    } else if(kind == ConstantValueKind::PolymorphicFunctionConstant) {
        return polymorphic_function.declaration->name.text;
    } else if(kind == ConstantValueKind::IntegerConstant) {
        char buffer[32];
        auto length = snprintf(buffer, 32, "%" PRIi64, (int64_t)integer);

        String string {};
        string.length = (size_t)length;
        string.elements = arena->allocate<char8_t>((size_t)length);
        memcpy(string.elements, buffer, (size_t)length);

        return string;
    } else if(kind == ConstantValueKind::FloatConstant) {
        char buffer[32];
        auto length = snprintf(buffer, 32, "%f", float_);

        String string {};
        string.length = (size_t)length;
        string.elements = arena->allocate<char8_t>((size_t)length);
        memcpy(string.elements, buffer, (size_t)length);

        return string;
    } else if(kind == ConstantValueKind::BooleanConstant) {
        if(boolean) {
            return u8"true"_S;
        } else {
            return u8"false"_S;
        }
    } else if(kind == ConstantValueKind::VoidConstant) {
        return u8""_S;
    } else if(kind == ConstantValueKind::ArrayConstant) {
        StringBuffer buffer(arena);

        buffer.append(u8"{ length = "_S);
        buffer.append(array.length->get_description(arena));
        buffer.append(u8", pointer = "_S);
        buffer.append(array.pointer->get_description(arena));
        buffer.append(u8" }"_S);

        return buffer;
    } else if(kind == ConstantValueKind::AggregateConstant) {
        if(aggregate.values.length == 0) {
            return u8"{}"_S;
        }

        StringBuffer buffer(arena);

        buffer.append(u8"{ "_S);

        for(size_t i = 0; i < aggregate.values.length; i += 1) {
            buffer.append(aggregate.values[i].get_description(arena));

            if(i != aggregate.values.length - 1) {
                buffer.append(u8", "_S);
            }
        }

        buffer.append(u8" }"_S);

        return buffer;
    } else if(kind == ConstantValueKind::FileModuleConstant) {
        return file_module.scope->get_file_path();
    } else if(kind == ConstantValueKind::TypeConstant) {
        return type.get_description(arena);
    } else if(kind == ConstantValueKind::UndefConstant) {
        return u8"undef"_S;
    } else {
        abort();
    }
}

void error(ConstantScope* scope, FileRange range, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(scope->get_file_path(), range, format, arguments);

    va_end(arguments);
}