#pragma once

#include <integers.h>

#if defined(OS_WINDOWS)
#include <Windows.h>

void *kernel_allocate(void *address, usize size) {
    return VirtualAlloc(address, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void kernel_deallocate(void *address) {
    VirtualFree(address, 0, MEM_RELEASE);
}
#endif