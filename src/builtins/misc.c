/*
 * builtins/misc.c — прочие команды, включая `help`.
 *
 *   true / false / exit / whoami / id / chmod / chown / version / help
 *
 * Команда help аккумулирует секции из всех групп через builtins.h,
 * чтобы добавление новой группы автоматически отражалось в выводе.
 */

#include "builtin.h"
#include "builtins.h"
#include "version.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stat.h>

static int cmd_true(char **argv, int argc) {
    (void)argv; (void)argc; return 0;
}

static int cmd_false(char **argv, int argc) {
    (void)argv; (void)argc; return 1;
}

static int cmd_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;
}

static int cmd_whoami(char **argv, int argc) {
    (void)argv; (void)argc;
    uid_t uid = getuid();
    char num[16];
    if (uid == 0) {
        write(STDOUT_FILENO, "root\n", 5);
    } else {
        write(STDOUT_FILENO, "user", 4);
        itoa((int)uid, num);
        write(STDOUT_FILENO, num, strlen(num));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int cmd_id(char **argv, int argc) {
    (void)argv; (void)argc;
    char num[16];
    uid_t uid  = getuid();
    gid_t gid  = getgid();
    uid_t euid = geteuid();
    gid_t egid = getegid();

    write(STDOUT_FILENO, "uid=", 4);
    itoa((int)uid,  num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " gid=", 5);
    itoa((int)gid,  num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " euid=", 6);
    itoa((int)euid, num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " egid=", 6);
    itoa((int)egid, num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int cmd_chmod(char **argv, int argc) {
    if (argc < 3) {
        write(STDERR_FILENO, "usage: chmod MODE FILE...\n", 26);
        return 1;
    }
    int mode = 0;
    const char *s = argv[1];
    while (*s >= '0' && *s <= '7') {
        mode = mode * 8 + (*s - '0');
        s++;
    }
    if (*s != '\0') {
        write(STDERR_FILENO, "chmod: invalid mode (use octal, e.g. 755)\n", 43);
        return 1;
    }
    int i, ret = 0;
    for (i = 2; i < argc; i++) {
        if (chmod(argv[i], mode) != 0) {
            write(STDERR_FILENO, "chmod: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int cmd_chown(char **argv, int argc) {
    if (argc < 3) {
        write(STDERR_FILENO, "usage: chown OWNER[:GROUP] FILE...\n", 35);
        return 1;
    }
    const char *spec = argv[1];
    int uid = 0, gid = -1;
    const char *p = spec;
    while (*p >= '0' && *p <= '9') { uid = uid * 10 + (*p - '0'); p++; }
    if (*p == ':') {
        p++;
        gid = 0;
        while (*p >= '0' && *p <= '9') { gid = gid * 10 + (*p - '0'); p++; }
    }
    if (*p != '\0') {
        write(STDERR_FILENO, "chown: invalid owner (use numeric uid[:gid])\n", 45);
        return 1;
    }
    int i, ret = 0;
    for (i = 2; i < argc; i++) {
        if (chown(argv[i], uid, gid) != 0) {
            write(STDERR_FILENO, "chown: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int cmd_version(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char msg[] = "cactsole " CACTSOLE_VERSION "\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 0;
}

static void put_section(const char *s) {
    write(STDOUT_FILENO, s, strlen((char *)s));
}

static int cmd_help(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char header[] = "cactsole built-in commands:\n";
    write(STDOUT_FILENO, header, sizeof(header) - 1);
    put_section(nav_help());
    put_section(files_help());
    put_section(sys_help());
    put_section(env_help());
    put_section(jobs_help());
    put_section(misc_help());
    put_section(net_help());
    return 0;
}

static const struct builtin_cmd table[] = {
    {"true",    cmd_true},
    {"false",   cmd_false},
    {"exit",    cmd_exit},
    {"whoami",  cmd_whoami},
    {"id",      cmd_id},
    {"chmod",   cmd_chmod},
    {"chown",   cmd_chown},
    {"version", cmd_version},
    {"help",    cmd_help},
    {NULL, NULL},
};

int misc_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *misc_help(void) {
    return
        "  Misc:\n"
        "    true                   exit 0\n"
        "    false                  exit 1\n"
        "    exit [code]            exit the shell\n"
        "    whoami                 print current user name\n"
        "    id                     print uid/gid/euid/egid\n"
        "    chmod MODE FILE...     change file permissions (octal)\n"
        "    chown OWNER[:GRP] F... change file owner (numeric)\n"
        "    version                print shell version\n"
        "    help                   show this message\n";
}
