/*
 * Линкер-скрипт для cactsole — PIE (ET_DYN) с фиксированной VA 0x08000000.
 * libc.so живёт на 0x10000000 (см. CactLib-x86_32/libc.ld), запас между
 * сегментами кода около 128 МБ — конфликтов по PT_LOAD не будет.
 */

ENTRY(_start)

SECTIONS {
    . = 0x08000000;

    .dynamic   : { *(.dynamic) }
    .hash      : { *(.hash) }
    .gnu.hash  : { *(.gnu.hash) }
    .dynsym    : { *(.dynsym) }
    .dynstr    : { *(.dynstr) }
    .rel.dyn   : { *(.rel.dyn) }
    .rel.plt   : { *(.rel.plt) }

    .init      : { *(.init) }
    .plt       : { *(.plt) }
    .text      : { *(.text .text.*) }
    .fini      : { *(.fini) }

    .rodata    : { *(.rodata .rodata.*) }
    .eh_frame  : { *(.eh_frame) }

    . = ALIGN(0x1000);
    .data      : { *(.data .data.*) }
    .got       : { *(.got) }
    .got.plt   : { *(.got.plt) }
    .bss       : { *(.bss .bss.*) *(COMMON) }
}
