/* SPDX-License-Identifier: GPL-2.0-only */
#include "vfs.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"

// Device filesystem - /dev
// Provides character device nodes for console, null, zero, random, tty, serial

extern ssize_t tty_read(int tty_num, char *buf, size_t count);
extern ssize_t tty_write(int tty_num, const char *buf, size_t count);
extern void serial_driver_write(const char *data, size_t len);
extern int serial_driver_read(char *buf, size_t len);

#define DEVFS_MAX_DEVICES 32

typedef struct {
    vfs_node_t *devices[DEVFS_MAX_DEVICES];
    int device_count;
} devfs_data_t;

static devfs_data_t devfs_root_data;
static ino_t devfs_next_inode = 5000;

// /dev/null
static ssize_t null_read(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;(void)b;(void)c;(void)o; return 0;
}
static ssize_t null_write(vfs_node_t *n, const void *b, size_t c, off_t o) {
    (void)n;(void)b;(void)o; return c;
}

// /dev/zero
static ssize_t zero_read(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;(void)o; memset(b, 0, c); return c;
}

// /dev/random (pseudo-random using LFSR)
static uint64_t rng_state = 0xDEADBEEFCAFE1234ULL;
static ssize_t random_read(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;(void)o;
    uint8_t *p = (uint8_t *)b;
    extern uint64_t pit_get_ticks(void);
    rng_state ^= pit_get_ticks();
    for (size_t i = 0; i < c; i++) {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 7;
        rng_state ^= rng_state << 17;
        p[i] = (uint8_t)(rng_state & 0xFF);
    }
    return c;
}

// /dev/console and /dev/tty0
static ssize_t console_read(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;(void)o;
    return tty_read(0, (char *)b, c);
}
static ssize_t console_write_dev(vfs_node_t *n, const void *b, size_t c, off_t o) {
    (void)n;(void)o;
    return tty_write(0, (const char *)b, c);
}

// /dev/ttyS0 (serial)
static ssize_t serial_read_dev(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;(void)o;
    return serial_driver_read((char *)b, c);
}
static ssize_t serial_write_dev(vfs_node_t *n, const void *b, size_t c, off_t o) {
    (void)n;(void)o;
    serial_driver_write((const char *)b, c);
    return c;
}

// /dev/fb0 (framebuffer)
extern framebuffer_t fb_info;
static ssize_t fb_read(vfs_node_t *n, void *b, size_t c, off_t o) {
    (void)n;
    if (!fb_info.address) return -ENODEV;
    size_t fb_size = fb_info.pitch * fb_info.height;
    if ((size_t)o >= fb_size) return 0;
    if (o + c > fb_size) c = fb_size - o;
    memcpy(b, (uint8_t*)fb_info.address + o, c);
    return c;
}
static ssize_t fb_write(vfs_node_t *n, const void *b, size_t c, off_t o) {
    (void)n;
    if (!fb_info.address) return -ENODEV;
    size_t fb_size = fb_info.pitch * fb_info.height;
    if ((size_t)o >= fb_size) return 0;
    if (o + c > fb_size) c = fb_size - o;
    memcpy((uint8_t*)fb_info.address + o, b, c);
    return c;
}

// Devfs operations
static file_ops_t null_ops = { .read = null_read, .write = null_write };
static file_ops_t zero_ops = { .read = zero_read, .write = null_write };
static file_ops_t random_ops = { .read = random_read, .write = null_write };
static file_ops_t console_ops = { .read = console_read, .write = console_write_dev };
static file_ops_t serial_ops = { .read = serial_read_dev, .write = serial_write_dev };
static file_ops_t fb_ops = { .read = fb_read, .write = fb_write };

static vfs_node_t *devfs_create_device(const char *name, mode_t mode, file_ops_t *ops) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strncpy(node->name, name, NAME_MAX - 1);
    node->mode = S_IFCHR | mode;
    node->inode = devfs_next_inode++;
    node->nlink = 1;
    node->ops = ops;
    return node;
}

static vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    for (int i = 0; i < devfs_root_data.device_count; i++) {
        if (strcmp(devfs_root_data.devices[i]->name, name) == 0)
            return devfs_root_data.devices[i];
    }
    return NULL;
}

static int devfs_readdir(vfs_node_t *node, struct dirent *entry, uint32_t index) {
    (void)node;
    if (index >= (uint32_t)devfs_root_data.device_count) return -1;
    entry->d_ino = devfs_root_data.devices[index]->inode;
    strncpy(entry->d_name, devfs_root_data.devices[index]->name, NAME_MAX - 1);
    entry->d_type = 2;  // DT_CHR
    return 0;
}

static file_ops_t devfs_dir_ops = {
    .readdir = devfs_readdir,
    .finddir = devfs_finddir,
};

static vfs_node_t *devfs_mount(const char *device, const char *options) {
    (void)device; (void)options;

    memset(&devfs_root_data, 0, sizeof(devfs_root_data));

    // Create device nodes
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("null", 0666, &null_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("zero", 0666, &zero_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("random", 0444, &random_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("urandom", 0444, &random_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("console", 0600, &console_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("tty0", 0600, &console_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("tty1", 0600, &console_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("ttyS0", 0660, &serial_ops);
    devfs_root_data.devices[devfs_root_data.device_count++] = devfs_create_device("fb0", 0660, &fb_ops);

    vfs_node_t *root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(root->name, "dev");
    root->mode = S_IFDIR | 0755;
    root->inode = devfs_next_inode++;
    root->nlink = 2;
    root->ops = &devfs_dir_ops;
    return root;
}

filesystem_t devfs_type = { .name = "devfs", .mount = devfs_mount, .umount = NULL };

void devfs_init(void) {
    vfs_register_fs(&devfs_type);
}
