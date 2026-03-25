/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_PMM_H
#define NEXUS_PMM_H

#include "../kernel/types.h"

void pmm_init(uint64_t mem_size);
void pmm_init_region(uint64_t base, uint64_t size);
void pmm_deinit_region(uint64_t base, uint64_t size);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t addr);
uint64_t pmm_alloc_pages(uint32_t count);
void pmm_free_pages(uint64_t addr, uint32_t count);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_total_memory(void);

#endif
