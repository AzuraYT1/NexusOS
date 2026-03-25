/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_GDT_H
#define NEXUS_GDT_H

#include "types.h"

typedef struct PACKED {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} tss_t;

void gdt_init(void);
void tss_set_kernel_stack(uint64_t stack);

extern tss_t tss;

#endif
