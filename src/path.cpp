#include "path.h"
#include <stdio.h>
#include "platform.h"
#include "util.h"
#include <string.h>

#if defined(PLATFORM_UNIX)

Result<const char *> path_relative_to_absolute(const char *path) {
    auto absolute_path = allocate<char>(PATH_MAX);
        
    if(realpath(path, absolute_path) == nullptr) {
        fprintf(stderr, "Invalid path %s\n", path);

        return { false };
    }
    
    return {
        true,
        absolute_path
    };
}

const char *path_get_directory_component(const char *path) {
    return dirname(path);
}

const char *path_get_file_component(const char *path) {
    auto path_directory = basename(path);

    auto path_directory_length = strlen(path_directory);

    if(path_directory_length != 0 && path_directory[0] != '/') {
        auto buffer = allocate<char>(path_directory_length + 2);

        strcpy(buffer, path_directory);
        strcat(buffer, "/");

        return buffer;
    } else {
        return path_directory;
    }
}

#elif defined(PLATFORM_WINDOWS)

#include <stdlib.h>

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

    printf("%s\n", path_drive);

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