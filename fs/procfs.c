/* SPDX-License-Identifier: GPL-2.0-only */
#include "vfs.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

// Simple /proc filesystem

extern uint64_t pit_get_ticks(void);
extern int smp_get_cpu_count(void);

static ino_t procfs_next_inode = 8000;

typedef struct {
    char name[NAME_MAX];
    ssize_t (*generate)(char *buf, size_t size);
    ino_t inode;
} procfs_entry_t;

#define PROCFS_MAX_ENTRIES 16
static procfs_entry_t procfs_entries[PROCFS_MAX_ENTRIES];
static int procfs_entry_count = 0;

// /proc/version
static ssize_t proc_version(char *buf, size_t size) {
    const char *ver = "nexusOS v0.01 (x86_64) clang/llvm SMP\n";
    size_t len = strlen(ver);
    if (len > size) len = size;
    memcpy(buf, ver, len);
    return len;
}

// /proc/uptime
static ssize_t proc_uptime(char *buf, size_t size) {
    uint64_t ticks = pit_get_ticks();
    uint64_t seconds = ticks / 100;
    char tmp[64];
    itoa(seconds, tmp, 10);
    strcat(tmp, ".00 ");
    char tmp2[32];
    itoa(seconds, tmp2, 10);
    strcat(tmp, tmp2);
    strcat(tmp, ".00\n");
    size_t len = strlen(tmp);
    if (len > size) len = size;
    memcpy(buf, tmp, len);
    return len;
}

// /proc/meminfo
static ssize_t proc_meminfo(char *buf, size_t size) {
    uint64_t total = pmm_get_total_memory() / 1024; // KB
    uint64_t free_mem = pmm_get_free_memory() / 1024;
    char tmp[512];
    tmp[0] = '\0';
    strcat(tmp, "MemTotal:    ");
    char num[32];
    utoa(total, num, 10);
    strcat(tmp, num);
    strcat(tmp, " kB\nMemFree:     ");
    utoa(free_mem, num, 10);
    strcat(tmp, num);
    strcat(tmp, " kB\nMemAvailable:");
    utoa(free_mem, num, 10);
    strcat(tmp, num);
    strcat(tmp, " kB\n");
    size_t len = strlen(tmp);
    if (len > size) len = size;
    memcpy(buf, tmp, len);
    return len;
}

// /proc/cpuinfo
static ssize_t proc_cpuinfo(char *buf, size_t size) {
    int cpus = smp_get_cpu_count();
    char tmp[512];
    tmp[0] = '\0';

    for (int i = 0; i < cpus && strlen(tmp) < 400; i++) {
        strcat(tmp, "processor\t: ");
        char num[8]; itoa(i, num, 10);
        strcat(tmp, num);
        strcat(tmp, "\nmodel name\t: nexusOS Virtual CPU\ncpu MHz\t\t: 2000.000\n\n");
    }
    size_t len = strlen(tmp);
    if (len > size) len = size;
    memcpy(buf, tmp, len);
    return len;
}

// /proc/stat
static ssize_t proc_stat(char *buf, size_t size) {
    uint64_t ticks = pit_get_ticks();
    char tmp[256];
    tmp[0] = '\0';
    strcat(tmp, "cpu  ");
    char num[32]; utoa(ticks, num, 10);
    strcat(tmp, num);
    strcat(tmp, " 0 0 0 0 0 0 0 0 0\n");
    size_t len = strlen(tmp);
    if (len > size) len = size;
    memcpy(buf, tmp, len);
    return len;
}

static ssize_t procfs_read(vfs_node_t *node, void *buf, size_t count, off_t offset) {
    procfs_entry_t *entry = (procfs_entry_t *)node->private_data;
    if (!entry || !entry->generate) return 0;

    char tmp[1024];
    ssize_t generated = entry->generate(tmp, sizeof(tmp));
    if (generated <= 0) return 0;
    if (offset >= generated) return 0;
    if (offset + (ssize_t)count > generated) count = generated - offset;
    memcpy(buf, tmp + offset, count);
    return count;
}

static vfs_node_t *procfs_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    for (int i = 0; i < procfs_entry_count; i++) {
        if (strcmp(procfs_entries[i].name, name) == 0) {
            vfs_node_t *n = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
            strncpy(n->name, name, NAME_MAX - 1);
            n->inode = procfs_entries[i].inode;
            n->mode = S_IFREG | 0444;
            n->ops = (file_ops_t *)kzalloc(sizeof(file_ops_t));
            n->ops->read = procfs_read;
            n->private_data = &procfs_entries[i];
            return n;
        }
    }
    return NULL;
}

static int procfs_readdir(vfs_node_t *node, struct dirent *entry, uint32_t index) {
    (void)node;
    if (index >= (uint32_t)procfs_entry_count) return -1;
    entry->d_ino = procfs_entries[index].inode;
    strncpy(entry->d_name, procfs_entries[index].name, NAME_MAX - 1);
    entry->d_type = 8;
    return 0;
}

static file_ops_t procfs_dir_ops = {
    .readdir = procfs_readdir,
    .finddir = procfs_finddir,
};

static void procfs_add_entry(const char *name, ssize_t (*gen)(char *, size_t)) {
    if (procfs_entry_count >= PROCFS_MAX_ENTRIES) return;
    procfs_entry_t *e = &procfs_entries[procfs_entry_count++];
    strncpy(e->name, name, NAME_MAX - 1);
    e->generate = gen;
    e->inode = procfs_next_inode++;
}

static vfs_node_t *procfs_mount(const char *device, const char *options) {
    (void)device; (void)options;

    procfs_entry_count = 0;
    procfs_add_entry("version", proc_version);
    procfs_add_entry("uptime", proc_uptime);
    procfs_add_entry("meminfo", proc_meminfo);
    procfs_add_entry("cpuinfo", proc_cpuinfo);
    procfs_add_entry("stat", proc_stat);

    vfs_node_t *root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(root->name, "proc");
    root->mode = S_IFDIR | 0555;
    root->inode = procfs_next_inode++;
    root->nlink = 2;
    root->ops = &procfs_dir_ops;
    return root;
}

filesystem_t procfs_type = { .name = "proc", .mount = procfs_mount, .umount = NULL };

void procfs_init(void) {
    vfs_register_fs(&procfs_type);
}
