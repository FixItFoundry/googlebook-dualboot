#!/bin/bash
# build-slotb-installer.sh
# Builds Depthcharge/Android v4 Slot B boot images (boot_b.img, init_boot_b.img, vendor_boot_b.img)
# for the Acer Googlebook 14 (Moonstone) from an extracted Linux installer ISO directory.
#
# Key features:
#   1. Injects coreboot_table.ko and framebuffer-coreboot.ko for the 2880x1800 OLED display (simpledrm).
#   2. Prunes bulky GPU/NIC firmware from initramfs and splits into two independent .cpio.zst archives
#      (rd_vendor.img <= 32 MiB + rd_init.img <= 8 MiB) so Depthcharge page padding never corrupts zstd.
#
# Usage:
#   ./build-slotb-installer.sh <extracted-iso-dir> <output-dir> <http-server-url> [coreboot-mods-dir] [extra-cmdline]
# Example:
#   ./build-slotb-installer.sh ./iso-extracted ./slotb-installer http://192.168.8.11:8080 ./coreboot-mods/prebuilt/6.19.10-300.fc44.x86_64
set -euo pipefail

ISO_DIR="${1:-}"
OUT_DIR="${2:-./slotb-installer}"
HTTP_URL="${3:-http://192.168.8.11:8080}"
MODS_DIR="${4:-}"
EXTRA_CMDLINE="${5:-}"

if [[ -z "$ISO_DIR" || ! -f "$ISO_DIR/images/pxeboot/vmlinuz" || ! -f "$ISO_DIR/images/pxeboot/initrd.img" ]]; then
  echo "Usage: $0 <extracted-iso-dir> <output-dir> <http-server-url> [coreboot-mods-dir]" >&2
  exit 1
fi

MKBOOTIMG=$(command -v mkbootimg || echo "/opt/homebrew/bin/mkbootimg")
if [[ ! -x "$MKBOOTIMG" ]]; then
  echo "Error: mkbootimg not found. Install android-tools." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "[1/4] Unpacking installer initrd.img..."
mkdir -p "$WORK/unpacked" "$WORK/split_init/usr/lib"
(cd "$WORK/unpacked" && ( /usr/lib/dracut/skipcpio "$ISO_DIR/images/pxeboot/initrd.img" 2>/dev/null || cat "$ISO_DIR/images/pxeboot/initrd.img" ) | zstd -d | cpio -idm --quiet 2>/dev/null || xz -dc "$ISO_DIR/images/pxeboot/initrd.img" | cpio -idm --quiet)

KVER=$(ls -1 "$WORK/unpacked/usr/lib/modules" | head -1)
echo "      Detected installer kernel version: $KVER"

if [[ -n "$MODS_DIR" && -d "$MODS_DIR" ]]; then
  echo "[2/4] Injecting Coreboot framebuffer modules from $MODS_DIR..."
  mkdir -p "$WORK/unpacked/usr/lib/modules/$KVER/extra"
  cp -f "$MODS_DIR"/*.ko "$WORK/unpacked/usr/lib/modules/$KVER/extra/"
  depmod -b "$WORK/unpacked/usr" -a "$KVER" 2>/dev/null || true
fi

echo "[3/4] Pruning unused GPU/NIC firmware and splitting into rd_vendor.img + rd_init.img..."
rm -rf "$WORK/unpacked/usr/lib/firmware"/{amdgpu,i915,xe,nvidia,radeon,qed,cxgb4,netronome,mellanox,cypress,mrvl,ath10k,ath11k,ath12k,brcm, mediatek,ti-connectivity} 2>/dev/null || true
rm -rf "$WORK/unpacked/usr/lib/modules/$KVER/kernel/drivers/gpu/drm"/{amd,i915,xe,nouveau,radeon} 2>/dev/null || true

# Include the board firmware and the exact target kernel's sensor dependencies.
SENSOR_MODS_DIR="$(cd "$(dirname "$0")/.." && pwd)/sensor-mods"
if [[ -d "$SENSOR_MODS_DIR" ]]; then
  mkdir -p "$WORK/unpacked/usr/local/share/googlebook-sensors" "$WORK/unpacked/usr/lib/firmware/intel/ish"
  cp "$SENSOR_MODS_DIR"/{Makefile,cros_ec.h,cros_ec_ishtp.c} "$WORK/unpacked/usr/local/share/googlebook-sensors/"
  cp -a "$SENSOR_MODS_DIR/firmware" "$WORK/unpacked/usr/local/share/googlebook-sensors/"
  cp "$SENSOR_MODS_DIR/firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin" "$WORK/unpacked/usr/lib/firmware/intel/ish/"
  for module in intel_ish_ipc cros_ec_ishtp cros_ec_sensors cros_ec_lid_angle cros_ec_keyb; do
    if dependencies=$(modprobe --show-depends --set-version "$KVER" "$module" 2>/dev/null); then
      while read -r kind path rest; do
        [[ $kind == insmod ]] || continue
        relative=${path#*/modules/$KVER/}
        [[ $relative != "$path" ]] || { echo "Unexpected module path: $path" >&2; exit 1; }
        install -D -m644 "$path" "$WORK/unpacked/usr/lib/modules/$KVER/$relative"
      done <<<"$dependencies"
    fi
  done
  if [[ -f "$SENSOR_MODS_DIR/prebuilt/$KVER/cros_ec_ishtp.ko" ]]; then
    [[ $(modinfo -F vermagic "$SENSOR_MODS_DIR/prebuilt/$KVER/cros_ec_ishtp.ko") == "$KVER "* ]] || {
      echo "cros_ec_ishtp prebuilt does not match $KVER" >&2
      exit 1
    }
    install -D -m644 "$SENSOR_MODS_DIR/prebuilt/$KVER/cros_ec_ishtp.ko" \
      "$WORK/unpacked/usr/lib/modules/$KVER/extra/cros_ec_ishtp.ko"
  fi
  depmod -b "$WORK/unpacked" -a "$KVER"
  for module in intel_ish_ipc cros_ec_ishtp cros_ec_sensors cros_ec_lid_angle cros_ec_keyb; do
    modprobe -d "$WORK/unpacked" --set-version "$KVER" --show-depends "$module" >/dev/null || {
      echo "Missing $module or its dependencies for installer kernel $KVER; build on a host with that kernel's modules installed." >&2
      exit 1
    }
  done
fi

mv "$WORK/unpacked/usr/lib/modules" "$WORK/split_init/usr/lib/modules"

(cd "$WORK/unpacked" && find . -print0 | cpio --null -o -H newc --quiet | zstd -19 -T0 > "$OUT_DIR/rd_vendor.img")
(cd "$WORK/split_init" && find . -print0 | cpio --null -o -H newc --quiet | zstd -19 -T0 > "$OUT_DIR/rd_init.img")

VSZ=$(stat -f %z "$OUT_DIR/rd_vendor.img" 2>/dev/null || stat -c %s "$OUT_DIR/rd_vendor.img")
ISZ=$(stat -f %z "$OUT_DIR/rd_init.img" 2>/dev/null || stat -c %s "$OUT_DIR/rd_init.img")
echo "      rd_vendor.img: $VSZ bytes (limit 33488896)"
echo "      rd_init.img  : $ISZ bytes (limit 8380416)"

(( VSZ <= 33488896 && ISZ <= 8380416 )) || {
  echo "Sensor-equipped installer ramdisks exceed Slot B capacity; no boot images written." >&2
  exit 1
}

echo "[4/4] Packing boot_b.img, init_boot_b.img, and vendor_boot_b.img..."
printf 'androidboot.googleboot=1\n' > "$WORK/bootconfig"
CMDLINE="inst.stage2=${HTTP_URL} inst.text inst.sshd rd.driver.pre=coreboot_table,framebuffer-coreboot clk_ignore_unused modprobe.blacklist=i915,xe console=tty0 bootconfig ${EXTRA_CMDLINE}"

"$MKBOOTIMG" --header_version 4 --kernel "$ISO_DIR/images/pxeboot/vmlinuz" --cmdline "" \
  --os_version 17.0.0 --os_patch_level 2026-09 -o "$OUT_DIR/boot_b.img"

"$MKBOOTIMG" --header_version 4 --ramdisk "$OUT_DIR/rd_init.img" \
  --os_version 17.0.0 --os_patch_level 2026-09 -o "$OUT_DIR/init_boot_b.img"

"$MKBOOTIMG" --header_version 4 --pagesize 0x800 --vendor_cmdline "$CMDLINE" \
  --vendor_bootconfig "$WORK/bootconfig" \
  --ramdisk_type platform --ramdisk_name '' --vendor_ramdisk_fragment "$OUT_DIR/rd_vendor.img" \
  --vendor_boot "$OUT_DIR/vendor_boot_b.img"

ls -lh "$OUT_DIR"/{boot_b.img,init_boot_b.img,vendor_boot_b.img}
echo "=== Done! Flash with: ./scripts/flash-slotb.sh $OUT_DIR ==="
