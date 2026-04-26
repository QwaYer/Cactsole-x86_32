#include "shell.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc; (void)argv;
    shell_run(envp);
    return 0;
}