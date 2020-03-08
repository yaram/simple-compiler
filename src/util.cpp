#include "util.h"
#include <string.h>
#include <stdio.h>

void string_buffer_append(char **string_buffer, const char *string) {
    const size_t minumum_increment = 16;

    auto string_length = strlen(string);

    if(*string_buffer == nullptr) {
        *string_buffer = (char*)malloc(string_length + 1);

        strcpy(*string_buffer, string);
    } else {
        auto string_buffer_length = strlen(*string_buffer);

        auto new_string_buffer_length = string_buffer_length;

        if(string_length >= minumum_increment) {
            new_string_buffer_length += string_length;
        } else {
            new_string_buffer_length += minumum_increment;
        }

        auto new_string_buffer = (char*)realloc((void*)(*string_buffer), new_string_buffer_length + 1);

        strcat(new_string_buffer, string);

        *string_buffer = new_string_buffer;
    }
}

void string_buffer_append(char **string_buffer, size_t number) {
    char buffer[32];
    sprintf(buffer, "%zu", number);

    string_buffer_append(string_buffer, (const char*)buffer);
}

void string_buffer_append_character(char **string_buffer, char character) {
    char buffer[2];
    buffer[0] = character;
    buffer[1] = 0;

    string_buffer_append(string_buffer, (const char*)buffer);
}