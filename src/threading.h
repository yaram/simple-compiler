#pragma once

#include "platform.h"

#if defined(OS_WINDOWS)
#include <Windows.h>

using Mutex = CRITICAL_SECTION;
#endif

Mutex create_mutex();
void lock_mutex(Mutex *mutex);
void release_mutex(Mutex *mutex);

void create_thread(void (*entry)());

int get_processor_count();