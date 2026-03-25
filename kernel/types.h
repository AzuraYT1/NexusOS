/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEXUS_TYPES_H
#define NEXUS_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;
typedef int64_t            off_t;
typedef int32_t            pid_t;
typedef uint32_t           uid_t;
typedef uint32_t           gid_t;
typedef uint32_t           mode_t;
typedef uint64_t           ino_t;
typedef uint64_t           dev_t;
typedef int64_t            time_t;
typedef uint64_t           uintptr_t;
typedef int64_t            intptr_t;

#define NULL ((void*)0)
#define true 1
#define false 0
typedef int bool;

#define PACKED __attribute__((packed))
#define ALIGN(x) __attribute__((aligned(x)))

#define PAGE_SIZE 4096
#define KERNEL_BASE 0xFFFFFFFF80000000ULL

// Limits
#define PATH_MAX 256
#define NAME_MAX 128
#define MAX_FDS 256
#define MAX_PROCS 256

// File types
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)

// Permissions
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRWXO 0007

// Open flags
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400
#define O_EXCL    0x0080
#define O_DIRECTORY 0x10000

// Seek
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Errno
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EEXIST   17
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define EMFILE   24
#define ENOSPC   28
#define ENOSYS   38
#define ENOTEMPTY 39
#define ERANGE   34

// dirent
struct dirent {
    ino_t d_ino;
    off_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[NAME_MAX];
};

// stat
struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    uint32_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
    uint64_t st_blksize;
    uint64_t st_blocks;
};

// utsname
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

// timespec
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#endif
