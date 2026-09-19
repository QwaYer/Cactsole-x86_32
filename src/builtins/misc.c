/*
 * builtins/misc.c — exit, help.
 *
 * `help` намеренно не держит каталог команд.  Всё, что лежит в CactUserBins,
 * он не описывает, а читает из каталогов PATH во время вызова; описания есть
 * только у билтинов — рядом с их реализацией, в тех же таблицах, по которым
 * они и исполняются.  Поэтому вывод не может разъехаться с реальностью.
 */

#include "builtin.h"
#include "builtins.h"
#include "shell.h"
#include "version.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#define PROG_MAX      256
#define PROG_NAME_MAX 64

static char prog_names[PROG_MAX][PROG_NAME_MAX];
static int  prog_count;

static int cmd_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;
}

static void out(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
}

static void err(const char *s) {
    write(STDERR_FILENO, s, strlen(s));
}

static void print_help_header(void) {
    out("\033[1;36m  ==============================\n");
    out("\033[1;36m     CactOS Shell v\033[0m");
    out(CACTSOLE_VERSION);
    out("\n");
    out("\033[1;36m  ==============================\033[0m\n\n");
}

static void print_usage(void) {
    out("  usage:  COMMAND [ARGS...]  ;  CMD1 | CMD2  ;  CMD &\n");
    out("          help <command>      builtin usage, or the program's own --help\n");
    out("\n");
}

static void print_section(const char *title) {
    char buf[160];
    int pos = 0;
    static const char pfx[] = "\033[1;33m  --- \033[0m ";
    memcpy(buf + pos, pfx, sizeof(pfx) - 1); pos += sizeof(pfx) - 1;
    int tlen = (int)strlen(title);
    memcpy(buf + pos, title, tlen); pos += tlen;
    buf[pos++] = ' ';
    int pad = 52 - tlen;
    if (pad < 3) pad = 3;
    for (int i = 0; i < pad; i++)
        buf[pos++] = '-';
    buf[pos++] = '\n';
    write(STDOUT_FILENO, buf, pos);
}

static void print_table(const struct builtin_cmd *t) {
    for (int i = 0; t[i].name != NULL; i++) {
        out("  ");
        out(t[i].help);
        out("\n");
    }
}

static int prog_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void prog_add(const char *name) {
    int len = (int)strlen(name);
    if (len == 0 || len >= PROG_NAME_MAX || prog_count >= PROG_MAX)
        return;
    for (int i = 0; i < prog_count; i++)
        if (strcmp(prog_names[i], name) == 0)
            return;
    memcpy(prog_names[prog_count], name, (size_t)len + 1);
    prog_count++;
}

static void prog_scan_dir(const char *dir, int dlen) {
    char path[300];
    if (dlen <= 0 || dlen >= (int)sizeof(path) - 2)
        return;
    memcpy(path, dir, (size_t)dlen);
    if (path[dlen - 1] != '/')
        path[dlen++] = '/';
    path[dlen] = '\0';

    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return;

    struct dirent buf[32];
    int n;
    while ((n = getdents(fd, buf, (unsigned int)sizeof(buf))) > 0) {
        int cnt = n / (int)sizeof(struct dirent);
        for (int i = 0; i < cnt; i++) {
            if (buf[i].d_name[0] == '.')
                continue;
            prog_add(buf[i].d_name);
        }
    }
    close(fd);
}

static void prog_collect(void) {
    const char *path = env_get("PATH");
    if (!path) path = "/bin:/sbin:/usr/bin";

    const char *seg = path;
    while (*seg) {
        const char *end = seg;
        while (*end && *end != ':') end++;
        prog_scan_dir(seg, (int)(end - seg));
        if (!*end) break;
        seg = end + 1;
    }

    if (prog_count > 1)
        qsort(prog_names, (size_t)prog_count, PROG_NAME_MAX, prog_cmp);
}

static void print_programs(void) {
    if (prog_count == 0) {
        out("  (no directory on PATH could be read)\n");
        return;
    }

    int w = 0;
    for (int i = 0; i < prog_count; i++) {
        int l = (int)strlen(prog_names[i]);
        if (l > w) w = l;
    }
    w += 2;

    int cols = 76 / w;
    if (cols < 1) cols = 1;
    int rows = (prog_count + cols - 1) / cols;

    char line[160];
    for (int r = 0; r < rows; r++) {
        int pos = 0;
        line[pos++] = ' ';
        line[pos++] = ' ';
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;
            if (idx >= prog_count)
                break;
            int l = (int)strlen(prog_names[idx]);
            memcpy(line + pos, prog_names[idx], (size_t)l);
            pos += l;
            if (idx + rows < prog_count)
                for (int s = l; s < w; s++)
                    line[pos++] = ' ';
        }
        line[pos++] = '\n';
        write(STDOUT_FILENO, line, pos);
    }
}

static int cmd_help(char **argv, int argc) {
    if (argc >= 2) {
        char *help_argv[3] = {argv[1], "--help", NULL};
        int st;
        if (builtin_invoke(help_argv, 2, &st))
            return st;

        /* Не билтин: справку печатает сама программа — у каждой утилиты
         * CactUserBins есть --help.  Шелл запускает её и отдаёт вывод как
         * есть: своих описаний чужих команд он не хранит. */
        char path[512];
        if (shell_path_find(argv[1], path, sizeof(path)) != 0) {
            err("cactsole: '");
            err(argv[1]);
            err("' is not a builtin and was not found on PATH\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            err("cactsole: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            execve(path, help_argv, shell_env);
            err("cactsole: exec failed: ");
            err(path);
            err("\n");
            exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
            return 1;
        return (status >> 8) & 0xff;
    }

    print_help_header();
    print_usage();

    print_section("Builtins (in cactsole)");
    print_table(nav_table());
    print_table(env_table());
    print_table(jobs_table());
    print_table(misc_table());

    print_section("Programs on PATH");
    prog_collect();
    print_programs();

    out("\n  These are separate programs (CactUserBins), not shell builtins.\n");

    return 0;
}

static const struct builtin_cmd table[] = {
    {"exit", cmd_exit, "exit [code]       leave shell (default: 0)"},
    {"help", cmd_help, "help [command]    show this screen or command help"},
    {NULL, NULL, NULL},
};

int misc_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const struct builtin_cmd *misc_table(void) {
    return table;
}
