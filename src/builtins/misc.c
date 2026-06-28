/*
 * builtins/misc.c — exit, help.
 */

#include "builtin.h"
#include "builtins.h"
#include "version.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int cmd_exit(char **argv, int argc) {
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;
}

static void print_help_header(void) {
    char buf[128];
    int pos = 0;
    static const char top[] = "\033[1;36m  ==============================\n";
    static const char tit[] = "\033[1;36m     CactOS Shell v\033[0m";
    static const char bot[] = "\033[1;36m  ==============================\033[0m\n\n";
    memcpy(buf + pos, top, sizeof(top) - 1); pos += sizeof(top) - 1;
    memcpy(buf + pos, tit, sizeof(tit) - 1); pos += sizeof(tit) - 1;
    int vl = (int)strlen(CACTSOLE_VERSION);
    memcpy(buf + pos, CACTSOLE_VERSION, vl); pos += vl;
    buf[pos++] = '\n';
    memcpy(buf + pos, bot, sizeof(bot) - 1); pos += sizeof(bot) - 1;
    write(STDOUT_FILENO, buf, pos);
}

static void print_usage(void) {
    static const char usage[] =
        "  usage:  COMMAND [ARGS...]  ;  CMD1 | CMD2  ;  CMD &\n"
        "          help <command>       show help for a specific command\n"
        "\n";
    write(STDOUT_FILENO, usage, sizeof(usage) - 1);
}

static void print_cmd(const char *cmd, const char *desc) {
    char buf[96];
    int pos = 0;
    static const char grn[] = "\033[1;32m";
    static const char rst[] = "\033[0m";
    memcpy(buf + pos, grn, sizeof(grn) - 1); pos += sizeof(grn) - 1;
    int cl = (int)strlen(cmd);
    memcpy(buf + pos, cmd, cl); pos += cl;
    memcpy(buf + pos, rst, sizeof(rst) - 1); pos += sizeof(rst) - 1;
    buf[pos++] = ' ';
    int dl = (int)strlen(desc);
    memcpy(buf + pos, desc, dl); pos += dl;
    buf[pos++] = '\n';
    write(STDOUT_FILENO, buf, pos);
}

static void print_section(const char *title) {
    char buf[96];
    int pos = 0;
    static const char pfx[] = "\033[1;33m  --- \033[0m ";
    memcpy(buf + pos, pfx, sizeof(pfx) - 1); pos += sizeof(pfx) - 1;
    int tlen = (int)strlen(title);
    memcpy(buf + pos, title, tlen); pos += tlen;
    buf[pos++] = ' ';
    int pad = 52 - tlen;
    if (pad < 3) pad = 3;
    for (int i = 0; i < pad; i++)
        buf[pos++] = '-';
    buf[pos++] = '\n';
    write(STDOUT_FILENO, buf, pos);
}

static int cmd_help(char **argv, int argc) {
    if (argc >= 2) {
        char *help_argv[3] = {argv[1], "--help", NULL};
        int st;
        if (builtin_invoke(help_argv, 2, &st))
            return st;
        write(STDERR_FILENO, "cactsole: no help for '", 23);
        write(STDERR_FILENO, argv[1], strlen(argv[1]));
        write(STDERR_FILENO, "'\n", 2);
        return 1;
    }

    print_help_header();
    print_usage();

    print_section("Navigation");
    print_cmd("cd [dir]",         "change directory                    ");
    print_cmd("pwd",              "print working directory             ");

    print_section("Environment");
    print_cmd("export [VAR=val]", "set/list environment variables      ");
    print_cmd("unset VAR...",     "remove environment variables         ");
    print_cmd("env",              "print all environment variables      ");

    print_section("Jobs");
    print_cmd("jobs",             "list background jobs                ");
    print_cmd("fg [n]",           "bring job n to foreground           ");
    print_cmd("bg [n]",           "resume job n in background          ");

    print_section("Shell");
    print_cmd("echo [args...]",   "print arguments                     ");
    print_cmd("exit [code]",      "leave shell                        ");
    print_cmd("help [command]",   "show this screen or command help    ");

    print_section("Files");
    print_cmd("ls [path]",        "list directory contents             [-l -a -h]");
    print_cmd("cat [file...]",    "print file contents                  [-n]");
    print_cmd("stat <file>",      "display file metadata               ");
    print_cmd("tch <file>...",    "create or truncate a file           ");
    print_cmd("rm <file>...",     "remove files                         [-r -f -v]");
    print_cmd("mv <src> <dst>",   "rename/move file                     [-f -v]");
    print_cmd("ln <tgt> <ln>",    "create hard link                     [-s -f]");
    print_cmd("readlink <link>",  "read symlink target                 ");
    print_cmd("mkdir <dir>...",   "create directories                   [-p -v]");
    print_cmd("rmdir <dir>...",   "remove empty directories             [-p -v]");
    print_cmd("wrt <file> <t>",   "overwrite file with text            ");
    print_cmd("cp <src> <dst>",   "copy file                            [-r -f -v]");

    print_section("System");
    print_cmd("clear",            "clear terminal screen                ");
    print_cmd("date",             "print current date/time             ");
    print_cmd("uptime",           "print system uptime                 ");
    print_cmd("sleep <sec>",      "sleep for N seconds                 ");
    print_cmd("free",             "show heap break address             ");
    print_cmd("fetch",            "display system info / logo          ");
    print_cmd("run <path> [a]",   "run a program via fork+exec        ");
    print_cmd("true",             "exit with code 0                     ");
    print_cmd("false",            "exit with code 1                     ");
    print_cmd("whoami",           "print current user name             ");
    print_cmd("id",               "print user/group identity           ");
    print_cmd("chmod <mode> <f>", "change file mode                    [-R -v]");
    print_cmd("chown <uid> <f>",  "change file owner                   [-R -v]");
    print_cmd("version",          "print cactsole version              ");

    print_section("System (sbin)");
    print_cmd("kill [-SIG] <p>",  "send signal to process             [-l -s]");
    print_cmd("su <uid> [gid]",   "substitute user                    ");
    print_cmd("modload <file>",   "load kernel module                 ");
    print_cmd("modunload <id>",   "unload kernel module               ");

    print_section("Network");
    print_cmd("nconn <ip> <p>",   "TCP connect: send/recv             ");
    print_cmd("net <ip> <p> [t]", "alias for nconn                    ");
    print_cmd("ping [-c N] <h>",  "send ICMP echo requests            ");
    print_cmd("dhcp",             "run DHCP client                    ");
    print_cmd("dns <hostname>",   "DNS A-record lookup                ");

    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static const struct builtin_cmd table[] = {
    {"exit", cmd_exit, "exit [code]       leave shell (default: 0)"},
    {"help", cmd_help, "help [command]    show this screen or command help"},
    {NULL, NULL, NULL},
};

int misc_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *misc_help_bin(void) {
    return "";
}

const char *misc_help_sbin(void) {
    return "";
}