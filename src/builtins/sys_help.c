#include "builtins.h"

const char *sys_help_bin(void) {
    return
        "  clear             clear terminal screen\n"
        "  date              print current date/time (UTC)\n"
        "  uptime            print system uptime\n"
        "  sleep <sec>       sleep for N seconds\n"
        "  free              show heap break address (sbrk(0))\n"
        "  fetch             display system info / CactOS logo\n"
        "  run <path> [args]  run a program via fork+exec\n"
        "  echo [args...]    print arguments\n"
        "  true              exit with code 0\n"
        "  false             exit with code 1\n"
        "  whoami            print current user name\n"
        "  id                print user/group identity\n"
        "  chmod <mode> <f>  change file mode (octal)\n"
        "  chown <uid> <f>   change file owner\n"
        "  version           print cactsole version\n";
}

const char *sys_help_sbin(void) {
    return
        "  kill [-SIG] <pid>  send signal to process (default: SIGTERM)\n"
        "  su <uid> [gid]     substitute user (setuid/setgid)\n"
        "  modload <file>     load kernel module (.cctk)\n"
        "  modunload <id>     unload kernel module\n";
}