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
    env_run,
    jobs_run,
    misc_run,
    NULL,
};

int builtin_invoke(char **argv, int argc, int *status) {
    if (argc == 0 || argv[0] == NULL || status == NULL) return 0;
    for (int i = 0; groups[i] != NULL; i++) {
        int rc = groups[i](argv, argc);
        if (rc != -1) {
            *status = rc;
            return 1;
        }
    }
    return 0;
}

int builtin_run(char **argv, int argc) {
    int st;
    if (!builtin_invoke(argv, argc, &st))
        return -1;
    if (st < 0)
        return 255;
    if (st > 255)
        return 255;
    return st;
}
