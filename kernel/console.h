/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_CONSOLE_H
#define NEXUS_CONSOLE_H

#include "types.h"

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *s);
void console_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch);
void console_putchar(char c);
void console_write(const char *s);
void console_clear(void);
void kprintf(const char *fmt, ...);
void console_set_color(uint32_t fg, uint32_t bg);

// Framebuffer info
typedef struct {
    uint32_t *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  // bytes per line
    uint32_t bpp;
} framebuffer_t;

extern framebuffer_t fb_info;

#endif
