# SPDX-License-Identifier: GPL-2.0-only
/* nexusOS linker script for x86_64 ELF kernel */
ENTRY(_start)

SECTIONS
{
    . = 1M;  /* Start at 1MB */

    .multiboot2 ALIGN(8) : {
        *(.multiboot2)
    }

    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }

    .ap_trampoline ALIGN(4K) : {
        *(.ap_trampoline)
    }

    .bss ALIGN(4K) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
