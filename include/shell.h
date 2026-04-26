#ifndef SHELL_H
#define SHELL_H

extern char **shell_env;

void  shell_run(char **envp);
char *env_get(const char *name);

#endif