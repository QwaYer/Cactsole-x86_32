#ifndef BUILTIN_H
#define BUILTIN_H

struct builtin_cmd {
    const char *name;
    int (*fn)(char **argv, int argc);
    const char *help;
};

int builtin_invoke(char **argv, int argc, int *status);

int builtin_run(char **argv, int argc);

int builtin_table_run(const struct builtin_cmd *table,
                      char **argv, int argc);

#endif