#pragma once

#include "result.h"
#include "string.h"

Result<String> path_relative_to_absolute(Arena* arena, String path);
Result<String> path_get_directory_component(Arena* arena, String path);
Result<String> path_get_file_component(Arena* arena, String path);
Result<String> get_executable_path(Arena* arena);