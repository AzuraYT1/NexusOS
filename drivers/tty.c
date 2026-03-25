/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"

// TTY subsystem
#define TTY_MAX 4
#define TTY_BUF_SIZE 4096

typedef struct {
    char input_buf[TTY_BUF_SIZE];
    volatile uint32_t input_read, input_write;
    char line_buf[1024];
    uint32_t line_pos;
    int active;
    int echo;
    int canonical;  // Line buffered mode
} tty_t;

static tty_t ttys[TTY_MAX];
static int current_tty = 0;

void tty_init(void) {
    for (int i = 0; i < TTY_MAX; i++) {
        memset(&ttys[i], 0, sizeof(tty_t));
        ttys[i].active = 0;
        ttys[i].echo = 1;
        ttys[i].canonical = 1;
    }
    ttys[0].active = 1;  // Activate first TTY
    kprintf("  TTY subsystem initialized (%d terminals)\n", TTY_MAX);
}

void tty_input_char(int tty_num, char c) {
    if (tty_num < 0 || tty_num >= TTY_MAX) return;
    tty_t *tty = &ttys[tty_num];
    if (!tty->active) return;
    
    if (tty->canonical) {
        if (c == '\b' || c == 127) {
            if (tty->line_pos > 0) {
                tty->line_pos--;
                if (tty->echo) {
                    console_putchar('\b');
                    console_putchar(' ');
                    console_putchar('\b');
                    serial_putchar('\b');
                    serial_putchar(' ');
                    serial_putchar('\b');
                }
            }
            return;
        }
        
        if (c == '\n' || c == '\r') {
            tty->line_buf[tty->line_pos++] = '\n';
            // Copy line to input buffer
            for (uint32_t i = 0; i < tty->line_pos; i++) {
                uint32_t next = (tty->input_write + 1) % TTY_BUF_SIZE;
                if (next != tty->input_read) {
                    tty->input_buf[tty->input_write] = tty->line_buf[i];
                    tty->input_write = next;
                }
            }
            tty->line_pos = 0;
            if (tty->echo) {
                console_putchar('\n');
                serial_putchar('\n');
            }
            return;
        }
        
        if (tty->line_pos < sizeof(tty->line_buf) - 2) {
            tty->line_buf[tty->line_pos++] = c;
            if (tty->echo) {
                console_putchar(c);
                serial_putchar(c);
            }
        }
    } else {
        // Raw mode: put directly in buffer
        uint32_t next = (tty->input_write + 1) % TTY_BUF_SIZE;
        if (next != tty->input_read) {
            tty->input_buf[tty->input_write] = c;
            tty->input_write = next;
        }
        if (tty->echo) {
            console_putchar(c);
            serial_putchar(c);
        }
    }
}

ssize_t tty_read(int tty_num, char *buf, size_t count) {
    if (tty_num < 0 || tty_num >= TTY_MAX) return -EBADF;
    tty_t *tty = &ttys[tty_num];
    
    // Wait for data
    while (tty->input_read == tty->input_write) {
        __asm__ volatile("hlt");
    }
    
    size_t read = 0;
    while (read < count && tty->input_read != tty->input_write) {
        buf[read++] = tty->input_buf[tty->input_read];
        tty->input_read = (tty->input_read + 1) % TTY_BUF_SIZE;
        // In canonical mode, stop after newline
        if (tty->canonical && buf[read-1] == '\n') break;
    }
    
    return read;
}

ssize_t tty_write(int tty_num, const char *buf, size_t count) {
    if (tty_num < 0 || tty_num >= TTY_MAX) return -EBADF;
    
    for (size_t i = 0; i < count; i++) {
        console_putchar(buf[i]);
        serial_putchar(buf[i]);
    }
    return count;
}

int tty_get_current(void) { return current_tty; }
void tty_set_current(int n) { if (n >= 0 && n < TTY_MAX) current_tty = n; }
