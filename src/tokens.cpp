#include "tokens.h"
#include <stdio.h>
#include <string.h>
#include "util.h"

void print_token(Token token) {
    printf("(%d, %d-%d): ", token.line, token.first_character, token.last_character);

    switch(token.type) {
        case TokenTypeKind::Dot: {
            printf("Dot");
        } break;

        case TokenTypeKind::DoubleDot: {
            printf("DoubleDot");
        } break;

        case TokenTypeKind::Comma: {
            printf("Comma");
        } break;

        case TokenTypeKind::Colon: {
            printf("Colon");
        } break;

        case TokenTypeKind::Semicolon: {
            printf("Semicolon");
        } break;

        case TokenTypeKind::Plus: {
            printf("Plus");
        } break;

        case TokenTypeKind::Dash: {
            printf("Dash");
        } break;

        case TokenTypeKind::Asterisk: {
            printf("Asterisk");
        } break;

        case TokenTypeKind::ForwardSlash: {
            printf("ForwardSlash");
        } break;

        case TokenTypeKind::Percent: {
            printf("Percent");
        } break;

        case TokenTypeKind::Equals: {
            printf("Equals");
        } break;

        case TokenTypeKind::DoubleEquals: {
            printf("DoubleEquals");
        } break;

        case TokenTypeKind::BangEquals: {
            printf("BangEquals");
        } break;

        case TokenTypeKind::PlusEquals: {
            printf("PlusEquals");
        } break;

        case TokenTypeKind::DashEquals: {
            printf("DashEquals");
        } break;

        case TokenTypeKind::AsteriskEquals: {
            printf("AsteriskEquals");
        } break;

        case TokenTypeKind::ForwardSlashEquals: {
            printf("ForwardSlashEquals");
        } break;

        case TokenTypeKind::PercentEquals: {
            printf("PercentEquals");
        } break;

        case TokenTypeKind::LeftArrow: {
            printf("LeftArrow");
        } break;

        case TokenTypeKind::RightArrow: {
            printf("RightArrow");
        } break;

        case TokenTypeKind::Ampersand: {
            printf("Ampersand");
        } break;

        case TokenTypeKind::DoubleAmpersand: {
            printf("DoubleAmpersand");
        } break;

        case TokenTypeKind::Pipe: {
            printf("Pipe");
        } break;

        case TokenTypeKind::DoublePipe: {
            printf("DoublePipe");
        } break;

        case TokenTypeKind::Hash: {
            printf("Hash");
        } break;

        case TokenTypeKind::Bang: {
            printf("Bang");
        } break;

        case TokenTypeKind::Arrow: {
            printf("Arrow");
        } break;

        case TokenTypeKind::Dollar: {
            printf("Dollar");
        } break;

        case TokenTypeKind::OpenRoundBracket: {
            printf("OpenRoundBracket");
        } break;

        case TokenTypeKind::CloseRoundBracket: {
            printf("CloseRoundBracket");
        } break;

        case TokenTypeKind::OpenCurlyBracket: {
            printf("OpenCurlyBracket");
        } break;

        case TokenTypeKind::CloseCurlyBracket: {
            printf("CloseCurlyBracket");
        } break;

        case TokenTypeKind::OpenSquareBracket: {
            printf("OpenSquareBracket");
        } break;

        case TokenTypeKind::CloseSquareBracket: {
            printf("CloseSquareBracket");
        } break;

        case TokenTypeKind::Identifier: {
            printf("Identifier(%s)", token.identifier);
        } break;

        case TokenTypeKind::String: {
            printf("String(%.*s)", (int)token.string.count, token.string.elements);
        } break;

        case TokenTypeKind::Integer: {
            printf("Integer(%lld)", token.integer);
        } break;

        case TokenTypeKind::FloatingPoint: {
            printf("FloatingPoint(%f)", token.floating_point);
        } break;
    }
}

const char *get_token_text(Token token) {
    switch(token.type) {
        case TokenTypeKind::Dot: {
            return ".";
        } break;

        case TokenTypeKind::DoubleDot: {
            return "..";
        } break;

        case TokenTypeKind::Comma: {
            return ",";
        } break;

        case TokenTypeKind::Colon: {
            return ":";
        } break;

        case TokenTypeKind::Semicolon: {
            return ";";
        } break;

        case TokenTypeKind::Plus: {
            return "+";
        } break;

        case TokenTypeKind::Dash: {
            return "-";
        } break;

        case TokenTypeKind::Asterisk: {
            return "*";
        } break;

        case TokenTypeKind::ForwardSlash: {
            return "/";
        } break;

        case TokenTypeKind::Percent: {
            return "%";
        } break;

        case TokenTypeKind::Equals: {
            return "=";
        } break;

        case TokenTypeKind::DoubleEquals: {
            return "==";
        } break;

        case TokenTypeKind::BangEquals: {
            return "!=";
        } break;

        case TokenTypeKind::PlusEquals: {
            return "+=";
        } break;

        case TokenTypeKind::DashEquals: {
            return "-=";
        } break;

        case TokenTypeKind::AsteriskEquals: {
            return "*=";
        } break;

        case TokenTypeKind::ForwardSlashEquals: {
            return "/=";
        } break;

        case TokenTypeKind::PercentEquals: {
            return "%=";
        } break;

        case TokenTypeKind::LeftArrow: {
            return "<";
        } break;

        case TokenTypeKind::RightArrow: {
            return ">";
        } break;

        case TokenTypeKind::Ampersand: {
            return "&";
        } break;

        case TokenTypeKind::DoubleAmpersand: {
            return "&&";
        } break;

        case TokenTypeKind::Pipe: {
            return "|";
        } break;

        case TokenTypeKind::DoublePipe: {
            return "||";
        } break;

        case TokenTypeKind::Hash: {
            return "#";
        } break;

        case TokenTypeKind::Bang: {
            return "!";
        } break;

        case TokenTypeKind::Arrow: {
            return "->";
        } break;

        case TokenTypeKind::Dollar: {
            return "$";
        } break;

        case TokenTypeKind::OpenRoundBracket: {
            return "(";
        } break;

        case TokenTypeKind::CloseRoundBracket: {
            return ")";
        } break;

        case TokenTypeKind::OpenCurlyBracket: {
            return "{";
        } break;

        case TokenTypeKind::CloseCurlyBracket: {
            return "}";
        } break;

        case TokenTypeKind::OpenSquareBracket: {
            return "[";
        } break;

        case TokenTypeKind::CloseSquareBracket: {
            return "]";
        } break;

        case TokenTypeKind::Identifier: {
            return token.identifier;
        } break;

        case TokenTypeKind::String: {
            auto buffer = allocate<char>(token.string.count + 3);

            buffer[0] = '"';
            memcpy(buffer + 1, token.string.elements, token.string.count);
            buffer[token.string.count + 1] = '"';
            buffer[token.string.count + 2] = '\0';

            return buffer;
        } break;

        case TokenTypeKind::Integer: {
            const size_t buffer_size = 32;

            auto buffer = allocate<char>(buffer_size);

            snprintf(buffer, buffer_size, "%lld", token.integer);

            return buffer;
        } break;

        case TokenTypeKind::FloatingPoint: {
            const size_t buffer_size = 32;

            auto buffer = allocate<char>(buffer_size);

            snprintf(buffer, buffer_size, "%f", token.floating_point);

            return buffer;
        } break;

        default: {
            abort();
        } break;
    }
}