#include "tokens.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

void Token::print() {
    printf("(%d, %d-%d): ", line, first_column, last_column);

    switch(kind) {
        case TokenKind::Dot: {
            printf("Dot");
        } break;

        case TokenKind::DoubleDot: {
            printf("DoubleDot");
        } break;

        case TokenKind::Comma: {
            printf("Comma");
        } break;

        case TokenKind::Colon: {
            printf("Colon");
        } break;

        case TokenKind::Semicolon: {
            printf("Semicolon");
        } break;

        case TokenKind::Plus: {
            printf("Plus");
        } break;

        case TokenKind::Dash: {
            printf("Dash");
        } break;

        case TokenKind::Asterisk: {
            printf("Asterisk");
        } break;

        case TokenKind::ForwardSlash: {
            printf("ForwardSlash");
        } break;

        case TokenKind::Percent: {
            printf("Percent");
        } break;

        case TokenKind::Equals: {
            printf("Equals");
        } break;

        case TokenKind::DoubleEquals: {
            printf("DoubleEquals");
        } break;

        case TokenKind::BangEquals: {
            printf("BangEquals");
        } break;

        case TokenKind::PlusEquals: {
            printf("PlusEquals");
        } break;

        case TokenKind::DashEquals: {
            printf("DashEquals");
        } break;

        case TokenKind::AsteriskEquals: {
            printf("AsteriskEquals");
        } break;

        case TokenKind::ForwardSlashEquals: {
            printf("ForwardSlashEquals");
        } break;

        case TokenKind::PercentEquals: {
            printf("PercentEquals");
        } break;

        case TokenKind::LeftArrow: {
            printf("LeftArrow");
        } break;

        case TokenKind::DoubleLeftArrow: {
            printf("DoubleLeftArrow");
        } break;

        case TokenKind::RightArrow: {
            printf("RightArrow");
        } break;

        case TokenKind::DoubleRightArrow: {
            printf("DoubleRightArrow");
        } break;

        case TokenKind::Ampersand: {
            printf("Ampersand");
        } break;

        case TokenKind::DoubleAmpersand: {
            printf("DoubleAmpersand");
        } break;

        case TokenKind::At: {
            printf("At");
        } break;

        case TokenKind::Pipe: {
            printf("Pipe");
        } break;

        case TokenKind::DoublePipe: {
            printf("DoublePipe");
        } break;

        case TokenKind::Hash: {
            printf("Hash");
        } break;

        case TokenKind::Bang: {
            printf("Bang");
        } break;

        case TokenKind::Arrow: {
            printf("Arrow");
        } break;

        case TokenKind::Dollar: {
            printf("Dollar");
        } break;

        case TokenKind::OpenRoundBracket: {
            printf("OpenRoundBracket");
        } break;

        case TokenKind::CloseRoundBracket: {
            printf("CloseRoundBracket");
        } break;

        case TokenKind::OpenCurlyBracket: {
            printf("OpenCurlyBracket");
        } break;

        case TokenKind::CloseCurlyBracket: {
            printf("CloseCurlyBracket");
        } break;

        case TokenKind::OpenSquareBracket: {
            printf("OpenSquareBracket");
        } break;

        case TokenKind::CloseSquareBracket: {
            printf("CloseSquareBracket");
        } break;

        case TokenKind::Identifier: {
            printf("Identifier(%.*s)", STRING_PRINTF_ARGUMENTS(identifier));
        } break;

        case TokenKind::String: {
            printf("String(%.*s)", STRING_PRINTF_ARGUMENTS(string));
        } break;

        case TokenKind::Integer: {
            printf("Integer(%" PRIu64 ")", integer);
        } break;

        case TokenKind::FloatingPoint: {
            printf("FloatingPoint(%f)", floating_point);
        } break;
    }
}

String Token::get_text() {
    switch(kind) {
        case TokenKind::Dot: {
            return u8"."_S;
        } break;

        case TokenKind::DoubleDot: {
            return u8".."_S;
        } break;

        case TokenKind::Comma: {
            return u8","_S;
        } break;

        case TokenKind::Colon: {
            return u8":"_S;
        } break;

        case TokenKind::Semicolon: {
            return u8";"_S;
        } break;

        case TokenKind::Plus: {
            return u8"+"_S;
        } break;

        case TokenKind::Dash: {
            return u8"-"_S;
        } break;

        case TokenKind::Asterisk: {
            return u8"*"_S;
        } break;

        case TokenKind::ForwardSlash: {
            return u8"/"_S;
        } break;

        case TokenKind::Percent: {
            return u8"%"_S;
        } break;

        case TokenKind::Equals: {
            return u8"="_S;
        } break;

        case TokenKind::DoubleEquals: {
            return u8"=="_S;
        } break;

        case TokenKind::BangEquals: {
            return u8"!="_S;
        } break;

        case TokenKind::PlusEquals: {
            return u8"+="_S;
        } break;

        case TokenKind::DashEquals: {
            return u8"-="_S;
        } break;

        case TokenKind::AsteriskEquals: {
            return u8"*="_S;
        } break;

        case TokenKind::ForwardSlashEquals: {
            return u8"/="_S;
        } break;

        case TokenKind::PercentEquals: {
            return u8"%="_S;
        } break;

        case TokenKind::LeftArrow: {
            return u8"<"_S;
        } break;

        case TokenKind::DoubleLeftArrow: {
            return u8"<<"_S;
        } break;

        case TokenKind::RightArrow: {
            return u8">"_S;
        } break;

        case TokenKind::DoubleRightArrow: {
            return u8">>"_S;
        } break;

        case TokenKind::Ampersand: {
            return u8"&"_S;
        } break;

        case TokenKind::DoubleAmpersand: {
            return u8"&&"_S;
        } break;

        case TokenKind::At: {
            return u8"@"_S;
        } break;

        case TokenKind::Pipe: {
            return u8"|"_S;
        } break;

        case TokenKind::DoublePipe: {
            return u8"||"_S;
        } break;

        case TokenKind::Hash: {
            return u8"#"_S;
        } break;

        case TokenKind::Bang: {
            return u8"!"_S;
        } break;

        case TokenKind::Arrow: {
            return u8"->"_S;
        } break;

        case TokenKind::Dollar: {
            return u8"$"_S;
        } break;

        case TokenKind::OpenRoundBracket: {
            return u8"("_S;
        } break;

        case TokenKind::CloseRoundBracket: {
            return u8")"_S;
        } break;

        case TokenKind::OpenCurlyBracket: {
            return u8"{"_S;
        } break;

        case TokenKind::CloseCurlyBracket: {
            return u8"}"_S;
        } break;

        case TokenKind::OpenSquareBracket: {
            return u8"["_S;
        } break;

        case TokenKind::CloseSquareBracket: {
            return u8"]"_S;
        } break;

        case TokenKind::Identifier: {
            return identifier;
        } break;

        case TokenKind::String: {
            auto buffer = allocate<char8_t>(string.length + 2);

            buffer[0] = '"';
            memcpy(&buffer[1], string.elements, string.length);
            buffer[string.length + 1] = '"';

            String result {};
            result.length = string.length + 2;
            result.elements = buffer;

            return result;
        } break;

        case TokenKind::Integer: {
            const size_t buffer_size = 32;

            auto buffer = allocate<char>(buffer_size);
            auto length = snprintf(buffer, buffer_size, "%" PRIu64, integer);

            String string {};
            string.length = (size_t)length;
            string.elements = (char8_t*)buffer;

            return string;
        } break;

        case TokenKind::FloatingPoint: {
            const size_t buffer_size = 32;

            auto buffer = allocate<char>(buffer_size);
            auto length = snprintf(buffer, buffer_size, "%f", floating_point);

            String string {};
            string.length = (size_t)length;
            string.elements = (char8_t*)buffer;

            return string;
        } break;

        default: {
            abort();
        } break;
    }
}