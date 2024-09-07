#include <string.h>
#include "string.h"
#include "util.h"
#include "profiler.h"

const String String::from_c_string(const char* c_string) {
    String string {};
    string.length = strlen(c_string);
    string.elements = (char*)c_string;
    return string;
}

char* String::to_c_string() {
    auto c_string = allocate<char>(length + 1);

    memcpy(c_string, elements, length);
    c_string[length] = '\0';

    return c_string;
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

        elements = (char*)malloc(capacity);
    } else {
        auto new_length = length + string.length;

        if(new_length > capacity) {
            auto new_capacity = new_length + minimum_allocation;

            auto new_elements = (char*)realloc(elements, new_capacity);

            capacity = new_capacity;
            elements = new_elements;
        }
    }

    memcpy(&elements[length], string.elements, string.length);

    length += string.length;
}

/*profiled_function_void(StringBuffer::append, (const char* c_string), (c_string)) {
    String string {};
    string.elements = (char*)c_string;
    string.length = strlen(c_string);

    append(string);
}*/

static void int_to_string_buffer(char buffer[32], size_t* length, size_t value, size_t radix) {
    if(value == 0) {
        buffer[0] = '0';

        *length = 1;

        return;
    }

    size_t index = 0;

    while(value > 0) {
        auto digit_value = value % radix;

        if(digit_value < 10){
            buffer[index] = (char)('0' + digit_value);
        } else {
            buffer[index] = (char)('A' + (digit_value - 10));
        }

        value = value / radix;
        index += 1;
    }

    *length = index;

    auto half_length = (*length - 1) / 2 + 1;

    for(size_t i = 0; i < half_length; i += 1) {
        auto temp = buffer[i];

        buffer[i] = buffer[*length - 1 - i];
        buffer[*length - 1 - i] = temp;
    }
}

void StringBuffer::append_integer(size_t number) {
    char buffer[32];
    size_t length;
    int_to_string_buffer(buffer, &length, number, 10);

    String string {};
    string.length = length;
    string.elements = buffer;

    append(string);
}

void StringBuffer::append_character(char character) {
    String string {};
    string.length = 1;
    string.elements = &character;

    append(string);
}