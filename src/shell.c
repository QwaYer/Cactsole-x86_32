#include "shell.h"
#include "readline.h"
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stat.h>

#define MAX_ARGS     64
#define MAX_PIPELINE  8
#define MAX_JOBS     16
#define LINE_MAX    1024
#define ENV_MAX      256
#define WORD_POOL_SZ (MAX_PIPELINE * MAX_ARGS)

#define SEP_NONE  0
#define SEP_PIPE  '|'
#define SEP_SEMI  ';'
#define SEP_AND   1
#define SEP_OR    2
#define SEP_BG    3

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
    char *redir_err;
    char *redir_err_app;
    char *redir_all;     /* &>   stdout+stderr */
    char *redir_all_app; /* &>> */
    int merge_err_to_out; /* 2>&1 */
    int merge_out_to_err; /* 1>&2 */
} Cmd;

typedef struct {
    pid_t pid;           /* representative process (the last member of a pipeline) */
    pid_t pgid;          /* the job's process group: what the console hands ^C to */
    int   done;
    int   stopped;
    char  cmd[64];
} Job;

static Job  jobs[MAX_JOBS];
static int  njobs;
static pid_t fg_pid = -1;
static pid_t fg_pgid = -1;
/* /dev/tty, held open so tcsetpgrp() has a console fd even with redirected stdio. */
static int   console_fd = -1;

/* Hand the console to `pgid`'s process group.  The line discipline then
 * delivers ^C/^\ /^Z and SIGWINCH to every member of that group, which is the
 * only way a signal reaches a whole pipeline; the shell takes the console back
 * for itself (getpid()) whenever no foreground job is running. */
static void console_set_fg(pid_t pgid) {
    if (console_fd >= 0) (void)tcsetpgrp(console_fd, pgid);
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int is_redir_suffix(char c) {
    return c == '\0' || is_space(c) || c == '|' || c == ';' || c == '&' ||
           c == '<' || c == '>';
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

/* Remove NAME from the environment.  Only the control channel's `unsetenv`
 * request calls this now — a child cannot change the shell's own environment. */
static void env_unset(const char *name) {
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


static int job_add(pid_t pid, const char *cmd) {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid  = pid;
            jobs[i].pgid = pid;     /* a job starts out as its own group */
            jobs[i].done = 0;
            jobs[i].stopped = 0;
            if (cmd) {
                int j;
                for (j = 0; cmd[j] && j < 63; j++) jobs[i].cmd[j] = cmd[j];
                jobs[i].cmd[j] = '\0';
            } else {
                jobs[i].cmd[0] = '\0';
            }
            if (i + 1 > njobs) njobs = i + 1;
            return i + 1;
        }
    }
    return -1;
}

/* Ctrl-Z (SIGSTOP from the console) turns the running foreground job into a
 * tracked stopped job. */
static int job_mark_stopped(pid_t pid, const char *cmd) {
    int n = job_add(pid, cmd);
    if (n > 0)
        jobs[n - 1].stopped = 1;
    return n;
}

/* A pipeline's members share the group of its first command, which is not the
 * pid the job is reported under (that is the last member, whose death means the
 * job is done). */
static void job_set_pgid(int n, pid_t pgid) {
    if (n > 0 && n <= MAX_JOBS && jobs[n - 1].pid != 0) jobs[n - 1].pgid = pgid;
}

/* "cmd1 | cmd2 | ..." for the job table, so a job is named by what was typed
 * rather than only the first word of a pipeline. */
static void job_label(Cmd *cmds, int n, char *out, int outsz) {
    int i, a, k = 0;
    for (i = 0; i < n; i++) {
        if (i > 0) {
            if (k < outsz - 1) out[k++] = ' ';
            if (k < outsz - 1) out[k++] = '|';
            if (k < outsz - 1) out[k++] = ' ';
        }
        for (a = 0; a < cmds[i].argc; a++) {
            const char *p;
            if (a > 0 && k < outsz - 1) out[k++] = ' ';
            for (p = cmds[i].argv[a]; *p && k < outsz - 1; p++) out[k++] = *p;
        }
    }
    out[k] = '\0';
}

static void job_report_stopped(int n, const char *cmd) {
    char num[16];
    write(STDOUT_FILENO, "[", 1);
    itoa(n, num);
    write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, "]+ Stopped  ", 12);
    write(STDOUT_FILENO, cmd, strlen(cmd));
    write(STDOUT_FILENO, "\n", 1);
}

static void notify_done_jobs(void) {
    int i;
    char num[16];
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0 && jobs[i].done) {
            write(STDOUT_FILENO, "[", 1);
            itoa(i + 1, num);
            write(STDOUT_FILENO, num, strlen(num));
            write(STDOUT_FILENO, "] done     ", 11);
            write(STDOUT_FILENO, jobs[i].cmd, strlen(jobs[i].cmd));
            write(STDOUT_FILENO, "\n", 1);
            jobs[i].pid  = 0;
            jobs[i].done = 0;
            jobs[i].cmd[0] = '\0';
        }
    }
}

/* ── job-table operations ────────────────────────────────────────────────────
 * jobs/fg/bg are separate binaries now; they act on the shell's job table by
 * sending a request through the control pipe (see ctl_apply). */

static void shell_list_jobs(void) {
    int i;
    char num[16];
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0) {
            write(STDOUT_FILENO, "[", 1);
            itoa(i + 1, num);
            write(STDOUT_FILENO, num, strlen(num));
            write(STDOUT_FILENO, "] ", 2);
            if (jobs[i].done) {
                write(STDOUT_FILENO, "done     ", 9);
            } else if (jobs[i].stopped) {
                write(STDOUT_FILENO, "stopped  ", 9);
            } else {
                write(STDOUT_FILENO, "running  ", 9);
            }
            write(STDOUT_FILENO, jobs[i].cmd, strlen(jobs[i].cmd));
            write(STDOUT_FILENO, "\n", 1);

            if (jobs[i].done) {
                jobs[i].pid  = 0;
                jobs[i].done = 0;
                jobs[i].cmd[0] = '\0';
            }
        }
    }
}

static int shell_fg_job(int n) {
    if (n < 1 || n > MAX_JOBS || jobs[n-1].pid == 0) {
        write(STDERR_FILENO, "fg: no such job\n", 16);
        return 1;
    }
    if (jobs[n-1].done) {
        write(STDERR_FILENO, "fg: job already done\n", 21);
        jobs[n-1].pid = 0;
        return 0;
    }
    {
        pid_t pid = jobs[n-1].pid;
        int status;
        if (jobs[n-1].stopped) {
            killpg(jobs[n-1].pgid, SIGCONT);
            jobs[n-1].stopped = 0;
        }
        fg_pid  = pid;
        fg_pgid = jobs[n-1].pgid;
        console_set_fg(fg_pgid);
        waitpid(pid, &status, WUNTRACED);
        console_set_fg(getpid());
        fg_pid  = -1;
        fg_pgid = -1;

        if (WIFSTOPPED(status)) {
            jobs[n-1].stopped = 1;
            return 128 + WSTOPSIG(status);
        }
        jobs[n-1].pid  = 0;
        jobs[n-1].done = 0;
        jobs[n-1].stopped = 0;
        jobs[n-1].cmd[0] = '\0';
        return WEXITSTATUS(status);
    }
}

static int shell_bg_job(int n) {
    if (n < 1 || n > MAX_JOBS || jobs[n-1].pid == 0) {
        write(STDERR_FILENO, "bg: no such job\n", 16);
        return 1;
    }
    killpg(jobs[n-1].pgid, SIGCONT);
    jobs[n-1].stopped = 0;
    return 0;
}

/* Most recently created tracked job (0 when there is none) — `kill %`. */
static int shell_current_job(void) {
    int i;
    for (i = MAX_JOBS - 1; i >= 0; i--)
        if (jobs[i].pid != 0) return i + 1;
    return 0;
}

/* Signal job n's whole process group (0 on success, 1 if there is no such job). */
static int shell_signal_job(int n, int sig) {
    if (n < 1 || n > MAX_JOBS || jobs[n-1].pid == 0) return 1;
    if (killpg(jobs[n-1].pgid, sig) == 0) return 0;
    if (kill(jobs[n-1].pid, sig) == 0) return 0;
    return 1;
}

int shell_path_find(const char *cmd, char *out, int outsz) {
    const char *p = cmd;
    struct stat st;
    int i;

    while (*p) { if (*p == '/') goto use_direct; p++; }

    {
        const char *path = env_get("PATH");
        if (!path) path = "/usr/bin:/usr/sbin";
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
        char tmp[16]; itoa(last_exit, tmp);
        while (tmp[oi] && oi < outsz - 1) { out[oi] = tmp[oi]; oi++; }
        p++;
    } else if (*p == '$') {
        char tmp[16]; itoa((int)getpid(), tmp);
        while (tmp[oi] && oi < outsz - 1) { out[oi] = tmp[oi]; oi++; }
        p++;
    } else if (*p == '{') {
        p++; vi = 0;
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
            while (*p && *p != '\'') { if (oi < outsz - 1) out[oi++] = *p; p++; }
            if (*p == '\'') p++;
            got = 1;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1)) { p++; if (oi < outsz-1) out[oi++] = *p++; }
                else if (*p == '$') { p++; oi += expand_dollar(&p, out+oi, outsz-oi); }
                else { if (oi < outsz-1) out[oi++] = *p++; }
            }
            if (*p == '"') p++;
            got = 1;
        } else if (*p == '\\' && *(p+1)) {
            p++;
            if (oi < outsz-1) out[oi++] = *p++;
            got = 1;
        } else if (*p == '$') {
            p++;
            oi += expand_dollar(&p, out+oi, outsz-oi);
            got = 1;
        } else if (*p == '~' && !got) {
            const char *home = env_get("HOME");
            if (!home) home = "/";
            while (*home && oi < outsz-1) out[oi++] = *home++;
            p++;
            got = 1;
        } else {
            if (oi < outsz-1) out[oi++] = *p++;
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

static char *tokenise_cmd(char *s, Cmd *cmd, int *sep) {
    char *p = s, *buf;
    memset(cmd, 0, sizeof(*cmd));
    *sep = SEP_NONE;

    while (1) {
        while (*p && is_space(*p)) p++;
        if (!*p)                         { *sep = SEP_NONE; break; }
        if (*p == '|' && *(p+1) == '|') { *sep = SEP_OR;   p += 2; break; }
        if (*p == '|')                  { *sep = SEP_PIPE;  p++;    break; }
        if (*p == ';')                  { *sep = SEP_SEMI;  p++;    break; }
        if (*p == '&' && *(p+1) == '&') { *sep = SEP_AND;   p += 2; break; }
        if (*p == '&' && *(p + 1) == '>') {
            p += 2;
            int append = (*p == '>');
            if (append) p++;
            while (*p && is_space(*p)) p++;
            buf = next_word_buf();
            if (buf) {
                collect_word(&p, buf, 256);
                if (append) cmd->redir_all_app = buf;
                else         cmd->redir_all     = buf;
            }
            continue;
        }
        if (*p == '&')                  { *sep = SEP_BG;    p++;    break; }

        if (p[0] == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1' &&
            is_redir_suffix(p[4])) {
            p += 4;
            cmd->merge_err_to_out = 1;
            continue;
        }
        if (p[0] == '1' && p[1] == '>' && p[2] == '&' && p[3] == '2' &&
            is_redir_suffix(p[4])) {
            p += 4;
            cmd->merge_out_to_err = 1;
            continue;
        }
        if (*p == '2' && *(p + 1) == '>') {
            p += 2;
            int append = (*p == '>');
            if (append) p++;
            while (*p && is_space(*p)) p++;
            buf = next_word_buf();
            if (buf) {
                collect_word(&p, buf, 256);
                if (append) cmd->redir_err_app = buf;
                else         cmd->redir_err     = buf;
            }
            continue;
        }
        if (*p == '>') {
            p++;
            int append = (*p == '>'); if (append) p++;
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
            if (buf) { collect_word(&p, buf, 256); cmd->redir_in = buf; }
            continue;
        }

        buf = next_word_buf();
        if (buf) {
            collect_word(&p, buf, 256);
            if (buf[0] != '\0' && cmd->argc < MAX_ARGS - 1)
                cmd->argv[cmd->argc++] = buf;
        } else {
            char discard[256]; collect_word(&p, discard, sizeof(discard));
        }
    }
    cmd->argv[cmd->argc] = NULL;
    return p;
}

static void sigint_handler(int sig) {
    (void)sig;
    /* The console usually delivers ^C straight to the foreground job's group,
     * so this fires only while the shell itself owns the console.  Signal the
     * whole job either way. */
    if (fg_pgid > 0)
        killpg(fg_pgid, SIGINT);
    else if (fg_pid > 0)
        kill(fg_pid, (int)SIGINT);
}

static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    int i;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].pid == pid) {
                jobs[i].done = 1;
                break;
            }
        }
    }
}

static void apply_redirections(Cmd *cmd) {
    int fd;
    if (cmd->redir_in) {
        fd = open(cmd->redir_in, O_RDONLY, 0);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_in, strlen(cmd->redir_in));
            write(STDERR_FILENO, "\n", 1); exit(1);
        }
        dup2(fd, STDIN_FILENO); close(fd);
    }
    /* &> / &>> — one file for stdout and stderr */
    if (cmd->redir_all || cmd->redir_all_app) {
        const char *path = cmd->redir_all_app ? cmd->redir_all_app : cmd->redir_all;
        int fl         = O_WRONLY | O_CREAT |
                         (cmd->redir_all_app ? O_APPEND : O_TRUNC);
        fd = open(path, fl, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, path, strlen(path));
            write(STDERR_FILENO, "\n", 1); exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (cmd->redir_out) {
            fd = open(cmd->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                write(STDERR_FILENO, "cactsole: cannot open ", 22);
                write(STDERR_FILENO, cmd->redir_out, strlen(cmd->redir_out));
                write(STDERR_FILENO, "\n", 1); exit(1);
            }
            dup2(fd, STDOUT_FILENO); close(fd);
        }
        if (cmd->redir_app) {
            fd = open(cmd->redir_app, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                write(STDERR_FILENO, "cactsole: cannot open ", 22);
                write(STDERR_FILENO, cmd->redir_app, strlen(cmd->redir_app));
                write(STDERR_FILENO, "\n", 1); exit(1);
            }
            dup2(fd, STDOUT_FILENO); close(fd);
        }
    }
    if (cmd->redir_err) {
        fd = open(cmd->redir_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_err, strlen(cmd->redir_err));
            write(STDERR_FILENO, "\n", 1); exit(1);
        }
        dup2(fd, STDERR_FILENO); close(fd);
    }
    if (cmd->redir_err_app) {
        fd = open(cmd->redir_err_app, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "cactsole: cannot open ", 22);
            write(STDERR_FILENO, cmd->redir_err_app, strlen(cmd->redir_err_app));
            write(STDERR_FILENO, "\n", 1); exit(1);
        }
        dup2(fd, STDERR_FILENO); close(fd);
    }
    if (cmd->merge_err_to_out)
        dup2(STDOUT_FILENO, STDERR_FILENO);
    if (cmd->merge_out_to_err)
        dup2(STDERR_FILENO, STDOUT_FILENO);
}

static void exec_external(char **argv) {
    char path[512];
    shell_path_find(argv[0], path, sizeof(path));
    execve(path, argv, shell_env);
    write(STDERR_FILENO, "cactsole: ", 10);
    write(STDERR_FILENO, argv[0], strlen(argv[0]));
    write(STDERR_FILENO, ": not found\n", 12);
    exit(127);
}

/* ── shell control channel ───────────────────────────────────────────────────
 * A command that has to change the shell's own state — its working directory,
 * its environment, its job table, its own existence — cannot do so from its
 * own process: a child never affects its parent.  cactsole therefore hands
 * every child the write end of a pipe in $CACT_CTL_FD, and after each
 * foreground command drains that pipe and applies the requests.  The shell
 * understands only these operations, never `cd`/`export`/... as commands, so
 * moving them out to CactUserBins binaries does not bring builtins back.
 *
 * Request framing: one request per line ("\n"), fields separated by TAB:
 *   cd\t<abs-path>          setenv\t<name>\t<value>      unsetenv\t<name>
 *   jobs                    fg\t<n>                      bg\t<n>
 *   kill\t<sig>\t<job>      exit\t<code> */
#define CTL_BUF 2048

static int  ctl_rfd = -1;
static char ctl_buf[CTL_BUF];
static int  ctl_len;

static void ctl_apply(char *line) {
    char *a = strchr(line, '\t');
    char *b = a ? strchr(a + 1, '\t') : NULL;
    if (a) *a = '\0';
    if (b) *b = '\0';
    const char *arg1 = a ? a + 1 : NULL;
    const char *arg2 = b ? b + 1 : NULL;

    if (strcmp(line, "cd") == 0 && arg1) {
        if (chdir(arg1) != 0) {
            write(STDERR_FILENO, "cd: ", 4);
            write(STDERR_FILENO, arg1, strlen(arg1));
            write(STDERR_FILENO, ": cannot change directory\n", 26);
        }
    } else if (strcmp(line, "setenv") == 0 && arg1) {
        env_set(arg1, arg2 ? arg2 : "");
    } else if (strcmp(line, "unsetenv") == 0 && arg1) {
        env_unset(arg1);
    } else if (strcmp(line, "jobs") == 0) {
        shell_list_jobs();
    } else if (strcmp(line, "fg") == 0) {
        shell_fg_job(arg1 ? atoi(arg1) : 1);
    } else if (strcmp(line, "bg") == 0) {
        shell_bg_job(arg1 ? atoi(arg1) : 1);
    } else if (strcmp(line, "kill") == 0 && arg1) {
        int sig = atoi(arg1);
        int n   = arg2 ? atoi(arg2) : 0;
        if (n <= 0) n = shell_current_job();
        if (n > 0) shell_signal_job(n, sig);
    } else if (strcmp(line, "exit") == 0) {
        readline_cleanup();
        exit(arg1 ? atoi(arg1) : 0);
    }
}

/* Read everything waiting on the control pipe and apply each complete request.
 * A partial line (a write split across reads) stays buffered for the next
 * flush; requests are short, so the pipe's PIPE_BUF write atomicity keeps a
 * single request intact in practice. */
static void ctl_flush(void) {
    if (ctl_rfd < 0) return;

    ssize_t n;
    while (ctl_len < CTL_BUF - 1 &&
           (n = read(ctl_rfd, ctl_buf + ctl_len,
                     (size_t)(CTL_BUF - 1 - ctl_len))) > 0)
        ctl_len += (int)n;

    int start = 0;
    for (int i = 0; i < ctl_len; i++) {
        if (ctl_buf[i] == '\n') {
            ctl_buf[i] = '\0';
            ctl_apply(ctl_buf + start);
            start = i + 1;
        }
    }
    if (start > 0) {
        memmove(ctl_buf, ctl_buf + start, (size_t)(ctl_len - start));
        ctl_len -= start;
    }
    if (ctl_len >= CTL_BUF - 1) ctl_len = 0;   /* unterminated: drop it */
}

static void ctl_init(void) {
    int fds[2];
    if (pipe(fds) != 0) return;
    ctl_rfd = fds[0];
    /* Children keep the write end (fds[1]) — its number goes to $CACT_CTL_FD.
     * The read end is the shell's alone: non-blocking so a drain never stops
     * the prompt, and close-on-exec so it does not leak into children. */
    int fl = fcntl(ctl_rfd, F_GETFL, 0);
    if (fl >= 0) fcntl(ctl_rfd, F_SETFL, fl | O_NONBLOCK);
    fcntl(ctl_rfd, F_SETFD, FD_CLOEXEC);

    char num[16];
    itoa(fds[1], num);
    env_set("CACT_CTL_FD", num);
}

static void exec_pipeline(Cmd *cmds, int n, int bg) {
    int i;
    char jnum[16], jpid[16];
    if (n == 0) return;

    char jlabel[64];
    job_label(cmds, n, jlabel, sizeof(jlabel));

    if (n == 1) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Its own process group: the console hands ^C to the foreground
             * group, so a job must not share the shell's. */
            setpgid(0, 0);
            apply_redirections(&cmds[0]);
            exec_external(cmds[0].argv);
        }
        if (pid > 0) setpgid(pid, pid);   /* both sides, so the group exists */
        if (bg) {
            int jn = job_add(pid, jlabel);
            write(STDOUT_FILENO, "[", 1);
            itoa(jn, jnum); write(STDOUT_FILENO, jnum, strlen(jnum));
            write(STDOUT_FILENO, "] ", 2);
            itoa((int)pid, jpid); write(STDOUT_FILENO, jpid, strlen(jpid));
            write(STDOUT_FILENO, "\n", 1);
            last_exit = 0;
        } else {
            fg_pid  = pid;
            fg_pgid = pid;
            console_set_fg(pid);
            int status; waitpid(pid, &status, WUNTRACED);
            console_set_fg(getpid());
            fg_pid  = -1;
            fg_pgid = -1;
            if (WIFSTOPPED(status)) {
                /* Ctrl-Z parked it: announce the stop and track it. */
                int jn = job_mark_stopped(pid, jlabel);
                if (jn > 0) job_report_stopped(jn, jlabel);
                last_exit = 128 + WSTOPSIG(status);
            } else {
                last_exit = WEXITSTATUS(status);
            }
        }
        ctl_flush();     /* a `cd`/`export`/`exit` request applies here */
        return;
    }

    int pipes[MAX_PIPELINE - 1][2];
    for (i = 0; i < MAX_PIPELINE - 1; i++) {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }
    for (i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            int j;
            for (j = 0; j < i; j++) {
                if (pipes[j][0] >= 0) close(pipes[j][0]);
                if (pipes[j][1] >= 0) close(pipes[j][1]);
            }
            write(STDERR_FILENO, "cactsole: pipe failed\n", 22);
            last_exit = 1;
            return;
        }
    }

    pid_t pids[MAX_PIPELINE];
    for (i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            int j;
            for (j = 0; j < n - 1; j++) {
                if (pipes[j][0] >= 0) close(pipes[j][0]);
                if (pipes[j][1] >= 0) close(pipes[j][1]);
            }
            for (j = 0; j < i; j++) waitpid(pids[j], NULL, 0);
            write(STDERR_FILENO, "cactsole: fork failed\n", 22);
            last_exit = 1;
            return;
        }
        if (pids[i] == 0) {
            int j;
            if (i > 0)   dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < n-1) dup2(pipes[i][1],   STDOUT_FILENO);
            for (j = 0; j < n-1; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            apply_redirections(&cmds[i]);
            exec_external(cmds[i].argv);
        }
    }
    /* One job, one process group, led by pids[0]: every member has to be in it
     * because the console delivers ^C to the foreground *group* — that is what
     * makes ^C stop a whole pipeline instead of one arbitrary member. */
    for (i = 0; i < n; i++) if (pids[i] > 0) setpgid(pids[i], pids[0]);
    for (i = 0; i < n-1; i++) { close(pipes[i][0]); close(pipes[i][1]); }

    if (bg) {
        int jn = job_add(pids[n-1], jlabel);
        job_set_pgid(jn, pids[0]);
        write(STDOUT_FILENO, "[", 1);
        itoa(jn, jnum); write(STDOUT_FILENO, jnum, strlen(jnum));
        write(STDOUT_FILENO, "] ", 2);
        itoa((int)pids[n-1], jpid); write(STDOUT_FILENO, jpid, strlen(jpid));
        write(STDOUT_FILENO, "\n", 1);
        last_exit = 0;
    } else {
        int stopped = 0, stop_sig = 0, last_status = 0;
        fg_pgid = pids[0];
        console_set_fg(fg_pgid);
        /* WUNTRACED on every member, not just the one the job is reported
         * under: ^Z stops the whole group, and the shell has to notice the stop
         * wherever it lands — otherwise it waits forever for a member that will
         * never run again, keeps the console pointed at the stopped group, and
         * the console goes dead (^C included). */
        for (i = 0; i < n; i++) {
            int status = 0;
            waitpid(pids[i], &status, WUNTRACED);
            if (WIFSTOPPED(status)) { stopped = 1; stop_sig = WSTOPSIG(status); }
            if (i == n - 1) last_status = status;
        }
        console_set_fg(getpid());
        fg_pgid = -1;
        if (stopped) {
            /* Ctrl-Z parked the job: announce the stop and track it. */
            int jn = job_mark_stopped(pids[n-1], jlabel);
            job_set_pgid(jn, pids[0]);
            if (jn > 0) job_report_stopped(jn, jlabel);
            last_exit = 128 + stop_sig;
        } else {
            last_exit = WEXITSTATUS(last_status);
        }
    }
    ctl_flush();     /* a `cd`/`export`/`exit` request applies here */
}

static int is_assignment(const char *tok) {
    int i = 0;
    if (!tok || !*tok || (tok[0] >= '0' && tok[0] <= '9')) return 0;
    while (tok[i] && tok[i] != '=') {
        char c = tok[i];
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_')) return 0;
        i++;
    }
    return tok[i] == '=';
}

static void run_line(char *line) {
    char *p = line;
    int run = 1;

    while (*p) {
        Cmd cmds[MAX_PIPELINE];
        int ncmds = 0, final_sep = SEP_NONE;
        int bg = 0;

        word_pool_top = 0;

        while (ncmds < MAX_PIPELINE) {
            int s;
            p = tokenise_cmd(p, &cmds[ncmds], &s);
            if (cmds[ncmds].argc > 0) ncmds++;
            if (s != SEP_PIPE) { final_sep = s; break; }
        }

        bg = (final_sep == SEP_BG);

        if (run && ncmds > 0) {
            if (ncmds == 1 && cmds[0].argc == 1 && is_assignment(cmds[0].argv[0])) {
                char *tok = cmds[0].argv[0];
                int eqpos = 0;
                char name[64];
                while (tok[eqpos] != '=') eqpos++;
                if (eqpos < 64) {
                    memcpy(name, tok, eqpos); name[eqpos] = '\0';
                    env_set(name, tok + eqpos + 1);
                }
                last_exit = 0;
            } else {
                exec_pipeline(cmds, ncmds, bg);
            }
        }

        if (final_sep == SEP_NONE) break;
        if (final_sep == SEP_SEMI || final_sep == SEP_BG) run = 1;
        else if (final_sep == SEP_AND) run = (last_exit == 0);
        else if (final_sep == SEP_OR)  run = (last_exit != 0);
        else break;
    }
}

int shell_run_cmd(const char *cmd, char **envp) {
    if (!cmd) return 0;
    env_init(envp);
    ctl_init();
    char line[LINE_MAX];
    int i = 0;
    while (cmd[i] && i < (int)sizeof(line) - 1) { line[i] = cmd[i]; i++; }
    line[i] = '\0';
    run_line(line);
    return last_exit;
}

void shell_run(char **envp) {
    char line[LINE_MAX];
    char cwd[256];

    env_init(envp);
    ctl_init();

    signal(SIGINT,  sigint_handler);
    signal(SIGCHLD, sigchld_handler);

    /* Own the console before reading anything: the shell becomes the session
     * leader and claims the console's foreground group, so ^C reaches the job
     * it hands the console to.  Without a claim there is no foreground group
     * and the line discipline passes ^C through as a raw byte instead. */
    setsid();
    console_fd = open("/dev/tty", O_RDWR);
    console_set_fg(getpid());

    readline_init();

    while (1) {
        notify_done_jobs();
        ctl_flush();     /* requests left by background jobs, applied now */

        if (getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = '\0';
        {
            char pbuf[256];
            int  pn = 0, i;
            const char *green = "\033[1;92m";
            const char *blue  = "\033[1;94m";
            const char *reset = "\033[0m";
            const char *p = green;
            while (*p) pbuf[pn++] = *p++;
            p = "cact:";
            while (*p) pbuf[pn++] = *p++;
            p = reset;
            while (*p) pbuf[pn++] = *p++;
            p = blue;
            while (*p) pbuf[pn++] = *p++;
            for (i = 0; cwd[i] && pn < (int)sizeof(pbuf) - 10; i++)
                pbuf[pn++] = cwd[i];
            p = reset;
            while (*p) pbuf[pn++] = *p++;
            pbuf[pn++] = '$';
            pbuf[pn++] = ' ';
            pbuf[pn] = '\0';
            write(STDOUT_FILENO, pbuf, pn);
            readline_set_prompt(pbuf);
        }

        int n = readline(line, sizeof(line));
        if (n < 0) { write(STDOUT_FILENO, "exit\n", 5); break; }
        if (n == 0) continue;

        while (n > 0 && is_space(line[n-1])) line[--n] = '\0';
        if (n == 0) continue;

        readline_add_history(line);
        run_line(line);
    }

    readline_cleanup();
}