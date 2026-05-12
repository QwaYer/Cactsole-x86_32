#ifndef BUILTINS_H
#define BUILTINS_H


int         nav_run    (char **argv, int argc);
const char *nav_help_bin  (void);
const char *nav_help_sbin (void);


const char *files_help_bin  (void);
const char *files_help_sbin (void);


const char *sys_help_bin  (void);
const char *sys_help_sbin (void);


int         env_run    (char **argv, int argc);
const char *env_help_bin  (void);
const char *env_help_sbin (void);


int         jobs_run   (char **argv, int argc);
const char *jobs_help_bin  (void);
const char *jobs_help_sbin (void);


int         misc_run   (char **argv, int argc);
const char *misc_help_bin  (void);
const char *misc_help_sbin (void);


const char *net_help_bin  (void);
const char *net_help_sbin (void);

#endif
