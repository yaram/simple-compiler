int MAIN();

void entry() {
    int result = MAIN();

    asm(
        "syscall"
        :
        : "a"(60), "D"(result)
    );
}