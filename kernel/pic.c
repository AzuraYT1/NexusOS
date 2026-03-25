/* SPDX-License-Identifier: GPL-2.0-only */
#include "types.h"
#include "console.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

// PIC ports
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init(void) {
    // ICW1: begin initialization sequence
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    // ICW2: remap IRQ offsets
    outb(PIC1_DATA, 0x20);  // Master: IRQ 0-7  -> INT 32-39
    outb(PIC2_DATA, 0x28);  // Slave:  IRQ 8-15 -> INT 40-47

    // ICW3: tell Master about Slave on IRQ2
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // Mask all IRQs except cascading (IRQ2)
    outb(PIC1_DATA, 0xFB);  // 1111 1011 - only IRQ2 unmasked
    outb(PIC2_DATA, 0xFF);  // all masked

    kprintf("  PIC remapped (IRQ 0-15 -> INT 32-47)\n");
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
