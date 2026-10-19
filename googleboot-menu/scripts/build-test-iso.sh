#!/bin/bash
# build-test-iso.sh
# Builds a bootable test ISO (`googleboot-menu.iso`) and Slot B ramdisk image
# containing the Linux 7.2.11 kernel, Coreboot framebuffer modules, kexec, and
# the graphical `googleboot-menu` DRM/KMS UI.
#
# Can be booted directly in QEMU/KVM (`qemu-system-x86_64 -enable-kvm -vga std -cdrom googleboot-menu.iso`)
# or flashed to Slot B on the Acer Googlebook 14 (`Moonstone`).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK_DIR="${WORK_DIR:-/tmp/googleboot-iso-build}"
KERNEL_IMG="${KERNEL_IMG:-/tmp/kernel}"
RD_VENDOR_SRC="${RD_VENDOR_SRC:-/tmp/v_unpack}"
RD_VENDOR_DIR="$WORK_DIR/v_unpack_menu"
RD_INIT_IMG="${RD_INIT_IMG:-/tmp/rd_init.img}"
OUT_ISO="${1:-/tmp/googleboot-menu.iso}"

echo "==> 1. Compiling googleboot-menu (x86_64 Linux DRM/KMS binary)..."
make -C "$SCRIPT_DIR"

echo "==> 2. Preparing initramfs root ($RD_VENDOR_DIR)..."
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/iso/boot/grub"
cp -a "$RD_VENDOR_SRC" "$RD_VENDOR_DIR"
"$SCRIPT_DIR/scripts/install-into-initramfs.sh" "$RD_VENDOR_DIR"

echo "==> 3. Packing combined initramfs for ISO & Slot B..."
(
  cd "$RD_VENDOR_DIR"
  find . -print0 | cpio --null -o -H newc | zstd --ultra -22 -T0 > "$WORK_DIR/rd_vendor_menu.img"
)
cat "$WORK_DIR/rd_vendor_menu.img" "$RD_INIT_IMG" > "$WORK_DIR/iso/boot/initrd.img"
cp -f "$KERNEL_IMG" "$WORK_DIR/iso/boot/vmlinuz"

cat > "$WORK_DIR/iso/boot/grub/grub.cfg" << 'EOF'
insmod all_video
insmod vbe
insmod vga
insmod gfxterm
set gfxmode=1280x800x32,1024x768x32,auto
set gfxpayload=keep
terminal_output gfxterm
set timeout=1
set default=0

menuentry "GoogleBoot Graphical kexec Bootloader (DRM/KMS)" {
    linux /boot/vmlinuz console=ttyS0,115200 console=tty0 rd.driver.pre=coreboot_table,framebuffer-coreboot clk_ignore_unused selinux=0
    initrd /boot/initrd.img
}
EOF

echo "==> 4. Building hybrid BIOS/UEFI bootable ISO ($OUT_ISO)..."
grub-mkrescue -o "$OUT_ISO" "$WORK_DIR/iso" 2>/dev/null
ls -lh "$OUT_ISO"
echo "==> Done! Test in QEMU with:"
echo "    qemu-system-x86_64 -enable-kvm -m 2048 -vga std -cdrom $OUT_ISO"
