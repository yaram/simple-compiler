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

void string_buffer_append(StringBuffer *string_buffer, size_t number) {
    const size_t buffer_size = 32;
    char buffer[buffer_size];
    snprintf(buffer, buffer_size, "%zu", number);

    string_buffer_append(string_buffer, (const char*)buffer);
}

void string_buffer_append_character(StringBuffer *string_buffer, char character) {
    char buffer[2];
    buffer[0] = character;
    buffer[1] = 0;

    string_buffer_append(string_buffer, (const char*)buffer);
}