/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/console.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

#define JOYSTICK_PORT 0x201

typedef struct {
    int x, y;
    int buttons;
    int present;
} joystick_state_t;

static joystick_state_t joystick;

void joystick_init(void) {
    // Try to detect joystick on standard game port
    outb(JOYSTICK_PORT, 0xFF);
    
    // Read back - if all bits high after timeout, no joystick
    int detected = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t val = inb(JOYSTICK_PORT);
        if ((val & 0x0F) != 0x0F) {
            detected = 1;
            break;
        }
    }
    
    joystick.present = detected;
    joystick.x = 128;
    joystick.y = 128;
    joystick.buttons = 0;
    
    if (detected) {
        kprintf("  Game port joystick detected\n");
    } else {
        kprintf("  Game port joystick: not detected (port 0x201)\n");
    }
}

void joystick_poll(void) {
    if (!joystick.present) return;
    
    uint8_t val = inb(JOYSTICK_PORT);
    joystick.buttons = (~val >> 4) & 0x0F;  // Buttons are active-low
    
    // Axis reading (simplified - would need timer-based reading for accuracy)
    outb(JOYSTICK_PORT, 0xFF);
    int x_count = 0, y_count = 0;
    for (int i = 0; i < 1000; i++) {
        val = inb(JOYSTICK_PORT);
        if (val & 0x01) x_count++;
        if (val & 0x02) y_count++;
        if (!(val & 0x03)) break;
    }
    joystick.x = x_count;
    joystick.y = y_count;
}

int joystick_get_x(void) { return joystick.x; }
int joystick_get_y(void) { return joystick.y; }
int joystick_get_buttons(void) { return joystick.buttons; }
int joystick_present(void) { return joystick.present; }
