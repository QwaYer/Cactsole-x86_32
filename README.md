# 💬 Cactsole/x86_32

<p align="center">
  <img src="https://img.shields.io/badge/version-2.0.0-green.svg?style=for-the-badge" alt="Version: 2.0.0">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C-orange.svg?style=for-the-badge" alt="Language: C">
  <img src="https://img.shields.io/badge/link-PIE%20%2B%20clibc.so-purple.svg?style=for-the-badge" alt="PIE + clibc.so">
  <img src="https://img.shields.io/badge/binary-cactsole-0369a1.svg?style=for-the-badge" alt="cactsole">
</p>

<p align="center">
  The interactive <strong>userspace shell</strong> for <strong>Cact OS</strong>: line editor with <strong>history</strong>, <strong>pipelines</strong>, <strong>redirections</strong>, and <strong>job control</strong>.<br>
  There are <strong>no builtins</strong>: every command is a <strong>separate ELF</strong> on <strong><code>PATH</code></strong> (<strong><a href="https://github.com/QwaYer/CactUserBins-x86_32">CactUserBins-x86_32</a></strong>), run with <strong><code>execve</code></strong>. Commands that must change the shell itself — <strong><code>cd</code></strong>, <strong><code>export</code></strong>, <strong><code>exit</code></strong>, <strong><code>jobs</code></strong>/<strong><code>fg</code></strong>/<strong><code>bg</code></strong> — do it through a <strong>control pipe</strong> the shell drains after each command.
</p>

---

## 📊 Stats

| | |
|---|---|
| **Version** | **2.0.0** — macros in [`include/version.h`](include/version.h) and [`VERSION`](VERSION) |
| **Prompt** | **`cact:`** *cwd* **`$ `** (see [`shell_run`](src/shell.c)) |
| **Line / history** | Input up to **1024** chars; ring history **64** lines; blinking block caret ([`readline.c`](src/readline.c)) |
| **Parser limits** | Up to **64** argv words, **8** pipeline stages, **16** background jobs ([`shell.c`](src/shell.c)) |
| **Environment** | Up to **256** tracked `NAME=value` pairs in the shell copy of **`environ`** |
| **Commands** | **No builtins** — a command is looked up on **`PATH`**, **`execve`**'d, and may answer over the **control pipe** |
| **Load address** | PIE **ET_DYN** at **`0x08000000`** ([`link.ld`](link.ld)); **`clibc.so`** at **`0x10000000`** (CactLib) |

---

## 🔗 Ecosystem

| Component | Role |
|-----------|------|
| **[CactLib-x86_32](https://github.com/QwaYer/CactLibc-x86_32)** | **`clibc.so`** + **`build-meson/start.o`** — syscalls, **`execve`**, **`tcgetattr`** for raw line input, etc. |
| **[CactUserBins-x86_32](../CactUserBins-x86_32)** | **84** `/usr/bin` and `/usr/sbin` tools; the shell **`execve`**s them after **`PATH`** lookup |
| **[Cgoct-x86_32](../Cgoct-x86_32)** | Supervisor that respawns **`/usr/bin/cactsole`** (and optionally **`cactsole-rescue`** — same ELF, different staged path in **LocalRepoCactOS-x86_32**) |
| **[LocalRepoCactOS-x86_32](../LocalRepoCactOS-x86_32)** | Stages **`cactsole`** into **`cctkfs`** as **`/usr/bin/cactsole`** |
| **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** | **Integrator** — builds **CactLib**, then runs the **Meson**/**Ninja** build here, then **LocalRepo** / **kernel** / **ISO** |
| **[CactKernel-x86_32](https://github.com/QwaYer/CactKernel-x86_32)** | First userspace is **`init`**; normal flow reaches **cactsole** as the interactive front-end |

---

## 🔨 Building

**Recommended — full workspace**

Run **`ninja -C CactOS-x86_32/build-meson stage`** from the **parent** of all sibling repos — **CactOS** builds **CactLibc** first, then builds this shell.

**Standalone — this repository**

```sh
meson setup build-meson --cross-file cross/i686-cact-clang.ini
ninja -C build-meson   # auto-detects ../CactLibc-x86_32 → build-meson/cactsole
ninja -C build-meson clean
```

Override the libc path if needed: `meson configure build-meson -Dcactlib=/custom/path`.

**Staging into cctkfs** is normally done by **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** → **LocalRepoCactOS**; this repo does not call sibling builds by path.

---

## 📂 Repository layout

```
Cactsole-x86_32/
├── meson.build
├── link.ld
├── LICENSE
├── VERSION
├── include/
│   ├── shell.h           # env + job control + PATH lookup
│   ├── readline.h
│   └── version.h
└── src/
    ├── main.c            # shell_run(envp)
    ├── shell.c           # parser, pipelines, redirections, external exec, control channel
    └── readline.c        # raw TTY mode, history, basic line editing
```

Every word that is not a redirection, an operator or an assignment is a
command: cactsole resolves it on **`PATH`** (`/usr/bin:/usr/sbin` by default)
and **`execve`**s it as a child process. The shell keeps no catalogue of the
**CactUserBins** tools — each program documents itself.

---

## 🧠 Shell features

| Feature | Detail |
|---------|--------|
| **Pipelines** | Stages separated by **`|`** (up to **`MAX_PIPELINE`**) |
| **Lists** | **`;`** between commands; **`&&`** / **`||`** for conditional chains |
| **Background** | Trailing **`&`** on a simple command or pipeline tail |
| **Job control** | Each job runs in its own process group, so **Ctrl-C** reaches the whole pipeline; a **Ctrl-Z** stop is announced as **`Stopped`** and tracked — **`jobs`** / **`fg`** / **`bg`** are separate binaries again (see the control channel) |
| **Redirections** | **`<`**, **`>`**, **`>>`**, **`2>`**, **`2>>`**, **`&>`** / **`&>>`**, **`2>&1`**, **`1>&2`** |
| **Signals** | **`SIGINT`** / **`SIGCHLD`** handlers integrate job notifications and **Ctrl+C** |
| **Assignments** | A lone **`VAR=value`** line updates the shell environment |

---

## 🔌 Control channel

A child process cannot change its parent, so a command that has to affect the
*shell itself* — its working directory, environment, job table, or the shell's
own existence — cannot work as a plain separate binary. cactsole therefore
hands every child the write end of a pipe in **`$CACT_CTL_FD`**, and after each
foreground command drains it and applies the requests. The shell still has **no
builtins** — it understands only these operations, never command names:

| Request | Fields (TAB-separated) | Effect |
|---------|------------------------|--------|
| **`cd`** | absolute path | `chdir`, so the *next* command in the line sees the new cwd |
| **`setenv`** / **`unsetenv`** | name \[, value\] | update the shell environment (passed to later `execve`) |
| **`exit`** | code | the shell exits with `code` |
| **`jobs`** | — | print the job table |
| **`fg`** / **`bg`** | job number | resume a stopped job in the foreground / background |
| **`kill`** | signal, job number | signal a job's whole process group (`0` = current job) |

The **CactUserBins** tools **`cd`**, **`export`**, **`unset`**, **`exit`**,
**`jobs`**, **`fg`** and **`bg`** send these requests; **`kill`** uses the
channel only for **`%JOB`** specs and signals a raw **`PID`** directly.
**`help`** needs no channel — it just reads **`PATH`**.

The read end is non-blocking (a drain never stalls the prompt) and
close-on-exec, so only the write end reaches children. Requests are one
`\n`-terminated line each; draining happens after every foreground command, so
`cd DIR; ls` changes directory before `ls` runs.

---

## 🚀 First session

After **cgoct** prints **`supervisor online`**, you should see the prompt:

```
cact:/$
```

Type any command on **`PATH`** — there are no builtins, so **`ls`**, **`cat`**,
**`cd`**, **`help`**, **`exit`**, … are all resolved the same way. The
state-changing ones (**`cd`**, **`export`**, **`exit`**, **`jobs`**, …) act
through the control channel; the rest are ordinary programs. Every
**CactUserBins** tool accepts **`--help`** and documents itself. **Ctrl-D** also
ends the session, returning control to **cgoct**, which may restart the shell
according to **`/etc/cgoct.conf`**.
