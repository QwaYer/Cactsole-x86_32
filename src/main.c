#include "shell.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc; (void)argv; (void)envp;
    shell_run();
    return 0;
}