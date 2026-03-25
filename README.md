# nexusOS

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-x86__64-blue)]()
[![Kernel](https://img.shields.io/badge/kernel-hobbyist-orange)]()
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](http://makeapullrequest.com)

**nexusOS** is a minimal, educational x86_64 kernel with multitasking, virtual memory, and a Unix-like userspace.

## Features

- 🖥️ **x86_64 architecture** with 4-level paging
- 🔄 **SMP support** (up to 2 CPUs detected via ACPI)
- 📦 **Memory management**: Physical Memory Manager (PMM) + Virtual Memory Manager (VMM)
- 📁 **Virtual File System (VFS)** with multiple backends:
  - `ramfs` - in-memory filesystem
  - `devfs` - device filesystem
  - `procfs` - process information
  - `tmpfs` - temporary filesystem
  - `ext2` - read/write ext2 support
- ⌨️ **Drivers**: PS/2 keyboard, PS/2 mouse, serial (COM1), TTY subsystem
- 🔧 **Syscall interface** with 37 system calls
- ⚙️ **Multitasking** with a simple round-robin scheduler
- 📦 **Initramfs** with Unix-like structure (`/bin`, `/etc`, `/home`, `/var`)

## Building

### Prerequisites

- **x86_64 cross-compiler** (GCC + binutils)
- **GRUB** (for ISO creation)
- **QEMU** (for testing)
- **Make**, **NASM**

### Build

```bash
make clean
make all
```
This will generate `nexusOS.iso`.

#### Run with QEMU
```bash
qemu-system-x86_64 -cdrom build/nexusOS.iso -serial stdio -smp 2
```

#### On real hardware
Burn `nexusOS.iso` to a CD/USB and boot from it. (Supports only PS/2 input for now.)
## License

This project is licensed under the **GNU General Public License v2.0** - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [OSDev Wiki](https://wiki.osdev.org/) for invaluable resources
- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot2/)
- GCC, Binutils, GRUB, QEMU communities

---
**Star ⭐ if you like this project!**
