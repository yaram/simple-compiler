#pragma once

#include <stdint.h>
#include "array.h"
#include "util.h"

enum struct TokenType {
    Dot,
    DoubleDot,
    Comma,
    Colon,
    Semicolon,
    Plus,
    Dash,
    Asterisk,
    ForwardSlash,
    Percent,
    Equals,
    DoubleEquals,
    BangEquals,
    PlusEquals,
    DashEquals,
    AsteriskEquals,
    ForwardSlashEquals,
    PercentEquals,
    LeftArrow,
    RightArrow,
    Ampersand,
    DoubleAmpersand,
    Pipe,
    DoublePipe,
    Hash,
    Bang,
    Arrow,
    Dollar,
    OpenRoundBracket,
    CloseRoundBracket,
    OpenCurlyBracket,
    CloseCurlyBracket,
    OpenSquareBracket,
    CloseSquareBracket,
    Identifier,
    String,
    Integer,
    FloatingPoint
};

struct Token {
    TokenType type;

    unsigned int line;
    unsigned int first_character;
    unsigned int last_character;

    union {
        String identifier;

        Array<char> string;

        uint64_t integer;

        double floating_point;
    };
};

void print_token(Token token);
const char *get_token_text(Token token);