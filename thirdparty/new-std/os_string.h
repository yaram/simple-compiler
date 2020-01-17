#pragma once

#include <string.h>
#include <platform.h>
#include <result.h>

#if defined(OS_WINDOWS)
#include <Windows.h>

using OSString = WCHAR*;

template <typename A>
Result<OSString> to_os_string(String string, A &allocator) {
    usize length = 0;

    {
        usize index = 0;
        while(1) {
            if(index == string.length) {
                break;
            }

            auto byte = string[index];

            u32 code_point;

            if((byte & 0b10000000) == 0b00000000) {
                code_point = byte & 0b01111111;

                index += 1;
            } else if((byte & 0b11100000) == 0b11000000) {
                code_point = (byte & 0b00011111) << 6;
                code_point |= string[index + 1] & 0b00111111;

                index += 2;
            } else if((byte & 0b11110000) == 0b11100000) {
                code_point = (byte & 0b00001111) << 12;
                code_point |= (string[index + 1] & 0b00111111) << 6;
                code_point |= string[index + 2] & 0b00111111;

                index += 3;
            } else if((byte & 0b11111000) == 0b11110000) {
                code_point = (byte & 0b00000111) << 18;
                code_point |= (string[index + 1] & 0b00111111) << 12;
                code_point |= (string[index + 2] & 0b00111111) << 6;
                code_point |= string[index + 3] & 0b00111111;

                index += 4;
            }

            if(code_point < 0x10000) {
                length += 1;
            } else {
                length += 2;
            }
        }
    }

    auto buffer = (WCHAR*)allocate(allocator, (length + 1) * sizeof(WCHAR));

    if(buffer == nullptr) {
        return err<OSString>();
    }

    usize string_index = 0;
    usize os_string_index = 0;
    while(1) {
        if(string_index == string.length) {
            buffer[os_string_index] = (WCHAR)'\0';

            return ok<OSString>(buffer);
        }

        auto byte = string[string_index];

        u32 code_point;

        if((byte & 0b10000000) == 0b00000000) {
            code_point = byte & 0b01111111;

            string_index += 1;
        } else if((byte & 0b11100000) == 0b11000000) {
            code_point = (byte & 0b00011111) << 6;
            code_point |= string[string_index + 1] & 0b00111111;

            string_index += 2;
        } else if((byte & 0b11110000) == 0b11100000) {
            code_point = (byte & 0b00001111) << 12;
            code_point |= (string[string_index + 1] & 0b00111111) << 6;
            code_point |= string[string_index + 2] & 0b00111111;

            string_index += 3;
        } else if((byte & 0b11111000) == 0b11110000) {
            code_point = (byte & 0b00000111) << 18;
            code_point |= (string[string_index + 1] & 0b00111111) << 12;
            code_point |= (string[string_index + 2] & 0b00111111) << 6;
            code_point |= string[string_index + 3] & 0b00111111;

            string_index += 4;
        }

        if(code_point < 0x10000) {
            buffer[os_string_index] = (WCHAR)code_point;

            os_string_index += 1;
        } else {
            code_point -= 0x10000;

            auto low_bits = code_point & 0b1111111111;
            auto high_bits = (code_point & 0b11111111110000000000) >> 10;

            buffer[os_string_index] = (WCHAR)(0xD800 + high_bits);
            buffer[os_string_index + 1] = (WCHAR)(0xDC00 + low_bits);

            os_string_index += 2;
        }
    }
}

usize length(OSString os_string) {
    usize length = 0;

    while(os_string[length] != (WCHAR)'\0') {
        length += 1;
    }

    return length;
}
#endif