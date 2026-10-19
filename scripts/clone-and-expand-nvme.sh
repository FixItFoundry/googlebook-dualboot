#!/bin/bash
# clone-and-expand-nvme.sh
# Clones the factory 512 GB Acer Googlebook 14 (Moonstone) NVMe to a larger (1 TB / 2 TB) NVMe
# and relocates the backup GPT header to the end of the disk (`sgdisk -e`) so the remaining
# unallocated space can be used for Fedomakase Linux (Slot B) alongside Android 17 (Slot A).
#
# Usage:
#   sudo ./clone-and-expand-nvme.sh <source-disk> <target-disk> [--skip-dd]
# Example:
#   sudo ./clone-and-expand-nvme.sh /dev/nvme0n1 /dev/sda
set -euo pipefail

if (( EUID != 0 )); then
  echo "Error: run as root (sudo $0 <source-disk> <target-disk>)" >&2
  exit 1
fi

SRC="${1:-}"
DST="${2:-}"
SKIP_DD="${3:-}"

if [[ -z "$SRC" || -z "$DST" || ! -b "$SRC" || ! -b "$DST" ]]; then
  echo "Usage: sudo $0 <source-disk> <target-disk> [--skip-dd]" >&2
  echo "Example: sudo $0 /dev/nvme0n1 /dev/sda" >&2
  exit 1
fi

if [[ "$SRC" == "$DST" ]]; then
  echo "Error: source and target disks must be different!" >&2
  exit 1
fi

SRC_BYTES=$(blockdev --getsize64 "$SRC")
DST_BYTES=$(blockdev --getsize64 "$DST")

echo "=== Googlebook Moonstone NVMe Clone & GPT Expansion ==="
echo "Source disk : $SRC ($((SRC_BYTES / 1024 / 1024 / 1024)) GiB)"
echo "Target disk : $DST ($((DST_BYTES / 1024 / 1024 / 1024)) GiB)"

if (( DST_BYTES < SRC_BYTES )); then
  echo "Error: target disk ($DST_BYTES bytes) is smaller than source disk ($SRC_BYTES bytes)!" >&2
  exit 1
fi

if [[ "$SKIP_DD" != "--skip-dd" ]]; then
  echo "[1/3] Cloning $SRC -> $DST (conv=sparse,fsync bs=16M)..."
  dd if="$SRC" of="$DST" bs=16M conv=sparse,fsync status=progress
  sync
else
  echo "[1/3] Skipping block copy (--skip-dd specified)."
fi

echo "[2/3] Relocating backup GPT header to end of $DST (sgdisk -e)..."
sgdisk -e "$DST"
partprobe "$DST" 2>/dev/null || blockdev --rereadpt "$DST" 2>/dev/null || true
udevadm settle 2>/dev/null || true

echo "[3/3] Verifying GPT partition table and free unallocated space on $DST..."
sgdisk -v "$DST"
parted -s "$DST" unit GiB print free | tail -n 15

echo "=== Clone and GPT expansion complete! ==="
