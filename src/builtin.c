#include "builtin.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int builtin_cd(char **argv, int argc) {
    const char *dir = (argc >= 2) ? argv[1] : "/";
    if (chdir(dir) != 0) {
        write(STDERR_FILENO, "cd: ", 4);
        write(STDERR_FILENO, dir, strlen(dir));
        write(STDERR_FILENO, ": no such directory\n", 20);
    }
    return 1;
}

static int builtin_pwd(char **argv, int argc) {
    (void)argv; (void)argc;
    char buf[512];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 1;
}

static int builtin_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 1;
}

static int builtin_echo(char **argv, int argc) {
    int i;
    for (i = 1; i < argc; i++) {
        write(STDOUT_FILENO, argv[i], strlen(argv[i]));
        if (i < argc - 1)
            write(STDOUT_FILENO, " ", 1);
    }
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

static int builtin_help(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char msg[] =
        "cactsole built-in commands:\n"
        "  cd [dir]       change directory (default /)\n"
        "  pwd            print working directory\n"
        "  echo [args...] print arguments\n"
        "  exit [code]    exit the shell\n"
        "  help           show this message\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 1;
}

static const struct builtin_cmd builtins[] = {
    {"cd",   builtin_cd},
    {"pwd",  builtin_pwd},
    {"exit", builtin_exit},
    {"echo", builtin_echo},
    {"help", builtin_help},
    {NULL,   NULL}
};

/* Returns 1 if command was a builtin and was executed, 0 otherwise. */
int builtin_run(char **argv, int argc) {
    if (argc == 0 || argv[0] == NULL)
        return 0;
    int i;
    for (i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(argv[0], (char *)builtins[i].name) == 0)
            return builtins[i].fn(argv, argc);
    }
    return 0;
}