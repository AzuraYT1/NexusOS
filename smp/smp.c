/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/console.h"
#include "../kernel/string.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern uint64_t rdmsr_safe(uint32_t msr);
extern void wrmsr_safe(uint32_t msr, uint64_t value);
extern uint32_t read_lapic_id(void);

// LAPIC registers
#define LAPIC_BASE_MSR    0x1B
#define LAPIC_ID          0x020
#define LAPIC_VER         0x030
#define LAPIC_TPR         0x080
#define LAPIC_EOI         0x0B0
#define LAPIC_SVR         0x0F0
#define LAPIC_ICR_LOW     0x300
#define LAPIC_ICR_HIGH    0x310
#define LAPIC_TIMER_LVT   0x320
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_CUR   0x390
#define LAPIC_TIMER_DIV   0x3E0

static volatile uint32_t *lapic_base = NULL;
static int cpu_count = 1;  // BSP is always 1
static volatile int ap_started = 0;
static int bsp_id = 0;

// Detect LAPIC base address
static uint64_t get_lapic_base(void) {
    uint64_t msr = rdmsr_safe(LAPIC_BASE_MSR);
    return msr & 0xFFFFF000ULL;
}

static void lapic_write(uint32_t reg, uint32_t val) {
    if (lapic_base)
        lapic_base[reg / 4] = val;
}

static uint32_t lapic_read(uint32_t reg) {
    if (lapic_base)
        return lapic_base[reg / 4];
    return 0;
}

// AP entry point (called from boot.asm trampoline)
void ap_main(uint64_t cpu_id) {
    ap_started++;
    kprintf("  CPU %d online\n", (int)cpu_id);

    // Enable LAPIC for this AP
    if (lapic_base) {
        lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | 0x100);
    }

    // AP idle loop
    for (;;) {
        __asm__ volatile("hlt");
    }
}

// Parse ACPI MADT to find CPU entries
// Simplified: just scan for MADT in memory
static int detect_cpus_from_acpi(void) {
    // Search for RSDP in BIOS area
    // In QEMU, ACPI tables are usually at 0xE0000-0xFFFFF
    int found_cpus = 0;

    for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((void *)addr, "RSD PTR ", 8) == 0) {
            uint32_t rsdt_addr = *(uint32_t *)(addr + 16);

            // Parse RSDT for MADT
            uint32_t *rsdt = (uint32_t *)(uint64_t)rsdt_addr;
            if (memcmp(rsdt, "RSDT", 4) != 0) continue;

            uint32_t rsdt_len = rsdt[1];
            int entries = (rsdt_len - 36) / 4;

            for (int i = 0; i < entries; i++) {
                uint32_t table_addr = rsdt[9 + i]; // entries start at offset 36
                uint32_t *table = (uint32_t *)(uint64_t)table_addr;

                if (memcmp(table, "APIC", 4) == 0) {
                    // MADT found - parse entries
                    uint32_t madt_len = table[1];
                    uint8_t *entry = (uint8_t *)table + 44;
                    uint8_t *end = (uint8_t *)table + madt_len;

                    while (entry < end) {
                        uint8_t type = entry[0];
                        uint8_t len = entry[1];
                        if (len == 0) break;

                        if (type == 0) {  // Processor Local APIC
                            uint8_t apic_id = entry[3];
                            uint32_t flags = *(uint32_t *)(entry + 4);
                            if (flags & 1) {  // Enabled
                                found_cpus++;
                                (void)apic_id;
                            }
                        }
                        entry += len;
                    }
                    break;
                }
            }
            break;
        }
    }

    return found_cpus > 0 ? found_cpus : 1;
}

void smp_init(void) {
    // Get LAPIC base
    uint64_t base = get_lapic_base();
    if (base == 0 || base == 0xFFFFF000ULL) {
        base = 0xFEE00000ULL;  // Default LAPIC base address
    }
    lapic_base = (volatile uint32_t *)base;
    bsp_id = read_lapic_id();

    kprintf("  SMP: BSP LAPIC ID=%d, base=%p\n", bsp_id, base);

    // Enable LAPIC
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | 0x100);

    // Detect CPUs via ACPI
    int detected = detect_cpus_from_acpi();
    kprintf("  SMP: Detected %d CPU(s) via ACPI\n", detected);

    if (detected > 1 && base >= 0xFEE00000ULL) {
        // Copy AP trampoline to 0x7000
        extern uint8_t ap_trampoline_start[];
        extern uint8_t ap_trampoline_end[];
        size_t trampoline_size = (size_t)(ap_trampoline_end - ap_trampoline_start);
        if (trampoline_size > 0 && trampoline_size < 4096) {
            memcpy((void *)0x7000, ap_trampoline_start, trampoline_size);
        }

        // Store GDT pointer at 0x7100
        extern uint64_t gdt64[];
        struct PACKED { uint16_t limit; uint64_t base; } gdt_ptr;
        gdt_ptr.limit = 7 * 8 - 1;
        gdt_ptr.base = (uint64_t)gdt64;
        memcpy((void *)0x7100, &gdt_ptr, sizeof(gdt_ptr));

        // Store CR3 at 0x7200
        extern uint64_t read_cr3(void);
        uint64_t cr3 = read_cr3();
        *(uint64_t *)0x7200 = cr3;

        // Send INIT IPI to all APs
        lapic_write(LAPIC_ICR_HIGH, 0);
        lapic_write(LAPIC_ICR_LOW, 0x000C4500);  // INIT, all excluding self

        // Wait 10ms
        for (volatile int i = 0; i < 1000000; i++);

        // Send SIPI (Startup IPI) with vector 0x07 (=> real mode address 0x7000)
        lapic_write(LAPIC_ICR_HIGH, 0);
        lapic_write(LAPIC_ICR_LOW, 0x000C4607);  // SIPI, all excluding self, vector=7

        // Wait for APs to start
        for (volatile int i = 0; i < 5000000; i++);

        // Send second SIPI if needed
        if (ap_started < detected - 1) {
            lapic_write(LAPIC_ICR_HIGH, 0);
            lapic_write(LAPIC_ICR_LOW, 0x000C4607);
            for (volatile int i = 0; i < 5000000; i++);
        }

        cpu_count = 1 + ap_started;
        kprintf("  SMP: %d CPU(s) active\n", cpu_count);
    } else {
        cpu_count = detected > 0 ? detected : 1;
        kprintf("  SMP: %d CPU(s) (AP startup skipped)\n", cpu_count);
    }
}

int smp_get_cpu_count(void) {
    return cpu_count;
}

int smp_get_bsp_id(void) {
    return bsp_id;
}
