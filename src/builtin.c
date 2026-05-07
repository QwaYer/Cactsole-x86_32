/*
 * builtin.c — диспетчер встроенных команд cactsole.
 *
 * Сами команды живут в src/builtins/<group>.c.  Здесь только две функции:
 *
 *   builtin_table_run() — общий поиск-и-вызов в локальной таблице группы;
 *   builtin_run()       — обходит группы по очереди до первого совпадения.
 *
 * Чтобы добавить новую группу команд:
 *   1) создать src/builtins/<name>.c, реализовать <name>_run / <name>_help;
 *   2) добавить пару в include/builtins.h;
 *   3) дописать строку в массив groups[] ниже и в put_section() в misc.c.
 */

#include "builtin.h"
#include "builtins.h"

#include <string.h>

int builtin_table_run(const struct builtin_cmd *table,
                      char **argv, int argc) {
    if (argc == 0 || argv[0] == NULL) return -1;
    for (int i = 0; table[i].name != NULL; i++)
        if (strcmp(argv[0], (char *)table[i].name) == 0)
            return table[i].fn(argv, argc);
    return -1;
}

typedef int (*group_run_fn)(char **argv, int argc);

static const group_run_fn groups[] = {
    nav_run,
    files_run,
    sys_run,
    env_run,
    jobs_run,
    misc_run,
    net_run,
    NULL,
};

int builtin_run(char **argv, int argc) {
    if (argc == 0 || argv[0] == NULL) return -1;
    for (int i = 0; groups[i] != NULL; i++) {
        int rc = groups[i](argv, argc);
        if (rc != -1) return rc;
    }
    return -1;
}
