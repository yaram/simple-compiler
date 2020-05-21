#pragma once

#include "result.h"

Result<const char *> path_relative_to_absolute(const char *path);
const char *path_get_directory_component(const char *path);
const char *path_get_file_component(const char *path);
const char *get_executable_path();