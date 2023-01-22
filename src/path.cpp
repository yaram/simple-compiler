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

    auto output_buffer_size = strlen(absolute_path) + 1;
    auto output_buffer = allocate<char>(output_buffer_size);
    strcpy_s(output_buffer, output_buffer_size, absolute_path);
    
    return ok(output_buffer);
}

const char *path_get_file_component(const char *path) {
    char input_buffer[PATH_MAX];
    strcpy_s(input_buffer, PATH_MAX, path);

    auto path_file = basename(input_buffer);

    auto output_buffer_size = strlen(path_file) + 1;
    auto output_buffer = allocate<char>(output_buffer_size);
    strcpy_s(output_buffer, output_buffer_size, path_file);

    return output_buffer;
}

const char *path_get_directory_component(const char *path) {
    char input_buffer[PATH_MAX];
    strcpy_s(input_buffer, PATH_MAX, path);

    auto path_directory = dirname(input_buffer);

    auto output_buffer_size = strlen(path_directory) + 2;
    auto output_buffer = allocate<char>(output_buffer_size);

    strcpy_s(output_buffer, output_buffer_size, path_directory);
    strcat_s(output_buffer, output_buffer_size, "/");

    return output_buffer;
}

#if defined(OS_LINUX)
#include <unistd.h>

const char *get_executable_path() {
    const auto buffer_size = 1024;
    auto buffer = allocate<char>(buffer_size);
    auto result = readlink("/proc/self/exe", buffer, buffer_size - 1);
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
    char path_drive[_MAX_DRIVE];
    char path_directory[_MAX_DIR];

    _splitpath_s(path, path_drive, _MAX_DRIVE, path_directory, _MAX_DIR, nullptr, 0, nullptr, 0);

    auto buffer = allocate<char>(_MAX_DRIVE + _MAX_DIR);

    strcpy_s(buffer, _MAX_DRIVE + _MAX_DIR, path_drive);
    strcat_s(buffer, _MAX_DRIVE + _MAX_DIR, path_directory);

    return buffer;
}

const char *path_get_file_component(const char *path) {
    char path_name[_MAX_FNAME];
    char path_extension[_MAX_EXT];

    _splitpath_s(path, nullptr, 0, nullptr, 0, path_name, _MAX_FNAME, path_extension, _MAX_EXT);

    auto buffer = allocate<char>(_MAX_FNAME + _MAX_EXT);

    strcpy_s(buffer, _MAX_FNAME + _MAX_EXT, path_name);
    strcat_s(buffer, _MAX_FNAME + _MAX_EXT, path_extension);

    return buffer;
}

const char *get_executable_path() {
    auto file_name = allocate<CHAR>(1024);
    auto result = GetModuleFileNameA(nullptr, file_name, 1024);
    assert(result != 1024);

    return file_name;
}

#endif