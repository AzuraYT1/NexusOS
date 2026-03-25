/* SPDX-License-Identifier: GPL-2.0-only */
#include "vfs.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../mm/vmm.h"

file_desc_t fd_table[MAX_GLOBAL_FDS];

static vfs_node_t *root_node = NULL;
static vfs_mount_t *mount_list = NULL;
static filesystem_t *fs_list[16];
static int fs_count = 0;
static char cwd[PATH_MAX] = "/";
static ino_t next_inode = 1;

void vfs_init(void) {
    memset(fd_table, 0, sizeof(fd_table));
    memset(fs_list, 0, sizeof(fs_list));
    fs_count = 0;
    mount_list = NULL;

    // Create root node
    root_node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(root_node->name, "/");
    root_node->mode = S_IFDIR | 0755;
    root_node->inode = next_inode++;
    root_node->nlink = 2;

    kprintf("  VFS initialized\n");
}

int vfs_register_fs(filesystem_t *fs) {
    if (fs_count >= 16) return -1;
    fs_list[fs_count++] = fs;
    return 0;
}

vfs_node_t *vfs_get_root(void) {
    return root_node;
}

// Find filesystem by name
static filesystem_t *find_fs(const char *name) {
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_list[i]->name, name) == 0)
            return fs_list[i];
    }
    return NULL;
}

// Find mount point for a given path
static vfs_mount_t *find_mount(const char *path) {
    vfs_mount_t *best = NULL;
    size_t best_len = 0;

    for (vfs_mount_t *m = mount_list; m; m = m->next) {
        size_t len = strlen(m->mount_point);
        if (strncmp(path, m->mount_point, len) == 0 && len > best_len) {
            if (path[len] == '/' || path[len] == '\0') {
                best = m;
                best_len = len;
            }
        }
    }
    return best;
}

int vfs_mount(const char *type, const char *device, const char *mount_point, const char *options) {
    filesystem_t *fs = find_fs(type);
    if (!fs) {
        kprintf("VFS: unknown filesystem '%s'\n", type);
        return -ENODEV;
    }

    vfs_node_t *fs_root = fs->mount(device, options);
    if (!fs_root) return -EIO;

    vfs_mount_t *mnt = (vfs_mount_t *)kzalloc(sizeof(vfs_mount_t));
    strncpy(mnt->mount_point, mount_point, PATH_MAX - 1);
    mnt->root = fs_root;
    mnt->fs = fs;
    mnt->next = mount_list;
    mount_list = mnt;

    kprintf("  Mounted %s on %s (type: %s)\n", device ? device : "none", mount_point, type);
    return 0;
}

int vfs_umount(const char *mount_point) {
    vfs_mount_t **prev = &mount_list;
    for (vfs_mount_t *m = mount_list; m; prev = &m->next, m = m->next) {
        if (strcmp(m->mount_point, mount_point) == 0) {
            if (m->fs->umount) m->fs->umount(m->root);
            *prev = m->next;
            kfree(m);
            return 0;
        }
    }
    return -ENOENT;
}

vfs_node_t *vfs_resolve_path(const char *path) {
    if (!path || !*path) return NULL;

    char abs_path[PATH_MAX];
    if (path[0] != '/') {
        strcpy(abs_path, cwd);
        if (abs_path[strlen(abs_path)-1] != '/') strcat(abs_path, "/");
        strcat(abs_path, path);
    } else {
        strncpy(abs_path, path, PATH_MAX - 1);
    }

    // Find best matching mount
    vfs_mount_t *mnt = find_mount(abs_path);
    if (!mnt) {
        // Return root if path is "/"
        if (strcmp(abs_path, "/") == 0) return root_node;
        return NULL;
    }

    // Get the relative path within the mount
    const char *rel_path = abs_path + strlen(mnt->mount_point);
    while (*rel_path == '/') rel_path++;
    if (!*rel_path) return mnt->root;

    // Walk the path
    vfs_node_t *current = mnt->root;
    char component[NAME_MAX];

    while (*rel_path && current) {
        // Extract next path component
        int i = 0;
        while (*rel_path && *rel_path != '/' && i < NAME_MAX - 1) {
            component[i++] = *rel_path++;
        }
        component[i] = '\0';
        while (*rel_path == '/') rel_path++;

        if (strcmp(component, ".") == 0) continue;
        if (strcmp(component, "..") == 0) {
            if (current->parent) current = current->parent;
            continue;
        }

        if (!current->ops || !current->ops->finddir) return NULL;
        current = current->ops->finddir(current, component);
    }
    return current;
}

// ---- File descriptor operations ----

static int alloc_fd(void) {
    for (int i = 3; i < MAX_GLOBAL_FDS; i++) {  // 0,1,2 reserved for stdin/stdout/stderr
        if (!fd_table[i].in_use) return i;
    }
    return -EMFILE;
}

int vfs_open(const char *path, int flags, mode_t mode) {
    vfs_node_t *node = vfs_resolve_path(path);

    if (!node && (flags & O_CREAT)) {
        // Create file: resolve parent directory
        char parent_path[PATH_MAX];
        strncpy(parent_path, path, PATH_MAX - 1);
        char *last_slash = strrchr(parent_path, '/');
        const char *filename;
        if (last_slash) {
            *last_slash = '\0';
            filename = last_slash + 1;
            if (parent_path[0] == '\0') strcpy(parent_path, "/");
        } else {
            strcpy(parent_path, cwd);
            filename = path;
        }
        vfs_node_t *parent = vfs_resolve_path(parent_path);
        if (!parent || !S_ISDIR(parent->mode)) return -ENOENT;
        if (!parent->ops || !parent->ops->create) return -ENOSYS;
        int ret = parent->ops->create(parent, filename, S_IFREG | (mode & 0777));
        if (ret < 0) return ret;
        node = parent->ops->finddir(parent, filename);
        if (!node) return -EIO;
    }

    if (!node) return -ENOENT;

    int fd = alloc_fd();
    if (fd < 0) return fd;

    fd_table[fd].node = node;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = flags;
    fd_table[fd].in_use = 1;

    if (node->ops && node->ops->open)
        node->ops->open(node, flags);

    if (flags & O_TRUNC) {
        if (node->ops && node->ops->truncate)
            node->ops->truncate(node, 0);
    }

    node->ref_count++;
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;

    vfs_node_t *node = fd_table[fd].node;
    if (node->ops && node->ops->close)
        node->ops->close(node);
    node->ref_count--;

    fd_table[fd].in_use = 0;
    fd_table[fd].node = NULL;
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;

    vfs_node_t *node = fd_table[fd].node;
    if (!node->ops || !node->ops->read) return -ENOSYS;

    ssize_t ret = node->ops->read(node, buf, count, fd_table[fd].offset);
    if (ret > 0) fd_table[fd].offset += ret;
    return ret;
}

ssize_t vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;

    vfs_node_t *node = fd_table[fd].node;
    if (!node->ops || !node->ops->write) return -ENOSYS;

    if (fd_table[fd].flags & O_APPEND) fd_table[fd].offset = node->size;

    ssize_t ret = node->ops->write(node, buf, count, fd_table[fd].offset);
    if (ret > 0) fd_table[fd].offset += ret;
    return ret;
}

off_t vfs_seek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;

    off_t new_offset;
    switch (whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = fd_table[fd].offset + offset; break;
        case SEEK_END: new_offset = fd_table[fd].node->size + offset; break;
        default: return -EINVAL;
    }
    if (new_offset < 0) return -EINVAL;
    fd_table[fd].offset = new_offset;
    return new_offset;
}

int vfs_stat(const char *path, struct stat *st) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;

    if (node->ops && node->ops->stat)
        return node->ops->stat(node, st);

    // Default stat from node info
    memset(st, 0, sizeof(struct stat));
    st->st_ino = node->inode;
    st->st_mode = node->mode;
    st->st_nlink = node->nlink;
    st->st_uid = node->uid;
    st->st_gid = node->gid;
    st->st_size = node->size;
    st->st_blksize = 4096;
    st->st_blocks = (node->size + 511) / 512;
    return 0;
}

int vfs_fstat(int fd, struct stat *st) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;
    vfs_node_t *node = fd_table[fd].node;
    if (node->ops && node->ops->stat)
        return node->ops->stat(node, st);
    memset(st, 0, sizeof(struct stat));
    st->st_ino = node->inode;
    st->st_mode = node->mode;
    st->st_nlink = node->nlink;
    st->st_size = node->size;
    return 0;
}

int vfs_mkdir(const char *path, mode_t mode) {
    char parent_path[PATH_MAX];
    strncpy(parent_path, path, PATH_MAX - 1);
    char *last_slash = strrchr(parent_path, '/');
    const char *dirname;
    if (last_slash) {
        *last_slash = '\0';
        dirname = last_slash + 1;
        if (parent_path[0] == '\0') strcpy(parent_path, "/");
    } else {
        strcpy(parent_path, cwd);
        dirname = path;
    }

    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || !S_ISDIR(parent->mode)) return -ENOENT;
    if (!parent->ops || !parent->ops->mkdir) return -ENOSYS;
    return parent->ops->mkdir(parent, dirname, mode);
}

int vfs_unlink(const char *path) {
    char parent_path[PATH_MAX];
    strncpy(parent_path, path, PATH_MAX - 1);
    char *last_slash = strrchr(parent_path, '/');
    const char *name;
    if (last_slash) {
        *last_slash = '\0';
        name = last_slash + 1;
        if (parent_path[0] == '\0') strcpy(parent_path, "/");
    } else {
        strcpy(parent_path, cwd);
        name = path;
    }

    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) return -ENOENT;
    if (!parent->ops || !parent->ops->unlink) return -ENOSYS;
    return parent->ops->unlink(parent, name);
}

int vfs_readdir(int fd, struct dirent *entry, uint32_t index) {
    if (fd < 0 || fd >= MAX_GLOBAL_FDS || !fd_table[fd].in_use)
        return -EBADF;
    vfs_node_t *node = fd_table[fd].node;
    if (!S_ISDIR(node->mode)) return -ENOTDIR;
    if (!node->ops || !node->ops->readdir) return -ENOSYS;
    return node->ops->readdir(node, entry, index);
}

int vfs_getcwd(char *buf, size_t size) {
    strncpy(buf, cwd, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

int vfs_chdir(const char *path) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;
    if (!S_ISDIR(node->mode)) return -ENOTDIR;

    if (path[0] == '/') {
        strncpy(cwd, path, PATH_MAX - 1);
    } else {
        if (cwd[strlen(cwd)-1] != '/') strcat(cwd, "/");
        strcat(cwd, path);
    }
    return 0;
}

int vfs_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_GLOBAL_FDS || !fd_table[oldfd].in_use)
        return -EBADF;
    int newfd = alloc_fd();
    if (newfd < 0) return newfd;
    memcpy(&fd_table[newfd], &fd_table[oldfd], sizeof(file_desc_t));
    fd_table[oldfd].node->ref_count++;
    return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_GLOBAL_FDS || !fd_table[oldfd].in_use)
        return -EBADF;
    if (newfd < 0 || newfd >= MAX_GLOBAL_FDS) return -EBADF;
    if (fd_table[newfd].in_use) vfs_close(newfd);
    memcpy(&fd_table[newfd], &fd_table[oldfd], sizeof(file_desc_t));
    fd_table[oldfd].node->ref_count++;
    return newfd;
}

int vfs_access(const char *path, int mode) {
    (void)mode;
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;
    return 0;
}
