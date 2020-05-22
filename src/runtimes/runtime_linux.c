int MAIN();

void entry() {
    asm("andq $-16, %rsp"); // Align stack to 16-byte boundaries for SSE to avoid segmentation fault

    int result = MAIN();

    asm(
        "movq $231, %%rax\n"
        "movq %0, %%rdi\n"
        "syscall"
        :
        : "r"((long long)result)
        : "rax", "rdi"
    );
}