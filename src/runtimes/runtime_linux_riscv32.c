int MAIN(void);

void entry(void) {
    int result = MAIN();

    // Call exit_group system call
    asm(
        "li a7, 231\n"
        "mv a0, %0\n"
        "ecall"
        :
        : "r"(result)
        : "a7", "a0"
    );
}