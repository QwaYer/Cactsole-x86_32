/*
 * builtins/net.c — сетевые команды cactsole.
 *
 * Назначение: исчерпывающе использовать все «сетевые» системные вызовы
 * ядра CactKernel (78..89). Каждое подмножество вызовов сосредоточено
 * в одной утилите и оформлено в духе обычных POSIX-инструментов
 * (ip / nc / udp-tools), но без зависимостей, которых у нас нет.
 *
 * Покрытие syscall'ов:
 *
 *   78 SYS_SOCKET     — все команды
 *   79 SYS_BIND       — nlisten, nudp recv
 *   80 SYS_CONNECT    — nconn
 *   81 SYS_LISTEN     — nlisten
 *   82 SYS_ACCEPT     — nlisten
 *   83 SYS_SEND       — nconn, nlisten
 *   84 SYS_RECV       — nconn, nlisten
 *   85 SYS_SENDTO     — nudp send
 *   86 SYS_RECVFROM   — nudp recv
 *   87 SYS_SHUTDOWN   — nconn, nlisten
 *   88 SYS_SETSOCKOPT — nopt, nlisten
 *   89 SYS_GETSOCKOPT — nopt, nconn, nsock
 */

#include "builtin.h"
#include "builtins.h"

#include <stdint.h>
#include <stddef.h>

#include <socket.h>
#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ────────────────────────────────────────────────────────────────────────── */
/*  Утилиты вывода                                                            */
/* ────────────────────────────────────────────────────────────────────────── */

static void w(const char *s)  { write(STDOUT_FILENO, s, strlen((char *)s)); }
static void we(const char *s) { write(STDERR_FILENO, s, strlen((char *)s)); }

static void wn(int v)  { char b[16]; itoa(v, b); w(b); }
static void wne(int v) { char b[16]; itoa(v, b); we(b); }

/* Печать IPv4 в host byte order (старший октет в битах 24..31). */
static void print_ipv4_h(uint32_t ip) {
    char b[8];
    int p[4] = {(int)((ip >> 24) & 0xFF),
                (int)((ip >> 16) & 0xFF),
                (int)((ip >>  8) & 0xFF),
                (int)( ip        & 0xFF)};
    for (int i = 0; i < 4; i++) {
        itoa(p[i], b); w(b);
        if (i < 3) w(".");
    }
}

/* Парсер «10.0.2.15» -> host byte order. 0 = OK, -1 = ошибка. */
static int parse_ipv4(const char *s, uint32_t *out) {
    uint32_t r = 0;
    int dots = 0, val = 0, has_digit = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;
            has_digit = 1;
        } else if (*s == '.') {
            if (!has_digit) return -1;
            r = (r << 8) | (uint32_t)val;
            val = 0; has_digit = 0;
            if (++dots > 3) return -1;
        } else {
            return -1;
        }
        s++;
    }
    if (!has_digit || dots != 3) return -1;
    r = (r << 8) | (uint32_t)val;
    *out = r;
    return 0;
}

static int parse_port(const char *s, uint16_t *out) {
    int v = 0;
    if (!*s) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        if (v > 65535) return -1;
        s++;
    }
    *out = (uint16_t)v;
    return 0;
}

static void perr(const char *who, const char *what, int rc) {
    we(who); we(": "); we(what); we(": rc=");
    wne(rc); we("\n");
}

static void fill_sin(struct sockaddr_in *a, uint32_t ip_h, uint16_t port_h) {
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port   = htons(port_h);
    a->sin_addr   = htonl(ip_h);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  nsock [tcp|udp]  — голая демонстрация SYS_SOCKET                          */
/* ────────────────────────────────────────────────────────────────────────── */

static int cmd_nsock(char **argv, int argc) {
    int type = SOCK_STREAM, proto = IPPROTO_TCP;
    const char *kind = "tcp";
    if (argc >= 2) {
        if (strcmp(argv[1], "tcp") == 0) {
            type = SOCK_STREAM; proto = IPPROTO_TCP; kind = "tcp";
        } else if (strcmp(argv[1], "udp") == 0) {
            type = SOCK_DGRAM;  proto = IPPROTO_UDP; kind = "udp";
        } else {
            we("nsock: unknown kind (use tcp|udp)\n");
            return 1;
        }
    }

    int fd = socket(AF_INET, type, proto);
    if (fd < 0) { perr("nsock", "socket", fd); return 1; }
    w("nsock: opened "); w(kind); w(" socket fd="); wn(fd); w("\n");

    /* Получим SO_ERROR для парности — заодно проверяем getsockopt. */
    int      err = -1;
    uint32_t len = sizeof(err);
    int rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (rc == 0) {
        w("nsock: SO_ERROR=");
        wn(err);
        w("\n");
    }
    close(fd);
    return 0;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  nopt — полный круг setsockopt/getsockopt на свежем TCP-сокете             */
/* ────────────────────────────────────────────────────────────────────────── */

static void show_opt(int fd, int level, int name, const char *label) {
    int      v   = -1;
    uint32_t len = sizeof(v);
    int rc = getsockopt(fd, level, name, &v, &len);
    w("  "); w(label); w(" = ");
    if (rc < 0) { w("<getsockopt rc="); wn(rc); w(">"); }
    else        { wn(v); }
    w("\n");
}

static int cmd_nopt(char **argv, int argc) {
    (void)argv; (void)argc;

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { perr("nopt", "socket", fd); return 1; }

    w("nopt: defaults on fresh TCP socket fd="); wn(fd); w("\n");
    show_opt(fd, SOL_SOCKET,  SO_REUSEADDR, "SO_REUSEADDR");
    show_opt(fd, SOL_SOCKET,  SO_KEEPALIVE, "SO_KEEPALIVE");
    show_opt(fd, IPPROTO_TCP, TCP_NODELAY,  "TCP_NODELAY ");
    show_opt(fd, SOL_SOCKET,  SO_ERROR,     "SO_ERROR    ");

    int one = 1;
    int rc;
    rc = setsockopt(fd, SOL_SOCKET,  SO_REUSEADDR, &one, sizeof(one));
    if (rc < 0) perr("nopt", "set SO_REUSEADDR", rc);
    rc = setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE, &one, sizeof(one));
    if (rc < 0) perr("nopt", "set SO_KEEPALIVE", rc);
    rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,  &one, sizeof(one));
    if (rc < 0) perr("nopt", "set TCP_NODELAY",  rc);

    w("nopt: after setsockopt\n");
    show_opt(fd, SOL_SOCKET,  SO_REUSEADDR, "SO_REUSEADDR");
    show_opt(fd, SOL_SOCKET,  SO_KEEPALIVE, "SO_KEEPALIVE");
    show_opt(fd, IPPROTO_TCP, TCP_NODELAY,  "TCP_NODELAY ");

    close(fd);
    return 0;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  nconn IP PORT [TEXT...]  — TCP-клиент: connect/send/recv/shutdown         */
/* ────────────────────────────────────────────────────────────────────────── */

static int cmd_nconn(char **argv, int argc) {
    if (argc < 3) {
        we("usage: nconn IP PORT [TEXT...]\n"
           "       шлёт TEXT (или 'PING\\n' по умолчанию) и читает ответ\n");
        return 1;
    }
    uint32_t ip;
    uint16_t port;
    if (parse_ipv4(argv[1], &ip)   < 0) { we("nconn: bad ip\n");   return 1; }
    if (parse_port(argv[2], &port) < 0) { we("nconn: bad port\n"); return 1; }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { perr("nconn", "socket", fd); return 1; }

    struct sockaddr_in dst;
    fill_sin(&dst, ip, port);

    w("nconn: connect "); print_ipv4_h(ip); w(":"); wn(port); w(" ... ");
    int rc = connect(fd, &dst, sizeof(dst));
    if (rc < 0) {
        w("FAILED rc="); wn(rc); w("\n");
        int err = 0; uint32_t l = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) == 0) {
            w("nconn: SO_ERROR="); wn(err); w("\n");
        }
        close(fd);
        return 1;
    }
    w("OK\n");

    char  outbuf[1024];
    int   outlen = 0;
    if (argc >= 4) {
        for (int i = 3; i < argc; i++) {
            int l = strlen(argv[i]);
            if (outlen + l + 2 > (int)sizeof(outbuf)) break;
            memcpy(outbuf + outlen, argv[i], l); outlen += l;
            if (i < argc - 1) outbuf[outlen++] = ' ';
        }
        outbuf[outlen++] = '\n';
    } else {
        const char *p = "PING\n";
        outlen = strlen(p);
        memcpy(outbuf, p, outlen);
    }

    int sent = send(fd, outbuf, (uint32_t)outlen, 0);
    w("nconn: send -> "); wn(sent); w(" bytes\n");

    char in[1024];
    int n = recv(fd, in, sizeof(in) - 1, 0);
    if (n < 0) {
        perr("nconn", "recv", n);
    } else if (n == 0) {
        w("nconn: peer closed without data\n");
    } else {
        in[n] = '\0';
        w("nconn: recv "); wn(n); w(" bytes:\n---\n");
        write(STDOUT_FILENO, in, (size_t)n);
        if (in[n - 1] != '\n') w("\n");
        w("---\n");
    }

    rc = shutdown(fd, SHUT_WR);
    if (rc < 0) perr("nconn", "shutdown(SHUT_WR)", rc);
    close(fd);
    return (n < 0) ? 1 : 0;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  nlisten PORT [-once]  — TCP-эхо-сервер                                    */
/* ────────────────────────────────────────────────────────────────────────── */

static int cmd_nlisten(char **argv, int argc) {
    if (argc < 2) {
        we("usage: nlisten PORT [-once]\n"
           "       по умолчанию обрабатывает соединения в цикле\n"
           "       -once    — выйти после первого клиента\n");
        return 1;
    }
    uint16_t port;
    if (parse_port(argv[1], &port) < 0) { we("nlisten: bad port\n"); return 1; }
    int once = (argc >= 3 && strcmp(argv[2], "-once") == 0);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { perr("nlisten", "socket", fd); return 1; }

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        we("nlisten: setsockopt(SO_REUSEADDR) failed (продолжаем)\n");
    }

    struct sockaddr_in addr;
    fill_sin(&addr, INADDR_ANY, port);

    int rc = bind(fd, &addr, sizeof(addr));
    if (rc < 0) { perr("nlisten", "bind", rc); close(fd); return 1; }

    rc = listen(fd, 4);
    if (rc < 0) { perr("nlisten", "listen", rc); close(fd); return 1; }

    w("nlisten: listening on 0.0.0.0:"); wn(port);
    w(once ? " (one-shot)\n" : " (loop, kill the shell to stop)\n");

    for (;;) {
        struct sockaddr_in peer;
        uint32_t plen = sizeof(peer);
        int cfd = accept(fd, &peer, &plen);
        if (cfd < 0) { perr("nlisten", "accept", cfd); close(fd); return 1; }

        uint32_t pip = ntohl(peer.sin_addr);
        uint16_t pp  = ntohs(peer.sin_port);
        w("nlisten: accepted from ");
        print_ipv4_h(pip); w(":"); wn(pp); w(" fd="); wn(cfd); w("\n");

        char buf[1024];
        for (;;) {
            int n = recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            int s = send(cfd, buf, (uint32_t)n, 0);
            w("nlisten:   echo "); wn(n); w(" bytes -> sent "); wn(s); w("\n");
            if (s < 0) break;
        }

        if (shutdown(cfd, SHUT_RDWR) < 0)
            we("nlisten: shutdown(client) failed\n");
        close(cfd);

        if (once) break;
    }

    close(fd);
    return 0;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  nudp send IP PORT TEXT...   — sendto                                      */
/*  nudp recv PORT [-n N]       — bind + recvfrom (по умолчанию 1 пакет)      */
/* ────────────────────────────────────────────────────────────────────────── */

static int cmd_nudp_send(char **argv, int argc) {
    if (argc < 5) {
        we("usage: nudp send IP PORT TEXT...\n");
        return 1;
    }
    uint32_t ip;
    uint16_t port;
    if (parse_ipv4(argv[2], &ip)   < 0) { we("nudp: bad ip\n");   return 1; }
    if (parse_port(argv[3], &port) < 0) { we("nudp: bad port\n"); return 1; }

    char buf[1024];
    int  len = 0;
    for (int i = 4; i < argc; i++) {
        int l = strlen(argv[i]);
        if (len + l + 2 > (int)sizeof(buf)) break;
        memcpy(buf + len, argv[i], l); len += l;
        if (i < argc - 1) buf[len++] = ' ';
    }
    buf[len++] = '\n';

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) { perr("nudp", "socket", fd); return 1; }

    struct sockaddr_in dst;
    fill_sin(&dst, ip, port);

    int rc = sendto(fd, buf, (uint32_t)len, 0, &dst, sizeof(dst));
    w("nudp send: ");
    if (rc < 0) { w("FAIL rc="); wn(rc); w("\n"); close(fd); return 1; }
    wn(rc); w(" bytes -> "); print_ipv4_h(ip); w(":"); wn(port); w("\n");

    close(fd);
    return 0;
}

static int cmd_nudp_recv(char **argv, int argc) {
    if (argc < 3) {
        we("usage: nudp recv PORT [-n COUNT]\n");
        return 1;
    }
    uint16_t port;
    if (parse_port(argv[2], &port) < 0) { we("nudp: bad port\n"); return 1; }

    int count = 1;
    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            count = atoi(argv[i + 1]);
            if (count <= 0) count = 1;
        }
    }

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) { perr("nudp", "socket", fd); return 1; }

    struct sockaddr_in addr;
    fill_sin(&addr, INADDR_ANY, port);
    int rc = bind(fd, &addr, sizeof(addr));
    if (rc < 0) { perr("nudp", "bind", rc); close(fd); return 1; }

    w("nudp recv: bound 0.0.0.0:"); wn(port);
    w(", waiting for "); wn(count); w(" datagram(s)\n");

    char buf[2048];
    for (int i = 0; i < count; i++) {
        struct sockaddr_in src;
        uint32_t slen = sizeof(src);
        int n = recvfrom(fd, buf, sizeof(buf) - 1, 0, &src, &slen);
        if (n < 0) { perr("nudp", "recvfrom", n); close(fd); return 1; }
        buf[n] = '\0';
        uint32_t sip = ntohl(src.sin_addr);
        uint16_t sp  = ntohs(src.sin_port);
        w("nudp recv: ["); wn(i + 1); w("/"); wn(count); w("] ");
        print_ipv4_h(sip); w(":"); wn(sp);
        w(" -> "); wn(n); w(" bytes:\n---\n");
        write(STDOUT_FILENO, buf, (size_t)n);
        if (n == 0 || buf[n - 1] != '\n') w("\n");
        w("---\n");
    }
    close(fd);
    return 0;
}

static int cmd_nudp(char **argv, int argc) {
    if (argc < 2) {
        we("usage: nudp send IP PORT TEXT...\n"
           "       nudp recv PORT [-n COUNT]\n");
        return 1;
    }
    if (strcmp(argv[1], "send") == 0) return cmd_nudp_send(argv, argc);
    if (strcmp(argv[1], "recv") == 0) return cmd_nudp_recv(argv, argc);
    we("nudp: unknown subcommand (send|recv)\n");
    return 1;
}

/* ────────────────────────────────────────────────────────────────────────── */

static const struct builtin_cmd table[] = {
    {"nsock",   cmd_nsock},
    {"nopt",    cmd_nopt},
    {"nconn",   cmd_nconn},
    {"nlisten", cmd_nlisten},
    {"nudp",    cmd_nudp},
    {NULL, NULL},
};

int net_run(char **argv, int argc) {
    return builtin_table_run(table, argv, argc);
}

const char *net_help(void) {
    return
        "  Network:\n"
        "    nsock [tcp|udp]               open socket and probe SO_ERROR\n"
        "    nopt                          dump/toggle SO_REUSEADDR/KEEPALIVE/TCP_NODELAY\n"
        "    nconn IP PORT [TEXT...]       TCP client: connect/send/recv/shutdown\n"
        "    nlisten PORT [-once]          TCP echo server (bind/listen/accept)\n"
        "    nudp send IP PORT TEXT...     UDP sendto\n"
        "    nudp recv PORT [-n N]         UDP bind + recvfrom\n";
}
