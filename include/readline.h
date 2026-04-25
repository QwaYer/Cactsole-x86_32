#ifndef READLINE_H
#define READLINE_H

void readline_init(void);
void readline_cleanup(void);
int  readline(char *buf, int max);
void readline_add_history(const char *line);

#endif