#include "readline.h"
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define HIST_MAX  64
#define LINE_MAX  1024

/* Caret blink period.  The line editor has no way to time out a blocking tty
 * read, but a pending SIGALRM makes tty_read() return -EINTR — so setitimer()
 * wakes the loop every CARET_BLINK_MS while a line is being edited.  The
 * handler only raises a flag; the read loop does the actual repaint. */
#define CARET_BLINK_MS 500

static struct termios saved_termios;
static int            raw_mode_active = 0;

static char hist_buf[HIST_MAX][LINE_MAX];
static int  hist_len = 0;

static char prompt_str[256];
static int  prompt_len = 0;

static int  caret_visible = 1;
static volatile int blink_tick = 0;

static void blink_stop(void);

void readline_set_prompt(const char *p) {
    int n = 0;
    while (p[n] && n < (int)sizeof(prompt_str) - 1) {
        prompt_str[n] = p[n];
        n++;
    }
    prompt_str[n] = '\0';
    prompt_len = n;
}

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
    /* Raw mode is entered per readline() call, not for the whole session: while
       a foreground command runs the terminal must be back in its normal mode,
       or the console cannot turn ^C into SIGINT (see readline() below). */
}

void readline_cleanup(void) {
    blink_stop();
    raw_mode_leave();
}

static void blink_alarm(int sig) {
    (void)sig;
    blink_tick = 1;
}

/* Arm the caret blink for one editing session.  SIGALRM is only a wakeup; the
 * read loop redraws.  If the timer cannot be set the editor still works, just
 * without a blinking caret, so failures are ignored. */
static void blink_arm(void) {
    static int installed = 0;
    struct itimerval it;

    if (!installed) {
        if (signal(SIGALRM, blink_alarm) == SIG_ERR)
            return;
        installed = 1;
    }
    it.it_value.tv_sec     = 0;
    it.it_value.tv_usec    = CARET_BLINK_MS * 1000;
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = CARET_BLINK_MS * 1000;
    blink_tick    = 0;
    caret_visible = 1;
    setitimer(ITIMER_REAL, &it, NULL);
}

static void blink_stop(void) {
    struct itimerval it;

    it.it_value.tv_sec     = 0;
    it.it_value.tv_usec    = 0;
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);
    blink_tick = 0;
}

/* Paint the block caret on the cell at `cur`.  redraw_line() has already
 * parked the console cursor there, so draw in place and park it in front of
 * the block again — Enter emits its '\n' from the caret column. */
static void paint_caret(const char *buf, int len, int cur) {
    char ch = (cur < len) ? buf[cur] : ' ';
    char seq[10];
    int  n = 0;

    seq[n++] = '\033'; seq[n++] = '['; seq[n++] = '7'; seq[n++] = 'm';
    seq[n++] = ch;
    seq[n++] = '\033'; seq[n++] = '['; seq[n++] = '2'; seq[n++] = '7'; seq[n++] = 'm';
    write(STDOUT_FILENO, seq, (size_t)n);
    write(STDOUT_FILENO, "\r", 1);
    if (prompt_len > 0)
        write(STDOUT_FILENO, prompt_str, prompt_len);
    write(STDOUT_FILENO, buf, cur);
}

static void redraw_line(const char *buf, int len, int cur, int prev_len) {
    int i, max = (len > prev_len) ? len : prev_len;
    write(STDOUT_FILENO, "\r", 1);
    if (prompt_len > 0)
        write(STDOUT_FILENO, prompt_str, prompt_len);
    write(STDOUT_FILENO, buf, len);
    for (i = len; i < max + 1; i++)
        write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\r", 1);
    if (prompt_len > 0)
        write(STDOUT_FILENO, prompt_str, prompt_len);
    write(STDOUT_FILENO, buf, cur);
    if (caret_visible)
        paint_caret(buf, len, cur);
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

static int readline_raw(char *out, int max) {
    char buf[LINE_MAX];
    buf[0] = '\0';
    int  len = 0;
    int  cur = 0;
    int  hist_idx = hist_len; 
    char saved_line[LINE_MAX];
    saved_line[0] = '\0';

    if (max > LINE_MAX)
        max = LINE_MAX;

    blink_arm();
    redraw_line(buf, len, cur, 0);

    while (1) {
        unsigned char c;
        ssize_t r;

        if (blink_tick) {
            blink_tick = 0;
            caret_visible = !caret_visible;
            redraw_line(buf, len, cur, len);
        }

        r = read(STDIN_FILENO, &c, 1);
        if (r == -EINTR)
            continue;              /* a caret tick — handled at the loop top */
        if (r != 1)
            break;

        caret_visible = 1;         /* typing keeps the caret on */

        if (c == '\n' || c == '\r') {
            caret_visible = 0;
            redraw_line(buf, len, len, len);   /* drop the caret, end at EOL */
            write(STDOUT_FILENO, "\n", 1);
            buf[len] = '\0';
            strncpy(out, buf, max - 1);
            out[max - 1] = '\0';
            blink_stop();
            return len;
        }

        if (c == '\x03') {
            caret_visible = 0;
            redraw_line(buf, len, len, len);
            write(STDOUT_FILENO, "^C\n", 3);
            out[0] = '\0';
            blink_stop();
            return 0;
        }

        if (c == '\x04' && len == 0) {
            caret_visible = 0;
            redraw_line(buf, 0, 0, 0);
            write(STDOUT_FILENO, "\n", 1);
            out[0] = '\0';
            blink_stop();
            return -1;
        }

        if (c == '\x7f' || c == '\b') { 
            if (cur > 0) {
                int old_len = len, i;
                for (i = cur - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];
                cur--;
                len--;
                buf[len] = '\0';
                redraw_line(buf, len, cur, old_len);
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
                    cur++;
                    redraw_line(buf, len, cur, len);
                } else if (b2 == 'D' && cur > 0) {
                    cur--;
                    redraw_line(buf, len, cur, len);
                } else if (b2 == 'H') {
                    cur = 0;
                    redraw_line(buf, len, cur, len);
                } else if (b2 == 'F') {
                    cur = len;
                    redraw_line(buf, len, cur, len);
                }
                continue;
            }
            if (b != '[')
                continue;
            unsigned char fin = 0;
            unsigned char param = 0;
            int k;
            for (k = 0; k < 24; k++) {
                if (read(STDIN_FILENO, &b, 1) != 1) {
                    fin = 0;
                    break;
                }
                if (b >= 0x30u && b <= 0x3fu) {
                    if (param == 0)
                        param = b;
                    continue;
                }
                if (b >= 0x20u && b <= 0x2fu)
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
                    cur++;
                    redraw_line(buf, len, cur, len);
                } else if (fin == 'D' && cur > 0) {
                    cur--;
                    redraw_line(buf, len, cur, len);
                } else if (fin == 'H') {
                    cur = 0;
                    redraw_line(buf, len, cur, len);
                } else if (fin == 'F') {
                    cur = len;
                    redraw_line(buf, len, cur, len);
                } else if (fin == '~') {
                    if (param == '3' && cur < len) {
                        int old_len = len, i;
                        for (i = cur; i < len - 1; i++)
                            buf[i] = buf[i + 1];
                        len--;
                        buf[len] = '\0';
                        redraw_line(buf, len, cur, old_len);
                    } else if (param == '5' && hist_len > 0) {
                        int prev_len = len;
                        if (hist_idx == hist_len)
                            strncpy(saved_line, buf, (size_t)len);
                        hist_idx = 0;
                        strncpy(buf, hist_buf[0], LINE_MAX - 1);
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    } else if (param == '6') {
                        int prev_len = len;
                        hist_idx = hist_len;
                        strncpy(buf, saved_line, LINE_MAX - 1);
                        buf[LINE_MAX - 1] = '\0';
                        len = (int)strlen(buf);
                        cur = len;
                        redraw_line(buf, len, cur, prev_len);
                    }
                }
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7f && len < max - 1) {
            int old_len = len, i;
            for (i = len; i > cur; i--)
                buf[i] = buf[i - 1];
            buf[cur] = (char)c;
            len++;
            cur++;
            redraw_line(buf, len, cur, old_len);
        }
    }

    blink_stop();
    buf[len] = '\0';
    strncpy(out, buf, max - 1);
    out[max - 1] = '\0';
    return len;
}

/* Edit one line with the terminal in raw mode, then hand the terminal back
 * exactly as it was.  The editor needs per-key input (ICANON off), but the
 * shell must not keep raw mode while a foreground command runs: a child needs
 * the terminal's normal canonical+ISIG settings, otherwise the console cannot
 * turn ^C into SIGINT and Ctrl+C reaches the child as a raw 0x03 byte instead
 * of stopping it. */
int readline(char *out, int max) {
    raw_mode_enter();
    int r = readline_raw(out, max);
    raw_mode_leave();
    return r;
}
