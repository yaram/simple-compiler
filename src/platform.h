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

bool does_os_exist(const char *os);
bool does_architecture_exist(const char *architecture);
bool is_supported_target(const char *os, const char *architecture);

struct ArchitectureSizes {
    RegisterSize address_size;

    RegisterSize default_integer_size;

    RegisterSize default_float_size;

    RegisterSize boolean_size;
};

ArchitectureSizes get_architecture_sizes(const char *architecture);

const char *get_llvm_triple(const char *architecture, const char *os);

const char *get_host_architecture();
const char *get_host_os();