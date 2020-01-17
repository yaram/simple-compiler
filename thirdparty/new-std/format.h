#pragma once

#include <file.h>
#include <string.h>

void format_print(File file, String format_string) {
    write_to_file(file, { format_string.length, format_string.bytes });
}

template <typename T, typename ... Ts>
void format_print(File file, String format_string, T value, Ts ... values) {
    usize index = 0;

    while(true) {
        if(index >= format_string.length) {
            return;
        }

        auto byte = format_string[index];

        if(byte == '%') {
            print(file, value);

            index += 1;

            format_print(file, substring(format_string, index, format_string.length - index), values...);

            return;
        } else {
            write_to_file(file, { 1, &byte });
        }

        index += 1;
    }
}

template <typename T>
void print_integer(File file, T integer, usize radix) {
    const auto integer_digits = "0123456789ABCDEF";

    const usize buffer_length = 64;
    u8 buffer[buffer_length];

    usize length = 0;

    bool is_negative;
    if(integer < 0) {
        is_negative = true;

        integer = -integer;
    } else {
        is_negative = false;
    }

    while(integer != 0) {
        auto digit_value = integer % radix;

        buffer[buffer_length - 1 - length] = integer_digits[digit_value];

        length += 1;
        integer /= radix;
    }

    if(length == 0) {
        buffer[buffer_length - 1] = '0';

        length = 1;
    } else if(is_negative) {
        buffer[buffer_length - length - 1] = '-';

        length += 1;
    }

    write_to_file(file, { length, buffer + buffer_length - length });
}

void print(File file, u8 integer) {
    print_integer(file, integer, 10);
}

void print(File file, u16 integer) {
    print_integer(file, integer, 10);
}

void print(File file, u32 integer) {
    print_integer(file, integer, 10);
}

void print(File file, u64 integer) {
    print_integer(file, integer, 10);
}

void print(File file, i8 integer) {
    print_integer(file, integer, 10);
}

void print(File file, i16 integer) {
    print_integer(file, integer, 10);
}

void print(File file, i32 integer) {
    print_integer(file, integer, 10);
}

void print(File file, i64 integer) {
    print_integer(file, integer, 10);
}

void print(File file, String string) {
    write_to_file(file, { string.length, string.bytes });
}

template <typename T>
void print(File file, Array<T> array) {
    print(file, "{"_S);

    for(usize i = 0; i < array.length; i += 1) {
        print(file, array[i]);

        if(i != (array.length - 1)) {
            print(file, ", "_S);
        }
    }

    print(file, "}"_S);
}

template <typename T>
void print(File file, T* pointer) {
    print(file, "&"_S);

    print(file, *pointer);

    print(file, "(0x"_S);

    print_integer(file, (usize)pointer, 16);

    print(file, ")"_S);
}