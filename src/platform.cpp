#include "platform.h"
#include <string.h>
#include "util.h"

bool does_os_exist(const char *os) {
    return
        strcmp(os, "linux") == 0 ||
        strcmp(os, "windows") == 0 ||
        strcmp(os, "emscripten") == 0
    ;
}

bool does_architecture_exist(const char *architecture) {
    return
        strcmp(architecture, "x86") == 0 ||
        strcmp(architecture, "x64") == 0 ||
        strcmp(architecture, "wasm32") == 0
    ;
}

bool is_supported_target(const char *os, const char *architecture) {
    if(strcmp(os, "linux") == 0) {
        return 
            strcmp(architecture, "x86") == 0 ||
            strcmp(architecture, "x64") == 0
        ;
    } else if(strcmp(os, "windows") == 0) {
        return 
            strcmp(architecture, "x86") == 0 ||
            strcmp(architecture, "x64") == 0
        ;
    } else if(strcmp(os, "emscripten") == 0) {
        return strcmp(architecture, "wasm32") == 0;
    } else {
        abort();
    }
}

ArchitectureSizes get_architecture_sizes(const char *architecture) {
    if(strcmp(architecture, "x86") == 0) {
        return {
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size8
        };
    } else if(strcmp(architecture, "x64") == 0) {
        return {
            RegisterSize::Size64,
            RegisterSize::Size64,
            RegisterSize::Size64,
            RegisterSize::Size8
        };
    } else if(strcmp(architecture, "wasm32") == 0) {
        return {
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size8
        };
    } else {
        abort();
    }
}

const char *get_llvm_triple(const char *architecture, const char *os) {
    StringBuffer buffer {};

    const char *triple_architecture;
    if(strcmp(architecture, "x86") == 0) {
        triple_architecture = "i686";
    } else if(strcmp(architecture, "x64") == 0) {
        triple_architecture = "x86_64";
    } else if(strcmp(architecture, "wasm32") == 0) {
        triple_architecture = "wasm32";
    } else {
        abort();
    }

    const char *triple_vendor;
    const char *triple_system;
    const char *triple_abi;
    if(strcmp(os, "linux") == 0) {
        triple_vendor = "unknown";
        triple_system = "linux";
        triple_abi = "gnu";
    } else if(strcmp(os, "windows") == 0) {
        triple_vendor = "pc";
        triple_system = "windows";
        triple_abi = "msvc";
    } else if(strcmp(os, "emscripten") == 0) {
        triple_vendor = "unknown";
        triple_system = "emscripten";
        triple_abi = "unknown";
    } else {
        abort();
    }

    string_buffer_append(&buffer, triple_architecture);
    string_buffer_append(&buffer, "-");
    string_buffer_append(&buffer, triple_vendor);
    string_buffer_append(&buffer, "-");
    string_buffer_append(&buffer, triple_system);
    string_buffer_append(&buffer, "-");
    string_buffer_append(&buffer, triple_abi);

    return buffer.data;
}

const char *get_host_architecture() {
#if defined(ARCH_X64)
    return "x64";
#endif
}

const char *get_host_os() {
#if defined(OS_LINUX)
    return "linux";
#elif defined(OS_WINDOWS)
    return "windows";
#endif
}
