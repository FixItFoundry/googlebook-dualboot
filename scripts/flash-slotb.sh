#!/bin/bash
# flash-slotb.sh
# Flashes Fedomakase Linux Slot B images (boot_b.img, init_boot_b.img, vendor_boot_b.img)
# to the Acer Googlebook 14 (Moonstone) via Fastboot (USB or TCP) and sets Slot B active.
#
# Usage:
#   ./flash-slotb.sh <image-dir> [fastboot-target]
# Examples:
#   ./flash-slotb.sh ./slotb-images                  # USB Fastboot
#   ./flash-slotb.sh ./slotb-images tcp:192.168.8.219 # TCP Fastboot
set -euo pipefail

IMG_DIR="${1:-.}"
TARGET="${2:-}"

FASTBOOT=$(command -v fastboot || echo "/opt/homebrew/bin/fastboot")
if [[ ! -x "$FASTBOOT" ]]; then
  echo "Error: fastboot binary not found. Install android-platform-tools / android-tools." >&2
  exit 1
fi

for f in boot_b.img init_boot_b.img vendor_boot_b.img; do
  if [[ ! -f "$IMG_DIR/$f" ]]; then
    echo "Error: missing $IMG_DIR/$f" >&2
    exit 1
  fi
done

FB_ARGS=()
[[ -n "$TARGET" ]] && FB_ARGS=(-s "$TARGET")

echo "=== Flashing Googlebook Moonstone Slot B ==="
"$FASTBOOT" "${FB_ARGS[@]}" getvar product
"$FASTBOOT" "${FB_ARGS[@]}" flash boot_b "$IMG_DIR/boot_b.img"
"$FASTBOOT" "${FB_ARGS[@]}" flash init_boot_b "$IMG_DIR/init_boot_b.img"
"$FASTBOOT" "${FB_ARGS[@]}" flash vendor_boot_b "$IMG_DIR/vendor_boot_b.img"
"$FASTBOOT" "${FB_ARGS[@]}" --set-active=b
echo "=== Rebooting into Slot B (Fedomakase Linux) ==="
"$FASTBOOT" "${FB_ARGS[@]}" reboot
