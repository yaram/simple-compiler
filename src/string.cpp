#include <string.h>
#include "string.h"
#include "profiler.h"

Result<void> validate_ascii_string(uint8_t* bytes, size_t length) {
    size_t index = 0;
    for(size_t i = 0; i < length; i += 1) {
        if(bytes[index] > 0x7F) {
            return err();
        }
    }

    return ok();
}

Result<void> validate_utf8_string(uint8_t* bytes, size_t length) {
    size_t index = 0;
    while(index < length) {
        auto first_byte = bytes[index];
        index += 1;

        if(first_byte >> 7 == 0) {
            // Definitely single-byte

            continue;
        }

        // Definitely at least 2 bytes

        if(first_byte >> 6 != 0b11) {
            return err();
        }

        if(index == length) {
            return err();
        }

        auto second_byte = bytes[index];
        index += 1;

        if(second_byte >> 6 != 0b10) {
            return err();
        }

        if(first_byte >> 5 == 0b110) {
            // Definitely 2 byte

            auto codepoint = (((uint32_t)first_byte & 0b11111) << 6) | ((uint32_t)second_byte & 0b111111);

            if(codepoint < 0x0080) {
                return err();
            }

            continue;
        }

        // Definitely at least 3 bytes

        if(index == length) {
            return err();
        }

        auto third_byte = bytes[index];
        index += 1;

        if(third_byte >> 6 != 0b10) {
            return err();
        }

        if(first_byte >> 4 == 0b1110) {
            // Definitely 3 byte

            auto codepoint =
                (((uint32_t)first_byte & 0b1111) << 12) |
                (((uint32_t)second_byte & 0b111111) << 6) |
                ((uint32_t)third_byte & 0b111111)
            ;

            if(codepoint < 0x0800) {
                return err();
            }

            continue;
        }

        // Definitely 4 byte

        if(first_byte >> 3 != 0b11110) {
            return err();
        }

        if(index == length) {
            return err();
        }

        auto fourth_byte = bytes[index];
        index += 1;

        if(fourth_byte >> 6 != 0b10) {
            return err();
        }

        auto codepoint =
            (((uint32_t)first_byte & 0b111) << 18) |
            (((uint32_t)second_byte & 0b111111) << 12) |
            (((uint32_t)third_byte & 0b111111) << 6) |
            ((uint32_t)fourth_byte & 0b111111)
        ;

        if(codepoint < 0x010000) {
            return err();
        }
    }

    return ok();
}

Result<size_t> validate_c_string(const char* c_string) {
    auto length = strlen(c_string);

    expect_void(validate_utf8_string((uint8_t*)c_string, length));

    return ok(length);
}

Result<String> String::from_c_string(Arena* arena, const char* c_string) {
    expect(length, validate_c_string(c_string));

    auto elements = arena->allocate<char8_t>(length);
    memcpy(elements, c_string, length);

    String string {};
    string.length = length;
    string.elements = elements;
    return ok(string);
}

char* String::to_c_string(Arena* arena) {
    auto c_string = arena->allocate<char>(length + 1);

    memcpy(c_string, elements, length);
    c_string[length] = '\0';

    return c_string;
}

String String::strip_whitespace() {
    if(length == 0) {
        return *this;
    }

    auto found_first_index = false;
    size_t first_index;
    for(size_t i = 0; i < length; i += 1) {
        auto code_unit = elements[i];

        if(code_unit != ' ' && code_unit != '\t') {
            found_first_index = true;
            first_index = i;
            break;
        }
    }

    if(!found_first_index) {
        return String::empty();
    }

    size_t last_index;
    for(size_t i = 0; i < length; i += 1) {
        auto index = length - 1 - i;
        auto code_unit = elements[index];

        if(code_unit != ' ' && code_unit != '\t') {
            last_index = i;
            break;
        }
    }

    return slice(first_index, first_index - last_index + 1);
}

bool String::operator==(String other) {
    if(length != other.length) {
        return false;
    }

    return memcmp(elements, other.elements, length) == 0;
}

bool String::operator!=(String other) {
    return !(*this == other);
}

profiled_function_void(StringBuffer::append, (String string), (string)) {
    const size_t minimum_allocation = 64;

    if(capacity == 0) {
        capacity = string.length + minimum_allocation;

        elements = arena->allocate<char8_t>(capacity);
    } else {
        auto new_length = length + string.length;

        if(new_length > capacity) {
            auto new_capacity = new_length + minimum_allocation;

            auto new_elements = arena->allocate<char8_t>(new_capacity);
            memcpy(new_elements, this->elements, capacity);

            capacity = new_capacity;
            elements = new_elements;
        }
    }

    memcpy(&elements[length], string.elements, string.length);

    length += string.length;
}

Result<void> StringBuffer::append_c_string(const char* c_string) {
    expect(length, validate_c_string(c_string));

    String string {};
    string.length = length;
    string.elements = (char8_t*)c_string;

    append(string);

    return ok();
}

static size_t int_to_chars(char8_t buffer[32], size_t value, size_t radix) {
    if(value == 0) {
        buffer[0] = '0';

        return 1;
    }

    size_t index = 0;

    while(value > 0) {
        auto digit_value = value % radix;

        if(digit_value < 10){
            buffer[index] = (char8_t)('0' + digit_value);
        } else {
            buffer[index] = (char8_t)('A' + (digit_value - 10));
        }

        value = value / radix;
        index += 1;
    }

    auto length = index;

    auto half_length = (length - 1) / 2 + 1;

    for(size_t i = 0; i < half_length; i += 1) {
        auto temp = buffer[i];

        buffer[i] = buffer[length - 1 - i];
        buffer[length - 1 - i] = temp;
    }

    return length;
}

void StringBuffer::append_integer(size_t number) {
    char8_t buffer[32];
    auto length = int_to_chars(buffer, number, 10);

    String string {};
    string.length = length;
    string.elements = buffer;

    append(string);
}

void StringBuffer::append_character(char32_t character) {
    char8_t buffer[4];

    auto codepoint = (uint32_t)character;

    String string {};
    string.elements = buffer;

    if(codepoint >> 7 == 0) {
        buffer[0] = (char8_t)codepoint;

        string.length = 1;
    } else if(codepoint >> 11 == 0) {
        buffer[0] = (char8_t)(0b11000000 | (uint8_t)(codepoint >> 6));
        buffer[1] = (char8_t)(0b10000000 | (uint8_t)(codepoint & 0b00111111));

        string.length = 2;
    } else if(codepoint >> 16 == 0) {
        buffer[0] = (char8_t)(0b11100000 | (uint8_t)(codepoint >> 12));
        buffer[1] = (char8_t)(0b10000000 | (uint8_t)((codepoint >> 6) & 0b00111111));
        buffer[2] = (char8_t)(0b10000000 | (uint8_t)(codepoint & 0b00111111));

        string.length = 3;
    } else {
        buffer[0] = (char8_t)(0b11110000 | (uint8_t)((codepoint >> 18) & 0b111));
        buffer[1] = (char8_t)(0b10000000 | (uint8_t)((codepoint >> 12) & 0b00111111));
        buffer[2] = (char8_t)(0b10000000 | (uint8_t)((codepoint >> 6) & 0b00111111));
        buffer[3] = (char8_t)(0b10000000 | (uint8_t)(codepoint & 0b00111111));

        string.length = 4;
    }

    append(string);
}