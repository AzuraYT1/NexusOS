/* SPDX-License-Identifier: GPL-2.0-only */
#include "types.h"
#include "idt.h"
#include "console.h"

extern void outb(uint16_t port, uint8_t val);
extern void pic_unmask(uint8_t irq);

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182

static volatile uint64_t tick_count = 0;
static uint32_t pit_frequency = 100; // Hz

static void pit_handler(registers_t *regs) {
    (void)regs;
    tick_count++;
}

void pit_init(uint32_t freq) {
    pit_frequency = freq;
    uint32_t divisor = PIT_FREQ / freq;

    outb(PIT_CMD, 0x36);  // Channel 0, lobyte/hibyte, rate generator
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    register_interrupt_handler(32, pit_handler);
    pic_unmask(0);

    kprintf("  PIT initialized at %d Hz\n", freq);
}

uint64_t pit_get_ticks(void) {
    return tick_count;
}

void pit_sleep(uint32_t ms) {
    uint64_t target = tick_count + (ms * pit_frequency / 1000);
    while (tick_count < target) {
        __asm__ volatile("hlt");
    }
}
