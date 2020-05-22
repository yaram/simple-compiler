int MAIN();

void __stdcall ExitProcess(unsigned int uExitCode);

void __stdcall entry() {
    asm("andq $-16, %rsp"); // Align stack to 16-byte boundaries for SSE to avoid segmentation fault

    int result = MAIN();

    ExitProcess(result);
}

int _fltused;