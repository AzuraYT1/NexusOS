/* SPDX-License-Identifier: GPL-2.0-only */
#include "vfs.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"

// Ext2 filesystem implementation (in-memory ramdisk based)
// Supports: read, write, permissions, mount, subdirectories

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_BLOCK_SIZE 1024
#define EXT2_ROOT_INO 2

// Ext2 on-disk structures
typedef struct PACKED {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // Extended fields
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint8_t  s_padding[934];
} ext2_superblock_t;

typedef struct PACKED {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc_t;

typedef struct PACKED {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

typedef struct PACKED {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[256];
} ext2_dir_entry_t;

// In-memory ext2 filesystem
// We simulate a small ext2 filesystem in RAM
#define EXT2_MAX_FILES 256
#define EXT2_MAX_DATA  (1024 * 1024)  // 1MB max per file

typedef struct ext2_mem_file {
    char path[PATH_MAX];
    char name[NAME_MAX];
    uint32_t inode_num;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint32_t parent_inode;
    int is_dir;
    struct ext2_mem_file *children[64];
    int child_count;
} ext2_mem_file_t;

typedef struct {
    ext2_mem_file_t *root;
    ext2_mem_file_t files[EXT2_MAX_FILES];
    int file_count;
    uint32_t next_inode;
} ext2_fs_t;

static ext2_fs_t ext2_instance;

static ext2_mem_file_t *ext2_alloc_file(ext2_fs_t *fs) {
    if (fs->file_count >= EXT2_MAX_FILES) return NULL;
    ext2_mem_file_t *f = &fs->files[fs->file_count++];
    memset(f, 0, sizeof(ext2_mem_file_t));
    f->inode_num = fs->next_inode++;
    return f;
}

static ext2_mem_file_t *ext2_find_child(ext2_mem_file_t *dir, const char *name) {
    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

// VFS operations for ext2
static ssize_t ext2_read(vfs_node_t *node, void *buf, size_t count, off_t offset) {
    ext2_mem_file_t *f = (ext2_mem_file_t *)node->private_data;
    if (!f || !f->data) return 0;
    if (offset >= (off_t)f->size) return 0;
    if (offset + (off_t)count > (off_t)f->size) count = f->size - offset;
    memcpy(buf, f->data + offset, count);
    return count;
}

static ssize_t ext2_write(vfs_node_t *node, const void *buf, size_t count, off_t offset) {
    ext2_mem_file_t *f = (ext2_mem_file_t *)node->private_data;
    if (!f) return -EIO;

    size_t needed = offset + count;
    if (needed > f->capacity) {
        size_t new_cap = needed < 4096 ? 4096 : needed * 2;
        if (new_cap > EXT2_MAX_DATA) new_cap = EXT2_MAX_DATA;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -ENOMEM;
        if (f->data) {
            memcpy(new_data, f->data, f->size);
            kfree(f->data);
        }
        f->data = new_data;
        f->capacity = new_cap;
    }

    memcpy(f->data + offset, buf, count);
    if (offset + count > f->size) {
        f->size = offset + count;
        node->size = f->size;
    }
    return count;
}

static int ext2_truncate(vfs_node_t *node, off_t length) {
    ext2_mem_file_t *f = (ext2_mem_file_t *)node->private_data;
    if (!f) return -EIO;
    if ((size_t)length <= f->capacity) {
        f->size = length;
        node->size = length;
        return 0;
    }
    return -EINVAL;
}

static vfs_node_t *ext2_make_vfs_node(ext2_mem_file_t *f);

static vfs_node_t *ext2_finddir(vfs_node_t *node, const char *name) {
    ext2_mem_file_t *dir = (ext2_mem_file_t *)node->private_data;
    if (!dir) return NULL;
    ext2_mem_file_t *child = ext2_find_child(dir, name);
    if (!child) return NULL;
    return ext2_make_vfs_node(child);
}

static int ext2_readdir(vfs_node_t *node, struct dirent *entry, uint32_t index) {
    ext2_mem_file_t *dir = (ext2_mem_file_t *)node->private_data;
    if (!dir || index >= (uint32_t)dir->child_count) return -1;
    ext2_mem_file_t *child = dir->children[index];
    entry->d_ino = child->inode_num;
    strncpy(entry->d_name, child->name, NAME_MAX - 1);
    entry->d_type = child->is_dir ? 4 : 8;
    return 0;
}

static int ext2_create(vfs_node_t *parent, const char *name, mode_t mode);
static int ext2_mkdir_op(vfs_node_t *parent, const char *name, mode_t mode);
static int ext2_unlink_op(vfs_node_t *parent, const char *name);
static int ext2_chmod(vfs_node_t *node, mode_t mode);
static int ext2_stat(vfs_node_t *node, struct stat *st);

static file_ops_t ext2_file_ops = {
    .read = ext2_read,
    .write = ext2_write,
    .readdir = ext2_readdir,
    .finddir = ext2_finddir,
    .create = ext2_create,
    .mkdir = ext2_mkdir_op,
    .unlink = ext2_unlink_op,
    .stat = ext2_stat,
    .chmod = ext2_chmod,
    .truncate = ext2_truncate,
};

static vfs_node_t *ext2_make_vfs_node(ext2_mem_file_t *f) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strncpy(node->name, f->name, NAME_MAX - 1);
    node->inode = f->inode_num;
    node->mode = f->mode;
    node->uid = f->uid;
    node->gid = f->gid;
    node->size = f->size;
    node->nlink = f->is_dir ? 2 : 1;
    node->private_data = f;
    node->ops = &ext2_file_ops;
    return node;
}

static int ext2_create(vfs_node_t *parent, const char *name, mode_t mode) {
    ext2_mem_file_t *dir = (ext2_mem_file_t *)parent->private_data;
    if (!dir || !dir->is_dir) return -ENOTDIR;
    if (dir->child_count >= 64) return -ENOSPC;
    if (ext2_find_child(dir, name)) return -EEXIST;

    ext2_mem_file_t *f = ext2_alloc_file(&ext2_instance);
    if (!f) return -ENOMEM;

    strncpy(f->name, name, NAME_MAX - 1);
    f->mode = mode;
    f->uid = 0;
    f->gid = 0;
    f->is_dir = S_ISDIR(mode);
    f->parent_inode = dir->inode_num;
    dir->children[dir->child_count++] = f;
    return 0;
}

static int ext2_mkdir_op(vfs_node_t *parent, const char *name, mode_t mode) {
    return ext2_create(parent, name, S_IFDIR | (mode & 0777));
}

static int ext2_unlink_op(vfs_node_t *parent, const char *name) {
    ext2_mem_file_t *dir = (ext2_mem_file_t *)parent->private_data;
    if (!dir) return -EIO;

    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            ext2_mem_file_t *f = dir->children[i];
            if (f->is_dir && f->child_count > 0) return -ENOTEMPTY;
            if (f->data) kfree(f->data);
            for (int j = i; j < dir->child_count - 1; j++)
                dir->children[j] = dir->children[j+1];
            dir->child_count--;
            return 0;
        }
    }
    return -ENOENT;
}

static int ext2_chmod(vfs_node_t *node, mode_t mode) {
    ext2_mem_file_t *f = (ext2_mem_file_t *)node->private_data;
    if (!f) return -EIO;
    f->mode = (f->mode & S_IFMT) | (mode & ~S_IFMT);
    node->mode = f->mode;
    return 0;
}

static int ext2_stat(vfs_node_t *node, struct stat *st) {
    ext2_mem_file_t *f = (ext2_mem_file_t *)node->private_data;
    memset(st, 0, sizeof(struct stat));
    st->st_ino = f->inode_num;
    st->st_mode = f->mode;
    st->st_nlink = f->is_dir ? 2 : 1;
    st->st_uid = f->uid;
    st->st_gid = f->gid;
    st->st_size = f->size;
    st->st_blksize = EXT2_BLOCK_SIZE;
    st->st_blocks = (f->size + 511) / 512;
    return 0;
}

static vfs_node_t *ext2_mount_fs(const char *device, const char *options) {
    (void)device; (void)options;

    memset(&ext2_instance, 0, sizeof(ext2_instance));
    ext2_instance.next_inode = EXT2_ROOT_INO;

    // Create root directory
    ext2_mem_file_t *root = ext2_alloc_file(&ext2_instance);
    strcpy(root->name, "/");
    root->mode = S_IFDIR | 0755;
    root->is_dir = 1;
    ext2_instance.root = root;

    // Create some default directories
    ext2_mem_file_t *etc = ext2_alloc_file(&ext2_instance);
    strcpy(etc->name, "etc");
    etc->mode = S_IFDIR | 0755;
    etc->is_dir = 1;
    etc->parent_inode = root->inode_num;
    root->children[root->child_count++] = etc;

    ext2_mem_file_t *home = ext2_alloc_file(&ext2_instance);
    strcpy(home->name, "home");
    home->mode = S_IFDIR | 0755;
    home->is_dir = 1;
    home->parent_inode = root->inode_num;
    root->children[root->child_count++] = home;

    ext2_mem_file_t *var = ext2_alloc_file(&ext2_instance);
    strcpy(var->name, "var");
    var->mode = S_IFDIR | 0755;
    var->is_dir = 1;
    var->parent_inode = root->inode_num;
    root->children[root->child_count++] = var;

    ext2_mem_file_t *bin = ext2_alloc_file(&ext2_instance);
    strcpy(bin->name, "bin");
    bin->mode = S_IFDIR | 0755;
    bin->is_dir = 1;
    bin->parent_inode = root->inode_num;
    root->children[root->child_count++] = bin;

    // Create /etc/hostname
    ext2_mem_file_t *hostname = ext2_alloc_file(&ext2_instance);
    strcpy(hostname->name, "hostname");
    hostname->mode = S_IFREG | 0644;
    hostname->is_dir = 0;
    hostname->data = (uint8_t *)kmalloc(64);
    strcpy((char *)hostname->data, "nexusOS\n");
    hostname->size = 8;
    hostname->capacity = 64;
    hostname->parent_inode = etc->inode_num;
    etc->children[etc->child_count++] = hostname;

    // Create /etc/os-release
    ext2_mem_file_t *osrel = ext2_alloc_file(&ext2_instance);
    strcpy(osrel->name, "os-release");
    osrel->mode = S_IFREG | 0644;
    osrel->is_dir = 0;
    osrel->data = (uint8_t *)kmalloc(256);
    const char *os_info = "NAME=\"nexusOS\"\nVERSION=\"0.01\"\nID=nexusos\nPRETTY_NAME=\"nexusOS v0.01\"\n";
    strcpy((char *)osrel->data, os_info);
    osrel->size = strlen(os_info);
    osrel->capacity = 256;
    osrel->parent_inode = etc->inode_num;
    etc->children[etc->child_count++] = osrel;

    return ext2_make_vfs_node(root);
}

filesystem_t ext2_type = { .name = "ext2", .mount = ext2_mount_fs, .umount = NULL };

void ext2_init(void) {
    vfs_register_fs(&ext2_type);
}
