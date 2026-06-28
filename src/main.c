#include "shell.h"
#include "version.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[], char *envp[]) {
    if (argc >= 2) {
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
            write(STDOUT_FILENO, "cactsole ", 9);
            write(STDOUT_FILENO, CACTSOLE_VERSION, strlen(CACTSOLE_VERSION));
            write(STDOUT_FILENO, "\n", 1);
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            static const char usage[] =
                "cactsole " CACTSOLE_VERSION " — interactive shell for Cact OS\n"
                "\n"
                "usage:  cactsole              start interactive shell\n"
                "        cactsole --help        show this message\n"
                "        cactsole --version     show version\n"
                "\n";
            write(STDOUT_FILENO, usage, sizeof(usage) - 1);
            return 0;
        }
    }
    (void)argc; (void)argv;
    shell_run(envp);
    return 0;
}