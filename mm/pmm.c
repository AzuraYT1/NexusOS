/* SPDX-License-Identifier: GPL-2.0-only */
#include "pmm.h"
#include "../kernel/console.h"
#include "../kernel/string.h"

// Bitmap-based physical memory manager
#define PMM_BLOCK_SIZE 4096
#define PMM_BITS_PER_ENTRY 64

static uint64_t *pmm_bitmap = NULL;
static uint64_t pmm_bitmap_size = 0;  // in uint64_t entries
static uint64_t pmm_total_blocks = 0;
static uint64_t pmm_used_blocks = 0;
static uint64_t pmm_total_memory = 0;

// Place bitmap at a fixed location after kernel
// We'll use physical address 0x200000 (2MB mark)
#define PMM_BITMAP_ADDR 0x200000

static void pmm_set_bit(uint64_t block) {
    pmm_bitmap[block / 64] |= (1ULL << (block % 64));
}

static void pmm_clear_bit(uint64_t block) {
    pmm_bitmap[block / 64] &= ~(1ULL << (block % 64));
}

static int pmm_test_bit(uint64_t block) {
    return (pmm_bitmap[block / 64] >> (block % 64)) & 1;
}

static int64_t pmm_first_free(void) {
    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            for (int j = 0; j < 64; j++) {
                if (!(pmm_bitmap[i] & (1ULL << j))) {
                    return i * 64 + j;
                }
            }
        }
    }
    return -1;
}

static int64_t pmm_first_free_contiguous(uint32_t count) {
    if (count == 1) return pmm_first_free();
    
    uint32_t found = 0;
    int64_t start = -1;
    
    for (uint64_t i = 0; i < pmm_total_blocks; i++) {
        if (!pmm_test_bit(i)) {
            if (found == 0) start = i;
            found++;
            if (found == count) return start;
        } else {
            found = 0;
            start = -1;
        }
    }
    return -1;
}

void pmm_init(uint64_t mem_size) {
    pmm_total_memory = mem_size;
    pmm_total_blocks = mem_size / PMM_BLOCK_SIZE;
    pmm_bitmap_size = (pmm_total_blocks + 63) / 64;
    pmm_bitmap = (uint64_t *)PMM_BITMAP_ADDR;
    pmm_used_blocks = pmm_total_blocks;
    
    // Mark all memory as used initially
    memset(pmm_bitmap, 0xFF, pmm_bitmap_size * sizeof(uint64_t));
    
    kprintf("  PMM: %d MB total, %d blocks, bitmap at %p\n",
            (int)(mem_size / (1024*1024)), (int)pmm_total_blocks, (uint64_t)PMM_BITMAP_ADDR);
}

void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t block = base / PMM_BLOCK_SIZE;
    uint64_t num_blocks = size / PMM_BLOCK_SIZE;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        if (block + i < pmm_total_blocks) {
            pmm_clear_bit(block + i);
            pmm_used_blocks--;
        }
    }
}

void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t block = base / PMM_BLOCK_SIZE;
    uint64_t num_blocks = size / PMM_BLOCK_SIZE;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        if (block + i < pmm_total_blocks) {
            pmm_set_bit(block + i);
            pmm_used_blocks++;
        }
    }
}

uint64_t pmm_alloc_page(void) {
    int64_t block = pmm_first_free();
    if (block < 0) return 0;
    
    pmm_set_bit(block);
    pmm_used_blocks++;
    return (uint64_t)block * PMM_BLOCK_SIZE;
}

void pmm_free_page(uint64_t addr) {
    uint64_t block = addr / PMM_BLOCK_SIZE;
    if (block < pmm_total_blocks && pmm_test_bit(block)) {
        pmm_clear_bit(block);
        pmm_used_blocks--;
    }
}

uint64_t pmm_alloc_pages(uint32_t count) {
    int64_t block = pmm_first_free_contiguous(count);
    if (block < 0) return 0;
    
    for (uint32_t i = 0; i < count; i++) {
        pmm_set_bit(block + i);
        pmm_used_blocks++;
    }
    return (uint64_t)block * PMM_BLOCK_SIZE;
}

void pmm_free_pages(uint64_t addr, uint32_t count) {
    uint64_t block = addr / PMM_BLOCK_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        if (block + i < pmm_total_blocks) {
            pmm_clear_bit(block + i);
            pmm_used_blocks--;
        }
    }
}

uint64_t pmm_get_free_memory(void) {
    return (pmm_total_blocks - pmm_used_blocks) * PMM_BLOCK_SIZE;
}

uint64_t pmm_get_total_memory(void) {
    return pmm_total_memory;
}
