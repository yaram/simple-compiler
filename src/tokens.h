#pragma once

#include <stdint.h>
#include "string.h"

enum struct TokenKind {
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
    DoubleLeftArrow,
    RightArrow,
    DoubleRightArrow,
    Ampersand,
    DoubleAmpersand,
    At,
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
    TokenKind kind;

    unsigned int line;
    unsigned int first_column;
    unsigned int last_column;

    union {
        String identifier;

        String string;

        uint64_t integer;

        double floating_point;
    };

    void print();
    String get_text();
};