/*
 * builtins/misc.c — exit, help. Остальное — ELF в CactUserBins-x86_32 → /bin.
 */

#include "builtin.h"
#include "builtins.h" /* misc_help_* declarations */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int cmd_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;
}

static int cmd_help(char **argv, int argc) {
    (void)argv;
    (void)argc;
    /* Plain help: one command (or group) per line, short gloss on the right. */
    static const char page[] =
        "\n"
        "cactsole help\n"
        "\n"
        "usage:  COMMAND [ARGS...]  ;  CMD1 | CMD2  ;  CMD &\n"
        "\n"
        "cd [DIR]                    chdir; omit DIR -> $HOME\n"
        "export [NAME[=VAL] ...]     set or list env vars\n"
        "unset NAME ...              remove env vars\n"
        "env                         print environment\n"
        "jobs                        list background jobs\n"
        "fg [N]  bg [N]              job control (N defaults to 1)\n"
        "exit [CODE]                 leave shell (default 0)\n"
        "help                        this screen\n"
        "\n"
        "pwd                         print working directory\n"
        "ls                          list directory\n"
        "mkdir                       make directory\n"
        "rmdir                       remove empty directory\n"
        "tch                         touch file\n"
        "rm                          remove file\n"
        "cat                         print file\n"
        "wrt                         write file\n"
        "stat                        file metadata\n"
        "mv                          move or rename\n"
        "ln                          hard link\n"
        "readlink                    read symlink target\n"
        "clear                       clear screen\n"
        "date                        print date/time\n"
        "uptime                      uptime\n"
        "sleep                       sleep seconds\n"
        "free                        memory summary\n"
        "fetch                       HTTP fetch\n"
        "run                         run script or command\n"
        "echo                        print arguments\n"
        "true                        exit 0\n"
        "false                       exit 1\n"
        "whoami                      current user\n"
        "id                          user id\n"
        "chmod                       change mode\n"
        "chown                       change owner\n"
        "version                     system version\n"
        "nconn                       network connect\n"
        "net                         network client\n"
        "\n"
        "kill                        send signal\n"
        "su                          substitute user\n"
        "modload                     load kernel module\n"
        "modunload                   unload kernel module\n"
        "ping                        ICMP echo\n"
        "dhcp                        DHCP client\n"
        "dns                         DNS lookup\n"
        "\n"
        "PIPE                        if line starts with  cd DIR  then a pipe,\n"
        "                            the shell runs cd first (e.g. cd /bin then ls).\n";
    write(STDOUT_FILENO, page, sizeof(page) - 1);
    return 0;
}

static const struct builtin_cmd table[] = {
    {"exit", cmd_exit},
    {"help", cmd_help},
    {NULL, NULL},
};

int misc_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *misc_help_bin(void) {
    return "";
}

const char *misc_help_sbin(void) {
    return "";
}
