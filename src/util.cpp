#include "util.h"
#include <stdio.h>
#include <stdarg.h>

void error(String path, FileRange range, const char* format, va_list arguments) {
    fprintf(stderr, "Error: %.*s(%u,%u): ", STRING_PRINTF_ARGUMENTS(path), range.first_line, range.first_column);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    if(range.first_line == range.last_line) {
        Arena* arena {};

        auto file = fopen(path.to_c_string(arena), "rb");

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

            for(unsigned int i = 1; i < range.first_column - skipped_spaces; i += 1) {
                fprintf(stderr, " ");
            }

            if(range.last_column - range.first_column == 0) {
                fprintf(stderr, "^");
            } else {
                for(unsigned int i = range.first_column; i <= range.last_column; i += 1) {
                    fprintf(stderr, "-");
                }
            }

            fprintf(stderr, "\n");

            fclose(file);
        }
    }
}

void error(String path, FileRange range, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    error(path, range, format, arguments);

    va_end(arguments);
}