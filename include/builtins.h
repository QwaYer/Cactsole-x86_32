#ifndef BUILTINS_H
#define BUILTINS_H

#include "builtin.h"

/*
 * Each runner exposes its own builtin table.  `help` prints the
 * list of builtins from these tables, so the command descriptions exist
 * in exactly one place — next to their implementation.
 */

int         nav_run  (char **argv, int argc);
int         env_run  (char **argv, int argc);
int         jobs_run (char **argv, int argc);
int         misc_run (char **argv, int argc);

const struct builtin_cmd *nav_table  (void);
const struct builtin_cmd *env_table  (void);
const struct builtin_cmd *jobs_table (void);
const struct builtin_cmd *misc_table (void);

#endif
