/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_VFS_H
#define NEXUS_VFS_H

#include "../kernel/types.h"

// Forward declarations
struct vfs_node;
struct vfs_mount;

// File operations
typedef struct {
    ssize_t (*read)(struct vfs_node *node, void *buf, size_t count, off_t offset);
    ssize_t (*write)(struct vfs_node *node, const void *buf, size_t count, off_t offset);
    int (*open)(struct vfs_node *node, int flags);
    int (*close)(struct vfs_node *node);
    int (*readdir)(struct vfs_node *node, struct dirent *entry, uint32_t index);
    struct vfs_node *(*finddir)(struct vfs_node *node, const char *name);
    int (*create)(struct vfs_node *parent, const char *name, mode_t mode);
    int (*mkdir)(struct vfs_node *parent, const char *name, mode_t mode);
    int (*unlink)(struct vfs_node *parent, const char *name);
    int (*stat)(struct vfs_node *node, struct stat *st);
    int (*chmod)(struct vfs_node *node, mode_t mode);
    int (*truncate)(struct vfs_node *node, off_t length);
} file_ops_t;

// Filesystem type
typedef struct {
    const char *name;
    struct vfs_node *(*mount)(const char *device, const char *options);
    int (*umount)(struct vfs_node *root);
} filesystem_t;

// VFS node (inode)
typedef struct vfs_node {
    char name[NAME_MAX];
    ino_t inode;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    uint32_t nlink;
    off_t size;
    time_t atime, mtime, ctime;
    file_ops_t *ops;
    void *private_data;
    struct vfs_node *parent;
    struct vfs_mount *mount;
    uint32_t ref_count;
} vfs_node_t;

// Mount point
typedef struct vfs_mount {
    char mount_point[PATH_MAX];
    vfs_node_t *root;
    filesystem_t *fs;
    struct vfs_mount *next;
} vfs_mount_t;

// File descriptor
typedef struct {
    vfs_node_t *node;
    off_t offset;
    int flags;
    int in_use;
} file_desc_t;

// VFS functions
void vfs_init(void);
int vfs_register_fs(filesystem_t *fs);
int vfs_mount(const char *type, const char *device, const char *mount_point, const char *options);
int vfs_umount(const char *mount_point);
vfs_node_t *vfs_resolve_path(const char *path);
vfs_node_t *vfs_get_root(void);

// File operations
int vfs_open(const char *path, int flags, mode_t mode);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_seek(int fd, off_t offset, int whence);
int vfs_stat(const char *path, struct stat *st);
int vfs_fstat(int fd, struct stat *st);
int vfs_mkdir(const char *path, mode_t mode);
int vfs_unlink(const char *path);
int vfs_readdir(int fd, struct dirent *entry, uint32_t index);
int vfs_getcwd(char *buf, size_t size);
int vfs_chdir(const char *path);
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);
int vfs_access(const char *path, int mode);

// File descriptor table (per-process, but for now global)
#define MAX_GLOBAL_FDS 1024
extern file_desc_t fd_table[MAX_GLOBAL_FDS];

#endif
