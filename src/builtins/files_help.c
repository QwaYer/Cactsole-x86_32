/* Только строки help: реализации в CactUserBins-x86_32/common/ex_files.c */

#include "builtins.h"

const char *files_help_bin(void) {
    return
        "\nFiles (/bin):\n"
        "  ls mkdir rmdir tch rm cat wrt stat mv ln readlink\n"
        "                          (отдельные ELF)\n";
}

const char *files_help_sbin(void) {
    return "";
}
