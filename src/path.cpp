#include "path.h"
#include <stdio.h>
#include "platform.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>

#if defined(OS_UNIX)

#include <limits.h>
#include <libgen.h>

Result<const char *> path_relative_to_absolute(const char *path) {
    char absolute_path[PATH_MAX];
    if(realpath(path, absolute_path) == nullptr) {
        fprintf(stderr, "Invalid path %s\n", path);

        return { false };
    }

    auto output_buffer = allocate<char>(strlen(absolute_path) + 1);
    strcpy(output_buffer, absolute_path);
    
    return {
        true,
        output_buffer
    };
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

    if(path_directory_length != 1 && path_directory[path_directory_length - 1] != '/') {
        auto output_buffer = allocate<char>(path_directory_length + 2);

        strcpy(output_buffer, path_directory);
        strcat(output_buffer, "/");

        return output_buffer;
    } else {
        auto output_buffer = allocate<char>(path_directory_length + 1);

        strcpy(output_buffer, path_directory);

        return output_buffer;
    }
}

#elif defined(OS_WINDOWS)

Result<const char *> path_relative_to_absolute(const char *path) {
    auto absolute_path = allocate<char>(_MAX_PATH);
    
    if(_fullpath(absolute_path, path, _MAX_PATH) == nullptr) {
        fprintf(stderr, "Invalid path %s\n", path);

        return { false };
    }

    return {
        true,
        absolute_path
    };
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

#endif