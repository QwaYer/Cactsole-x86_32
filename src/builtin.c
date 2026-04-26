#include "builtin.h"
#include "shell.h"
#include "version.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int builtin_cd(char **argv, int argc) {
    const char *dir;
    if (argc >= 2) {
        dir = argv[1];
    } else {
        dir = env_get("HOME");
        if (!dir) dir = "/";
    }
    if (chdir(dir) != 0) {
        write(STDERR_FILENO, "cd: ", 4);
        write(STDERR_FILENO, dir, strlen(dir));
        write(STDERR_FILENO, ": no such directory\n", 20);
        return 1;
    }
    return 0;
}

static int builtin_pwd(char **argv, int argc) {
    (void)argv; (void)argc;
    char buf[512];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int builtin_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;
}

static int builtin_echo(char **argv, int argc) {
    int i;
    for (i = 1; i < argc; i++) {
        write(STDOUT_FILENO, argv[i], strlen(argv[i]));
        if (i < argc - 1) write(STDOUT_FILENO, " ", 1);
    }
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_export(char **argv, int argc) {
    int i;
    if (argc < 2) {
        for (i = 0; shell_env[i]; i++) {
            write(STDOUT_FILENO, "export ", 7);
            write(STDOUT_FILENO, shell_env[i], strlen(shell_env[i]));
            write(STDOUT_FILENO, "\n", 1);
        }
        return 0;
    }
    for (i = 1; i < argc; i++) {
        char *arg = argv[i], *eq = arg;
        char name[64]; int nlen;
        while (*eq && *eq != '=') eq++;
        nlen = (int)(eq - arg);
        if (*eq == '=') {
            if (nlen < 64) {
                memcpy(name, arg, nlen); name[nlen] = '\0';
                env_set(name, eq + 1);
            }
        } else {
            if (!env_get(arg)) env_set(arg, "");
        }
    }
    return 0;
}

static int builtin_unset(char **argv, int argc) {
    int i;
    for (i = 1; i < argc; i++) env_unset(argv[i]);
    return 0;
}

static int builtin_env(char **argv, int argc) {
    (void)argv; (void)argc;
    int i;
    for (i = 0; shell_env[i]; i++) {
        write(STDOUT_FILENO, shell_env[i], strlen(shell_env[i]));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int builtin_jobs(char **argv, int argc) {
    (void)argv; (void)argc;
    shell_list_jobs();
    return 0;
}

static int builtin_fg(char **argv, int argc) {
    int n = (argc >= 2) ? atoi(argv[1]) : 1;
    return shell_fg_job(n);
}

static int builtin_bg(char **argv, int argc) {
    int n = (argc >= 2) ? atoi(argv[1]) : 1;
    return shell_bg_job(n);
}

static int builtin_true(char **argv, int argc) {
    (void)argv; (void)argc; return 0;
}

static int builtin_false(char **argv, int argc) {
    (void)argv; (void)argc; return 1;
}

static int builtin_version(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char msg[] = "cactsole " CACTSOLE_VERSION "\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 0;
}

static int builtin_help(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char msg[] =
        "cactsole built-in commands:\n"
        "  cd [dir]          change directory (default $HOME)\n"
        "  pwd               print working directory\n"
        "  echo [args...]    print arguments\n"
        "  export [VAR[=v]]  set/list environment variables\n"
        "  unset VAR...      remove environment variables\n"
        "  env               print all environment variables\n"
        "  jobs              list background jobs\n"
        "  fg [n]            bring job n to foreground\n"
        "  bg [n]            resume job n in background\n"
        "  true              exit 0\n"
        "  false             exit 1\n"
        "  exit [code]       exit the shell\n"
        "  help              show this message\n"
        "  version           print shell version\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 0;
}

static const struct builtin_cmd builtins[] = {
    {"cd",      builtin_cd},
    {"pwd",     builtin_pwd},
    {"exit",    builtin_exit},
    {"echo",    builtin_echo},
    {"export",  builtin_export},
    {"unset",   builtin_unset},
    {"env",     builtin_env},
    {"jobs",    builtin_jobs},
    {"fg",      builtin_fg},
    {"bg",      builtin_bg},
    {"true",    builtin_true},
    {"false",   builtin_false},
    {"version", builtin_version},
    {"help",    builtin_help},
    {NULL,      NULL}
};

int builtin_run(char **argv, int argc) {
    int i;
    if (argc == 0 || argv[0] == NULL) return -1;
    for (i = 0; builtins[i].name != NULL; i++)
        if (strcmp(argv[0], (char *)builtins[i].name) == 0)
            return builtins[i].fn(argv, argc);
    return -1;
}