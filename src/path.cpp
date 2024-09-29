#include "path.h"
#include <stdio.h>
#include "platform.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#if defined(OS_UNIX)

#include <limits.h>
#include <libgen.h>

Result<String> path_relative_to_absolute(String path) {
    char absolute_path[PATH_MAX];
    if(realpath(path.to_c_string(), absolute_path) == nullptr) {
        fprintf(stderr, "Invalid path %.*s\n", STRING_PRINTF_ARGUMENTS(path));

        return err();
    }

    auto result = String::from_c_string(absolute_path);

    if(!result.status) {
        fprintf(stderr, "Invalid path %.*s\n", STRING_PRINTF_ARGUMENTS(path));

        return err();
    }

    return ok(result.value);
}

Result<String> path_get_file_component(String path) {
    if(path.length >= PATH_MAX - 1) {
        fprintf(stderr, "Invalid path %.*s\n", STRING_PRINTF_ARGUMENTS(path));

        return err();
    }

    char input_buffer[PATH_MAX];

    memcpy(input_buffer, path.elements, path.length);
    input_buffer[path.length] = '\0';

    auto path_file = basename(input_buffer);

    auto result = String::from_c_string(path_file);

    if(!result.status) {
        fprintf(stderr, "Invalid path %.*s\n", STRING_PRINTF_ARGUMENTS(path));

        return err();
    }

    return ok(result.value);
}

Result<String> path_get_directory_component(String path) {
    if(path.length >= PATH_MAX - 1) {
        fprintf(stderr, "Invalid path %.*s\n", STRING_PRINTF_ARGUMENTS(path));

        return err();
    }

    char input_buffer[PATH_MAX];

    memcpy(input_buffer, path.elements, path.length);
    input_buffer[path.length] = '\0';

    auto path_directory = dirname(input_buffer);

    StringBuffer buffer {};

    buffer.append_c_string(path_directory);
    buffer.append_character('/');

    return ok((String)buffer);
}

#if defined(OS_LINUX)
#include <unistd.h>

Result<String> get_executable_path() {
    auto buffer = allocate<char>(PATH_MAX);
    auto result = readlink("/proc/self/exe", buffer, PATH_MAX);
    if(result == -1) {
        fprintf(stderr, "Cannot get executable path\n");

        return err();
    }

    auto utf8_result = validate_utf8_string((uint8_t*)buffer, (size_t)result);
    if(!utf8_result.status) {
        fprintf(stderr, "Cannot get executable path\n");

        return err();
    }

    String string {};
    string.length = (size_t)result;
    string.elements = (char8_t*)buffer;

    return ok(string);
}
#endif

#elif defined(OS_WINDOWS)

#include <Windows.h>

Result<String> path_relative_to_absolute(String path) {
    auto absolute_path = allocate<char>(_MAX_PATH);
    
    if(_fullpath(absolute_path, path, _MAX_PATH) == nullptr) {
        fprintf(stderr, "Invalid path %.*s\n", path);

        return err();
    }

    return ok(String::from_c_string(absolute_path));
}

String path_get_directory_component(String path) {
    char path_drive[_MAX_DRIVE];
    char path_directory[_MAX_DIR];

    _splitpath_s(path, path_drive, _MAX_DRIVE, path_directory, _MAX_DIR, nullptr, 0, nullptr, 0);

    auto buffer = allocate<char>(_MAX_DRIVE + _MAX_DIR);

    strcpy_s(buffer, _MAX_DRIVE + _MAX_DIR, path_drive);
    strcat_s(buffer, _MAX_DRIVE + _MAX_DIR, path_directory);

    return String::from_c_string(buffer);
}

String path_get_file_component(String path) {
    char path_name[_MAX_FNAME];
    char path_extension[_MAX_EXT];

    _splitpath_s(path, nullptr, 0, nullptr, 0, path_name, _MAX_FNAME, path_extension, _MAX_EXT);

    auto buffer = allocate<char>(_MAX_FNAME + _MAX_EXT);

    strcpy_s(buffer, _MAX_FNAME + _MAX_EXT, path_name);
    strcat_s(buffer, _MAX_FNAME + _MAX_EXT, path_extension);

    return String::from_c_string(buffer);
}

String get_executable_path() {
    auto file_name = allocate<CHAR>(1024);
    auto result = GetModuleFileNameA(nullptr, file_name, 1024);
    assert(result != 1024);

    return String::from_c_string(file_name);
}

#endif