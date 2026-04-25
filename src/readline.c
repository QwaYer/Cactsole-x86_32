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
    /* Move cursor to start of editable area, rewrite, clear tail */
    int i;
    /* Erase from current position to end of old content */
    if (prev_len > 0) {
        /* Go back to start: cur characters left */
        for (i = 0; i < cur; i++)
            write(STDOUT_FILENO, "\033[D", 3);
    }
    write(STDOUT_FILENO, buf, len);
    /* Erase any leftover chars from a shorter previous line */
    for (i = len; i < prev_len; i++)
        write(STDOUT_FILENO, " ", 1);
    /* Move cursor back to cur position */
    for (i = len; i > cur; i--)
        write(STDOUT_FILENO, "\033[D", 3);
    if (prev_len > len) {
        /* Also move back the erased padding */
        int pad = prev_len - len;
        for (i = 0; i < pad; i++)
            write(STDOUT_FILENO, "\033[D", 3);
    }
}

void readline_add_history(const char *line) {
    if (line[0] == '\0')
        return;
    /* Avoid duplicate of last entry */
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
    int  hist_idx = hist_len; /* points past last entry = "empty" */
    char saved_line[LINE_MAX];
    saved_line[0] = '\0';

    if (max > LINE_MAX)
        max = LINE_MAX;

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0)
            break;

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            buf[len] = '\0';
            strncpy(out, buf, max - 1);
            out[max - 1] = '\0';
            return len;
        }

        if (c == '\x03') { /* Ctrl+C */
            write(STDOUT_FILENO, "^C\n", 3);
            out[0] = '\0';
            return 0;
        }

        if (c == '\x04' && len == 0) { /* Ctrl+D on empty line */
            write(STDOUT_FILENO, "\n", 1);
            out[0] = '\0';
            return -1;
        }

        if (c == '\x7f' || c == '\b') { /* Backspace */
            if (cur > 0) {
                /* Shift left */
                int i;
                for (i = cur - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];
                cur--;
                len--;
                buf[len] = '\0';
                /* Move cursor back one, redraw suffix + space */
                write(STDOUT_FILENO, "\033[D", 3);
                write(STDOUT_FILENO, buf + cur, len - cur);
                write(STDOUT_FILENO, " ", 1);
                /* Move cursor back to cur */
                int back = len - cur + 1;
                for (i = 0; i < back; i++)
                    write(STDOUT_FILENO, "\033[D", 3);
            }
            continue;
        }

        if (c == '\x1b') { /* Escape sequence */
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (seq[0] != '[') continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            int prev_len = len;

            if (seq[1] == 'A') { /* Up arrow — previous history */
                if (hist_idx > 0) {
                    if (hist_idx == hist_len)
                        strncpy(saved_line, buf, len);
                    hist_idx--;
                    int slot = hist_idx % HIST_MAX;
                    strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                    buf[LINE_MAX - 1] = '\0';
                    len = strlen(buf);
                    cur = len;
                    redraw_line(buf, len, cur, prev_len);
                }
            } else if (seq[1] == 'B') { /* Down arrow — next history */
                if (hist_idx < hist_len) {
                    hist_idx++;
                    if (hist_idx == hist_len) {
                        strncpy(buf, saved_line, LINE_MAX - 1);
                    } else {
                        int slot = hist_idx % HIST_MAX;
                        strncpy(buf, hist_buf[slot], LINE_MAX - 1);
                    }
                    buf[LINE_MAX - 1] = '\0';
                    len = strlen(buf);
                    cur = len;
                    redraw_line(buf, len, cur, prev_len);
                }
            } else if (seq[1] == 'C') { /* Right arrow */
                if (cur < len) {
                    write(STDOUT_FILENO, "\033[C", 3);
                    cur++;
                }
            } else if (seq[1] == 'D') { /* Left arrow */
                if (cur > 0) {
                    write(STDOUT_FILENO, "\033[D", 3);
                    cur--;
                }
            } else if (seq[1] == 'H' || seq[1] == '1') { /* Home */
                while (cur > 0) {
                    write(STDOUT_FILENO, "\033[D", 3);
                    cur--;
                }
            } else if (seq[1] == 'F' || seq[1] == '4') { /* End */
                while (cur < len) {
                    write(STDOUT_FILENO, "\033[C", 3);
                    cur++;
                }
            }
            continue;
        }

        /* Printable character: insert at cur */
        if (c >= 0x20 && c < 0x7f && len < max - 1) {
            int i;
            /* Shift right */
            for (i = len; i > cur; i--)
                buf[i] = buf[i - 1];
            buf[cur] = c;
            len++;
            /* Echo the char and redraw suffix */
            write(STDOUT_FILENO, &c, 1);
            cur++;
            if (cur < len) {
                write(STDOUT_FILENO, buf + cur, len - cur);
                /* Move cursor back */
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