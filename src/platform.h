#pragma once

#include "register_size.h"

#if defined(unix) || defined(__unix__) || defined(__unix)
#define OS_UNIX
#if defined(__linux__)
#define OS_LINUX
#else
#error Unsupported OS
#endif
#elif defined(_WIN32)
#define OS_WINDOWS
#else
#error Unsupported OS
#endif

#if defined(__x86_64__) || defined(_M_AMD64)
#define ARCH_X64
#else
#error Unsupported architecture
#endif

struct RegisterSizes {
    RegisterSize address_size;
    RegisterSize default_size;
};

RegisterSizes get_register_sizes(const char *architecture);
const char *get_llvm_triple(const char *architecture, const char *os);