/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/idt.h"
#include "../kernel/console.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern void pic_unmask(uint8_t irq);

// Keyboard buffer
#define KB_BUF_SIZE 256
static char kb_buffer[KB_BUF_SIZE];
static volatile uint32_t kb_read = 0, kb_write = 0;
static int shift_held = 0, ctrl_held = 0, caps_lock = 0;

// US QWERTY scancode -> ASCII
static const char scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0, 0,0,0,0,0,0,0,0,0,0, 0,0,
    0,0,0,'-', 0,0,0,'+', 0,0,0,0,0, 0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_shift_table[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0, 0,0,0,0,0,0,0,0,0,0, 0,0,
    0,0,0,'-', 0,0,0,'+', 0,0,0,0,0, 0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);
    
    // Key release
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36) shift_held = 0;
        if (key == 0x1D) ctrl_held = 0;
        return;
    }
    
    // Key press
    if (scancode == 0x2A || scancode == 0x36) { shift_held = 1; return; }
    if (scancode == 0x1D) { ctrl_held = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }
    
    char c = 0;
    if (shift_held) {
        c = scancode_shift_table[scancode];
    } else {
        c = scancode_table[scancode];
    }
    
    // Caps lock affects letters only
    if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
    else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;
    
    // Ctrl+C
    if (ctrl_held && c == 'c') c = 3;
    // Ctrl+D
    if (ctrl_held && c == 'd') c = 4;
    
    if (c) {
        uint32_t next = (kb_write + 1) % KB_BUF_SIZE;
        if (next != kb_read) {
            kb_buffer[kb_write] = c;
            kb_write = next;
        }
    }
}

void keyboard_init(void) {
    // Clear buffer
    while (inb(0x64) & 1) inb(0x60);
    
    register_interrupt_handler(33, keyboard_handler);
    pic_unmask(1);
    kprintf("  PS/2 Keyboard driver loaded\n");
}

int keyboard_read(void) {
    if (kb_read == kb_write) return -1;
    char c = kb_buffer[kb_read];
    kb_read = (kb_read + 1) % KB_BUF_SIZE;
    return c;
}

int keyboard_available(void) {
    return kb_read != kb_write;
}
