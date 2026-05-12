CC      = gcc
LD      = ld

# PIE: -fPIE для генерации position-independent кода, линкуется через -pie.
# Линкуемся против libc.so динамически; libc.a больше не нужна в этой цепочке.
CFLAGS  = -m32 -ffreestanding -fPIE -fno-stack-protector -nostdlib \
          -I./include -I../CactLib-x86_32/include -Wall -Wextra
LDFLAGS = -m elf_i386 -pie --no-dynamic-linker --hash-style=both \
          -nostdlib -T link.ld

CACTLIB  = ../CactLib-x86_32
START_O  = $(CACTLIB)/build/pic/start.o
LIBC_SO  = $(CACTLIB)/libc.so

CORE_SRCS    = src/main.c src/readline.c src/shell.c src/builtin.c
BUILTIN_SRCS = $(wildcard src/builtins/*.c)
SRCS = $(CORE_SRCS) $(BUILTIN_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = cactsole

all: $(LIBC_SO) $(START_O) $(TARGET)

$(LIBC_SO):
	$(MAKE) -C $(CACTLIB)

$(START_O):
	$(MAKE) -C $(CACTLIB)

$(TARGET): $(OBJS) $(START_O) $(LIBC_SO) link.ld
	$(LD) $(LDFLAGS) $(START_O) $(OBJS) $(LIBC_SO) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
