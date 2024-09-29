#include "platform.h"
#include "util.h"

bool does_os_exist(String os) {
    return
        os == u8"linux"_S ||
        os == u8"windows"_S ||
        os == u8"emscripten"_S ||
        os == u8"wasi"_S
    ;
}

bool does_architecture_exist(String architecture) {
    return
        architecture == u8"x86"_S ||
        architecture == u8"x64"_S ||
        architecture == u8"riscv32"_S ||
        architecture == u8"riscv64"_S ||
        architecture == u8"wasm32"_S
    ;
}

bool does_toolchain_exist(String toolchain) {
    return
        toolchain == u8"gnu"_S ||
        toolchain == u8"msvc"_S
    ;
}

bool is_supported_target(String os, String architecture, String toolchain) {
    if(os == u8"linux"_S) {
        return 
            (
                architecture == u8"x86"_S ||
                architecture == u8"x64"_S ||
                architecture == u8"riscv32"_S ||
                architecture == u8"riscv64"_S
            ) &&
            toolchain == u8"gnu"_S
        ;
    } else if(os == u8"windows"_S) {
        return 
            (
                architecture == u8"x86"_S ||
                architecture == u8"x64"_S
            ) &&
            (
                toolchain == u8"gnu"_S ||
                toolchain == u8"msvc"_S
            )
        ;
    } else if(os == u8"emscripten"_S) {
        return architecture == u8"wasm32"_S && toolchain == u8"gnu"_S;
    } else if(os == u8"wasi"_S) {
        return architecture == u8"wasm32"_S && toolchain == u8"gnu"_S;
    } else {
        abort();
    }
}

ArchitectureSizes get_architecture_sizes(String architecture) {
    if(architecture == u8"x86"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size32;
        sizes.default_integer_size = RegisterSize::Size32;
        sizes.default_float_size = RegisterSize::Size32;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == u8"x64"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size64;
        sizes.default_integer_size = RegisterSize::Size64;
        sizes.default_float_size = RegisterSize::Size64;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == u8"riscv32"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size32;
        sizes.default_integer_size = RegisterSize::Size32;
        sizes.default_float_size = RegisterSize::Size32;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == u8"riscv64"_S) {
        ArchitectureSizes sizes {};
        sizes.address_size = RegisterSize::Size64;
        sizes.default_integer_size = RegisterSize::Size64;
        sizes.default_float_size = RegisterSize::Size64;
        sizes.boolean_size = RegisterSize::Size8;

        return sizes;
    } else if(architecture == u8"wasm32"_S) {
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
    if(os == u8"linux"_S) {
        return u8"gnu"_S;
    } else if(os == u8"windows"_S) {
        auto host_os = get_host_os();

        if(host_os == u8"windows"_S) {
            return u8"msvc"_S;
        } else {
            return u8"gnu"_S;
        }
    } else if(os == u8"emscripten"_S) {
        return u8"gnu"_S;
    } else if(os == u8"wasi"_S) {
        return u8"gnu"_S;
    } else {
        abort();
    }
}

String get_llvm_triple(String architecture, String os, String toolchain) {
    StringBuffer buffer {};

    String triple_architecture;
    if(architecture == u8"x86"_S) {
        triple_architecture = u8"i686"_S;
    } else if(architecture == u8"x64"_S) {
        triple_architecture = u8"x86_64"_S;
    } else if(architecture == u8"riscv32"_S) {
        triple_architecture = u8"riscv32"_S;
    } else if(architecture == u8"riscv64"_S) {
        triple_architecture = u8"riscv64"_S;
    } else if(architecture == u8"wasm32"_S) {
        triple_architecture = u8"wasm32"_S;
    } else {
        abort();
    }

    String triple_vendor;
    String triple_system;
    if(os == u8"linux"_S) {
        triple_vendor = u8"unknown"_S;
        triple_system = u8"linux"_S;
    } else if(os == u8"windows"_S) {
        triple_vendor = u8"pc"_S;
        triple_system = u8"windows"_S;
    } else if(os == u8"emscripten"_S) {
        triple_vendor = u8"unknown"_S;
        triple_system = u8"emscripten"_S;
    } else if(os == u8"wasi"_S) {
        triple_vendor = u8"unknown"_S;
        triple_system = u8"wasi"_S;
    } else {
        abort();
    }

    String triple_abi;
    if(os == u8"emscripten"_S) {
        triple_abi = u8""_S;
    } else {
        if(toolchain == u8"gnu"_S) {
            triple_abi = u8"gnu"_S;
        } else if(toolchain == u8"msvc"_S) {
            triple_abi = u8"msvc"_S;
        } else {
            abort();
        }
    }

    buffer.append(triple_architecture);
    buffer.append(u8"-"_S);
    buffer.append(triple_vendor);
    buffer.append(u8"-"_S);
    buffer.append(triple_system);

    if(triple_abi.length != 0) {
        buffer.append(u8"-"_S);
        buffer.append(triple_abi);
    }

    return buffer;
}

String get_llvm_features(String architecture) {
    if(architecture == u8"x86"_S) {
        return u8""_S;
    } else if(architecture == u8"x64"_S) {
        return u8""_S;
    } else if(architecture == u8"riscv32"_S) {
        return u8"+m,+a,+f,+d,+c,+zicsr"_S;
    } else if(architecture == u8"riscv64"_S) {
        return u8"+m,+a,+f,+d,+c,+zicsr"_S;
    } else if(architecture == u8"wasm32"_S) {
        return u8""_S;
    } else {
        abort();
    }
}

String get_host_architecture() {
#if defined(ARCH_X64)
    return u8"x64"_S;
#elif defined(ARCH_RISCV64)
    return u8"riscv64"_S;
#endif
}

String get_host_os() {
#if defined(OS_LINUX)
    return u8"linux"_S;
#elif defined(OS_WINDOWS)
    return u8"windows"_S;
#endif
}
