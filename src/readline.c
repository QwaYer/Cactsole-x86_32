#include "readline.h"
#include <termios.h>
#include <unistd.h>
#include <string.h>

#define HIST_MAX  64
#define LINE_MAX  1024

static struct termios saved_termios;
static int            raw_mode_active = 0;

static char hist_buf[HIST_MAX][LINE_MAX];
static int  hist_len = 0;

static void raw_mode_enter(void) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0)
        return;
    raw = saved_termios;
    raw.c_lflag &= ~(unsigned int)(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    raw_mode_active = 1;
}

static void raw_mode_leave(void) {
    if (raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        raw_mode_active = 0;
    }
}

void readline_init(void) {
    raw_mode_enter();
}

void readline_cleanup(void) {
    raw_mode_leave();
}

static void redraw_line(const char *buf, int len, int cur, int prev_len) {
    int i;
    if (prev_len > 0) {
        for (i = 0; i < cur; i++)
            write(STDOUT_FILENO, "\033[D", 3);
    }
    write(STDOUT_FILENO, buf, len);
    for (i = len; i < prev_len; i++)
        write(STDOUT_FILENO, " ", 1);
    for (i = len; i > cur; i--)
        write(STDOUT_FILENO, "\033[D", 3);
    if (prev_len > len) {
        int pad = prev_len - len;
        for (i = 0; i < pad; i++)
            write(STDOUT_FILENO, "\033[D", 3);
    }
}

void readline_add_history(const char *line) {
    if (line[0] == '\0')
        return;
    if (hist_len > 0 && strcmp(hist_buf[(hist_len - 1) % HIST_MAX], (char *)line) == 0)
        return;
    strncpy(hist_buf[hist_len % HIST_MAX], line, LINE_MAX - 1);
    hist_buf[hist_len % HIST_MAX][LINE_MAX - 1] = '\0';
    hist_len++;
}

int readline(char *out, int max) {
    char buf[LINE_MAX];
    int  len = 0;
    int  cur = 0;
    int  hist_idx = hist_len; 
    char saved_line[LINE_MAX];
    saved_line[0] = '\0';

    if (max > LINE_MAX)
        max = LINE_MAX;

    while (1) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            break;

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            buf[len] = '\0';
            strncpy(out, buf, max - 1);
            out[max - 1] = '\0';
            return len;
        }

        if (c == '\x03') { 
            write(STDOUT_FILENO, "^C\n", 3);
            out[0] = '\0';
            return 0;
        }

        if (c == '\x04' && len == 0) { 
            write(STDOUT_FILENO, "\n", 1);
            out[0] = '\0';
            return -1;
        }

        if (c == '\x7f' || c == '\b') { 
            if (cur > 0) {
                int i;
                for (i = cur - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];
                cur--;
                len--;
                buf[len] = '\0';
                write(STDOUT_FILENO, "\033[D", 3);
                write(STDOUT_FILENO, buf + cur, len - cur);
                write(STDOUT_FILENO, " ", 1);
                int back = len - cur + 1;
                for (i = 0; i < back; i++)
                    write(STDOUT_FILENO, "\033[D", 3);
            }
            continue;
        }

        if (c == '\x1b') { 
            unsigned char b;
            if (read(STDIN_FILENO, &b, 1) != 1)
                continue;
            if (b == 'O') {
                unsigned char b2;
                if (read(STDIN_FILENO, &b2, 1) != 1)
                    continue;
                int prev_len = len;
                if (b2 == 'A') {
                    if (hist_idx > 0) {
                        if (hist_idx == hist_len)
                            strncpy(saved_line, buf, (size_t)len);
                        hist_idx--;
                        int slot = hist_idx % HIST_MAX;
                        strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    }
                } else if (b2 == 'B') {
                    if (hist_idx < hist_len) {
                        hist_idx++;
                        if (hist_idx == hist_len) {
                            strncpy(buf, saved_line, LINE_MAX - 1);
                        } else {
                            int slot = hist_idx % HIST_MAX;
                            strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                        }
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    }
                } else if (b2 == 'C' && cur < len) {
                    write(STDOUT_FILENO, "\033[C", 3);
                    cur++;
                } else if (b2 == 'D' && cur > 0) {
                    write(STDOUT_FILENO, "\033[D", 3);
                    cur--;
                } else if (b2 == 'H') {
                    while (cur > 0) {
                        write(STDOUT_FILENO, "\033[D", 3);
                        cur--;
                    }
                } else if (b2 == 'F') {
                    while (cur < len) {
                        write(STDOUT_FILENO, "\033[C", 3);
                        cur++;
                    }
                }
                continue;
            }
            if (b != '[')
                continue;
            unsigned char fin = 0;
            int k;
            for (k = 0; k < 24; k++) {
                if (read(STDIN_FILENO, &b, 1) != 1) {
                    fin = 0;
                    break;
                }
                if ((b >= 0x30u && b <= 0x3fu) || (b >= 0x20u && b <= 0x2fu))
                    continue;
                fin = b;
                break;
            }
            if (fin == 0)
                continue;
            if (fin == 'O') {
                if (read(STDIN_FILENO, &b, 1) == 1) {
                    if (b == 'H')
                        fin = 'H';
                    else if (b == 'F')
                        fin = 'F';
                }
            }
            {
                int prev_len = len;
                if (fin == 'A') {
                    if (hist_idx > 0) {
                        if (hist_idx == hist_len)
                            strncpy(saved_line, buf, (size_t)len);
                        hist_idx--;
                        int slot = hist_idx % HIST_MAX;
                        strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    }
                } else if (fin == 'B') {
                    if (hist_idx < hist_len) {
                        hist_idx++;
                        if (hist_idx == hist_len) {
                            strncpy(buf, saved_line, LINE_MAX - 1);
                        } else {
                            int slot = hist_idx % HIST_MAX;
                            strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                        }
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    }
                } else if (fin == 'C' && cur < len) {
                    write(STDOUT_FILENO, "\033[C", 3);
                    cur++;
                } else if (fin == 'D' && cur > 0) {
                    write(STDOUT_FILENO, "\033[D", 3);
                    cur--;
                } else if (fin == 'H') {
                    while (cur > 0) {
                        write(STDOUT_FILENO, "\033[D", 3);
                        cur--;
                    }
                } else if (fin == 'F') {
                    while (cur < len) {
                        write(STDOUT_FILENO, "\033[C", 3);
                        cur++;
                    }
                }
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7f && len < max - 1) {
            int i;
            for (i = len; i > cur; i--)
                buf[i] = buf[i - 1];
            buf[cur] = (char)c;
            len++;
            write(STDOUT_FILENO, &c, 1);
            cur++;
            if (cur < len) {
                write(STDOUT_FILENO, buf + cur, len - cur);
                for (i = 0; i < len - cur; i++)
                    write(STDOUT_FILENO, "\033[D", 3);
            }
        }
    }

    buf[len] = '\0';
    strncpy(out, buf, max - 1);
    out[max - 1] = '\0';
    return len;
}
