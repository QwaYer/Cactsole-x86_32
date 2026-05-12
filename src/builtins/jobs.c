/*
 * builtins/jobs.c — управление фоновыми задачами.
 *
 *   jobs / fg / bg
 */

#include "builtin.h"
#include "builtins.h"
#include "shell.h"

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
    {"jobs", cmd_jobs},
    {"fg",   cmd_fg},
    {"bg",   cmd_bg},
    {NULL, NULL},
};

int jobs_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *jobs_help_bin(void) {
    return
        "\nJobs:\n"
        "  jobs                    list background jobs\n"
        "  fg [n]                  bring job n to foreground\n"
        "  bg [n]                  resume job n in background\n";
}

const char *jobs_help_sbin(void) {
    return "";
}
