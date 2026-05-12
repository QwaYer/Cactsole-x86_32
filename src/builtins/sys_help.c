#include "builtins.h"

const char *sys_help_bin(void) {
    return
        "\nSystem (/bin):\n"
        "  clear date uptime sleep free fetch run\n"
        "                          (отдельные ELF)\n";
}

const char *sys_help_sbin(void) {
    return
        "\nSystem (/sbin):\n"
        "  kill su modload modunload\n"
        "                          (отдельные ELF)\n";
}
