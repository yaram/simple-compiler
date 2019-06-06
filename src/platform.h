#pragma once

#if defined(unix) || defined(__unix__) || defined(__unix)
#define PLATFORM_UNIX
#elif defined(_WIN32)
#define PLATFORM_WINDOWS
#else
#error Unsupported platform
#endif