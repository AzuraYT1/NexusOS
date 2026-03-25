# SPDX-License-Identifier: GPL-2.0-only
#!/bin/bash
# nexusOS v0.01 - Automated build and test script
set -e

echo "========================================="
echo "  nexusOS v0.01 Build System"
echo "========================================="

cd "$(dirname "$0")"

echo ""
echo "[1/5] Cleaning previous build..."
make clean 2>/dev/null || true

echo ""
echo "[2/5] Compiling kernel..."
make all 2>&1

echo ""
echo "[3/5] Creating initramfs..."
make initramfs 2>&1

echo ""
echo "[4/5] Building ISO..."
make iso 2>&1

echo ""
echo "[5/5] Testing in QEMU..."
echo "-----------------------------------------"
echo "  Boot log (8 second timeout):"
echo "-----------------------------------------"
make test 2>&1

echo ""
echo "========================================="
echo "  Build complete!"
echo "  ISO: nexusOS.iso"
echo "  Kernel: nexus.elf"
echo "========================================="
