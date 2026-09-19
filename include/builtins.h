#ifndef BUILTINS_H
#define BUILTINS_H

#include "builtin.h"

/*
 * Каждый runner отдаёт наружу свою таблицу билтинов.  `help` печатает
 * список билтинов из этих таблиц, поэтому описания команд существуют
 * ровно в одном месте — рядом с их реализацией.
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
