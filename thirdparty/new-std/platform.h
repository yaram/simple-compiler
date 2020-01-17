#pragma once

#if defined(__x86_64__) | defined(_M_X64)
#define ARCH_X64
#elif defined(__i386__) | defined(_M_I86)
#define ARCH_X86
#else
#define Unsupported architecture
#endif

#if defined(_WIN32)
#define OS_WINDOWS
#else
#define Unsupported operating system
#endif