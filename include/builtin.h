#ifndef BUILTIN_H
#define BUILTIN_H

typedef int (*builtin_fn)(char **argv, int argc);

struct builtin_cmd {
    const char *name;
    builtin_fn  fn;
};

int builtin_run(char **argv, int argc);

#endif