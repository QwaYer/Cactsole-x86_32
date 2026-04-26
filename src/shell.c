#include "shell.h"
#include "readline.h"
#include "builtin.h"
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stat.h>

#define MAX_ARGS     64
#define MAX_PIPELINE  8
#define LINE_MAX    1024
#define ENV_MAX      256
#define WORD_POOL_SZ (MAX_PIPELINE * MAX_ARGS)

char **shell_env;

static char *env_buf[ENV_MAX + 1];
static int   env_cnt;
static int   last_exit;

static char word_pool[WORD_POOL_SZ][256];
static int  word_pool_top;

typedef struct {
    char *argv[MAX_ARGS];
    int   argc;
    char *redir_in;
    char *redir_out;
    char *redir_app;
} Cmd;

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int starts_with_n(const char *s, const char *p, int n) {
    int i;
    for (i = 0; i < n; i++)
        if (s[i] != p[i]) return 0;
    return 1;
}

char *env_get(const char *name) {
    int i, nlen = strlen((char *)name);
    for (i = 0; i < env_cnt; i++)
        if (starts_with_n(env_buf[i], name, nlen) && env_buf[i][nlen] == '=')
            return env_buf[i] + nlen + 1;
    return NULL;
}

void env_set(const char *name, const char *val) {
    int i, nlen = strlen((char *)name), vlen = strlen((char *)val);
    char *e;
    for (i = 0; i < env_cnt; i++) {
        if (starts_with_n(env_buf[i], name, nlen) && env_buf[i][nlen] == '=') {
            e = malloc(nlen + 1 + vlen + 1);
            if (!e) return;
            memcpy(e, name, nlen);
            e[nlen] = '=';
            memcpy(e + nlen + 1, val, vlen + 1);
            free(env_buf[i]);
            env_buf[i] = e;
            return;
        }
    }
    if (env_cnt < ENV_MAX) {
        e = malloc(nlen + 1 + vlen + 1);
        if (!e) return;
        memcpy(e, name, nlen);
        e[nlen] = '=';
        memcpy(e + nlen + 1, val, vlen + 1);
        env_buf[env_cnt++] = e;
        env_buf[env_cnt] = NULL;
    }
}

void env_unset(const char *name) {
    int i, j, nlen = strlen((char *)name);
    for (i = 0; i < env_cnt; i++) {
        if (starts_with_n(env_buf[i], name, nlen) && env_buf[i][nlen] == '=') {
            free(env_buf[i]);
            for (j = i; j < env_cnt - 1; j++)
                env_buf[j] = env_buf[j + 1];
            env_cnt--;
            env_buf[env_cnt] = NULL;
            return;
        }
    }
}

static void env_init(char **envp) {
    env_cnt = 0;
    if (envp) {
        int i;
        for (i = 0; envp[i] && env_cnt < ENV_MAX; i++) {
            int len = strlen(envp[i]);
            char *e = malloc(len + 1);
            if (!e) break;
            memcpy(e, envp[i], len + 1);
            env_buf[env_cnt++] = e;
        }
    }
    env_buf[env_cnt] = NULL;
    shell_env = env_buf;
}

static int path_find_exec(const char *cmd, char *out, int outsz) {
    const char *p = cmd;
    struct stat st;
    int i;

    while (*p) { if (*p == '/') goto use_direct; p++; }

    {
        const char *path = env_get("PATH");
        if (!path) path = "/bin:/usr/bin";
        int cmdlen = strlen((char *)cmd);
        const char *seg = path;
        while (*seg) {
            const char *end = seg;
            while (*end && *end != ':') end++;
            int dlen = (int)(end - seg);
            if (dlen + 1 + cmdlen < outsz) {
                for (i = 0; i < dlen; i++) out[i] = seg[i];
                out[dlen] = '/';
                memcpy(out + dlen + 1, cmd, cmdlen + 1);
                if (stat(out, &st) == 0)
                    return 0;
            }
            if (!*end) break;
            seg = end + 1;
        }
    }

use_direct:
    i = strlen((char *)cmd);
    if (i >= outsz) i = outsz - 1;
    memcpy(out, cmd, i);
    out[i] = '\0';
    return -1;
}

static int expand_dollar(char **pp, char *out, int outsz) {
    char *p = *pp;
    char vname[64];
    int vi, oi = 0;
    char *val;

    if (*p == '?') {
        char tmp[16];
        itoa(last_exit, tmp);
        while (tmp[oi] && oi < outsz - 1) { out[oi] = tmp[oi]; oi++; }
        p++;
    } else if (*p == '$') {
        char tmp[16];
        itoa((int)getpid(), tmp);
        while (tmp[oi] && oi < outsz - 1) { out[oi] = tmp[oi]; oi++; }
        p++;
    } else if (*p == '{') {
        p++;
        vi = 0;
        while (*p && *p != '}') { if (vi < 63) vname[vi++] = *p; p++; }
        if (*p == '}') p++;
        vname[vi] = '\0';
        val = env_get(vname);
        if (val) while (*val && oi < outsz - 1) out[oi++] = *val++;
    } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
        vi = 0;
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
               (*p >= '0' && *p <= '9') || *p == '_') {
            if (vi < 63) vname[vi++] = *p;
            p++;
        }
        vname[vi] = '\0';
        val = env_get(vname);
        if (val) while (*val && oi < outsz - 1) out[oi++] = *val++;
    } else {
        if (oi < outsz - 1) out[oi++] = '$';
    }

    *pp = p;
    return oi;
}

static int collect_word(char **pp, char *out, int outsz) {
    char *p = *pp;
    int oi = 0, got = 0;

    while (*p && !is_space(*p) &&
           *p != '|' && *p != ';' && *p != '<' && *p != '>' && *p != '&') {

        if (*p == '\'') {
            p++;
            while (*p && *p != '\'') {
                if (oi < outsz - 1) out[oi++] = *p;
                p++;
            }
            if (*p == '\'') p++;
            got = 1;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) {
                    p++;
                    if (oi < outsz - 1) out[oi++] = *p++;
                } else if (*p == '$') {
                    p++;
                    oi += expand_dollar(&p, out + oi, outsz - oi);
                } else {
                    if (oi < outsz - 1) out[oi++] = *p++;
                }
            }
            if (*p == '"') p++;
            got = 1;
        } else if (*p == '\\' && *(p + 1)) {
            p++;
            if (oi < outsz - 1) out[oi++] = *p++;
            got = 1;
        } else if (*p == '$') {
            p++;
            oi += expand_dollar(&p, out + oi, outsz - oi);
            got = 1;
        } else if (*p == '~' && !got) {
            const char *home = env_get("HOME");
            if (!home) home = "/";
            while (*home && oi < outsz - 1) out[oi++] = *home++;
            p++;
            got = 1;
        } else {
            if (oi < outsz - 1) out[oi++] = *p++;
            got = 1;
        }
    }

    out[oi] = '\0';
    *pp = p;
    return got;
}

static char *next_word_buf(void) {
    if (word_pool_top < WORD_POOL_SZ)
        return word_pool[word_pool_top++];
    return NULL;
}

static char *tokenise_cmd(char *s, Cmd *cmd) {
    char *p = s;
    char *buf;
    memset(cmd, 0, sizeof(*cmd));

    while (1) {
        while (*p && is_space(*p)) p++;
        if (!*p || *p == '|') break;

        if (*p == '>') {
            p++;
            int append = (*p == '>');
            if (append) p++;
            while (*p && is_space(*p)) p++;
            buf = next_word_buf();
            if (buf) {
                collect_word(&p, buf, 256);
                if (append) cmd->redir_app = buf;
                else        cmd->redir_out = buf;
            }
            continue;
        }

        if (*p == '<') {
            p++;
            while (*p && is_space(*p)) p++;
            buf = next_word_buf();
            if (buf) {
                collect_word(&p, buf, 256);
                cmd->redir_in = buf;
            }
            continue;
        }

        buf = next_word_buf();
        if (buf) {
            collect_word(&p, buf, 256);
            if (buf[0] != '\0' && cmd->argc < MAX_ARGS - 1)
                cmd->argv[cmd->argc++] = buf;
        } else {
            char discard[256];
            collect_word(&p, discard, sizeof(discard));
        }
    }

    cmd->argv[cmd->argc] = NULL;
    if (*p == '|') p++;
    return p;
}

static pid_t fg_pid = -1;

static void sigint_handler(int sig) {
    (void)sig;
    if (fg_pid > 0)
        kill(fg_pid, (int)SIGINT);
}

static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

static void apply_redirections(Cmd *cmd) {
    int fd;
    if (cmd->redir_in) {
        fd = open(cmd->redir_in, O_RDONLY, 0);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_in, strlen(cmd->redir_in));
            write(STDERR_FILENO, "\n", 1);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->redir_out) {
        fd = open(cmd->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_out, strlen(cmd->redir_out));
            write(STDERR_FILENO, "\n", 1);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (cmd->redir_app) {
        fd = open(cmd->redir_app, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_app, strlen(cmd->redir_app));
            write(STDERR_FILENO, "\n", 1);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

static void exec_external(char **argv) {
    char path[512];
    path_find_exec(argv[0], path, sizeof(path));
    execve(path, argv, shell_env);
    write(STDERR_FILENO, "cactsole: ", 10);
    write(STDERR_FILENO, argv[0], strlen(argv[0]));
    write(STDERR_FILENO, ": not found\n", 12);
    exit(127);
}

static void exec_pipeline(Cmd *cmds, int n) {
    int i;
    if (n == 0) return;

    if (n == 1) {
        int rc = builtin_run(cmds[0].argv, cmds[0].argc);
        if (rc >= 0) {
            last_exit = rc;
            return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            apply_redirections(&cmds[0]);
            exec_external(cmds[0].argv);
        }
        fg_pid = pid;
        int status;
        waitpid(pid, &status, 0);
        fg_pid = -1;
        last_exit = (status >> 8) & 0xff;
        return;
    }

    int pipes[MAX_PIPELINE - 1][2];
    for (i = 0; i < n - 1; i++)
        pipe(pipes[i]);

    pid_t pids[MAX_PIPELINE];
    for (i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            int j;
            if (i > 0)   dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < n-1) dup2(pipes[i][1],   STDOUT_FILENO);
            for (j = 0; j < n-1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            apply_redirections(&cmds[i]);
            exec_external(cmds[i].argv);
        }
    }

    for (i = 0; i < n-1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    for (i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == n - 1)
            last_exit = (status >> 8) & 0xff;
    }
}

/* Return 1 if tok is a valid NAME=value assignment token. */
static int is_assignment(const char *tok) {
    int i = 0;
    if (!tok || !*tok) return 0;
    if (tok[0] >= '0' && tok[0] <= '9') return 0;
    while (tok[i] && tok[i] != '=') {
        char c = tok[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
        i++;
    }
    return tok[i] == '=';
}

void shell_run(char **envp) {
    char line[LINE_MAX];
    char cwd[256];

    env_init(envp);

    signal(SIGINT,  sigint_handler);
    signal(SIGCHLD, sigchld_handler);

    readline_init();

    while (1) {
        if (getcwd(cwd, sizeof(cwd)) == NULL)
            cwd[0] = '\0';
        write(STDOUT_FILENO, "[", 1);
        write(STDOUT_FILENO, cwd, strlen(cwd));
        write(STDOUT_FILENO, "] $ ", 4);

        int n = readline(line, sizeof(line));
        if (n < 0) {
            write(STDOUT_FILENO, "exit\n", 5);
            break;
        }
        if (n == 0) continue;

        while (n > 0 && is_space(line[n-1]))
            line[--n] = '\0';
        if (n == 0) continue;

        readline_add_history(line);

        Cmd  cmds[MAX_PIPELINE];
        int  ncmds = 0;
        char *p = line;
        word_pool_top = 0;
        while (*p && ncmds < MAX_PIPELINE) {
            p = tokenise_cmd(p, &cmds[ncmds]);
            if (cmds[ncmds].argc > 0)
                ncmds++;
        }

        /* Standalone VAR=value assignment */
        if (ncmds == 1 && cmds[0].argc == 1 && is_assignment(cmds[0].argv[0])) {
            char *tok = cmds[0].argv[0];
            int eqpos = 0;
            char name[64];
            while (tok[eqpos] != '=') eqpos++;
            if (eqpos < 64) {
                memcpy(name, tok, eqpos);
                name[eqpos] = '\0';
                env_set(name, tok + eqpos + 1);
            }
            last_exit = 0;
            continue;
        }

        if (ncmds > 0)
            exec_pipeline(cmds, ncmds);
    }

    readline_cleanup();
}