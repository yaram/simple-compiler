#pragma once

#include "result.h"
#include "string.h"

Result<String> path_relative_to_absolute(String path);
String path_get_directory_component(String path);
String path_get_file_component(String path);
String get_executable_path();