CC      = gcc
LD      = ld
CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib \
          -I./include -I../CactLib-x86_32/include -Wall -Wextra
LDFLAGS = -m elf_i386 -nostdlib -T link.ld

CACTLIB  = ../CactLib-x86_32
START_O  = $(CACTLIB)/build/start.o
LIBC_A   = $(CACTLIB)/libc.a

SRCS = src/main.c src/readline.c src/shell.c src/builtin.c
OBJS = $(SRCS:.c=.o)

TARGET = cactsole

all: $(LIBC_A) $(TARGET)

$(LIBC_A):
	$(MAKE) -C $(CACTLIB)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(START_O) $(OBJS) $(LIBC_A) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean