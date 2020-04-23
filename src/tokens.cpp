#include "tokens.h"
#include <stdio.h>
#include <string.h>
#include "util.h"

void print_token(Token token) {
    printf("(%d, %d-%d): ", token.line, token.first_character, token.last_character);

    switch(token.type) {
        case TokenType::Dot: {
            printf("Dot");
        } break;

        case TokenType::Comma: {
            printf("Comma");
        } break;

        case TokenType::Colon: {
            printf("Colon");
        } break;

        case TokenType::Semicolon: {
            printf("Semicolon");
        } break;

        case TokenType::Plus: {
            printf("Plus");
        } break;

        case TokenType::Dash: {
            printf("Dash");
        } break;

        case TokenType::Asterisk: {
            printf("Asterisk");
        } break;

        case TokenType::ForwardSlash: {
            printf("ForwardSlash");
        } break;

        case TokenType::Percent: {
            printf("Percent");
        } break;

        case TokenType::Equals: {
            printf("Equals");
        } break;

        case TokenType::DoubleEquals: {
            printf("DoubleEquals");
        } break;

        case TokenType::BangEquals: {
            printf("BangEquals");
        } break;

        case TokenType::PlusEquals: {
            printf("PlusEquals");
        } break;

        case TokenType::DashEquals: {
            printf("DashEquals");
        } break;

        case TokenType::AsteriskEquals: {
            printf("AsteriskEquals");
        } break;

        case TokenType::ForwardSlashEquals: {
            printf("ForwardSlashEquals");
        } break;

        case TokenType::PercentEquals: {
            printf("PercentEquals");
        } break;

        case TokenType::LeftArrow: {
            printf("LeftArrow");
        } break;

        case TokenType::RightArrow: {
            printf("RightArrow");
        } break;

        case TokenType::Ampersand: {
            printf("Ampersand");
        } break;

        case TokenType::DoubleAmpersand: {
            printf("DoubleAmpersand");
        } break;

        case TokenType::Pipe: {
            printf("Pipe");
        } break;

        case TokenType::DoublePipe: {
            printf("DoublePipe");
        } break;

        case TokenType::Hash: {
            printf("Hash");
        } break;

        case TokenType::Bang: {
            printf("Bang");
        } break;

        case TokenType::Arrow: {
            printf("Arrow");
        } break;

        case TokenType::Dollar: {
            printf("Dollar");
        } break;

        case TokenType::OpenRoundBracket: {
            printf("OpenRoundBracket");
        } break;

        case TokenType::CloseRoundBracket: {
            printf("CloseRoundBracket");
        } break;

        case TokenType::OpenCurlyBracket: {
            printf("OpenCurlyBracket");
        } break;

        case TokenType::CloseCurlyBracket: {
            printf("CloseCurlyBracket");
        } break;

        case TokenType::OpenSquareBracket: {
            printf("OpenSquareBracket");
        } break;

        case TokenType::CloseSquareBracket: {
            printf("CloseSquareBracket");
        } break;

        case TokenType::Identifier: {
            printf("Identifier(%s)", token.identifier);
        } break;

        case TokenType::String: {
            printf("String(%.*s)", (int)token.string.count, token.string.elements);
        } break;

        case TokenType::Integer: {
            printf("Integer(%lld)", token.integer);
        } break;

        case TokenType::FloatingPoint: {
            printf("FloatingPoint(%f)", token.floating_point);
        } break;
    }
}

const char *get_token_text(Token token) {
    switch(token.type) {
        case TokenType::Dot: {
            return ".";
        } break;

        case TokenType::Comma: {
            return ",";
        } break;

        case TokenType::Colon: {
            return ":";
        } break;

        case TokenType::Semicolon: {
            return ";";
        } break;

        case TokenType::Plus: {
            return "+";
        } break;

        case TokenType::Dash: {
            return "-";
        } break;

        case TokenType::Asterisk: {
            return "*";
        } break;

        case TokenType::ForwardSlash: {
            return "/";
        } break;

        case TokenType::Percent: {
            return "%";
        } break;

        case TokenType::Equals: {
            return "=";
        } break;

        case TokenType::DoubleEquals: {
            return "==";
        } break;

        case TokenType::BangEquals: {
            return "!=";
        } break;

        case TokenType::PlusEquals: {
            return "+=";
        } break;

        case TokenType::DashEquals: {
            return "-=";
        } break;

        case TokenType::AsteriskEquals: {
            return "*=";
        } break;

        case TokenType::ForwardSlashEquals: {
            return "/=";
        } break;

        case TokenType::PercentEquals: {
            return "%=";
        } break;

        case TokenType::LeftArrow: {
            return "<";
        } break;

        case TokenType::RightArrow: {
            return ">";
        } break;

        case TokenType::Ampersand: {
            return "&";
        } break;

        case TokenType::DoubleAmpersand: {
            return "&&";
        } break;

        case TokenType::Pipe: {
            return "|";
        } break;

        case TokenType::DoublePipe: {
            return "||";
        } break;

        case TokenType::Hash: {
            return "#";
        } break;

        case TokenType::Bang: {
            return "!";
        } break;

        case TokenType::Arrow: {
            return "->";
        } break;

        case TokenType::Dollar: {
            return "$";
        } break;

        case TokenType::OpenRoundBracket: {
            return "(";
        } break;

        case TokenType::CloseRoundBracket: {
            return ")";
        } break;

        case TokenType::OpenCurlyBracket: {
            return "{";
        } break;

        case TokenType::CloseCurlyBracket: {
            return "}";
        } break;

        case TokenType::OpenSquareBracket: {
            return "[";
        } break;

        case TokenType::CloseSquareBracket: {
            return "]";
        } break;

        case TokenType::Identifier: {
            return token.identifier;
        } break;

        case TokenType::String: {
            auto buffer = allocate<char>(token.string.count + 3);

            buffer[0] = '"';
            memcpy(buffer + 1, token.string.elements, token.string.count);
            buffer[token.string.count + 1] = '"';
            buffer[token.string.count + 2] = '\0';

            return buffer;
        } break;

        case TokenType::Integer: {
            const size_t buffer_size = 32;

            auto buffer = allocate<char>(buffer_size);

            snprintf(buffer, buffer_size, "%lld", token.integer);

            return buffer;
        } break;

        case TokenType::FloatingPoint: {
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