int __stdcall MAIN(void);

void __stdcall ExitProcess(unsigned int uExitCode);

void __stdcall entry() {
    asm("and $-16, %esp"); // Align stack to 16-byte boundaries for SSE to avoid segmentation fault

    int result = MAIN();

    ExitProcess(result);
}

// Needed by GNU toolchain (MinGW)
void __main() {}

int _fltused;
// Adapted from https://github.com/llvm/llvm-project/blob/57b08b0/compiler-rt/lib/builtins/i386/chkstk2.S
asm(
".text\n"
".balign 4\n"
".global __chkstk\n"
"__chkstk:\n"
    "push %ecx\n"
    "cmp $0x1000, %eax\n"
    "lea 8(%esp), %ecx\n"
    "jb .end\n"
".loop:\n"
    "sub $0x1000, %ecx\n"
    "test %ecx, (%ecx)\n"
    "sub $0x1000, %eax\n"
    "cmp $0x1000, %eax\n"
    "ja .loop\n"
".end:\n"
    "sub %eax, %ecx\n"
    "test %ecx, (%ecx)\n"
    "lea 4(%esp), %eax\n"
    "mov %ecx, %esp\n"
    "mov -4(%eax), %ecx\n"
    "push (%eax)\n"
    "sub %esp, %eax\n"
    "ret"
);