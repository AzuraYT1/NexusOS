/* SPDX-License-Identifier: GPL-2.0-only */
#include "vfs.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"

// Simple RAM filesystem - used for ramfs, tmpfs
// Each node stores data in a dynamically allocated buffer

#define RAMFS_MAX_CHILDREN 128

typedef struct ramfs_data {
    uint8_t *data;
    size_t capacity;
    vfs_node_t *children[RAMFS_MAX_CHILDREN];
    int child_count;
} ramfs_data_t;

static ino_t ramfs_next_inode = 1000;

static ssize_t ramfs_read(vfs_node_t *node, void *buf, size_t count, off_t offset) {
    ramfs_data_t *rd = (ramfs_data_t *)node->private_data;
    if (!rd || !rd->data) return 0;
    if (offset >= node->size) return 0;
    if (offset + (off_t)count > node->size) count = node->size - offset;
    memcpy(buf, rd->data + offset, count);
    return count;
}

static ssize_t ramfs_write(vfs_node_t *node, const void *buf, size_t count, off_t offset) {
    ramfs_data_t *rd = (ramfs_data_t *)node->private_data;
    if (!rd) return -EIO;

    size_t needed = offset + count;
    if (needed > rd->capacity) {
        size_t new_cap = needed < 4096 ? 4096 : needed * 2;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -ENOMEM;
        if (rd->data) {
            memcpy(new_data, rd->data, node->size);
            kfree(rd->data);
        }
        rd->data = new_data;
        rd->capacity = new_cap;
    }

    memcpy(rd->data + offset, buf, count);
    if ((off_t)(offset + count) > node->size)
        node->size = offset + count;
    return count;
}

static int ramfs_truncate(vfs_node_t *node, off_t length) {
    ramfs_data_t *rd = (ramfs_data_t *)node->private_data;
    if (!rd) return -EIO;
    if ((size_t)length > rd->capacity) {
        size_t new_cap = length < 4096 ? 4096 : (size_t)length;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -ENOMEM;
        if (rd->data) {
            size_t copy = node->size < length ? node->size : length;
            memcpy(new_data, rd->data, copy);
            kfree(rd->data);
        }
        rd->data = new_data;
        rd->capacity = new_cap;
    }
    node->size = length;
    return 0;
}

static vfs_node_t *ramfs_finddir(vfs_node_t *node, const char *name) {
    ramfs_data_t *rd = (ramfs_data_t *)node->private_data;
    if (!rd) return NULL;
    for (int i = 0; i < rd->child_count; i++) {
        if (strcmp(rd->children[i]->name, name) == 0)
            return rd->children[i];
    }
    return NULL;
}

static int ramfs_readdir(vfs_node_t *node, struct dirent *entry, uint32_t index) {
    ramfs_data_t *rd = (ramfs_data_t *)node->private_data;
    if (!rd || index >= (uint32_t)rd->child_count) return -1;

    entry->d_ino = rd->children[index]->inode;
    strncpy(entry->d_name, rd->children[index]->name, NAME_MAX - 1);
    entry->d_type = S_ISDIR(rd->children[index]->mode) ? 4 : 8;
    return 0;
}

static vfs_node_t *ramfs_create_node(const char *name, mode_t mode) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    ramfs_data_t *rd = (ramfs_data_t *)kzalloc(sizeof(ramfs_data_t));

    strncpy(node->name, name, NAME_MAX - 1);
    node->mode = mode;
    node->inode = ramfs_next_inode++;
    node->nlink = 1;
    node->private_data = rd;

    static file_ops_t ramfs_ops = {
        .read = ramfs_read,
        .write = ramfs_write,
        .readdir = ramfs_readdir,
        .finddir = ramfs_finddir,
        .create = NULL,  // Set below
        .mkdir = NULL,
        .unlink = NULL,
        .truncate = ramfs_truncate,
    };
    node->ops = &ramfs_ops;
    return node;
}

static int ramfs_create(vfs_node_t *parent, const char *name, mode_t mode);
static int ramfs_mkdir_op(vfs_node_t *parent, const char *name, mode_t mode);
static int ramfs_unlink_op(vfs_node_t *parent, const char *name);

static file_ops_t ramfs_file_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .readdir = ramfs_readdir,
    .finddir = ramfs_finddir,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir_op,
    .unlink = ramfs_unlink_op,
    .truncate = ramfs_truncate,
};

static int ramfs_create(vfs_node_t *parent, const char *name, mode_t mode) {
    ramfs_data_t *rd = (ramfs_data_t *)parent->private_data;
    if (!rd || rd->child_count >= RAMFS_MAX_CHILDREN) return -ENOSPC;

    // Check if exists
    if (ramfs_finddir(parent, name)) return -EEXIST;

    vfs_node_t *node = ramfs_create_node(name, mode);
    node->ops = &ramfs_file_ops;
    node->parent = parent;
    rd->children[rd->child_count++] = node;
    return 0;
}

static int ramfs_mkdir_op(vfs_node_t *parent, const char *name, mode_t mode) {
    return ramfs_create(parent, name, S_IFDIR | (mode & 0777));
}

static int ramfs_unlink_op(vfs_node_t *parent, const char *name) {
    ramfs_data_t *rd = (ramfs_data_t *)parent->private_data;
    if (!rd) return -EIO;

    for (int i = 0; i < rd->child_count; i++) {
        if (strcmp(rd->children[i]->name, name) == 0) {
            // Remove from children array
            for (int j = i; j < rd->child_count - 1; j++)
                rd->children[j] = rd->children[j+1];
            rd->child_count--;
            return 0;
        }
    }
    return -ENOENT;
}

static vfs_node_t *ramfs_mount(const char *device, const char *options) {
    (void)device; (void)options;
    vfs_node_t *root = ramfs_create_node("/", S_IFDIR | 0755);
    root->ops = &ramfs_file_ops;
    root->nlink = 2;
    return root;
}

static int ramfs_umount(vfs_node_t *root) {
    (void)root;
    return 0;
}

// Register both ramfs and tmpfs (tmpfs = ramfs in our implementation)
filesystem_t ramfs_type = { .name = "ramfs", .mount = ramfs_mount, .umount = ramfs_umount };
filesystem_t tmpfs_type = { .name = "tmpfs", .mount = ramfs_mount, .umount = ramfs_umount };

void ramfs_init(void) {
    vfs_register_fs(&ramfs_type);
    vfs_register_fs(&tmpfs_type);
}
