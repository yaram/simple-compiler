#pragma once

#include "register_size.h"
#include "string.h"

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
#elif defined(__riscv) && __riscv_xlen == 64
#define ARCH_RISCV64
#else
#error Unsupported architecture
#endif

bool does_os_exist(String os);
bool does_architecture_exist(String architecture);
bool does_toolchain_exist(String toolchain);
bool is_supported_target(String os, String architecture, String toolchain);

struct ArchitectureSizes {
    RegisterSize address_size;

    RegisterSize default_integer_size;

    RegisterSize default_float_size;

    RegisterSize boolean_size;
};

ArchitectureSizes get_architecture_sizes(String architecture);

String get_default_toolchain(String os);

String get_llvm_triple(Arena* arena, String architecture, String os, String toolchain);
String get_llvm_features(String architecture);

String get_host_architecture();
String get_host_os();