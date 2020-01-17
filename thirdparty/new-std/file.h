#pragma once

#include <platform.h>
#include <os_string.h>
#include <result.h>

#if defined(OS_WINDOWS)
#include <Windows.h>

using File = HANDLE;

Result<File> create_file(OSString path, bool overwrite_existing, bool write) {
    auto desired_access = GENERIC_READ;

    if(write) {
        desired_access |= GENERIC_WRITE;
    }

    DWORD creation_disposition;
    if(overwrite_existing) {
        creation_disposition = CREATE_ALWAYS;
    } else {
        creation_disposition = CREATE_NEW;
    }

    auto file = CreateFileW(path, desired_access, 0, nullptr, creation_disposition, 0, nullptr);

    if(file == INVALID_HANDLE_VALUE)  {
        return err<File>();
    }

    return ok(file);
}

Result<File> open_file(OSString path, bool write) {
    auto desired_access = GENERIC_READ;

    if(write) {
        desired_access |= GENERIC_WRITE;
    }

    auto file = CreateFileW(path, desired_access, 0, 0, OPEN_EXISTING, 0, 0);

    if(file == INVALID_HANDLE_VALUE)  {
        return err<File>();
    }

    return ok(file);
}

bool close_file(File file) {
    return CloseHandle(file) != 0;
}

usize file_size(File file) {
    return (usize)GetFileSize(file, nullptr);
}

Result<usize> read_from_file(File file, Array<u8> buffer) {
    DWORD bytes_read;

    if(ReadFile(file, buffer.elements, buffer.length, &bytes_read, nullptr) == 0) {
        return err<usize>();
    }

    return ok((usize)bytes_read);
}

Result<usize> write_to_file(File file, Array<u8> data) {
    DWORD bytes_written;

    if(WriteFile(file, data.elements, data.length, &bytes_written, nullptr) == 0) {
        return err<usize>();
    }

    return ok((usize)bytes_written);
}

template <typename A>
Result<Array<u8>> read_entire_file(OSString path, A &allocator) {
    auto file = CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    if(file == 0) {
        return err<Array<u8>>();
    }

    auto size = GetFileSize(file, nullptr);

    u8 *data;
    if(size != 0) {
        data = (u8*)allocate(allocator, size);

        if(data == nullptr) {
            return err<Array<u8>>();
        }

        if(ReadFile(file, data, size, nullptr, nullptr) == 0) {
            deallocate(allocator, data, size);

            return err<Array<u8>>();
        }
    } else {
        data = nullptr;
    }

    CloseHandle(file);

    return ok<Array<u8>>({
        size,
        data
    });
}

File stdin_file() {
    return (File)STD_INPUT_HANDLE;
}

File stdout_file() {
    return (File)STD_OUTPUT_HANDLE;
}

File stderr_file() {
    return (File)STD_ERROR_HANDLE;
}
#endif