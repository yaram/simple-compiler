#pragma once

#include "ir.h"

bool generate_llvm_object(
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *output_directory,
    const char *output_name
);