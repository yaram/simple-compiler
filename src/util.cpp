#include "util.h"
#include <string.h>
#include <stdio.h>

void string_buffer_append(StringBuffer *string_buffer, const char *string) {
    const size_t minimum_allocation = 64;

    auto string_length = strlen(string);

    if(string_buffer->capacity == 0) {
        auto capacity = string_length + minimum_allocation;

        auto data = (char*)malloc(capacity + 1);

        string_buffer->capacity = capacity;
        string_buffer->data = data;
    } else {
        auto new_length = string_buffer->length + string_length;

        if(new_length > string_buffer->capacity) {
            auto new_capacity = new_length + minimum_allocation;

            auto new_data = (char*)realloc(string_buffer->data, new_capacity + 1);

            string_buffer->capacity = new_capacity;
            string_buffer->data = new_data;
        }
    }

    memcpy(&string_buffer->data[string_buffer->length], string, string_length + 1);

    string_buffer->length += string_length;
}

static void int_to_string(char buffer[32], size_t value, size_t radix) {
    if(value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';

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

    auto length = index;

    auto half_length = (length - 1) / 2 + 1;

    for(size_t i = 0; i < half_length; i += 1) {
        auto temp = buffer[i];

        buffer[i] = buffer[length - 1 - i];
        buffer[length - 1 - i] = temp;
    }

    buffer[length] = '\0';
}

void string_buffer_append(StringBuffer *string_buffer, size_t number) {
    char buffer[32];
    int_to_string(buffer, number, 10);

    string_buffer_append(string_buffer, (const char*)buffer);
}

void string_buffer_append_character(StringBuffer *string_buffer, char character) {
    char buffer[2];
    buffer[0] = character;
    buffer[1] = 0;

    string_buffer_append(string_buffer, (const char*)buffer);
}