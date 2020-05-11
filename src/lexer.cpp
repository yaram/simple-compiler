#include "lexer.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "profiler.h"
#include "list.h"
#include "util.h"

static void error(const char *path, unsigned int line, unsigned int character, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "Error: %s(%u,%u): ", path, line, character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    auto file = fopen(path, "rb");

    if(file != nullptr) {
        unsigned int current_line = 1;

        while(current_line != line) {
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

        for(unsigned int i = 1; i < character - skipped_spaces; i += 1) {
            fprintf(stderr, " ");
        }

        fprintf(stderr, "^\n");

        fclose(file);
    }

    va_end(arguments);
}

void append_single_character_token(unsigned int line, unsigned int character, List<Token> *tokens, TokenType type) {
    Token token;
    token.type = type;
    token.line = line;
    token.first_character = character;
    token.last_character = character;

    append(tokens, token);
}

void append_double_character_token(unsigned int line, unsigned int first_character, List<Token> *tokens, TokenType type) {
    Token token;
    token.type = type;
    token.line = line;
    token.first_character = first_character;
    token.last_character = first_character + 1;

    append(tokens, token);
}

profiled_function(Result<Array<Token>>, tokenize_source, (const char *path), (path)) {
    enter_region("read source file");

    auto file = fopen(path, "rb");

    if(file == nullptr) {
        fprintf(stderr, "Error: Unable to read source file at '%s'\n", path);

        leave_region();

        return { false };
    }

    fseek(file, 0, SEEK_END);

    auto length = ftell(file);

    fseek(file, 0, SEEK_SET);

    auto source = allocate<char>(length + 1);

    fread(source, 1, length, file);

    source[length] = '\0';

    leave_region();

    size_t index = 0;

    unsigned int line = 1;
    unsigned int character = 1;

    List<Token> tokens{};

    while(index < length) {
        if(source[index] == ' ') {
            index += 1;

            character += 1;
        } else if(source[index] == '\r') {
            index += 1;

            if(index < length && source[index] == '\n') {
                index += 1;
            }

            line += 1;
            character = 1;
        } else if(source[index] == '\n') {
            index += 1;

            line += 1;
            character = 1;
        } else if(source[index] == '/') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index == length) {
                append_single_character_token(line, first_character, &tokens, TokenType::ForwardSlash);
            } else if(source[index] == '/') {
                index += 2;

                while(true) {
                    if(index == length) {
                        break;
                    } else if(source[index] == '\r') {
                        index += 1;

                        if(source[index] == '\n') {
                            index += 1;
                        }

                        line += 1;
                        character = 1;

                        break;
                    } else if(source[index] == '\n') {
                        index += 1;

                        line += 1;
                        character = 1;

                        break;
                    } else {
                        index += 1;
                    }
                }
            } else if(source[index] == '*') {
                index += 1;

                character += 1;

                unsigned int level = 1;

                while(level > 0) {
                    if(index == length) {
                        error(path, line, character, "Unexpected end of file");

                        return { false };
                    } else if(source[index] == '\r') {
                        index += 1;

                        if(source[index] == '\n') {
                            index += 1;
                        }

                        line += 1;
                        character = 1;
                    } else if(source[index] == '\n') {
                        index += 1;

                        line += 1;
                        character = 1;
                    } else if(source[index] == '/') {
                        index += 1;

                        character += 1;

                        if(source[index] == '*') {
                            index += 1;

                            character += 1;

                            level += 1;
                        }
                    } else if(source[index] == '*') {
                        index += 1;

                        character += 1;

                        if(source[index] == '/') {
                            index += 1;

                            character += 1;

                            level -= 1;
                        }
                    } else {
                        index += 1;

                        character += 1;
                    }
                }
            } else if(source[index + 1] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::ForwardSlashEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::ForwardSlash);
            }
        } else if(source[index] == '.') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '.') {
                append_double_character_token(line, first_character, &tokens, TokenType::DoubleDot);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Dot);
            }
        } else if(source[index] == ',') {
            append_single_character_token(line, character, &tokens, TokenType::Comma);

            index += 1;

            character += 1;
        } else if(source[index] == ':') {
            append_single_character_token(line, character, &tokens, TokenType::Colon);

            index += 1;

            character += 1;
        } else if(source[index] == ';') {
            append_single_character_token(line, character, &tokens, TokenType::Semicolon);

            index += 1;

            character += 1;
        } else if(source[index] == '+') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::PlusEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Plus);
            }
        } else if(source[index] == '-') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length) {
                switch(source[index]) {
                    case '>': {
                        append_double_character_token(line, first_character, &tokens, TokenType::Arrow);

                        index += 1;

                        character += 1;
                    } break;

                    case '=': {
                        append_double_character_token(line, first_character, &tokens, TokenType::DashEquals);

                        index += 1;

                        character += 1;
                    } break;

                    default: {
                        append_single_character_token(line, first_character, &tokens, TokenType::Dash);
                    } break;
                }
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Dash);
            }
        } else if(source[index] == '*') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::AsteriskEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Asterisk);
            }
        } else if(source[index] == '%') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::PercentEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Percent);
            }
        } else if(source[index] == '=') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::DoubleEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Equals);
            }
        } else if(source[index] == '<') {
            append_single_character_token(line, character, &tokens, TokenType::LeftArrow);

            index += 1;

            character += 1;
        } else if(source[index] == '>') {
            append_single_character_token(line, character, &tokens, TokenType::RightArrow);

            index += 1;

            character += 1;
        } else if(source[index] == '&') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '&') {
                append_double_character_token(line, first_character, &tokens, TokenType::DoubleAmpersand);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Ampersand);
            }
        } else if(source[index] == '|') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '|') {
                append_double_character_token(line, first_character, &tokens, TokenType::DoublePipe);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Pipe);
            }
        } else if(source[index] == '#') {
            append_single_character_token(line, character, &tokens, TokenType::Hash);

            index += 1;

            character += 1;
        } else if(source[index] == '!') {
            auto first_character = character;

            index += 1;

            character += 1;

            if(index != length && source[index] == '=') {
                append_double_character_token(line, first_character, &tokens, TokenType::BangEquals);

                index += 1;

                character += 1;
            } else {
                append_single_character_token(line, first_character, &tokens, TokenType::Bang);
            }
        } else if(source[index] == '$') {
            append_single_character_token(line, character, &tokens, TokenType::Dollar);

            index += 1;

            character += 1;
        } else if(source[index] == '(') {
            append_single_character_token(line, character, &tokens, TokenType::OpenRoundBracket);

            index += 1;

            character += 1;
        } else if(source[index] == ')') {
            append_single_character_token(line, character, &tokens, TokenType::CloseRoundBracket);

            index += 1;

            character += 1;
        } else if(source[index] == '{') {
            append_single_character_token(line, character, &tokens, TokenType::OpenCurlyBracket);

            index += 1;

            character += 1;
        } else if(source[index] == '}') {
            append_single_character_token(line, character, &tokens, TokenType::CloseCurlyBracket);

            index += 1;

            character += 1;
        } else if(source[index] == '[') {
            append_single_character_token(line, character, &tokens, TokenType::OpenSquareBracket);

            index += 1;

            character += 1;
        } else if(source[index] == ']') {
            append_single_character_token(line, character, &tokens, TokenType::CloseSquareBracket);

            index += 1;

            character += 1;
        } else if(source[index] == '"') {
            index += 1;

            character += 1;

            auto first_character = character;

            List<char> buffer{};

            while(true) {
                if(index == length) {
                    error(path, line, character, "Unexpected end of file");

                    return { false };
                } else if(source[index] == '\n' || source[index] == '\r') {
                    error(path, line, character, "Unexpected newline");

                    return { false };
                } else if(source[index] == '"') {
                    index += 1;

                    character += 1;

                    break;
                } else if(source[index] == '\\') {
                    index += 1;

                    character += 1;

                    if(index == length) {
                        error(path, line, character, "Unexpected end of file");

                        return { false };
                    } else if(source[index] == '\\') {
                        append(&buffer, '\\');
                    } else if(source[index] == '"') {
                        append(&buffer, '"');
                    } else if(source[index] == '0') {
                        append(&buffer, '\0');
                    } else if(source[index] == 'r') {
                        append(&buffer, '\r');
                    } else if(source[index] == 'n') {
                        append(&buffer, '\n');
                    } else if(source[index] == 'r' || source[index] == '\n') {
                        error(path, line, character, "Unexpected newline");

                        return { false };
                    } else {
                        error(path, line, character, "Unknown escape code '\\%c'", source[index]);

                        return { false };
                    }

                    index += 1;

                    character += 1;
                } else {
                    append(&buffer, source[index]);

                    index += 1;

                    character += 1;
                }
            }

            Token token;
            token.type = TokenType::String;
            token.line = line;
            token.first_character = first_character;
            token.last_character = character - 2;
            token.string = to_array(buffer);

            append(&tokens, token);
        } else if(
            (source[index] >= 'a' && source[index] <= 'z') ||
            (source[index] >= 'A' && source[index] <= 'Z') ||
            source[index] == '_'
        ) {
            List<char> buffer{};

            append(&buffer, source[index]);

            auto first_character = character;

            index += 1;

            character += 1;

            while(
                index < length &&
                (
                    (source[index] >= 'a' && source[index] <= 'z') ||
                    (source[index] >= 'A' && source[index] <= 'Z') ||
                    (source[index] >= '0' && source[index] <= '9') ||
                    source[index] == '_'
                )
            ) {
                append(&buffer, source[index]);

                index += 1;

                character += 1;
            }

            append(&buffer, '\0');

            Token token;
            token.type = TokenType::Identifier;
            token.line = line;
            token.first_character = first_character;
            token.last_character = character - 1;
            token.identifier = buffer.elements;

            append(&tokens, token);
        } else if((source[index] >= '0' && source[index] <= '9') || source[index] == '.') {
            size_t radix = 10;

            auto definitely_integer = false;
            auto definitely_float = false;
            auto seen_dot = false;
            auto seen_e = false;

            if(source[index] == '.') {
                definitely_float = true;
            } else if(source[index] == '0' && index + 1 < length) {
                if(source[index + 1] == 'b' || source[index + 1] == 'B') {
                    definitely_integer = true;

                    index += 2;

                    character += 2;

                    radix = 2;
                } else if(source[index + 1] == 'o' || source[index + 1] == 'O') {
                    definitely_integer = true;

                    index += 2;

                    character += 2;

                    radix = 8;
                } else if(source[index + 1] == 'x' || source[index + 1] == 'X') {
                    definitely_integer = true;

                    index += 2;

                    character += 2;

                    radix = 16;
                }
            }

            auto first_index = index;
            auto first_character = character;

            index += 1;

            character += 1;

            while(index < length) {
                if(source[index] == '.' && (!definitely_integer && !seen_dot && !seen_e)) {
                    // Not quite happy about this
                    if(index + 1 < length && source[index + 1] == '.') {
                        break;
                    }

                    definitely_float = true;
                    seen_dot = true;

                    index += 1;

                    character += 1;
                } else if(source[index] >= '0' && source[index] <= '7') {
                    index += 1;

                    character += 1;
                } else if(source[index] >= '8' && source[index] <= '9' && radix >= 10) {
                    index += 1;

                    character += 1;
                } else if((source[index] == 'e' || source[index] == 'E') && (!definitely_integer && !seen_e)) {
                    definitely_float = true;

                    seen_e = true;

                    index += 1;

                    character += 1;
                } else if(
                    (
                        (source[index] >= 'a' && source[index] <= 'f' && radix == 16) ||
                        (source[index] >= 'A' && source[index] <= 'F' && radix == 16)
                    ) &&
                    definitely_integer
                ) {
                    index += 1;

                    character += 1;
                } else {
                    break;
                }
            }

            auto count = index - first_index;

            Token token;
            token.line = line;
            token.first_character = first_character;
            token.last_character = character - 1;

            if(definitely_integer || !definitely_float) {
                uint64_t value = 0;

                uint64_t place_offset = 1;

                for(size_t i = 0; i < count; i += 1) {
                    auto offset = count - 1 - i;
                    auto digit = source[first_index + offset];

                    uint64_t digit_value;
                    if((digit >= '0' && digit <= '7') || (digit >= '8' && digit <= '9' && radix >= 10)) {
                        digit_value = digit - '0';
                    } else if(digit >= 'a' && digit <= 'f' && radix == 16) {
                        digit_value = digit - 'a' + 10;
                    } else if(digit >= 'A' && digit <= 'F' && radix == 16) {
                        digit_value = digit - 'A' + 10;
                    } else {
                        abort();
                    }

                    value += place_offset * digit_value;
                    place_offset *= radix;
                }

                token.type = TokenType::Integer;
                token.integer = value;

                append(&tokens, token);
            } else {
                auto buffer = allocate<char>(count + 1);

                for(size_t i = 0; i < count; i += 1) {
                    buffer[i] = source[first_index + i];
                }

                buffer[count] = '\0';

                token.type = TokenType::FloatingPoint;
                token.floating_point = atof(buffer);

                append(&tokens, token);
            }
        } else {
            error(path, line, character, "Unexpected character '%c'", source[index]);

            return { false };
        }
    }

    return {
        true,
        to_array(tokens)
    };
}