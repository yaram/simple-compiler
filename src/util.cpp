#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "profiler.h"

profiled_function_void(string_buffer_append, (StringBuffer *string_buffer, const char *string), (string_buffer, string)) {
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

void error(const char *path, FileRange range, const char *format, va_list arguments) {
    fprintf(stderr, "Error: %s(%u,%u): ", path, range.first_line, range.first_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    if(range.first_line == range.last_line) {
        auto file = fopen(path, "rb");

        if(file != nullptr) {
            unsigned int current_line = 1;

            while(current_line != range.first_line) {
                auto character = fgetc(file);

                switch(character) {
                    case '\r': {
                        auto character = fgetc(file);

                        if(character == '\n') {
                            current_line += 1;
                        } else {
                            ungetc(character, file);

                            current_line += 1;
                        }
                    } break;

                    case '\n': {
                        current_line += 1;
                    } break;

                    case EOF: {
                        fclose(file);

                        va_end(arguments);

                        return;
                    } break;
                }
            }

            unsigned int skipped_spaces = 0;
            auto done_skipping_spaces = false;

            auto done = false;
            while(!done) {
                auto character = fgetc(file);

                switch(character) {
                    case '\r':
                    case '\n': {
                        done = true;
                    } break;

                    case ' ': {
                        if(!done_skipping_spaces) {
                            skipped_spaces += 1;
                        } else {
                            fprintf(stderr, "%c", character);
                        }
                    } break;

                    case EOF: {
                        fclose(file);

                        va_end(arguments);

                        return;
                    } break;

                    default: {
                        fprintf(stderr, "%c", character);

                        done_skipping_spaces = true;
                    } break;
                }
            }

            fprintf(stderr, "\n");

            for(unsigned int i = 1; i < range.first_character - skipped_spaces; i += 1) {
                fprintf(stderr, " ");
            }

            if(range.last_character - range.first_character == 0) {
                fprintf(stderr, "^");
            } else {
                for(unsigned int i = range.first_character; i <= range.last_character; i += 1) {
                    fprintf(stderr, "-");
                }
            }

            fprintf(stderr, "\n");

            fclose(file);
        }
    }
}

void error(const char *path, FileRange range, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(path, range, format, arguments);

    va_end(arguments);
}