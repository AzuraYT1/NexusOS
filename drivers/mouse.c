/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/idt.h"
#include "../kernel/console.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern void pic_unmask(uint8_t irq);

static int mouse_x = 0, mouse_y = 0;
static int mouse_buttons = 0;
static uint8_t mouse_cycle = 0;
static int8_t mouse_bytes[3];

static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout--) {
        if (!(inb(0x64) & 2)) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 1) return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(0x64, 0xD4);  // Send to mouse
    mouse_wait_write();
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(0x60);
}

static void mouse_handler(registers_t *regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) return;  // Not from mouse
    
    int8_t data = inb(0x60);
    
    switch (mouse_cycle) {
        case 0:
            mouse_bytes[0] = data;
            if (data & 0x08) mouse_cycle++;  // Validate first byte
            break;
        case 1:
            mouse_bytes[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_bytes[2] = data;
            mouse_cycle = 0;
            
            // Process packet
            mouse_buttons = mouse_bytes[0] & 0x07;
            mouse_x += mouse_bytes[1];
            mouse_y -= mouse_bytes[2];
            
            // Clamp
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1023) mouse_x = 1023;
            if (mouse_y > 767) mouse_y = 767;
            break;
    }
}

void mouse_init(void) {
    // Enable auxiliary mouse device
    mouse_wait_write();
    outb(0x64, 0xA8);  // Enable aux PS/2 port
    
    // Enable interrupts
    mouse_wait_write();
    outb(0x64, 0x20);  // Get compaq status
    mouse_wait_read();
    uint8_t status = inb(0x60);
    status |= 2;       // Enable IRQ12
    status &= ~0x20;   // Enable mouse clock
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);
    
    // Reset mouse
    mouse_write(0xFF);
    mouse_read();  // ACK
    mouse_read();  // Self-test result
    mouse_read();  // Mouse ID
    
    // Set defaults
    mouse_write(0xF6);
    mouse_read();
    
    // Enable data reporting
    mouse_write(0xF4);
    mouse_read();
    
    register_interrupt_handler(44, mouse_handler);
    pic_unmask(12);
    
    kprintf("  PS/2 Mouse driver loaded\n");
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }
