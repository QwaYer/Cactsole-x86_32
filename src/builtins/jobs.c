/*
 * builtins/jobs.c — управление фоновыми задачами.
 *
 *   jobs / fg / bg
 */

#include "builtin.h"
#include "builtins.h"
#include "shell.h"

#include <unistd.h>
#include <stdlib.h>

static int cmd_jobs(char **argv, int argc) {
    (void)argv; (void)argc;
    shell_list_jobs();
    return 0;
}

static int cmd_fg(char **argv, int argc) {
    int n = (argc >= 2) ? atoi(argv[1]) : 1;
    return shell_fg_job(n);
}

static int cmd_bg(char **argv, int argc) {
    int n = (argc >= 2) ? atoi(argv[1]) : 1;
    return shell_bg_job(n);
}

static const struct builtin_cmd table[] = {
    {"jobs", cmd_jobs, "jobs               list background jobs"},
    {"fg",   cmd_fg,   "fg [n]             bring job n to foreground (default: 1)"},
    {"bg",   cmd_bg,   "bg [n]             resume job n in background (default: 1)"},
    {NULL, NULL, NULL},
};

int jobs_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *jobs_help_bin(void) {
    return "";
}

const char *jobs_help_sbin(void) {
    return "";
}
