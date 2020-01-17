#pragma once

#include <platform.h>

#if defined(ARCH_X64)
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 usize;

typedef signed char i8;
typedef short i16;
typedef int i32;
typedef long long i64;
typedef i64 isize;
#elif defined(ARCH_X86)
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u32 usize;

typedef signed char i8;
typedef short i16;
typedef int u32;
typedef long long i64;
typedef i32 isize;
#endif