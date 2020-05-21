int MAIN();

void __stdcall ExitProcess(unsigned int uExitCode);

void __stdcall entry() {
    int result = MAIN();

    ExitProcess(result);
}

int _fltused;