#!/bin/bash
# build-uefi-bridge.sh
# Compiles Das U-Boot (coreboot64_defconfig + EFI_LOADER + NVMe + Coreboot Framebuffer)
# into an experimental ELF artifact. This is not a validated kexec payload
# and does not provide a supported Windows boot path.

set -euo pipefail

UBOOT_TAG="${UBOOT_TAG:-v2025.01}"
WORK_DIR="${WORK_DIR:-/tmp/googlebook-uboot-efi}"
OUT_FILE="${1:-./uefi-bridge.elf}"
mkdir -p "$(dirname "$OUT_FILE")"
OUT_FILE="$(cd "$(dirname "$OUT_FILE")" && pwd)/$(basename "$OUT_FILE")"

echo "==> Building Googlebook Moonstone Bare-Metal UEFI Bridge ($UBOOT_TAG)..."
mkdir -p "$WORK_DIR"
if [[ ! -d "$WORK_DIR/u-boot" ]]; then
  git clone --depth 1 --branch "$UBOOT_TAG" https://source.denx.de/u-boot/u-boot.git "$WORK_DIR/u-boot"
fi

cd "$WORK_DIR/u-boot"
make coreboot64_defconfig

# Enable full UEFI Boot/Runtime Services, GOP Framebuffer, NVMe, USB XHCI, and auto-booting bootmgfw.efi
./scripts/config --enable CONFIG_EFI_LOADER
./scripts/config --enable CONFIG_CMD_BOOTEFI
./scripts/config --enable CONFIG_EFI_BOOTMGR
./scripts/config --enable CONFIG_VIDEO_COREBOOT
./scripts/config --enable CONFIG_NVME
./scripts/config --enable CONFIG_NVME_PCI
./scripts/config --enable CONFIG_USB_XHCI_HCD
./scripts/config --enable CONFIG_USB_XHCI_PCI
./scripts/config --enable CONFIG_USB_STORAGE
./scripts/config --enable CONFIG_FS_FAT
./scripts/config --enable CONFIG_DOS_PARTITION
./scripts/config --enable CONFIG_EFI_PARTITION
./scripts/config --set-str CONFIG_BOOTCOMMAND "pci enum; nvme scan; usb start; fatload nvme 0:1 \${loadaddr} /EFI/Microsoft/Boot/bootmgfw.efi && bootefi \${loadaddr}; bootefi bootmgr"

make olddefconfig
make -j"$(nproc)"

if [[ -f u-boot.rom ]]; then
  cp -f u-boot "$OUT_FILE"
  echo "==> Experimental U-Boot ELF: $OUT_FILE (not a bootable bzImage)"
fi
