#include "threading.h"
#include <assert.h>

#if defined(OS_WINDOWS)

Mutex create_mutex() {
    CRITICAL_SECTION critical_section;
    InitializeCriticalSection(&critical_section);

    return critical_section;
}

void lock_mutex(Mutex *mutex) {
    EnterCriticalSection(mutex);
}

void release_mutex(Mutex *mutex) {
    LeaveCriticalSection(mutex);
}

static DWORD WINAPI thread_thunk(LPVOID parameter) {
    auto entry = (void (*)())(parameter);

    entry();
}

void create_thread(void (*entry)()) {
    auto thread = CreateThread(nullptr, 0, thread_thunk, (LPVOID)entry, 0, nullptr);

    assert(thread != nullptr);
}

int get_processor_count() {
    SYSTEM_INFO system_info;

    GetNativeSystemInfo(&system_info);

    return system_info.dwNumberOfProcessors;
}

#endif