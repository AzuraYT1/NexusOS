# SPDX-License-Identifier: GPL-2.0-only
# nexusOS v0.01 Makefile
# Build with clang/llvm toolchain

CC = clang
LD = ld.lld
ASM = nasm

CFLAGS = -target x86_64-unknown-none-elf \
         -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
         -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
         -O2 -c -I.

LDFLAGS = -T arch/x86_64/linker.ld -nostdlib -static

ASMFLAGS_BOOT = -f elf64

# Source files
ASM_SRCS = arch/x86_64/boot.asm arch/x86_64/interrupts.asm
C_SRCS = kernel/kernel.c kernel/console.c kernel/gdt.c kernel/idt.c \
         kernel/pic.c kernel/pit.c \
         mm/pmm.c mm/vmm.c \
         drivers/keyboard.c drivers/mouse.c drivers/joystick.c \
         drivers/serial.c drivers/tty.c \
         fs/vfs.c fs/ramfs.c fs/devfs.c fs/ext2.c fs/procfs.c \
         smp/smp.c syscall/syscall.c

# Object files
ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(ASM_OBJS) $(C_OBJS)

# Output
KERNEL = nexus.elf
ISO = nexusOS.iso

.PHONY: all clean iso run initramfs

all: $(KERNEL)

# Compile assembly files
%.o: %.asm
	$(ASM) $(ASMFLAGS_BOOT) $< -o $@

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# Link kernel
$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Kernel built: $(KERNEL)"
	@ls -la $(KERNEL)

# Create initramfs (CPIO newc format)
initramfs:
	@mkdir -p initramfs_root/etc initramfs_root/bin initramfs_root/home initramfs_root/var
	@echo "nexusOS" > initramfs_root/etc/hostname
	@echo 'NAME="nexusOS"' > initramfs_root/etc/os-release
	@echo 'VERSION="0.01"' >> initramfs_root/etc/os-release
	@echo "#!/bin/sh" > initramfs_root/init
	@echo "echo nexusOS init started" >> initramfs_root/init
	@chmod +x initramfs_root/init
	@echo "root:x:0:0:root:/root:/bin/sh" > initramfs_root/etc/passwd
	@echo "root:x:0:" > initramfs_root/etc/group
	@cd initramfs_root && find . | cpio -o -H newc > ../initramfs.cpio 2>/dev/null
	@echo "Initramfs created: initramfs.cpio"
	@ls -la initramfs.cpio

# Build ISO
iso: $(KERNEL) initramfs
	@mkdir -p isodir/boot/grub
	@cp $(KERNEL) isodir/boot/nexus.elf
	@cp initramfs.cpio isodir/boot/initramfs.cpio
	@cp boot/grub/grub.cfg isodir/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) isodir 2>/dev/null
	@echo "ISO built: $(ISO)"
	@ls -la $(ISO)

# Run in QEMU
run: iso
	qemu-system-x86_64 \
		-cdrom $(ISO) \
		-m 256M \
		-smp 2 \
		-serial stdio \
		-vga std \
		-no-reboot \
		-d int,cpu_reset -no-shutdown 2>/dev/null

# Run headless (serial only)
run-headless: iso
	qemu-system-x86_64 \
		-cdrom $(ISO) \
		-m 256M \
		-smp 2 \
		-serial stdio \
		-nographic \
		-no-reboot

# Test boot (timeout after 5 seconds)
test: iso
	timeout 8 qemu-system-x86_64 \
		-cdrom $(ISO) \
		-m 256M \
		-smp 2 \
		-serial stdio \
		-nographic \
		-no-reboot || true

clean:
	rm -f $(OBJS) $(KERNEL) $(ISO) initramfs.cpio
	rm -rf isodir initramfs_root
