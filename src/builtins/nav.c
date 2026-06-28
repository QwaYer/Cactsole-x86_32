/*
 * builtins/nav.c — только cd (pwd — отдельный ELF /bin/pwd).
 */

#include "builtin.h"
#include "builtins.h"
#include "shell.h"

#include <unistd.h>
#include <string.h>

static int cmd_cd(char **argv, int argc) {
    const char *dir;
    if (argc >= 2) {
        dir = argv[1];
    } else {
        dir = env_get("HOME");
        if (!dir) dir = "/";
    }
    if (chdir(dir) != 0) {
        write(STDERR_FILENO, "cd: ", 4);
        write(STDERR_FILENO, dir, strlen(dir));
        write(STDERR_FILENO, ": no such directory\n", 20);
        return 1;
    }
    return 0;
}

static const struct builtin_cmd table[] = {
    {"cd", cmd_cd, "cd [dir]         change directory (default: $HOME)"},
    {NULL, NULL, NULL},
};

int nav_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *nav_help_bin(void) {
    return
        "  pwd               print working directory (/bin/pwd)\n";
}

const char *nav_help_sbin(void) {
    return "";
}
