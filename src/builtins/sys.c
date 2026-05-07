/*
 * builtins/sys.c — системные команды.
 *
 *   clear / date / uptime / kill / su / sleep / free / fetch
 */

#include "builtin.h"
#include "builtins.h"
#include "version.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static int cmd_clear(char **argv, int argc) {
    (void)argv; (void)argc;
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    return 0;
}

static int cmd_date(char **argv, int argc) {
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

static int cmd_uptime(char **argv, int argc) {
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

static int cmd_kill(char **argv, int argc) {
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

static int cmd_su(char **argv, int argc) {
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

static int cmd_sleep(char **argv, int argc) {
    if (argc < 2) { write(STDERR_FILENO, "usage: sleep SECONDS\n", 21); return 1; }
    sleep((unsigned int)atoi(argv[1]));
    return 0;
}

static int cmd_free(char **argv, int argc) {
    (void)argv; (void)argc;
    void *brk = sbrk(0);
    char buf[32];
    write(STDOUT_FILENO, "heap brk: 0x", 12);
    hex_to_ascii((unsigned int)(size_t)brk, buf);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int cmd_fetch(char **argv, int argc) {
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

static const struct builtin_cmd table[] = {
    {"clear",  cmd_clear},
    {"date",   cmd_date},
    {"uptime", cmd_uptime},
    {"kill",   cmd_kill},
    {"su",     cmd_su},
    {"sleep",  cmd_sleep},
    {"free",   cmd_free},
    {"fetch",  cmd_fetch},
    {NULL, NULL},
};

int sys_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *sys_help(void) {
    return
        "  System:\n"
        "    clear                  clear screen\n"
        "    date                   print current date/time (UTC)\n"
        "    uptime                 show system uptime\n"
        "    kill [-SIG] PID...     send signal (POSIX nums mapped)\n"
        "    su [uid [gid]]         switch user/group\n"
        "    sleep N                sleep N seconds\n"
        "    free                   show heap break address\n"
        "    fetch                  show system info banner\n";
}
