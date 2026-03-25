/* SPDX-License-Identifier: GPL-2.0-only */
#include "types.h"
#include "string.h"
#include "console.h"
#include "multiboot2.h"
#include "gdt.h"
#include "idt.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../fs/vfs.h"

// External functions
extern void pic_init(void);
extern void pit_init(uint32_t freq);
extern uint64_t pit_get_ticks(void);
extern void pit_sleep(uint32_t ms);
extern void keyboard_init(void);
extern void mouse_init(void);
extern void joystick_init(void);
extern void serial_driver_init(void);
extern void tty_init(void);
extern void smp_init(void);
extern void syscall_init(void);
extern void ramfs_init(void);
extern void devfs_init(void);
extern void ext2_init(void);
extern void procfs_init(void);
extern void tss_set_kernel_stack(uint64_t stack);
extern void tty_input_char(int tty_num, char c);
extern int keyboard_read(void);
extern int keyboard_available(void);
extern void sti_wrap(void);
extern void cli_wrap(void);

// CPIO newc initramfs parsing
#define CPIO_MAGIC "070701"

typedef struct PACKED {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
} cpio_newc_header_t;

static uint32_t hex_to_uint(const char *s, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    }
    return val;
}

static void parse_initramfs(uint8_t *data, uint32_t size) {
    uint8_t *ptr = data;
    uint8_t *end = data + size;

    kprintf("  Parsing initramfs (newc format, %d bytes)...\n", size);

    while (ptr + sizeof(cpio_newc_header_t) < end) {
        cpio_newc_header_t *hdr = (cpio_newc_header_t *)ptr;

        if (memcmp(hdr->magic, CPIO_MAGIC, 6) != 0) break;

        uint32_t namesize = hex_to_uint(hdr->namesize, 8);
        uint32_t filesize = hex_to_uint(hdr->filesize, 8);
        uint32_t mode = hex_to_uint(hdr->mode, 8);

        char *name = (char *)(ptr + sizeof(cpio_newc_header_t));

        // Align name
        uint32_t name_offset = sizeof(cpio_newc_header_t) + namesize;
        name_offset = (name_offset + 3) & ~3;

        uint8_t *file_data = ptr + name_offset;
        uint32_t data_offset = name_offset + filesize;
        data_offset = (data_offset + 3) & ~3;

        if (strcmp(name, "TRAILER!!!") == 0) break;

        // Skip . and empty names
        if (namesize > 1 && strcmp(name, ".") != 0) {
            // Create in root filesystem
            char path[PATH_MAX];
            if (name[0] != '/') {
                path[0] = '/';
                strncpy(path + 1, name, PATH_MAX - 2);
            } else {
                strncpy(path, name, PATH_MAX - 1);
            }

            if (S_ISDIR(mode)) {
                vfs_mkdir(path, mode & 0777);
            } else {
                int fd = vfs_open(path, O_CREAT | O_WRONLY, mode & 0777);
                if (fd >= 0) {
                    if (filesize > 0) {
                        vfs_write(fd, file_data, filesize);
                    }
                    vfs_close(fd);
                }
            }
            kprintf("    %s [%d bytes]\n", path, filesize);
        }

        ptr += data_offset;
    }
}

// Shell - built-in kernel shell for tty0
static void shell_prompt(void) {
    console_set_color(0x0055FF55, 0);  // green
    kprintf("tty0");
    console_set_color(0x00AAAAAA, 0);  // gray
    kprintf("> ");
}

static void shell_execute(const char *cmd) {
    // Skip leading whitespace
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "help") == 0) {
        kprintf("nexusOS built-in commands:\n");
        kprintf("  help       - Show this help\n");
        kprintf("  uname      - System information\n");
        kprintf("  uname -a   - Full system information\n");
        kprintf("  ls [path]  - List directory\n");
        kprintf("  cat <file> - Display file contents\n");
        kprintf("  echo <txt> - Echo text\n");
        kprintf("  mkdir <d>  - Create directory\n");
        kprintf("  touch <f>  - Create file\n");
        kprintf("  write <f> <text> - Write text to file\n");
        kprintf("  rm <file>  - Remove file\n");
        kprintf("  cd <dir>   - Change directory\n");
        kprintf("  pwd        - Print working directory\n");
        kprintf("  clear      - Clear screen\n");
        kprintf("  meminfo    - Memory information\n");
        kprintf("  cpuinfo    - CPU information\n");
        kprintf("  uptime     - System uptime\n");
        kprintf("  ps         - Process list\n");
        kprintf("  mount      - Show mounts\n");
        kprintf("  reboot     - Reboot system\n");
    }
    else if (strcmp(cmd, "uname") == 0) {
        kprintf("nexusOS\n");
    }
    else if (strcmp(cmd, "uname -a") == 0) {
        kprintf("nexusOS nexus 0.01 nexusOS v0.01 SMP x86_64\n");
    }
    else if (strncmp(cmd, "ls", 2) == 0) {
        const char *path = cmd + 2;
        while (*path == ' ') path++;
        if (!*path) path = ".";

        // Resolve actual path
        char abs[PATH_MAX];
        if (path[0] != '/') {
            vfs_getcwd(abs, PATH_MAX);
            if (abs[strlen(abs)-1] != '/') strcat(abs, "/");
            strcat(abs, path);
        } else {
            strncpy(abs, path, PATH_MAX - 1);
        }

        int fd = vfs_open(abs, O_RDONLY | O_DIRECTORY, 0);
        if (fd < 0) {
            kprintf("ls: cannot access '%s': No such file or directory\n", path);
            return;
        }
        struct dirent entry;
        uint32_t idx = 0;
        while (vfs_readdir(fd, &entry, idx) == 0) {
            if (entry.d_type == 4) {
                console_set_color(0x005555FF, 0);
                kprintf("%s/", entry.d_name);
                console_set_color(0x00AAAAAA, 0);
            } else {
                kprintf("%s", entry.d_name);
            }
            kprintf("  ");
            idx++;
        }
        if (idx > 0) kprintf("\n");
        vfs_close(fd);
    }
    else if (strncmp(cmd, "cat ", 4) == 0) {
        const char *path = cmd + 4;
        while (*path == ' ') path++;
        int fd = vfs_open(path, O_RDONLY, 0);
        if (fd < 0) {
            kprintf("cat: %s: No such file or directory\n", path);
            return;
        }
        char buf[512];
        ssize_t n;
        while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            kprintf("%s", buf);
        }
        vfs_close(fd);
    }
    else if (strncmp(cmd, "echo ", 5) == 0) {
        kprintf("%s\n", cmd + 5);
    }
    else if (strncmp(cmd, "mkdir ", 6) == 0) {
        const char *path = cmd + 6;
        while (*path == ' ') path++;
        if (vfs_mkdir(path, 0755) < 0)
            kprintf("mkdir: cannot create '%s'\n", path);
    }
    else if (strncmp(cmd, "touch ", 6) == 0) {
        const char *path = cmd + 6;
        while (*path == ' ') path++;
        int fd = vfs_open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) vfs_close(fd);
        else kprintf("touch: cannot create '%s'\n", path);
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        // write <file> <text>
        const char *args = cmd + 6;
        while (*args == ' ') args++;
        const char *space = strchr(args, ' ');
        if (!space) { kprintf("Usage: write <file> <text>\n"); return; }
        char fname[PATH_MAX];
        int flen = space - args;
        memcpy(fname, args, flen);
        fname[flen] = '\0';
        const char *text = space + 1;

        int fd = vfs_open(fname, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            vfs_write(fd, text, strlen(text));
            vfs_write(fd, "\n", 1);
            vfs_close(fd);
        } else {
            kprintf("write: cannot open '%s'\n", fname);
        }
    }
    else if (strncmp(cmd, "rm ", 3) == 0) {
        const char *path = cmd + 3;
        while (*path == ' ') path++;
        if (vfs_unlink(path) < 0)
            kprintf("rm: cannot remove '%s'\n", path);
    }
    else if (strncmp(cmd, "cd ", 3) == 0) {
        const char *path = cmd + 3;
        while (*path == ' ') path++;
        if (vfs_chdir(path) < 0)
            kprintf("cd: %s: No such directory\n", path);
    }
    else if (strcmp(cmd, "pwd") == 0) {
        char buf[PATH_MAX];
        vfs_getcwd(buf, PATH_MAX);
        kprintf("%s\n", buf);
    }
    else if (strcmp(cmd, "clear") == 0) {
        console_clear();
    }
    else if (strcmp(cmd, "meminfo") == 0) {
        uint64_t total = pmm_get_total_memory();
        uint64_t free_mem = pmm_get_free_memory();
        kprintf("Total:     %d MB\n", (int)(total / (1024*1024)));
        kprintf("Free:      %d MB\n", (int)(free_mem / (1024*1024)));
        kprintf("Used:      %d MB\n", (int)((total - free_mem) / (1024*1024)));
    }
    else if (strcmp(cmd, "cpuinfo") == 0) {
        extern int smp_get_cpu_count(void);
        kprintf("Architecture: x86_64\n");
        kprintf("CPU(s):       %d\n", smp_get_cpu_count());
        kprintf("Model:        nexusOS Virtual CPU\n");
    }
    else if (strcmp(cmd, "uptime") == 0) {
        uint64_t ticks = pit_get_ticks();
        uint64_t secs = ticks / 100;
        kprintf("up %d seconds\n", (int)secs);
    }
    else if (strcmp(cmd, "ps") == 0) {
        kprintf("  PID TTY      CMD\n");
        kprintf("    1 tty0     /init\n");
        kprintf("    2 tty0     /bin/sh (kernel shell)\n");
    }
    else if (strcmp(cmd, "mount") == 0) {
        kprintf("ext2 on / type ext2 (rw)\n");
        kprintf("devfs on /dev type devfs (rw)\n");
        kprintf("proc on /proc type proc (ro)\n");
        kprintf("tmpfs on /tmp type tmpfs (rw)\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        kprintf("Rebooting...\n");
        extern void outb(uint16_t port, uint8_t val);
        outb(0x64, 0xFE);  // Keyboard controller reset
        for(;;) __asm__ volatile("hlt");
    }
    else {
        kprintf("%s: command not found\n", cmd);
    }
}

// Kernel main entry
void kernel_main(uint32_t magic, uint32_t *mbi_addr) {
    // Initialize serial first for early debug output
    serial_init();
    serial_write("\r\n");

    // Parse Multiboot2 info for framebuffer and memory
    uint32_t *fb_addr = NULL;
    uint32_t fb_width = 1024, fb_height = 768, fb_pitch = 4096;
    uint64_t total_mem = 128 * 1024 * 1024;  // Default 128MB

    uint8_t *initramfs_start = NULL;
    uint32_t initramfs_size = 0;

    if (magic == MULTIBOOT2_MAGIC && mbi_addr) {
        mb2_tag_t *tag = mb2_first_tag(mbi_addr);
        uint32_t total_size = ((mb2_info_header_t *)mbi_addr)->total_size;
        uint8_t *mbi_end = (uint8_t *)mbi_addr + total_size;

        while ((uint8_t *)tag < mbi_end && tag->type != MB2_TAG_END) {
            switch (tag->type) {
                case MB2_TAG_FRAMEBUFFER: {
                    mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t *)tag;
                    fb_addr = (uint32_t *)(uint64_t)fb->framebuffer_addr;
                    fb_width = fb->framebuffer_width;
                    fb_height = fb->framebuffer_height;
                    fb_pitch = fb->framebuffer_pitch;
                    break;
                }
                case MB2_TAG_MEMORY_MAP: {
                    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
                    uint8_t *entry_ptr = (uint8_t *)mmap + 16;
                    uint8_t *mmap_end = (uint8_t *)mmap + mmap->size;
                    total_mem = 0;
                    while (entry_ptr < mmap_end) {
                        mb2_mmap_entry_t *e = (mb2_mmap_entry_t *)entry_ptr;
                        if (e->type == 1) {  // Available
                            uint64_t end = e->base_addr + e->length;
                            if (end > total_mem) total_mem = end;
                        }
                        entry_ptr += mmap->entry_size;
                    }
                    break;
                }
                case MB2_TAG_MODULE: {
                    mb2_tag_module_t *mod = (mb2_tag_module_t *)tag;
                    initramfs_start = (uint8_t *)(uint64_t)mod->mod_start;
                    initramfs_size = mod->mod_end - mod->mod_start;
                    break;
                }
            }
            tag = mb2_next_tag(tag);
        }
    }

    // Initialize console (framebuffer or VGA text fallback)
    if (fb_addr) {
        console_init(fb_addr, fb_width, fb_height, fb_pitch);
    } else {
        serial_write("WARNING: No framebuffer, using serial console only\r\n");
    }

    // Boot banner
    console_set_color(0x0055FFFF, 0);  // Cyan
    kprintf("\n");
    kprintf("  ███╗   ██╗███████╗██╗  ██╗██╗   ██╗███████╗ ██████╗ ███████╗\n");
    kprintf("  ████╗  ██║██╔════╝╚██╗██╔╝██║   ██║██╔════╝██╔═══██╗██╔════╝\n");
    kprintf("  ██╔██╗ ██║█████╗   ╚███╔╝ ██║   ██║███████╗██║   ██║███████╗\n");
    kprintf("  ██║╚██╗██║██╔══╝   ██╔██╗ ██║   ██║╚════██║██║   ██║╚════██║\n");
    kprintf("  ██║ ╚████║███████╗██╔╝ ██╗╚██████╔╝███████║╚██████╔╝███████║\n");
    kprintf("  ╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚══════╝\n");
    console_set_color(0x00FFFF55, 0);  // Yellow
    kprintf("                     v0.01 - x86_64 kernel\n\n");
    console_set_color(0x00AAAAAA, 0);  // Gray

    kprintf("nexusOS v0.01\n");
    kprintf("Initializing kernel subsystems...\n\n");

    // GDT
    kprintf("[INIT] GDT...\n");
    gdt_init();

    // IDT
    kprintf("[INIT] IDT...\n");
    idt_init();

    // PIC
    kprintf("[INIT] PIC...\n");
    pic_init();

    // PIT (100 Hz timer)
    kprintf("[INIT] PIT...\n");
    pit_init(100);

    // Enable interrupts
    sti_wrap();

    // PMM
    kprintf("[INIT] Memory Manager...\n");
    pmm_init(total_mem);

    // Mark usable regions (skip first 4MB for kernel, page tables, heap)
    // Available memory starts after 16MB (kernel + heap + buffers)
    if (total_mem > 0x1000000) {
        pmm_init_region(0x1000000, total_mem - 0x1000000);
    }
    // Reserve first 16MB (kernel, page tables, heap, framebuffer)
    pmm_deinit_region(0, 0x1000000);

    kprintf("  PMM: %d MB free / %d MB total\n",
            (int)(pmm_get_free_memory() / (1024*1024)),
            (int)(pmm_get_total_memory() / (1024*1024)));

    // VMM
    kprintf("[INIT] VMM...\n");
    vmm_init();

    // Heap
    kprintf("[INIT] Kernel Heap...\n");
    heap_init();

    // SMP
    kprintf("[INIT] SMP...\n");
    smp_init();

    // SYSCALL/SYSRET
    kprintf("[INIT] Syscall Interface...\n");
    syscall_init();

    // Set kernel stack for Ring 3 returns
    extern uint8_t stack_top[];
    tss_set_kernel_stack((uint64_t)stack_top);

    // Drivers
    kprintf("[INIT] Drivers...\n");
    keyboard_init();
    mouse_init();
    joystick_init();
    serial_driver_init();
    tty_init();

    // Filesystems
    kprintf("[INIT] Filesystems...\n");
    vfs_init();
    ramfs_init();
    devfs_init();
    ext2_init();
    procfs_init();

    // Mount filesystems
    kprintf("[INIT] Mounting filesystems...\n");
    vfs_mount("ext2", "ramdisk", "/", NULL);
    vfs_mount("devfs", NULL, "/dev", NULL);
    vfs_mount("proc", NULL, "/proc", NULL);
    vfs_mount("tmpfs", NULL, "/tmp", NULL);

    // Parse initramfs if present
    if (initramfs_start && initramfs_size > 0) {
        kprintf("[INIT] Initramfs...\n");
        parse_initramfs(initramfs_start, initramfs_size);
    }

    // Create standard FD entries (0=stdin, 1=stdout, 2=stderr)
    // These map to the console TTY
    extern vfs_node_t *vfs_resolve_path(const char *);
    vfs_node_t *console_node = vfs_resolve_path("/dev/console");
    if (console_node) {
        fd_table[0].node = console_node; fd_table[0].in_use = 1; fd_table[0].flags = O_RDONLY;
        fd_table[1].node = console_node; fd_table[1].in_use = 1; fd_table[1].flags = O_WRONLY;
        fd_table[2].node = console_node; fd_table[2].in_use = 1; fd_table[2].flags = O_WRONLY;
    }

    kprintf("\n[INIT] Starting init...\n\n");

    // System ready
    console_set_color(0x0055FF55, 0);  // Green
    kprintf("Welcome to nexusOS v0.01\n");
    kprintf("Type 'help' for available commands.\n\n");
    console_set_color(0x00AAAAAA, 0);  // Gray

    // Main kernel shell loop - simple line editor
    shell_prompt();

    static char shell_line[256];
    static int shell_pos = 0;

    for (;;) {
        __asm__ volatile("hlt");  // Wait for interrupt

        while (keyboard_available()) {
            int c = keyboard_read();
            if (c < 0) continue;

            if (c == '\n' || c == '\r') {
                // Execute command
                console_putchar('\n');
                serial_putchar('\r');
                serial_putchar('\n');
                shell_line[shell_pos] = '\0';
                if (shell_pos > 0) {
                    shell_execute(shell_line);
                }
                shell_pos = 0;
                shell_prompt();
            } else if (c == '\b' || c == 127) {
                if (shell_pos > 0) {
                    shell_pos--;
                    console_putchar('\b');
                    console_putchar(' ');
                    console_putchar('\b');
                    serial_putchar('\b');
                    serial_putchar(' ');
                    serial_putchar('\b');
                }
            } else if (c == 3) {
                // Ctrl+C
                kprintf("^C\n");
                shell_pos = 0;
                shell_prompt();
            } else if (c >= 32 && c < 127 && shell_pos < 254) {
                shell_line[shell_pos++] = (char)c;
                console_putchar(c);
                serial_putchar(c);
            }
        }
    }
}
