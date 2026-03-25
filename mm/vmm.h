/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_VMM_H
#define NEXUS_VMM_H

#include "../kernel/types.h"

// Page table entry flags
#define PTE_PRESENT    (1ULL << 0)
#define PTE_WRITABLE   (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_WRITETHROUGH (1ULL << 3)
#define PTE_NOCACHE    (1ULL << 4)
#define PTE_ACCESSED   (1ULL << 5)
#define PTE_DIRTY      (1ULL << 6)
#define PTE_HUGE       (1ULL << 7)
#define PTE_GLOBAL     (1ULL << 8)
#define PTE_NX         (1ULL << 63)

#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

void vmm_init(void);
uint64_t vmm_get_pml4(void);
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
void vmm_map_range(uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags);

// Kernel heap (simple bump allocator)
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t align);
void *kzalloc(size_t size);
void kfree(void *ptr);
void heap_init(void);

#endif
