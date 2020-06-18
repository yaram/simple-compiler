int MAIN();

void entry() {
    asm("and $-16, %esp"); // Align stack to 16-byte boundaries for SSE to avoid segmentation fault

    int result = MAIN();

    // Call exit_group system call
    asm(
        "movq $231, %%eax\n"
        "movq %0, %%ebx\n"
        "int $0x80"
        :
        : "r"(result)
        : "eax", "ebx"
    );
}