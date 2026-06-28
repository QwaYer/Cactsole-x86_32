#include "builtins.h"

const char *net_help_bin(void) {
    return
        "  nconn <ip> <port> [txt]  TCP connect: send text, read response\n"
        "  net <ip> <port> [txt]    alias for nconn\n";
}

const char *net_help_sbin(void) {
    return
        "  ping [-c N] <host>  send ICMP echo requests\n"
        "  dhcp                 run DHCP client\n"
        "  dns <hostname>       DNS A-record lookup\n";
}