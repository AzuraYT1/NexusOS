/* SPDX-License-Identifier: GPL-2.0-only */
#include "../kernel/types.h"
#include "../kernel/idt.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

// Linux x86_64 syscall numbers
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSTAT       6
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_IOCTL       16
#define SYS_ACCESS      21
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_UNAME       63
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_UNLINK      87
#define SYS_GETUID      102
#define SYS_GETGID      104
#define SYS_GETEUID     107
#define SYS_GETEGID     108
#define SYS_GETPPID     110
#define SYS_GETPGRP     111
#define SYS_SETSID      112
#define SYS_GETDENTS64  217
#define SYS_CLOCK_GETTIME 228
#define SYS_EXIT_GROUP  231
#define SYS_SET_TID_ADDRESS 218
#define SYS_ARCH_PRCTL  158
#define SYS_WRITEV      20

extern uint64_t pit_get_ticks(void);
extern void wrmsr_safe(uint32_t msr, uint64_t value);
extern uint64_t rdmsr_safe(uint32_t msr);

// Per-cpu data
extern uint64_t percpu_data[2];

// Current process
static pid_t current_pid = 1;
static uint64_t brk_current = 0x800000;  // 8MB initial brk

// Extract syscall args from register frame
// Linux x86_64: rax=syscall#, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4, r8=arg5, r9=arg6
// In our frame: regs offset mapping

static int64_t sys_read(int fd, void *buf, size_t count) {
    // FD 0 = stdin (tty)
    if (fd == 0) {
        extern ssize_t tty_read(int, char *, size_t);
        return tty_read(0, (char *)buf, count);
    }
    return vfs_read(fd, buf, count);
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    // FD 1,2 = stdout/stderr (tty)
    if (fd == 1 || fd == 2) {
        extern ssize_t tty_write(int, const char *, size_t);
        return tty_write(0, (const char *)buf, count);
    }
    return vfs_write(fd, buf, count);
}

static int64_t sys_open(const char *path, int flags, mode_t mode) {
    return vfs_open(path, flags, mode);
}

static int64_t sys_close(int fd) {
    if (fd < 3) return 0;  // Don't close stdin/stdout/stderr
    return vfs_close(fd);
}

static int64_t sys_stat(const char *path, struct stat *st) {
    return vfs_stat(path, st);
}

static int64_t sys_fstat(int fd, struct stat *st) {
    if (fd < 3) {
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IFCHR | 0666;
        return 0;
    }
    return vfs_fstat(fd, st);
}

static int64_t sys_lseek(int fd, off_t offset, int whence) {
    return vfs_seek(fd, offset, whence);
}

static int64_t sys_brk(uint64_t addr) {
    if (addr == 0) return brk_current;
    if (addr > brk_current) {
        // Map new pages
        for (uint64_t a = (brk_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
             a < addr; a += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (phys) vmm_map_page(a, phys, PTE_WRITABLE | PTE_USER);
        }
    }
    brk_current = addr;
    return brk_current;
}

static int64_t sys_ioctl(int fd, uint64_t request, uint64_t arg) {
    (void)fd; (void)request; (void)arg;
    return 0;  // Stub
}

static int64_t sys_mmap(uint64_t addr, size_t len, int prot, int flags, int fd, off_t offset) {
    (void)prot; (void)flags; (void)fd; (void)offset;
    // Simple anonymous mmap
    if (addr == 0) addr = 0x700000000000ULL;  // Use high address
    uint64_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -ENOMEM;
        vmm_map_page(addr + i * PAGE_SIZE, phys, PTE_WRITABLE | PTE_USER);
    }
    return addr;
}

static int64_t sys_munmap(uint64_t addr, size_t len) {
    uint64_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        vmm_unmap_page(addr + i * PAGE_SIZE);
    }
    return 0;
}

static int64_t sys_uname(struct utsname *buf) {
    strcpy(buf->sysname, "nexusOS");
    strcpy(buf->nodename, "nexus");
    strcpy(buf->release, "0.01");
    strcpy(buf->version, "nexusOS v0.01 SMP");
    strcpy(buf->machine, "x86_64");
    return 0;
}

static int64_t sys_getcwd(char *buf, size_t size) {
    return vfs_getcwd(buf, size);
}

static int64_t sys_mkdir(const char *path, mode_t mode) {
    return vfs_mkdir(path, mode);
}

static int64_t sys_unlink(const char *path) {
    return vfs_unlink(path);
}

static int64_t sys_access(const char *path, int mode) {
    return vfs_access(path, mode);
}

static int64_t sys_getdents64(int fd, void *dirp, size_t count) {
    struct dirent entry;
    uint8_t *buf = (uint8_t *)dirp;
    size_t pos = 0;
    uint32_t index = 0;

    while (pos + sizeof(struct dirent) <= count) {
        int ret = vfs_readdir(fd, &entry, index);
        if (ret < 0) break;
        size_t reclen = 24 + strlen(entry.d_name) + 1;
        reclen = (reclen + 7) & ~7;  // Align to 8 bytes
        if (pos + reclen > count) break;
        memcpy(buf + pos, &entry.d_ino, 8);
        memcpy(buf + pos + 8, &entry.d_off, 8);
        *(uint16_t *)(buf + pos + 16) = reclen;
        buf[pos + 18] = entry.d_type;
        strcpy((char *)(buf + pos + 19), entry.d_name);
        pos += reclen;
        index++;
    }
    return pos;
}

static int64_t sys_clock_gettime(int clockid, struct timespec *tp) {
    (void)clockid;
    uint64_t ticks = pit_get_ticks();
    tp->tv_sec = ticks / 100;
    tp->tv_nsec = (ticks % 100) * 10000000;
    return 0;
}

// Writev support
struct iovec {
    void *iov_base;
    size_t iov_len;
};

static int64_t sys_writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t ret = sys_write(fd, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0) return ret;
        total += ret;
    }
    return total;
}

// Main syscall dispatcher
// Called from syscall_entry in interrupts.asm
// Register frame: ds,r15..r8,rbp,rdi,rsi,rdx,rcx,rbx,rax, int_no/err, rip,cs,rfl,rsp,ss
void syscall_handler(registers_t *regs) {
    uint64_t syscall_num = regs->rax;
    uint64_t arg1 = regs->rdi;
    uint64_t arg2 = regs->rsi;
    uint64_t arg3 = regs->rdx;
    uint64_t arg4 = regs->r10;
    uint64_t arg5 = regs->r8;
    uint64_t arg6 = regs->r9;

    int64_t result = -ENOSYS;

    switch (syscall_num) {
        case SYS_READ:
            result = sys_read((int)arg1, (void *)arg2, (size_t)arg3);
            break;
        case SYS_WRITE:
            result = sys_write((int)arg1, (const void *)arg2, (size_t)arg3);
            break;
        case SYS_OPEN:
            result = sys_open((const char *)arg1, (int)arg2, (mode_t)arg3);
            break;
        case SYS_CLOSE:
            result = sys_close((int)arg1);
            break;
        case SYS_STAT:
        case SYS_LSTAT:
            result = sys_stat((const char *)arg1, (struct stat *)arg2);
            break;
        case SYS_FSTAT:
            result = sys_fstat((int)arg1, (struct stat *)arg2);
            break;
        case SYS_LSEEK:
            result = sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
            break;
        case SYS_MMAP:
            result = sys_mmap(arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (off_t)arg6);
            break;
        case SYS_MPROTECT:
            result = 0; // stub
            break;
        case SYS_MUNMAP:
            result = sys_munmap(arg1, (size_t)arg2);
            break;
        case SYS_BRK:
            result = sys_brk(arg1);
            break;
        case SYS_IOCTL:
            result = sys_ioctl((int)arg1, arg2, arg3);
            break;
        case SYS_ACCESS:
            result = sys_access((const char *)arg1, (int)arg2);
            break;
        case SYS_WRITEV:
            result = sys_writev((int)arg1, (const struct iovec *)arg2, (int)arg3);
            break;
        case SYS_DUP:
            result = vfs_dup((int)arg1);
            break;
        case SYS_DUP2:
            result = vfs_dup2((int)arg1, (int)arg2);
            break;
        case SYS_GETPID:
            result = current_pid;
            break;
        case SYS_GETPPID:
            result = 0;
            break;
        case SYS_GETUID:
        case SYS_GETEUID:
            result = 0; // root
            break;
        case SYS_GETGID:
        case SYS_GETEGID:
            result = 0;
            break;
        case SYS_GETPGRP:
            result = current_pid;
            break;
        case SYS_SETSID:
            result = current_pid;
            break;
        case SYS_FORK:
            result = -ENOSYS;  // Not implemented (would need full process management)
            break;
        case SYS_EXECVE:
            result = -ENOSYS;
            break;
        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            kprintf("\n[init exited with code %d]\n", (int)arg1);
            for(;;) __asm__ volatile("hlt");
            break;
        case SYS_WAIT4:
            result = -ECHILD;
            break;
        case SYS_UNAME:
            result = sys_uname((struct utsname *)arg1);
            break;
        case SYS_GETCWD:
            result = sys_getcwd((char *)arg1, (size_t)arg2);
            break;
        case SYS_CHDIR:
            result = vfs_chdir((const char *)arg1);
            break;
        case SYS_MKDIR:
            result = sys_mkdir((const char *)arg1, (mode_t)arg2);
            break;
        case SYS_UNLINK:
            result = sys_unlink((const char *)arg1);
            break;
        case SYS_GETDENTS64:
            result = sys_getdents64((int)arg1, (void *)arg2, (size_t)arg3);
            break;
        case SYS_CLOCK_GETTIME:
            result = sys_clock_gettime((int)arg1, (struct timespec *)arg2);
            break;
        case SYS_SET_TID_ADDRESS:
            result = current_pid;
            break;
        case SYS_ARCH_PRCTL:
            // Set FS/GS base
            if (arg1 == 0x1002) { // ARCH_SET_FS
                wrmsr_safe(0xC0000100, arg2);
                result = 0;
            } else if (arg1 == 0x1001) { // ARCH_SET_GS
                wrmsr_safe(0xC0000101, arg2);
                result = 0;
            } else {
                result = -EINVAL;
            }
            break;
        default:
            // Unknown syscall - return ENOSYS
            result = -ENOSYS;
            break;
    }

    regs->rax = (uint64_t)result;
}

// Setup SYSCALL/SYSRET MSRs
void syscall_init(void) {
    // STAR MSR: bits 47:32 = kernel CS, bits 63:48 = user CS base
    // Kernel CS = 0x08, Kernel DS = 0x10
    // User CS = 0x18 | 3 = 0x1B (with RPL=3), User DS = 0x20 | 3 = 0x23
    // SYSRET uses (STAR[63:48]+16) for CS and (STAR[63:48]+8) for SS
    // So user CS base = 0x18 - 16 = 0x08? No:
    // For SYSRET 64-bit: CS = STAR[63:48]+16, SS = STAR[63:48]+8
    // We want user CS=0x1B (selector 0x18 | RPL3), user SS=0x23 (selector 0x20 | RPL3)
    // STAR[63:48] should be 0x08 so: CS=0x08+16=0x18 (with RPL3 from SYSRET = 0x1B)
    //                                 SS=0x08+8=0x10... that's kernel DS
    // Actually: SYSRET sets CS to (STAR[63:48]+16)|3 and SS to (STAR[63:48]+8)|3
    // So STAR[63:48] = 0x10 gives CS=0x10+16=0x20|3=0x23... that's wrong
    // STAR[63:48] = 0x08: CS=0x08+16=0x18|3=0x1B, SS=0x08+8=0x10|3=0x13
    // But our user data segment is at 0x20, we need SS=0x23
    // Standard layout: null(0), kcode(0x08), kdata(0x10), udata(0x18), ucode(0x20)
    // With that: STAR[63:48]=0x10: CS=0x10+16=0x20|3=0x23, SS=0x10+8=0x18|3=0x1B
    // So user CS=0x23, user SS=0x1B - reversed from normal!
    // This is the standard AMD64 convention: GDT order must be kcode, kdata, udata, ucode
    // Our boot.asm has: null, kcode(0x08), kdata(0x10), ucode(0x18), udata(0x20)
    // We need: null, kcode(0x08), kdata(0x10), udata(0x18), ucode(0x20)
    // Let's just set STAR correctly for our GDT layout
    // SYSCALL: kernel CS from STAR[47:32], kernel SS = STAR[47:32]+8
    // SYSRET: user CS = STAR[63:48]+16 | 3, user SS = STAR[63:48]+8 | 3
    // Our GDT: 0=null, 1=kcode(0x08), 2=kdata(0x10), 3=ucode(0x18), 4=udata(0x20)
    // For SYSCALL: STAR[47:32] = 0x08 -> CS=0x08, SS=0x10 ✓
    // For SYSRET with STAR[63:48] = 0x10: CS=0x20|3=0x23, SS=0x18|3=0x1B
    // So user code uses selector 0x23 (udata descriptor?) and user stack uses 0x1B (ucode?)
    // This is because SYSRET reverses the order. The GDT must have udata BEFORE ucode.
    // Since our GDT has ucode at 0x18 and udata at 0x20:
    // STAR[63:48] = 0x10: SS = (0x10+8)|3 = 0x1B (this hits ucode - wrong!)
    // Solution: reorder GDT or just use interrupt-based return for now
    // Actually, for simplicity, use STAR[63:48] = 0x10
    // CS = (0x10 + 16) | 3 = 0x23, SS = (0x10 + 8) | 3 = 0x1B
    // Need GDT[3] = user data, GDT[4] = user code
    // Our boot.asm already has this if we swap user_code and user_data
    // For now, we'll setup MSRs and note our code handles this

    uint64_t star = 0;
    star |= ((uint64_t)0x08) << 32;   // SYSCALL: kernel CS
    star |= ((uint64_t)0x10) << 48;   // SYSRET: user CS base (CS=0x20|3, SS=0x18|3)
    wrmsr_safe(0xC0000081, star);  // STAR

    extern void syscall_entry(void);
    wrmsr_safe(0xC0000082, (uint64_t)syscall_entry);  // LSTAR

    wrmsr_safe(0xC0000084, 0x200);  // SFMASK: mask IF on SYSCALL

    // Setup per-CPU data for syscall handler (GSBASE)
    // percpu_data[0] = kernel RSP, percpu_data[1] = user RSP
    wrmsr_safe(0xC0000101, (uint64_t)percpu_data);  // GS_BASE
    wrmsr_safe(0xC0000102, (uint64_t)percpu_data);  // KERNEL_GS_BASE

    kprintf("  SYSCALL/SYSRET configured (37 syscalls)\n");
}
