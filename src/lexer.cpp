#include "lexer.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "profiler.h"
#include "list.h"
#include "util.h"

static void error(String path, unsigned int line, unsigned int column, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    FileRange range {};
    range.first_line = line;
    range.first_column = column;
    range.last_line = line;
    range.last_column = column;

    error(path, range, format, arguments);

    va_end(arguments);
}

void append_single_character_token(unsigned int line, unsigned int column, List<Token>* tokens, TokenKind type) {
    Token token;
    token.kind = type;
    token.line = line;
    token.first_column = column;
    token.last_column = column;

    tokens->append(token);
}

void append_double_character_token(unsigned int line, unsigned int first_column, List<Token>* tokens, TokenKind type) {
    Token token;
    token.kind = type;
    token.line = line;
    token.first_column = first_column;
    token.last_column = first_column + 1;

    tokens->append(token);
}

namespace {
    struct Lexer {
        Arena* arena;

        String path;

        size_t length;
        uint8_t* source;

        size_t index;

        unsigned int line;
        unsigned int column;

        inline Result<char32_t> get_current_character() {
            assert(index < length);

            auto temp_index = index;

            auto first_byte = source[temp_index];
            temp_index += 1;

            if(first_byte >> 7 == 0) {
                // Definitely single-byte

                return ok((char32_t)first_byte);
            }

            // Definitely at least 2 bytes

            if(first_byte >> 6 != 0b11) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            if(temp_index == length) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            auto second_byte = source[temp_index];
            temp_index += 1;

            if(second_byte >> 6 != 0b10) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            if(first_byte >> 5 == 0b110) {
                // Definitely 2 byte

                auto codepoint = (((uint32_t)first_byte & 0b11111) << 6) | ((uint32_t)second_byte & 0b111111);

                if(codepoint < 0x0080) {
                    error(path, line, column, "Invalid UTF-8 byte sequence");
                    return err();
                }

                return ok((char32_t)codepoint);
            }

            // Definitely at least 3 bytes

            if(temp_index == length) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            auto third_byte = source[temp_index];
            temp_index += 1;

            if(third_byte >> 6 != 0b10) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
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
                    error(path, line, column, "Invalid UTF-8 byte sequence");
                    return err();
                }

                return ok((char32_t)codepoint);
            }

            // Definitely 4 byte

            if(first_byte >> 3 != 0b11110) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            if(temp_index == length) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            auto fourth_byte = source[temp_index];
            temp_index += 1;

            if(fourth_byte >> 6 != 0b10) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            auto codepoint =
                (((uint32_t)first_byte & 0b111) << 18) |
                (((uint32_t)second_byte & 0b111111) << 12) |
                (((uint32_t)third_byte & 0b111111) << 6) |
                ((uint32_t)fourth_byte & 0b111111)
            ;

            if(codepoint < 0x010000) {
                error(path, line, column, "Invalid UTF-8 byte sequence");
                return err();
            }

            return ok((char32_t)codepoint);
        }

        inline void consume_current_character() {
            auto first_byte = source[index];

            if(first_byte >> 7 == 0) {
                index += 1;
                column += 1;
            } else if(first_byte >> 5 == 0b110) {
                index += 2;
                column += 2;
            } else if(first_byte >> 4 == 0b1110) {
                index += 3;
                column += 3;
            } else {
                index += 4;
                column += 4;
            }
        }

        Result<Array<Token>> tokenize() {
            List<Token> tokens(arena);

            while(index < length) {
                expect(character, get_current_character());

                if(character == ' ') {
                    consume_current_character();
                } else if(character == '\r') {
                    consume_current_character();

                    if(index < length) {
                        expect(character, get_current_character());

                        if(character == '\n') {
                            consume_current_character();
                        }
                    }

                    line += 1;
                    column = 1;
                } else if(character == '\n') {
                    consume_current_character();

                    line += 1;
                    column = 1;
                } else if(character == '/') {
                    auto first_column = column;

                    consume_current_character();

                    expect(character, get_current_character());

                    if(index == length) {
                        append_single_character_token(line, first_column, &tokens, TokenKind::ForwardSlash);
                    } else if(character == '/') {
                        consume_current_character();

                        while(index < length) {
                            expect(character, get_current_character());

                            if(character == '\r') {
                                consume_current_character();

                                if(character == '\n') {
                                    consume_current_character();
                                }

                                line += 1;
                                column = 1;

                                break;
                            } else if(character == '\n') {
                                consume_current_character();

                                line += 1;
                                column = 1;

                                break;
                            } else {
                                consume_current_character();
                            }
                        }
                    } else if(character == '*') {
                        consume_current_character();

                        unsigned int level = 1;

                        while(level > 0) {
                            expect(character, get_current_character());

                            if(index == length) {
                                error(path, line, column, "Unexpected end of file");

                                return err();
                            } else if(character == '\r') {
                                consume_current_character();

                                if(index < length) {
                                    expect(character, get_current_character());

                                    if(character == '\n') {
                                        consume_current_character();
                                    }
                                }

                                line += 1;
                                column = 1;
                            } else if(character == '\n') {
                                consume_current_character();

                                line += 1;
                                column = 1;
                            } else if(character == '/') {
                                consume_current_character();

                                if(index < length) {
                                    expect(character, get_current_character());

                                    if(character == '*') {
                                        consume_current_character();

                                        level += 1;
                                    }
                                }
                            } else if(character == '*') {
                                consume_current_character();

                                if(index < length) {
                                    expect(character, get_current_character());

                                    if(character == '/') {
                                        consume_current_character();

                                        level -= 1;
                                    }
                                }
                            } else {
                                consume_current_character();
                            }
                        }
                    } else if(character == '=') {
                        append_double_character_token(line, first_column, &tokens, TokenKind::ForwardSlashEquals);

                        consume_current_character();
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::ForwardSlash);
                    }
                } else if(character == '.') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '.') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoubleDot);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Dot);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Dot);
                    }
                } else if(character == ',') {
                    append_single_character_token(line, column, &tokens, TokenKind::Comma);

                    consume_current_character();
                } else if(character == ':') {
                    append_single_character_token(line, column, &tokens, TokenKind::Colon);

                    consume_current_character();
                } else if(character == ';') {
                    append_single_character_token(line, column, &tokens, TokenKind::Semicolon);

                    consume_current_character();
                } else if(character == '+') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '=') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::PlusEquals);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Plus);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Plus);
                    }
                } else if(character == '-') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        switch(character) {
                            case '>': {
                                append_double_character_token(line, first_column, &tokens, TokenKind::Arrow);

                                consume_current_character();
                            } break;

                            case '=': {
                                append_double_character_token(line, first_column, &tokens, TokenKind::DashEquals);

                                consume_current_character();
                            } break;

                            default: {
                                append_single_character_token(line, first_column, &tokens, TokenKind::Dash);
                            } break;
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Dash);
                    }
                } else if(character == '*') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '=') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::AsteriskEquals);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Asterisk);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Asterisk);
                    }
                } else if(character == '%') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '=') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::PercentEquals);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Percent);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Percent);
                    }
                } else if(character == '=') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '=') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoubleEquals);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Equals);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Equals);
                    }
                } else if(character == '<') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '<') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoubleLeftArrow);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, column, &tokens, TokenKind::LeftArrow);
                        }
                    } else {
                        append_single_character_token(line, column, &tokens, TokenKind::LeftArrow);
                    }
                } else if(character == '>') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '>') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoubleRightArrow);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, column, &tokens, TokenKind::RightArrow);
                        }
                    } else {
                        append_single_character_token(line, column, &tokens, TokenKind::RightArrow);
                    }
                } else if(character == '&') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '&') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoubleAmpersand);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Ampersand);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Ampersand);
                    }
                } else if(character == '@') {
                    append_single_character_token(line, column, &tokens, TokenKind::At);

                    consume_current_character();
                } else if(character == '|') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '|') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::DoublePipe);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Pipe);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Pipe);
                    }
                } else if(character == '#') {
                    append_single_character_token(line, column, &tokens, TokenKind::Hash);

                    consume_current_character();
                } else if(character == '!') {
                    auto first_column = column;

                    consume_current_character();

                    if(index != length) {
                        expect(character, get_current_character());

                        if(character == '=') {
                            append_double_character_token(line, first_column, &tokens, TokenKind::BangEquals);

                            consume_current_character();
                        } else {
                            append_single_character_token(line, first_column, &tokens, TokenKind::Bang);
                        }
                    } else {
                        append_single_character_token(line, first_column, &tokens, TokenKind::Bang);
                    }
                } else if(character == '$') {
                    append_single_character_token(line, column, &tokens, TokenKind::Dollar);

                    consume_current_character();
                } else if(character == '(') {
                    append_single_character_token(line, column, &tokens, TokenKind::OpenRoundBracket);

                    consume_current_character();
                } else if(character == ')') {
                    append_single_character_token(line, column, &tokens, TokenKind::CloseRoundBracket);

                    consume_current_character();
                } else if(character == '{') {
                    append_single_character_token(line, column, &tokens, TokenKind::OpenCurlyBracket);

                    consume_current_character();
                } else if(character == '}') {
                    append_single_character_token(line, column, &tokens, TokenKind::CloseCurlyBracket);

                    consume_current_character();
                } else if(character == '[') {
                    append_single_character_token(line, column, &tokens, TokenKind::OpenSquareBracket);

                    consume_current_character();
                } else if(character == ']') {
                    append_single_character_token(line, column, &tokens, TokenKind::CloseSquareBracket);

                    consume_current_character();
                } else if(character == '"') {
                    consume_current_character();

                    auto first_column = column;

                    StringBuffer buffer(arena);

                    while(true) {
                        if(index == length) {
                            error(path, line, column, "Unexpected end of file");

                            return err();
                        }

                        expect(character, get_current_character());

                        if(character == '\n' || character == '\r') {
                            error(path, line, column, "Unexpected newline");

                            return err();
                        } else if(character == '"') {
                            consume_current_character();

                            break;
                        } else if(character == '\\') {
                            consume_current_character();

                            if(index == length) {
                                error(path, line, column, "Unexpected end of file");

                                return err();
                            }

                            expect(character, get_current_character());

                            if(character== '\\') {
                                buffer.append_character('\\');
                            } else if(character == '"') {
                                buffer.append_character('"');
                            } else if(character == '0') {
                                buffer.append_character('\0');
                            } else if(character == 'r') {
                                buffer.append_character('\r');
                            } else if(character == 'n') {
                                buffer.append_character('\n');
                            } else if(character == '\r' || character == '\n') {
                                error(path, line, column, "Unexpected newline");

                                return err();
                            } else {
                                StringBuffer buffer(arena);
                                buffer.append_character(character);

                                error(path, line, column, "Unknown escape code '\\%.*s'", STRING_PRINTF_ARGUMENTS(buffer));

                                return err();
                            }

                            consume_current_character();
                        } else {
                            buffer.append_character(character);

                            consume_current_character();
                        }
                    }

                    Token token;
                    token.kind = TokenKind::String;
                    token.line = line;
                    token.first_column = first_column;
                    token.last_column = column - 2;
                    token.string = buffer;

                    tokens.append(token);
                } else if(
                    (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z') ||
                    character == '_'
                ) {
                    StringBuffer buffer(arena);

                    buffer.append_character(character);

                    auto first_column = column;

                    consume_current_character();

                    while(index < length) {
                        expect(character, get_current_character());

                        if(
                            (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '_'
                        ) {
                            buffer.append_character(character);

                            consume_current_character();
                        } else {
                            break;
                        }
                    }

                    Token token;
                    token.kind = TokenKind::Identifier;
                    token.line = line;
                    token.first_column = first_column;
                    token.last_column = column - 1;
                    token.identifier = buffer;

                    tokens.append(token);
                } else if((character >= '0' && character <= '9') || character == '.') {
                    size_t radix = 10;

                    auto first_column = column;

                    auto definitely_integer = false;
                    auto definitely_float = false;
                    auto seen_dot = false;
                    auto seen_e = false;

                    if(character == '.') {
                        definitely_float = true;
                    } else if(character == '0' && index < length) {
                        consume_current_character();

                        expect(character, get_current_character());

                        if(character == 'b' || character == 'B') {
                            definitely_integer = true;

                            consume_current_character();

                            radix = 2;
                        } else if(character == 'o' || character == 'O') {
                            definitely_integer = true;

                            consume_current_character();

                            radix = 8;
                        } else if(character == 'x' || character == 'X') {
                            definitely_integer = true;

                            consume_current_character();

                            radix = 16;
                        }
                    }

                    StringBuffer buffer(arena);

                    while(index < length) {
                        expect(character, get_current_character());

                        if(character == '.' && (!definitely_integer && !seen_dot && !seen_e)) {
                            // Not quite happy about this, 2 character lookahead required here to differentiate . and ..

                            auto original_index = index;

                            consume_current_character();

                            if(index < length) {
                                expect(character, get_current_character());

                                if(character == '.') {
                                    index = original_index;

                                    break;
                                }
                            }

                            definitely_float = true;
                            seen_dot = true;
                        } else if(character >= '0' && character <= '7') {
                            consume_current_character();
                        } else if(character >= '8' && character <= '9' && radix >= 10) {
                            consume_current_character();
                        } else if((character == 'e' || character == 'E') && (!definitely_integer && !seen_e)) {
                            definitely_float = true;

                            seen_e = true;

                            consume_current_character();

                            expect(next_character, get_current_character());

                            if(next_character == '-') {
                                buffer.append_character(character);

                                consume_current_character();

                                character = next_character;
                            }
                        } else if(
                            (
                                (character >= 'a' && character <= 'f' && radix == 16) ||
                                (character >= 'A' && character <= 'F' && radix == 16)
                            ) &&
                            definitely_integer
                        ) {
                            consume_current_character();
                        } else {
                            break;
                        }

                        buffer.append_character(character);
                    }

                    Token token;
                    token.line = line;
                    token.first_column = first_column;
                    token.last_column = column - 1;

                    if(definitely_integer || !definitely_float) {
                        uint64_t value = 0;

                        uint64_t place_offset = 1;

                        for(size_t i = 0; i < buffer.length; i += 1) {
                            auto offset = buffer.length - 1 - i;
                            auto digit = buffer[offset];

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

                        token.kind = TokenKind::Integer;
                        token.integer = value;

                        tokens.append(token);
                    } else {
                        token.kind = TokenKind::FloatingPoint;
                        token.floating_point = atof(buffer.to_c_string(arena));

                        tokens.append(token);
                    }
                } else {
                    StringBuffer buffer(arena);
                    buffer.append_character(character);

                    error(path, line, column, "Unexpected character '%.*s'", STRING_PRINTF_ARGUMENTS(buffer));

                    return err();
                }
            }

            return ok((Array<Token>)tokens);
        }
    };
};

profiled_function(Result<Array<Token>>, tokenize_source, (Arena* arena, String path), (path)) {
    enter_region("read source file");

    auto file = fopen(path.to_c_string(arena), "rb");

    if(file == nullptr) {
        fprintf(stderr, "Error: Unable to read source file at '%.*s'\n", STRING_PRINTF_ARGUMENTS(path));

        leave_region();

        return err();
    }

    fseek(file, 0, SEEK_END);

    auto signed_length = ftell(file);

    if(signed_length == -1) {
        fprintf(stderr, "Error: Unable to determine length of source file at '%.*s'\n", STRING_PRINTF_ARGUMENTS(path));

        leave_region();

        return err();
    }

    auto length = (size_t)signed_length;

    fseek(file, 0, SEEK_SET);

    auto source = arena->allocate<uint8_t>(length);

    if(fread(source, length, 1, file) != 1) {
        fprintf(stderr, "Error: Unable to read source file at '%.*s'\n", STRING_PRINTF_ARGUMENTS(path));

        leave_region();

        return err();
    }

    fclose(file);

    leave_region();

    return tokenize_source(arena, path, Array(length, source));
}

profiled_function(Result<Array<Token>>, tokenize_source, (Arena* arena, String path, Array<uint8_t> source), (path)) {
    Lexer lexer {};
    lexer.arena = arena;
    lexer.path = path;

    lexer.source = source.elements;
    lexer.length = source.length;

    lexer.index = 0;

    lexer.line = 1;
    lexer.column = 1;

    return lexer.tokenize();
}