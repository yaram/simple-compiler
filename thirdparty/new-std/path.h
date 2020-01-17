#pragma once

#include <string.h>
#include <platform.h>
#include <result.h>
#include <list.h>

#if defined(OS_WINDOWS)
template <typename A>
Result<OSString> get_path_file(OSString path, A &allocator) {
    auto result = GetFullPathNameW(path, 0, nullptr, nullptr);

    if(result == 0) {
        return err<OSString>();
    }

    auto buffer = (WCHAR*)allocate(allocator, result * sizeof(WCHAR));

    if(buffer == nullptr) {
        return err<OSString>();
    }

    WCHAR *file_part;

    GetFullPathNameW(path, result, buffer, &file_part);

    if(file_part == 0) {
        return err<OSString>();
    }

    auto file_part_length = buffer + result - file_part - 1;

    for(usize i = 0; i < (usize)file_part_length; i += 1) {
        buffer[file_part_length - 1 - i] = buffer[result - 2 - i];
    }

    buffer[file_part_length] = (WCHAR)'\0';

    buffer = (WCHAR*)reallocate(allocator, buffer, result * sizeof(WCHAR), (file_part_length + 1) * sizeof(WCHAR));

    if(buffer == nullptr) {
        return err<OSString>();
    }

    return ok(buffer);
}

template <typename A>
Result<OSString> get_path_directory(OSString path, A &allocator) {
    auto result = GetFullPathNameW(path, 0, nullptr, nullptr);

    if(result == 0) {
        return err<OSString>();
    }

    auto buffer = (WCHAR*)allocate(allocator, result * sizeof(WCHAR));

    if(buffer == nullptr) {
        return err<OSString>();
    }

    WCHAR *file_part;

    GetFullPathNameW(path, result, buffer, &file_part);

    if(file_part != 0) {
        auto directory_part_length = file_part - buffer;

        buffer[directory_part_length] = (WCHAR)'\0';

        buffer = (WCHAR*)reallocate(allocator, buffer, result * sizeof(WCHAR), (directory_part_length + 1) * sizeof(WCHAR));

        if(buffer == nullptr) {
            return err<OSString>();
        }
    }

    return ok(buffer);
}
#endif