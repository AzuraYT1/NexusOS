/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_STRING_H
#define NEXUS_STRING_H

#include "types.h"

static inline void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static inline void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static inline int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static inline char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

static inline char *strcat(char *dest, const char *src) {
    char *d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

static inline char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (char *)last;
}

static inline int atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

// Integer to string
static inline void itoa(int64_t value, char *str, int base) {
    char *p = str;
    char *p1, *p2;
    uint64_t uvalue;
    int negative = 0;

    if (base == 10 && value < 0) {
        negative = 1;
        uvalue = (uint64_t)(-(value + 1)) + 1;
    } else {
        uvalue = (uint64_t)value;
    }

    do {
        uint64_t remainder = uvalue % base;
        *p++ = (remainder < 10) ? remainder + '0' : remainder - 10 + 'a';
    } while (uvalue /= base);

    if (negative) *p++ = '-';
    *p = '\0';

    // Reverse
    p1 = str; p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        p1++; p2--;
    }
}

static inline void utoa(uint64_t value, char *str, int base) {
    char *p = str;
    char *p1, *p2;
    do {
        uint64_t remainder = value % base;
        *p++ = (remainder < 10) ? remainder + '0' : remainder - 10 + 'a';
    } while (value /= base);
    *p = '\0';
    p1 = str; p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1; *p1 = *p2; *p2 = tmp;
        p1++; p2--;
    }
}

#endif
