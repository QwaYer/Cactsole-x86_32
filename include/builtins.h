#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * Каталог групп встроенных команд cactsole.
 *
 * Каждая группа выставляет два символа:
 *   <group>_run(argv, argc)  — выполняет команду argv[0] из этой группы
 *                              и возвращает её код завершения, либо -1,
 *                              если argv[0] этой группе не принадлежит;
 *   <group>_help()           — статическая строка-секция для команды `help`.
 *
 * Диспетчер builtin_run() вызывает группы по порядку, поэтому добавить
 * новую группу = создать src/builtins/<name>.c, реализовать пару функций
 * и сослаться на них в builtin.c.
 */

/* navigation: cd, pwd */
int         nav_run    (char **argv, int argc);
const char *nav_help   (void);

/* files: ls, mkdir, rmdir, tch, rm, cat, wrt, stat, mv, ln, readlink */
int         files_run  (char **argv, int argc);
const char *files_help (void);

/* system: clear, date, uptime, kill, su, sleep, free, fetch */
int         sys_run    (char **argv, int argc);
const char *sys_help   (void);

/* environment: echo, export, unset, env */
int         env_run    (char **argv, int argc);
const char *env_help   (void);

/* jobs: jobs, fg, bg */
int         jobs_run   (char **argv, int argc);
const char *jobs_help  (void);

/* misc: true, false, exit, whoami, id, chmod, chown, version, help */
int         misc_run   (char **argv, int argc);
const char *misc_help  (void);

/* network: nsock, nopt, nconn, nlisten, nudp */
int         net_run    (char **argv, int argc);
const char *net_help   (void);

#endif /* BUILTINS_H */
