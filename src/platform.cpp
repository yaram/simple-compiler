#include "platform.h"
#include <string.h>
#include "util.h"

RegisterSizes get_register_sizes(const char *architecture) {
    if(strcmp(architecture, "x64") == 0) {
        return {
            RegisterSize::Size64,
            RegisterSize::Size64
        };
    } else {
        abort();
    }
}

const char *get_llvm_triple(const char *architecture, const char *os) {
    char *buffer{};

    const char *triple_architecture;
    if(strcmp(architecture, "x64") == 0) {
        triple_architecture = "x86_64";
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
    } else if(strcmp(os, "macos") == 0) {
        triple_vendor = "apple";
        triple_system = "linux";
        triple_abi = "macho";
    } else if(strcmp(os, "windows") == 0) {
        triple_vendor = "pc";
        triple_system = "windows";
        triple_abi = "msvc";
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

    return buffer;
}