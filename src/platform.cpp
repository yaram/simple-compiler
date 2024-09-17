#include "platform.h"
#include "util.h"

bool does_os_exist(String os) {
    return
        os == "linux"_S ||
        os == "windows"_S ||
        os == "emscripten"_S ||
        os == "wasi"_S
    ;
}

bool does_architecture_exist(String architecture) {
    return
        architecture == "x86"_S ||
        architecture == "x64"_S ||
        architecture == "riscv32"_S ||
        architecture == "riscv64"_S ||
        architecture == "wasm32"_S
    ;
}

bool does_toolchain_exist(String toolchain) {
    return
        toolchain == "gnu"_S ||
        toolchain == "msvc"_S
    ;
}

bool is_supported_target(String os, String architecture, String toolchain) {
    if(os == "linux"_S) {
        return 
            (
                architecture == "x86"_S ||
                architecture == "x64"_S ||
                architecture == "riscv32"_S ||
                architecture == "riscv64"_S
            ) &&
            toolchain == "gnu"_S
        ;
    } else if(os == "windows"_S) {
        return 
            (
                architecture == "x86"_S ||
                architecture == "x64"_S
            ) &&
            (
                toolchain == "gnu"_S ||
                toolchain == "msvc"_S
            )
        ;
    } else if(os == "emscripten"_S) {
        return architecture == "wasm32"_S && toolchain == "gnu"_S;
    } else if(os == "wasi"_S) {
        return architecture == "wasm32"_S && toolchain == "gnu"_S;
    } else {
        abort();
    }
}

ArchitectureSizes get_architecture_sizes(String architecture) {
    if(architecture == "x86"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size32;
        sizes.default_integer_size = RegisterSize::Size32;
        sizes.default_float_size = RegisterSize::Size32;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == "x64"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size64;
        sizes.default_integer_size = RegisterSize::Size64;
        sizes.default_float_size = RegisterSize::Size64;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == "riscv32"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size32;
        sizes.default_integer_size = RegisterSize::Size32;
        sizes.default_float_size = RegisterSize::Size32;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == "riscv64"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size64;
        sizes.default_integer_size = RegisterSize::Size64;
        sizes.default_float_size = RegisterSize::Size64;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == "wasm32"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size64;
        sizes.default_integer_size = RegisterSize::Size64;
        sizes.default_float_size = RegisterSize::Size64;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else {
        abort();
    }
}

String get_default_toolchain(String os) {
    if(os == "linux"_S) {
        return "gnu"_S;
    } else if(os == "windows"_S) {
        auto host_os = get_host_os();

        if(host_os == "windows"_S) {
            return "msvc"_S;
        } else {
            return "gnu"_S;
        }
    } else if(os == "emscripten"_S) {
        return "gnu"_S;
    } else if(os == "wasi"_S) {
        return "gnu"_S;
    } else {
        abort();
    }
}

String get_llvm_triple(String architecture, String os, String toolchain) {
    StringBuffer buffer {};

    String triple_architecture;
    if(architecture == "x86"_S) {
        triple_architecture = "i686"_S;
    } else if(architecture == "x64"_S) {
        triple_architecture = "x86_64"_S;
    } else if(architecture == "riscv32"_S) {
        triple_architecture = "riscv32"_S;
    } else if(architecture == "riscv64"_S) {
        triple_architecture = "riscv64"_S;
    } else if(architecture == "wasm32"_S) {
        triple_architecture = "wasm32"_S;
    } else {
        abort();
    }

    String triple_vendor;
    String triple_system;
    if(os == "linux"_S) {
        triple_vendor = "unknown"_S;
        triple_system = "linux"_S;
    } else if(os == "windows"_S) {
        triple_vendor = "pc"_S;
        triple_system = "windows"_S;
    } else if(os == "emscripten"_S) {
        triple_vendor = "unknown"_S;
        triple_system = "emscripten"_S;
    } else if(os == "wasi"_S) {
        triple_vendor = "unknown"_S;
        triple_system = "wasi"_S;
    } else {
        abort();
    }

    String triple_abi;
    if(toolchain == "gnu"_S) {
        triple_abi = "gnu"_S;
    } else if(toolchain == "msvc"_S) {
        triple_abi = "msvc"_S;
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

String get_llvm_features(String architecture) {
    if(architecture == "x86"_S) {
        return ""_S;
    } else if(architecture == "x64"_S) {
        return ""_S;
    } else if(architecture == "riscv32"_S) {
        return "+m,+a,+f,+d,+c,+zicsr"_S;
    } else if(architecture == "riscv64"_S) {
        return "+m,+a,+f,+d,+c,+zicsr"_S;
    } else if(architecture == "wasm32"_S) {
        return ""_S;
    } else {
        abort();
    }
}

String get_host_architecture() {
#if defined(ARCH_X64)
    return "x64"_S;
#elif defined(ARCH_RISCV64)
    return "riscv64"_S;
#endif
}

String get_host_os() {
#if defined(OS_LINUX)
    return "linux"_S;
#elif defined(OS_WINDOWS)
    return "windows"_S;
#endif
}
