#include "builtins.h"

const char *net_help_bin(void) {
    return
        "\nNetwork (/bin):\n"
        "  nconn net\n"
        "                          (отдельные ELF)\n";
}

const char *net_help_sbin(void) {
    return
        "\nNetwork (/sbin):\n"
        "  ping dhcp dns\n"
        "                          (отдельные ELF)\n";
}
