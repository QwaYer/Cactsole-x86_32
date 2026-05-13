_ACTIVE := $(filter-out clean,$(or $(MAKECMDGOALS),all))

CACTLIB ?= $(abspath ../CactLib-x86_32)

ifneq ($(_ACTIVE),)
ifndef CACTLIB
$(error Set CACTLIB to the libc project root (built by CactOS-x86_32 target libc))
endif
endif

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -fPIE -fno-stack-protector -nostdlib \
          -I./include -I$(CACTLIB)/include -Wall -Wextra
LDFLAGS = -m elf_i386 -pie --no-dynamic-linker --hash-style=both \
          -nostdlib -T link.ld

START_O  = $(CACTLIB)/build/pic/start.o
LIBC_SO  = $(CACTLIB)/libc.so

CORE_SRCS    = src/main.c src/readline.c src/shell.c src/builtin.c
BUILTIN_SRCS = $(wildcard src/builtins/*.c)
SRCS = $(CORE_SRCS) $(BUILTIN_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = cactsole

all: $(LIBC_SO) $(START_O) $(TARGET)

$(LIBC_SO) $(START_O):
	@test -f $(LIBC_SO) && test -f $(START_O) || (echo >&2 "Missing libc or start.o — build libc first (CACTLIB=$(CACTLIB))"; exit 1)

$(TARGET): $(OBJS) $(START_O) $(LIBC_SO) link.ld
	$(LD) $(LDFLAGS) $(START_O) $(OBJS) $(LIBC_SO) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
