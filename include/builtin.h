#ifndef BUILTIN_H
#define BUILTIN_H

struct builtin_cmd {
    const char *name;
    int (*fn)(char **argv, int argc);
};

int builtin_run(char **argv, int argc);

#endif