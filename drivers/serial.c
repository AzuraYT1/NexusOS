/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/idt.h"
#include "../kernel/console.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern void pic_unmask(uint8_t irq);

#define COM1 0x3F8
#define COM1_IRQ 4

#define SERIAL_BUF_SIZE 256
static char serial_buf[SERIAL_BUF_SIZE];
static volatile uint32_t serial_read_pos = 0, serial_write_pos = 0;

static void serial_irq_handler(registers_t *regs) {
    (void)regs;
    while (inb(COM1 + 5) & 1) {
        char c = inb(COM1);
        uint32_t next = (serial_write_pos + 1) % SERIAL_BUF_SIZE;
        if (next != serial_read_pos) {
            serial_buf[serial_write_pos] = c;
            serial_write_pos = next;
        }
    }
}

void serial_driver_init(void) {
    // COM1 is already initialized in console.c serial_init()
    // Enable receive interrupts
    outb(COM1 + 1, 0x01);  // Enable data available interrupt
    
    register_interrupt_handler(36, serial_irq_handler);  // IRQ4 -> INT 36
    pic_unmask(COM1_IRQ);
    
    kprintf("  Serial COM1 driver loaded (38400 baud)\n");
}

void serial_driver_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        while (!(inb(COM1 + 5) & 0x20));
        outb(COM1, data[i]);
    }
}

int serial_driver_read(char *buf, size_t len) {
    size_t count = 0;
    while (count < len && serial_read_pos != serial_write_pos) {
        buf[count++] = serial_buf[serial_read_pos];
        serial_read_pos = (serial_read_pos + 1) % SERIAL_BUF_SIZE;
    }
    return count;
}

int serial_driver_available(void) {
    return serial_read_pos != serial_write_pos;
}
