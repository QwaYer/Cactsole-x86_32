# 💬 Cactsole/x86_32

<p align="center">
  <img src="https://img.shields.io/badge/version-1.0.0-green.svg?style=for-the-badge" alt="Version: 1.0.0">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C-orange.svg?style=for-the-badge" alt="Language: C">
  <img src="https://img.shields.io/badge/link-PIE%20%2B%20libc.so-purple.svg?style=for-the-badge" alt="PIE + libc.so">
  <img src="https://img.shields.io/badge/binary-cactsole-0369a1.svg?style=for-the-badge" alt="cactsole">
</p>

<p align="center">
  The interactive <strong>userspace shell</strong> for <strong>Cact OS</strong>: line editor with <strong>history</strong>, <strong>pipelines</strong>, <strong>redirections</strong>, <strong>job control</strong>, and a small set of <strong>builtins</strong>.<br>
  Everything else (<strong><code>ls</code></strong>, <strong><code>cat</code></strong>, <strong><code>ping</code></strong>, …) is implemented as <strong>separate ELFs</strong> in <strong><a href="https://github.com/QwaYer/CactUserBins-x86_32">CactUserBins-x86_32</a></strong> on <strong><code>PATH</code></strong>.
</p>

---

## 📊 Stats

| | |
|---|---|
| **Version** | **1.0.0** — macros in [`include/version.h`](include/version.h) and [`VERSION`](VERSION) |
| **Prompt** | **`cact:`** *cwd* **`$ `** (see [`shell_run`](src/shell.c)) |
| **Line / history** | Input up to **1024** chars; ring history **64** lines ([`readline.c`](src/readline.c)) |
| **Parser limits** | Up to **64** argv words, **8** pipeline stages, **16** background jobs ([`shell.c`](src/shell.c)) |
| **Environment** | Up to **256** tracked `NAME=value` pairs in the shell copy of **`environ`** |
| **Builtins** | **`cd`**, **`export`** / **`unset`** / **`env`**, **`jobs`** / **`fg`** / **`bg`**, **`exit`**, **`help`** |
| **Load address** | PIE **ET_DYN** at **`0x08000000`** ([`link.ld`](link.ld)); **`libc.so`** at **`0x10000000`** (CactLib) |

---

## 🔗 Ecosystem

| Component | Role |
|-----------|------|
| **[CactLib-x86_32](https://github.com/QwaYer/CactLib-x86_32)** | **`libc.so`** + **`build/pic/start.o`** — syscalls, **`execve`**, **`tcgetattr`** for raw line input, etc. |
| **[CactUserBins-x86_32](../CactUserBins-x86_32)** | **36** `/bin` and `/sbin` tools; the shell **`execve`**s them after **`PATH`** lookup |
| **[Cgoct-x86_32](../Cgoct-x86_32)** | Supervisor that respawns **`/bin/cactsole`** (and optionally **`cactsole-rescue`** — same ELF, different staged path in **LocalRepoCactOS**) |
| **[LocalRepoCactOS](../LocalRepoCactOS)** | Stages **`cactsole`** into **`cctkfs`** as **`/bin/cactsole`** |
| **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** | **Integrator** — builds **CactLib**, then **`make CACTLIB=…`** here, then **LocalRepo** / **kernel** / **ISO** |
| **[CactKernel-x86_32](https://github.com/QwaYer/CactKernel-x86_32)** | First userspace is **`init`**; normal flow reaches **cactsole** as the interactive front-end |

---

## 🔨 Building

**Recommended — full workspace**

Run **`make`** or **`make -C CactOS-x86_32 iso`** from the **parent** of all sibling repos — **CactOS** builds **CactLib** first, then invokes **`make CACTLIB=…`** here.

**Standalone — this repository**

```sh
make -j"$(nproc)"   # auto-detects ../CactLib-x86_32 → ./cactsole
make clean
```

Override path if needed: `make CACTLIB=/custom/path`.

**Staging into cctkfs** is normally done by **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** → **LocalRepoCactOS**; this repo does not call other **`Makefile`**s by path.

---

## 📂 Repository layout

```
Cactsole-x86_32/
├── Makefile
├── link.ld
├── LICENSE
├── VERSION
├── include/
│   ├── shell.h           # env + job control hooks for builtins
│   ├── readline.h
│   ├── builtin.h
│   ├── builtins.h        # nav/env/jobs/misc group runners + help decls
│   └── version.h
└── src/
    ├── main.c            # shell_run(envp)
    ├── shell.c           # parser, pipelines, redirections, external exec
    ├── readline.c        # raw TTY mode, history, basic line editing
    ├── builtin.c         # dispatches builtin groups
    └── builtins/
        ├── nav.c         # cd
        ├── env.c         # export, unset, env
        ├── jobs.c        # jobs, fg, bg
        ├── misc.c        # exit, help (static command list)
        ├── files_help.c  # categorized help strings (files)
        ├── sys_help.c    # … (system utilities)
        └── net_help.c    # … (network)
```

---

## 🧠 Shell features

| Feature | Detail |
|---------|--------|
| **Pipelines** | Stages separated by **`|`** (up to **`MAX_PIPELINE`**) |
| **Lists** | **`;`** between commands; **`&&`** / **`||`** for conditional chains |
| **Background** | Trailing **`&`** on a simple command or pipeline tail |
| **Redirections** | **`<`**, **`>`**, **`>>`**, **`2>`**, **`2>>`**, **`&>`** / **`&>>`**, **`2>&1`**, **`1>&2`** |
| **`cd` + pipe** | If the line begins with **`cd DIR`** then a **`|`** pipeline, **`cd`** runs in the shell before the rest of the pipeline (see comments in [`shell.c`](src/shell.c)) |
| **Signals** | **`SIGINT`** / **`SIGCHLD`** handlers integrate job notifications and **Ctrl+C** |
| **Assignments** | A lone **`VAR=value`** line updates the shell environment |

---

## 🚀 First session

After **cgoct** prints **`supervisor online`**, you should see the prompt:

```
cact:/$
```

Type **`help`** for the built-in overview (short gloss for every **CactUserBins** tool the shell expects on **`PATH`**). Typing **`exit`** returns control to **cgoct**, which may restart the shell according to **`/etc/cgoct.conf`**.
