/* SPDX-License-Identifier: GPL-2.0-only */
#include "vmm.h"
#include "pmm.h"
#include "../kernel/console.h"
#include "../kernel/string.h"

extern void invlpg(uint64_t addr);
extern uint64_t read_cr3(void);
extern void write_cr3(uint64_t cr3);

static uint64_t *pml4_table;

// Get or create page table entry
static uint64_t *vmm_get_next_level(uint64_t *table, uint64_t index, uint64_t flags) {
    if (!(table[index] & PTE_PRESENT)) {
        uint64_t page = pmm_alloc_page();
        if (!page) return NULL;
        memset((void *)page, 0, PAGE_SIZE);
        table[index] = page | flags | PTE_PRESENT | PTE_WRITABLE;
    }
    return (uint64_t *)(table[index] & PTE_ADDR_MASK);
}

void vmm_init(void) {
    // Use existing page tables from bootloader initially
    pml4_table = (uint64_t *)read_cr3();
    
    kprintf("  VMM: Page tables at %p\n", (uint64_t)pml4_table);
    kprintf("  VMM: 4-level paging active (PML4)\n");
}

uint64_t vmm_get_pml4(void) {
    return (uint64_t)pml4_table;
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t user_flags = flags & PTE_USER ? PTE_USER : 0;
    
    uint64_t *pdpt = vmm_get_next_level(pml4_table, pml4_idx, user_flags);
    if (!pdpt) return;
    
    uint64_t *pd = vmm_get_next_level(pdpt, pdpt_idx, user_flags);
    if (!pd) return;
    
    uint64_t *pt = vmm_get_next_level(pd, pd_idx, user_flags);
    if (!pt) return;
    
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(virt);
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(pml4_table[pml4_idx] & PTE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4_table[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PTE_PRESENT)) return;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);
    
    pt[pt_idx] = 0;
    invlpg(virt);
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(pml4_table[pml4_idx] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4_table[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    // Check for 2MB huge page
    if (pd[pd_idx] & PTE_HUGE) {
        return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);
    }
    
    if (!(pd[pd_idx] & PTE_PRESENT)) return 0;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);
    
    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;
    return (pt[pt_idx] & PTE_ADDR_MASK) + (virt & 0xFFF);
}

void vmm_map_range(uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags) {
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_map_page(virt_start + offset, phys_start + offset, flags);
    }
}

// ---- Simple Kernel Heap ----
// Uses a simple free-list allocator

#define HEAP_START  0x400000   // 4MB
#define HEAP_SIZE   (16 * 1024 * 1024)  // 16MB heap
#define HEAP_END    (HEAP_START + HEAP_SIZE)

typedef struct heap_block {
    uint64_t size;
    struct heap_block *next;
    int free;
    uint64_t magic;  // 0xDEADBEEF for validation
} heap_block_t;

#define HEAP_MAGIC 0xDEADBEEFULL
#define BLOCK_SIZE sizeof(heap_block_t)

static heap_block_t *heap_head = NULL;

void heap_init(void) {
    heap_head = (heap_block_t *)HEAP_START;
    heap_head->size = HEAP_SIZE - BLOCK_SIZE;
    heap_head->next = NULL;
    heap_head->free = 1;
    heap_head->magic = HEAP_MAGIC;
    
    kprintf("  Heap: %d MB at %p\n", (int)(HEAP_SIZE / (1024*1024)), (uint64_t)HEAP_START);
}

static void split_block(heap_block_t *block, size_t size) {
    if (block->size >= size + BLOCK_SIZE + 64) {
        heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + BLOCK_SIZE + size);
        new_block->size = block->size - size - BLOCK_SIZE;
        new_block->next = block->next;
        new_block->free = 1;
        new_block->magic = HEAP_MAGIC;
        
        block->size = size;
        block->next = new_block;
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Align to 16 bytes
    size = (size + 15) & ~15ULL;
    
    heap_block_t *block = heap_head;
    while (block) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return (void *)((uint8_t *)block + BLOCK_SIZE);
        }
        block = block->next;
    }
    
    kprintf("kmalloc: out of memory (requested %d)\n", (int)size);
    return NULL;
}

void *kmalloc_aligned(size_t size, size_t align) {
    // Simple: allocate extra and align
    void *ptr = kmalloc(size + align);
    if (!ptr) return NULL;
    uint64_t addr = (uint64_t)ptr;
    addr = (addr + align - 1) & ~(align - 1);
    return (void *)addr;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - BLOCK_SIZE);
    if (block->magic != HEAP_MAGIC) return;
    
    block->free = 1;
    
    // Coalesce with next block
    if (block->next && block->next->free) {
        block->size += BLOCK_SIZE + block->next->size;
        block->next = block->next->next;
    }
}
