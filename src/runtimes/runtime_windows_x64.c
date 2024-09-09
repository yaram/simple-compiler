int MAIN(void);

void ExitProcess(unsigned int uExitCode);

void entry(void) {
    asm("and $-16, %rsp"); // Align stack to 16-byte boundaries for SSE to avoid segmentation fault

    int result = MAIN();

    ExitProcess(result);
}

// Needed by GNU toolchain (MinGW)
void __main(void) {}

int _fltused;

// Adapted from https://github.com/llvm/llvm-project/blob/57b08b0/compiler-rt/lib/builtins/x86_64/chkstk2.S
asm(
".text\n"
".balign 4\n"
".global __chkstk\n"
"__chkstk:\n"
    "push %rcx\n"
    "cmp $0x1000, %rax\n"
    "lea 16(%rsp), %rcx\n"
    "jb .end\n"
".loop:\n"
    "sub $0x1000, %rcx\n"
    "test %rcx, (%rcx)\n"
    "sub $0x1000, %rax\n"
    "cmp $0x1000, %rax\n"
    "ja .loop\n"
".end:\n"
    "sub %rax, %rcx\n"
    "test %rcx, (%rcx)\n"
    "lea 8(%rsp), %rax\n"
    "mov %rcx, %rsp\n"
    "mov -8(%rax), %rcx\n"
    "push (%rax)\n"
    "sub %rsp, %rax\n"
    "ret"
);