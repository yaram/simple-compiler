#pragma once

#include <stdarg.h>
#include "string.h"

struct FileRange {
    unsigned int first_line;
    unsigned int first_column;

    unsigned int last_line;
    unsigned int last_column;
};

void error(String path, FileRange range, const char* format, va_list arguments);
void error(String path, FileRange range, const char* format, ...);