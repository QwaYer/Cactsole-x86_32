/*
 * builtins/nav.c — навигация по файловой системе.
 *
 *   cd [DIR]   сменить текущий каталог; без аргументов — переход в $HOME (или /).
 *   pwd        напечатать текущий каталог.
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

static int cmd_pwd(char **argv, int argc) {
    (void)argv; (void)argc;
    char buf[512];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static const struct builtin_cmd table[] = {
    {"cd",  cmd_cd},
    {"pwd", cmd_pwd},
    {NULL,  NULL},
};

int nav_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *nav_help(void) {
    return
        "  Navigation:\n"
        "    cd [dir]               change directory (default $HOME)\n"
        "    pwd                    print working directory\n";
}
