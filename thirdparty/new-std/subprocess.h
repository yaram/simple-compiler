#pragma once

#include <platform.h>
#include <result.h>
#include <integers.h>
#include <os_string.h>

#if defined(OS_WINDOWS)
Result<u32> run_command(OSString command_string) {
    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(STARTUPINFOW);
    startup_info.hStdInput = (HANDLE)STD_INPUT_HANDLE;
    startup_info.hStdOutput = (HANDLE)STD_OUTPUT_HANDLE;
    startup_info.hStdError = (HANDLE)STD_ERROR_HANDLE;

    PROCESS_INFORMATION process_info;
    if(CreateProcessW(nullptr, command_string, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup_info, &process_info) == 0) {
        return err<u32>();
    }

    if(WaitForSingleObject(process_info.hProcess, INFINITE) != WAIT_OBJECT_0) {
        return err<u32>();
    }

    DWORD exit_code;
    if(GetExitCodeProcess(process_info.hProcess, &exit_code) == 0) {
        return err<u32>();
    }

    return ok((u32)exit_code);
}
#endif