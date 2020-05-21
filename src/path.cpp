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

Result<const char *> path_relative_to_absolute(const char *path) {
    char absolute_path[PATH_MAX];
    if(realpath(path, absolute_path) == nullptr) {
        fprintf(stderr, "Invalid path %s\n", path);

        return err;
    }

    auto output_buffer = allocate<char>(strlen(absolute_path) + 1);
    strcpy(output_buffer, absolute_path);
    
    return ok(output_buffer);
}

const char *path_get_file_component(const char *path) {
    char input_buffer[PATH_MAX];
    strcpy(input_buffer, path);

    auto path_file = basename(input_buffer);

    auto output_buffer = allocate<char>(strlen(path_file) + 1);
    strcpy(output_buffer, path_file);

    return output_buffer;
}

const char *path_get_directory_component(const char *path) {
    char input_buffer[PATH_MAX];
    strcpy(input_buffer, path);

    auto path_directory = dirname(input_buffer);

    auto path_directory_length = strlen(path_directory);

    auto output_buffer = allocate<char>(path_directory_length + 2);

    strcpy(output_buffer, path_directory);
    strcat(output_buffer, "/");

    return output_buffer;
}

#if defined(OS_LINUX)
#include <unistd.h>

const char *get_executable_path() {
    const auto buffer_size = 1024;
    auto buffer = allocate<char>(buffer_size);
    auto result = readlink("/proc/self/exe", buffer, buffer_size);
    assert(result != -1);

    buffer[result] = '\0';

    return buffer;
}
#endif

#elif defined(OS_WINDOWS)

#include <Windows.h>

Result<const char *> path_relative_to_absolute(const char *path) {
    auto absolute_path = allocate<char>(_MAX_PATH);
    
    if(_fullpath(absolute_path, path, _MAX_PATH) == nullptr) {
        fprintf(stderr, "Invalid path %s\n", path);

        return err;
    }

    return ok(absolute_path);
}

const char *path_get_directory_component(const char *path) {
    char path_drive[10];
    char path_directory[_MAX_DIR];

    _splitpath(path, path_drive, path_directory, nullptr, nullptr);

    auto buffer = allocate<char>(_MAX_DRIVE + _MAX_DIR);

    strcpy(buffer, path_drive);
    strcat(buffer, path_directory);

    return buffer;
}

const char *path_get_file_component(const char *path) {
    char path_name[_MAX_FNAME];
    char path_extension[_MAX_EXT];

    _splitpath(path, nullptr, nullptr, path_name, path_extension);

    auto buffer = allocate<char>(_MAX_FNAME + _MAX_EXT);

    strcpy(buffer, path_name);
    strcat(buffer, path_extension);

    return buffer;
}

const char *get_executable_path() {
    auto file_name = allocate<CHAR>(1024);
    auto result = GetModuleFileNameA(nullptr, file_name, 1024);
    assert(result != 1024);

    return file_name;
}

#endif