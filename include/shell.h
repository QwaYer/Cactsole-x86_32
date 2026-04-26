#ifndef SHELL_H
#define SHELL_H

extern char **shell_env;

void  shell_run(char **envp);
char *env_get(const char *name);
void  env_set(const char *name, const char *val);
void  env_unset(const char *name);

#endif