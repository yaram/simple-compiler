#include "platform.h"
#include "util.h"

bool does_os_exist(String os) {
    return
        os == "linux"_S ||
        os == "windows"_S ||
        os == "emscripten"_S
    ;
}

bool does_architecture_exist(String architecture) {
    return
        architecture == "x86"_S ||
        architecture == "x64"_S ||
        architecture == "wasm32"_S
    ;
}

bool is_supported_target(String os, String architecture) {
    if(os == "linux"_S) {
        return 
            architecture == "x86"_S ||
            architecture == "x64"_S
        ;
    } else if(os == "windows"_S) {
        return 
            architecture == "x86"_S ||
            architecture == "x64"_S
        ;
    } else if(os == "emscripten"_S) {
        return architecture == "wasm32"_S;
    } else {
        abort();
    }
}

ArchitectureSizes get_architecture_sizes(String architecture) {
    if(architecture == "x86"_S) {
        return {
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size32,
            RegisterSize::Size8
        };
    } else if(architecture == "x64"_S) {
        return {
            RegisterSize::Size64,
            RegisterSize::Size64,
            RegisterSize::Size64,
            RegisterSize::Size8
        };
    } else if(architecture == "wasm32"_S) {
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

String get_llvm_triple(String architecture, String os) {
    StringBuffer buffer {};

    String triple_architecture;
    if(architecture == "x86"_S) {
        triple_architecture = "i686"_S;
    } else if(architecture == "x64"_S) {
        triple_architecture = "x86_64"_S;
    } else if(architecture == "wasm32"_S) {
        triple_architecture = "wasm32"_S;
    } else {
        abort();
    }

    String triple_vendor;
    String triple_system;
    String triple_abi;
    if(os == "linux"_S) {
        triple_vendor = "unknown"_S;
        triple_system = "linux"_S;
        triple_abi = "gnu"_S;
    } else if(os == "windows"_S) {
        triple_vendor = "pc"_S;
        triple_system = "windows"_S;
        triple_abi = "msvc"_S;
    } else if(os == "emscripten"_S) {
        triple_vendor = "unknown"_S;
        triple_system = "emscripten"_S;
        triple_abi = "unknown"_S;
    } else {
        abort();
    }

    buffer.append(triple_architecture);
    buffer.append("-"_S);
    buffer.append(triple_vendor);
    buffer.append("-"_S);
    buffer.append(triple_system);
    buffer.append("-"_S);
    buffer.append(triple_abi);

    return buffer;
}

String get_host_architecture() {
#if defined(ARCH_X64)
    return "x64"_S;
#endif
}

String get_host_os() {
#if defined(OS_LINUX)
    return "linux"_S;
#elif defined(OS_WINDOWS)
    return "windows"_S;
#endif
}
