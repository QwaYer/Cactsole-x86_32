/*
 * builtins/env.c — export / unset / env (echo — /bin/echo).
 */

#include "builtin.h"
#include "builtins.h"
#include "shell.h"

#include <unistd.h>
#include <string.h>

static int cmd_export(char **argv, int argc) {
    int i;
    if (argc < 2) {
        for (i = 0; shell_env[i]; i++) {
            write(STDOUT_FILENO, "export ", 7);
            write(STDOUT_FILENO, shell_env[i], strlen(shell_env[i]));
            write(STDOUT_FILENO, "\n", 1);
        }
        return 0;
    }
    for (i = 1; i < argc; i++) {
        char *arg = argv[i], *eq = arg;
        char name[64];
        int nlen;
        while (*eq && *eq != '=') eq++;
        nlen = (int)(eq - arg);
        if (*eq == '=') {
            if (nlen < 64) {
                memcpy(name, arg, nlen);
                name[nlen] = '\0';
                env_set(name, eq + 1);
            }
        } else {
            if (!env_get(arg)) env_set(arg, "");
        }
    }
    return 0;
}

static int cmd_unset(char **argv, int argc) {
    int i;
    for (i = 1; i < argc; i++) env_unset(argv[i]);
    return 0;
}

static int cmd_env(char **argv, int argc) {
    (void)argv;
    (void)argc;
    int i;
    for (i = 0; shell_env[i]; i++) {
        write(STDOUT_FILENO, shell_env[i], strlen(shell_env[i]));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static const struct builtin_cmd table[] = {
    {"export", cmd_export, "export [VAR=value]  set/list environment variables"},
    {"unset",  cmd_unset,  "unset VAR...        remove environment variables"},
    {"env",    cmd_env,    "env                 print all environment variables"},
    {NULL, NULL, NULL},
};

int env_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *env_help_bin(void) {
    return
        "  echo [args...]     print arguments (/bin/echo)\n";
}

const char *env_help_sbin(void) {
    return "";
}
