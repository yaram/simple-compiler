#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if(argc != 3) {
        return 1;
    }

    char command[1024];

    strcpy(command, argv[1]);
    strcat(command, " ");
    strcat(command, argv[2]);

    if(system(command) != 0) {
        return 1;
    }

    auto test_result = system("out");

    if(test_result != 0) {
        fprintf(stderr, "Expected 0, got %d\n", test_result);

        return 1;
    }

    return 0;
}