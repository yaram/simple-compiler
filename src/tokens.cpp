#include "tokens.h"
#include <stdio.h>

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

        case TokenType::Equals: {
            printf("Equals");
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

        case TokenType::DoubleEquals: {
            printf("DoubleEquals");
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
            printf("OpenSquarBracket");
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

    }
}