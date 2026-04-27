#include "builtin.h"
#include "shell.h"
#include "version.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <syscall.h>


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

static int builtin_whoami(char **argv, int argc) {
    (void)argv; (void)argc;
    uid_t uid = getuid();
    char num[16];
    if (uid == 0) {
        write(STDOUT_FILENO, "root\n", 5);
    } else {
        write(STDOUT_FILENO, "user", 4);
        itoa((int)uid, num);
        write(STDOUT_FILENO, num, strlen(num));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int builtin_id(char **argv, int argc) {
    (void)argv; (void)argc;
    char num[16];
    uid_t uid  = getuid();
    gid_t gid  = getgid();
    uid_t euid = geteuid();
    gid_t egid = getegid();

    write(STDOUT_FILENO, "uid=", 4);
    itoa((int)uid,  num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " gid=", 5);
    itoa((int)gid,  num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " euid=", 6);
    itoa((int)euid, num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, " egid=", 6);
    itoa((int)egid, num); write(STDOUT_FILENO, num, strlen(num));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_chmod(char **argv, int argc) {
    if (argc < 3) {
        write(STDERR_FILENO, "usage: chmod MODE FILE...\n", 26);
        return 1;
    }
    int mode = 0;
    const char *s = argv[1];
    while (*s >= '0' && *s <= '7') {
        mode = mode * 8 + (*s - '0');
        s++;
    }
    if (*s != '\0') {
        write(STDERR_FILENO, "chmod: invalid mode (use octal, e.g. 755)\n", 43);
        return 1;
    }
    int i, ret = 0;
    for (i = 2; i < argc; i++) {
        if (chmod(argv[i], mode) != 0) {
            write(STDERR_FILENO, "chmod: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_chown(char **argv, int argc) {
    if (argc < 3) {
        write(STDERR_FILENO, "usage: chown OWNER[:GROUP] FILE...\n", 35);
        return 1;
    }
    const char *spec = argv[1];
    int uid = 0, gid = -1;
    const char *p = spec;
    while (*p >= '0' && *p <= '9') { uid = uid * 10 + (*p - '0'); p++; }
    if (*p == ':') {
        p++;
        gid = 0;
        while (*p >= '0' && *p <= '9') { gid = gid * 10 + (*p - '0'); p++; }
    }
    if (*p != '\0') {
        write(STDERR_FILENO, "chown: invalid owner (use numeric uid[:gid])\n", 45);
        return 1;
    }
    int i, ret = 0;
    for (i = 2; i < argc; i++) {
        if (chown(argv[i], uid, gid) != 0) {
            write(STDERR_FILENO, "chown: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}


static int builtin_ls(char **argv, int argc) {
    const char *path = (argc >= 2) ? argv[1] : ".";
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        write(STDERR_FILENO, "ls: cannot open ", 16);
        write(STDERR_FILENO, path, strlen(path));
        write(STDERR_FILENO, "\n", 1);
        return 1;
    }
    struct dirent buf[32];
    int n, any = 0;
    while ((n = getdents(fd, buf, sizeof(buf))) > 0) {
        int i, count = n / (int)sizeof(struct dirent);
        for (i = 0; i < count; i++) {
            if (buf[i].d_name[0] == '\0') continue;
            write(STDOUT_FILENO, buf[i].d_name, strlen(buf[i].d_name));
            write(STDOUT_FILENO, "\n", 1);
            any = 1;
        }
    }
    if (!any) write(STDOUT_FILENO, "(empty)\n", 8);
    close(fd);
    return 0;
}

static int builtin_mkdir(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: mkdir DIR...\n", 20); return 1; }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) != 0) {
            write(STDERR_FILENO, "mkdir: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_rmdir(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: rmdir DIR...\n", 20); return 1; }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        if (rmdir(argv[i]) != 0) {
            write(STDERR_FILENO, "rmdir: ", 7);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_tch(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: tch FILE...\n", 19); return 1; }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "tch: ", 5);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        } else {
            close(fd);
        }
    }
    return ret;
}

static int builtin_rm(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: rm FILE...\n", 18); return 1; }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        if (__syscall1(SYS_DELETE, (int)argv[i]) != 0) {
            write(STDERR_FILENO, "rm: ", 4);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_cat(char **argv, int argc) {
    if (argc < 2) {
        char buf[512];
        int n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, (size_t)n);
        return 0;
    }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            write(STDERR_FILENO, "cat: ", 5);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": not found\n", 12);
            ret = 1;
            continue;
        }
        char buf[4096];
        int n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, (size_t)n);
        close(fd);
    }
    return ret;
}

static int builtin_wrt(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: wrt FILE [text...]\n", 26); return 1; }
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write(STDERR_FILENO, "wrt: cannot open ", 17);
        write(STDERR_FILENO, argv[1], strlen(argv[1]));
        write(STDERR_FILENO, "\n", 1);
        return 1;
    }
    if (argc >= 3) {
        int i;
        for (i = 2; i < argc; i++) {
            write(fd, argv[i], strlen(argv[i]));
            if (i < argc - 1) write(fd, " ", 1);
        }
        write(fd, "\n", 1);
    }
    close(fd);
    return 0;
}

static int builtin_clear(char **argv, int argc) {
    (void)argv; (void)argc;
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    return 0;
}

static int builtin_date(char **argv, int argc) {
    (void)argv; (void)argc;
    struct timeval tv;
    gettimeofday(&tv, 0);
    long s = tv.tv_sec;
    int sec_v  = (int)(s % 60); s /= 60;
    int min_v  = (int)(s % 60); s /= 60;
    int hour_v = (int)(s % 24); s /= 24;
    long days  = s;
    int year   = 1970;
    while (1) {
        int ydays = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
        if (days < (long)ydays) break;
        days -= ydays;
        year++;
    }
    int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) mdays[1] = 29;
    int month = 0;
    while (month < 12 && days >= (long)mdays[month]) { days -= mdays[month]; month++; }
    int day = (int)days + 1;
    char buf[16];
    itoa(year, buf);        write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "-", 1);
    if (month + 1 < 10)     write(STDOUT_FILENO, "0", 1);
    itoa(month + 1, buf);   write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "-", 1);
    if (day < 10)           write(STDOUT_FILENO, "0", 1);
    itoa(day, buf);         write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, " ", 1);
    if (hour_v < 10)        write(STDOUT_FILENO, "0", 1);
    itoa(hour_v, buf);      write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, ":", 1);
    if (min_v < 10)         write(STDOUT_FILENO, "0", 1);
    itoa(min_v, buf);       write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, ":", 1);
    if (sec_v < 10)         write(STDOUT_FILENO, "0", 1);
    itoa(sec_v, buf);       write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, " UTC\n", 5);
    return 0;
}

static int builtin_uptime(char **argv, int argc) {
    (void)argv; (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long total = ts.tv_sec;
    long d     = total / 86400; total %= 86400;
    int  h     = (int)(total / 3600); total %= 3600;
    int  m     = (int)(total / 60);
    char buf[32];
    write(STDOUT_FILENO, "up ", 3);
    if (d > 0) {
        itoa((int)d, buf); write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, " day(s), ", 9);
    }
    itoa(h, buf); write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, ":", 1);
    if (m < 10) write(STDOUT_FILENO, "0", 1);
    itoa(m, buf); write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int map_sig(int n) {
    switch (n) {
        case 1:  return (int)SIGHUP;
        case 2:  return (int)SIGINT;
        case 3:  return (int)SIGQUIT;
        case 9:  return (int)SIGKILL;
        case 15: return (int)SIGTERM;
        case 17: return (int)SIGCHLD;
        case 18: return (int)SIGCONT;
        case 19: return (int)SIGSTOP;
        default: return n;
    }
}

static int builtin_kill(char **argv, int argc) {
    if (argc < 2) {
        write(STDERR_FILENO, "usage: kill [-SIG] PID...\n", 26);
        write(STDERR_FILENO, "  -1 SIGKILL  -2 SIGTERM  -9 SIGKILL(posix)  -15 SIGTERM(posix)\n", 65);
        return 1;
    }
    int sig   = (int)SIGTERM;
    int start = 1;
    if (argv[1][0] == '-') {
        sig   = map_sig(atoi(argv[1] + 1));
        start = 2;
    }
    int i, ret = 0;
    for (i = start; i < argc; i++) {
        pid_t pid = (pid_t)atoi(argv[i]);
        if (kill(pid, sig) != 0) {
            write(STDERR_FILENO, "kill: ", 6);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": failed\n", 9);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_su(char **argv, int argc) {
    uid_t uid = 0;
    gid_t gid = 0;
    if (argc >= 2) {
        uid = (uid_t)atoi(argv[1]);
        gid = (gid_t)uid;
        if (argc >= 3) gid = (gid_t)atoi(argv[2]);
    }
    if (setuid(uid) != 0 || setgid(gid) != 0) {
        write(STDERR_FILENO, "su: permission denied\n", 22);
        return 1;
    }
    char buf[16];
    write(STDOUT_FILENO, "switched to uid=", 16);
    itoa((int)uid, buf); write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, " gid=", 5);
    itoa((int)gid, buf); write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_stat(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: stat FILE...\n", 20); return 1; }
    int i, ret = 0;
    for (i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            write(STDERR_FILENO, "stat: ", 6);
            write(STDERR_FILENO, argv[i], strlen(argv[i]));
            write(STDERR_FILENO, ": not found\n", 12);
            ret = 1;
            continue;
        }
        char buf[32];
        write(STDOUT_FILENO, "File:  ", 7);
        write(STDOUT_FILENO, argv[i], strlen(argv[i]));
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, "Size:  ", 7);
        itoa((int)st.st_size, buf); write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, "Inode: ", 7);
        itoa((int)st.st_ino, buf);  write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, "Mode:  ", 7);
        itoa((int)st.st_mode, buf); write(STDOUT_FILENO, buf, strlen(buf));
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, "Type:  ", 7);
        if      (S_ISDIR(st.st_mode))  write(STDOUT_FILENO, "directory\n",    10);
        else if (S_ISREG(st.st_mode))  write(STDOUT_FILENO, "regular file\n",  13);
        else if (S_ISCHR(st.st_mode))  write(STDOUT_FILENO, "char device\n",   12);
        else if (S_ISBLK(st.st_mode))  write(STDOUT_FILENO, "block device\n",  13);
        else if (S_ISFIFO(st.st_mode)) write(STDOUT_FILENO, "fifo/pipe\n",     10);
        else                           write(STDOUT_FILENO, "other\n",          6);
    }
    return ret;
}

static int builtin_mv(char **argv, int argc) {
    if (argc < 3) { write(STDERR_FILENO, "usage: mv SRC DST\n", 18); return 1; }
    if (rename(argv[1], argv[2]) != 0) {
        write(STDERR_FILENO, "mv: rename failed\n", 18);
        return 1;
    }
    return 0;
}

static int builtin_ln(char **argv, int argc) {
    int soft = 0, base = 1;
    if (argc >= 2 && strcmp(argv[1], "-s") == 0) { soft = 1; base = 2; }
    if (argc < base + 2) {
        write(STDERR_FILENO, "usage: ln [-s] SRC DST\n", 23);
        return 1;
    }
    int r = soft
        ? __syscall2(SYS_SYMLINK, (int)argv[base], (int)argv[base + 1])
        : __syscall2(SYS_LINK,    (int)argv[base], (int)argv[base + 1]);
    if (r != 0) {
        write(STDERR_FILENO, "ln: failed\n", 11);
        return 1;
    }
    return 0;
}

static int builtin_readlink(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: readlink PATH\n", 21); return 1; }
    char buf[512];
    int n = __syscall3(SYS_READLINK, (int)argv[1], (int)buf, (int)(sizeof(buf) - 1));
    if (n < 0) {
        write(STDERR_FILENO, "readlink: failed\n", 17);
        return 1;
    }
    buf[n] = '\0';
    write(STDOUT_FILENO, buf, (size_t)n);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_sleep(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: sleep SECONDS\n", 21); return 1; }
    sleep((unsigned int)atoi(argv[1]));
    return 0;
}

static int builtin_free(char **argv, int argc) {
    (void)argv; (void)argc;
    void *brk = sbrk(0);
    char buf[32];
    write(STDOUT_FILENO, "heap brk: 0x", 12);
    hex_to_ascii((unsigned int)(size_t)brk, buf);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_fetch(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char logo[] =
        "\033[36m"
        "  _                 \n"
        " | |   _   ___  __  \n"
        " | |  | | | \\ \\/ /  \n"
        " | |__| |_| |>  <   \n"
        " |_____\\__,_/_/\\_\\  \n"
        "\033[0m\n";
    static const char shell_line[] =
        "\033[33m Shell: \033[0m  cactsole " CACTSOLE_VERSION "\n";
    write(STDOUT_FILENO, logo,       sizeof(logo)       - 1);
    write(STDOUT_FILENO, "\033[33m Kernel:\033[0m  Lux x86_32\n", 30);
    write(STDOUT_FILENO, shell_line, sizeof(shell_line) - 1);
    write(STDOUT_FILENO, "\033[33m Arch:  \033[0m  i686\n",       26);
    char buf[16];
    uid_t uid = getuid();
    write(STDOUT_FILENO, "\033[33m User:  \033[0m  uid=", 23);
    itoa((int)uid, buf); write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

/* ── help ────────────────────────────────────────────────────────────── */

static int builtin_help(char **argv, int argc) {
    (void)argv; (void)argc;
    static const char msg[] =
        "cactsole built-in commands:\n"
        "  Navigation:\n"
        "    cd [dir]               change directory (default $HOME)\n"
        "    pwd                    print working directory\n"
        "  Files:\n"
        "    ls [path]              list directory entries\n"
        "    mkdir DIR...           create directories\n"
        "    rmdir DIR...           remove empty directories\n"
        "    tch FILE...            create/touch files\n"
        "    rm FILE...             delete files\n"
        "    cat [FILE...]          print file(s) to stdout\n"
        "    wrt FILE [text]        write text to file (overwrite)\n"
        "    stat FILE...           show file info\n"
        "    mv SRC DST             rename/move file\n"
        "    ln [-s] SRC DST        create hard or soft link\n"
        "    readlink PATH          print symlink target\n"
        "  System:\n"
        "    clear                  clear screen\n"
        "    date                   print current date/time (UTC)\n"
        "    uptime                 show system uptime\n"
        "    kill [-SIG] PID...     send signal (POSIX nums mapped)\n"
        "    su [uid [gid]]         switch user/group\n"
        "    sleep N                sleep N seconds\n"
        "    free                   show heap break address\n"
        "    fetch                  show system info banner\n"
        "  Environment:\n"
        "    echo [args...]         print arguments\n"
        "    export [VAR[=v]]       set/list environment variables\n"
        "    unset VAR...           remove environment variables\n"
        "    env                    print all environment variables\n"
        "  Jobs:\n"
        "    jobs                   list background jobs\n"
        "    fg [n]                 bring job n to foreground\n"
        "    bg [n]                 resume job n in background\n"
        "  Misc:\n"
        "    true                   exit 0\n"
        "    false                  exit 1\n"
        "    exit [code]            exit the shell\n"
        "    whoami                 print current user name\n"
        "    id                     print uid/gid/euid/egid\n"
        "    chmod MODE FILE...     change file permissions (octal)\n"
        "    chown OWNER[:GRP] F... change file owner (numeric)\n"
        "    version                print shell version\n"
        "    help                   show this message\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 0;
}


static const struct builtin_cmd builtins[] = {
    /* navigation */
    {"cd",       builtin_cd},
    {"pwd",      builtin_pwd},
    /* files */
    {"ls",       builtin_ls},
    {"mkdir",    builtin_mkdir},
    {"rmdir",    builtin_rmdir},
    {"tch",      builtin_tch},
    {"rm",       builtin_rm},
    {"cat",      builtin_cat},
    {"wrt",      builtin_wrt},
    {"stat",     builtin_stat},
    {"mv",       builtin_mv},
    {"ln",       builtin_ln},
    {"readlink", builtin_readlink},
    /* system */
    {"clear",    builtin_clear},
    {"date",     builtin_date},
    {"uptime",   builtin_uptime},
    {"kill",     builtin_kill},
    {"su",       builtin_su},
    {"sleep",    builtin_sleep},
    {"free",     builtin_free},
    {"fetch",    builtin_fetch},
    /* environment */
    {"echo",     builtin_echo},
    {"export",   builtin_export},
    {"unset",    builtin_unset},
    {"env",      builtin_env},
    /* jobs */
    {"jobs",     builtin_jobs},
    {"fg",       builtin_fg},
    {"bg",       builtin_bg},
    /* misc */
    {"true",     builtin_true},
    {"false",    builtin_false},
    {"exit",     builtin_exit},
    {"whoami",   builtin_whoami},
    {"id",       builtin_id},
    {"chmod",    builtin_chmod},
    {"chown",    builtin_chown},
    {"version",  builtin_version},
    {"help",     builtin_help},
    {NULL, NULL}
};

int builtin_run(char **argv, int argc) {
    int i;
    if (argc == 0 || argv[0] == NULL) return -1;
    for (i = 0; builtins[i].name != NULL; i++)
        if (strcmp(argv[0], (char *)builtins[i].name) == 0)
            return builtins[i].fn(argv, argc);
    return -1;
}