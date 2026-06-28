/*
 * Только строки help: реализации в CactUserBins-x86_32/common/ex_files.c
 */

#include "builtins.h"

const char *files_help_bin(void) {
    return
        "  ls [path]         list directory contents\n"
        "  mkdir <dir>...    create directories\n"
        "  rmdir <dir>...    remove empty directories\n"
        "  tch <file>...     create or truncate a file\n"
        "  rm <file>...      remove files\n"
        "  cat [file...]     print file contents (stdin if no args)\n"
        "  wrt <file> <txt>  overwrite file with text (no create)\n"
        "  stat <file>       display file metadata\n"
        "  mv <src> <dst>    rename/move file\n"
        "  ln [-s] <tgt> <ln>  create hard link; -s for symlink\n"
        "  readlink <link>   read symlink target\n";
}

const char *files_help_sbin(void) {
    return "";
}