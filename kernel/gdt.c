/* SPDX-License-Identifier: GPL-2.0-only */
#include "gdt.h"
#include "console.h"
#include "string.h"

extern void tss_load(uint16_t selector);

// The GDT is already set up in boot.asm, but we need to properly configure TSS
tss_t tss;

// Per-CPU data for SYSCALL (kernel rsp, user rsp)
uint64_t percpu_data[2] __attribute__((aligned(16))) = {0, 0};

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.iopb = sizeof(tss);

    // Get the GDT pointer
    extern uint64_t gdt64[];

    // TSS descriptor is at offset 0x28 in our GDT (index 5)
    uint64_t tss_addr = (uint64_t)&tss;
    uint16_t tss_limit = sizeof(tss) - 1;

    // Encode TSS descriptor (it's 16 bytes for 64-bit TSS)
    uint64_t low = 0;
    low |= (uint64_t)(tss_limit & 0xFFFF);
    low |= (uint64_t)(tss_addr & 0xFFFF) << 16;
    low |= (uint64_t)((tss_addr >> 16) & 0xFF) << 32;
    low |= (uint64_t)0x89ULL << 40;  // present, type=TSS available
    low |= (uint64_t)((tss_limit >> 16) & 0xF) << 48;
    low |= (uint64_t)((tss_addr >> 24) & 0xFF) << 56;

    uint64_t high = (tss_addr >> 32) & 0xFFFFFFFF;

    gdt64[5] = low;
    gdt64[6] = high;

    // Reload GDT with updated TSS
    struct PACKED {
        uint16_t limit;
        uint64_t base;
    } gdtr;
    gdtr.limit = 7 * 8 - 1;  // 7 entries
    gdtr.base = (uint64_t)gdt64;

    extern void gdt_load(void *gdtr);
    gdt_load(&gdtr);

    // Load TSS (selector 0x28)
    tss_load(0x28);

    kprintf("  GDT initialized with TSS\n");
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
    percpu_data[0] = stack;  // For SYSCALL handler
}
