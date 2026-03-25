/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_MULTIBOOT2_H
#define NEXUS_MULTIBOOT2_H

#include "types.h"

#define MULTIBOOT2_MAGIC 0x36d76289

// Tag types
#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_BOOTLOADER   2
#define MB2_TAG_MODULE       3
#define MB2_TAG_BASIC_MEM    4
#define MB2_TAG_BIOS_BOOT    5
#define MB2_TAG_MEMORY_MAP   6
#define MB2_TAG_FRAMEBUFFER  8
#define MB2_TAG_ELF_SECTIONS 9
#define MB2_TAG_APM          10
#define MB2_TAG_ACPI_OLD     14
#define MB2_TAG_ACPI_NEW     15

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct PACKED {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_header_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    // entries follow
} mb2_tag_mmap_t;

typedef struct PACKED {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;    // 1=available, 2=reserved, 3=ACPI reclaimable, 4=NVS, 5=badram
    uint32_t reserved;
} mb2_mmap_entry_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
} mb2_tag_framebuffer_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char string[0];
} mb2_tag_module_t;

// Helper to iterate tags
static inline mb2_tag_t *mb2_first_tag(void *mbi) {
    return (mb2_tag_t *)((uint8_t *)mbi + 8);
}

static inline mb2_tag_t *mb2_next_tag(mb2_tag_t *tag) {
    uint64_t addr = (uint64_t)tag + ((tag->size + 7) & ~7);
    return (mb2_tag_t *)addr;
}

#endif
