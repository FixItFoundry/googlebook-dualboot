#!/bin/bash
# build-slotb-installer.sh
# Builds Depthcharge/Android v4 Slot B boot images (boot_b.img, init_boot_b.img, vendor_boot_b.img)
# for the Acer Googlebook 14 (Moonstone) from any extracted Linux Live or Stage2 installer ISO.
#
# Supports:
#   - Fedora / RHEL Live (LiveOS/squashfs.img) & Stage2 (images/install.img)
#   - Ubuntu / Pop!_OS / Mint Live (casper/vmlinuz + casper/initrd*)
#   - Debian Live (live/vmlinuz* + live/initrd*)
#   - Arch / CachyOS Live (arch/boot/x86_64/vmlinuz* + initramfs*.img)
#
# Usage:
#   ./build-slotb-installer.sh <extracted-iso-dir> [output-dir] [http-url|LABEL=...|usb] [coreboot-mods-dir] [extra-cmdline]
set -euo pipefail

ISO_DIR="${1:-}"
OUT_DIR="${2:-./slotb-installer}"
SOURCE="${3:-usb}"
MODS_DIR="${4:-}"
EXTRA_CMDLINE="${5:-}"

if [[ -z "$ISO_DIR" || ! -d "$ISO_DIR" ]]; then
  echo "Usage: $0 <extracted-iso-dir> [output-dir] [http-url|LABEL=...|usb] [coreboot-mods-dir] [extra-cmdline]" >&2
  exit 1
fi

SCRIPT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MKBOOTIMG="$SCRIPT_ROOT/bin/mkbootimg"
[[ -x "$MKBOOTIMG" ]] || MKBOOTIMG=$(command -v mkbootimg || true)
if [[ -z "$MKBOOTIMG" || ! -x "$MKBOOTIMG" ]]; then
  echo "Error: mkbootimg not found. Install android-tools." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
ISO_DIR=$(cd "$ISO_DIR" && pwd)
OUT_DIR=$(cd "$OUT_DIR" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

first_file() {
  local f
  for f in "$@"; do [[ -f "$f" ]] && { printf '%s\n' "$f"; return 0; }; done
  return 1
}

# Detect installer layout (reusing googleboot-scan ISO conventions)
MODE="" VMLINUZ="" INITRD=""
if [[ -f "$ISO_DIR/images/pxeboot/vmlinuz" && -f "$ISO_DIR/images/pxeboot/initrd.img" ]]; then
  VMLINUZ="$ISO_DIR/images/pxeboot/vmlinuz"
  INITRD="$ISO_DIR/images/pxeboot/initrd.img"
  if [[ -f "$ISO_DIR/LiveOS/squashfs.img" && ! -f "$ISO_DIR/images/install.img" ]]; then
    MODE="fedora-live"
  else
    MODE="fedora-stage2"
  fi
elif VMLINUZ=$(first_file "$ISO_DIR"/arch/boot/x86_64/vmlinuz*) && INITRD=$(first_file "$ISO_DIR"/arch/boot/x86_64/initramfs*.img); then
  MODE="arch-live"
elif VMLINUZ=$(first_file "$ISO_DIR"/casper/vmlinuz*) && INITRD=$(first_file "$ISO_DIR"/casper/initrd*); then
  MODE="ubuntu-live"
elif VMLINUZ=$(first_file "$ISO_DIR"/live/vmlinuz*) && INITRD=$(first_file "$ISO_DIR"/live/initrd*); then
  MODE="debian-live"
else
  echo "Error: Unrecognized ISO layout in $ISO_DIR" >&2
  exit 1
fi

ISO_LABEL="${SOURCE#LABEL=}"
if [[ "$SOURCE" == "usb" || "$SOURCE" == "auto" || -z "$SOURCE" ]]; then
  ISO_LABEL=$(grep -rhoE '(CDLABEL|LABEL|archisolabel)=[^ "]+' "$ISO_DIR"/{EFI,boot,isolinux,loader} 2>/dev/null | head -1 | cut -d= -f2 || true)
  ISO_LABEL="${ISO_LABEL:-LIVE}"
fi

echo "[1/4] Unpacking $MODE initrd (${INITRD#$ISO_DIR/})..."
mkdir -p "$WORK/unpacked" "$WORK/split_init/usr/lib"
unpack_initrd() {
  local img="$1" dst="$2"
  if command -v unmkinitramfs >/dev/null 2>&1 && unmkinitramfs "$img" "$dst/umk" 2>/dev/null; then
    for sub in "$dst/umk"/early* "$dst/umk/main" "$dst/umk"; do
      [[ -d "$sub" ]] && cp -a "$sub"/. "$dst/" 2>/dev/null || true
    done
    rm -rf "$dst/umk"
    return 0
  fi
  local stream="$img"
  if [[ -x /usr/lib/dracut/skipcpio ]]; then
    /usr/lib/dracut/skipcpio "$img" > "$WORK/stripped.img" 2>/dev/null && [[ -s "$WORK/stripped.img" ]] && stream="$WORK/stripped.img"
  fi
  (cd "$dst" && (
    zstd -d < "$stream" 2>/dev/null | cpio -idm --quiet 2>/dev/null ||
    xz -dc "$stream" 2>/dev/null | cpio -idm --quiet 2>/dev/null ||
    lz4 -dc "$stream" 2>/dev/null | cpio -idm --quiet 2>/dev/null ||
    gzip -dc "$stream" 2>/dev/null | cpio -idm --quiet 2>/dev/null ||
    zstd -d < "$img" 2>/dev/null | cpio -idm --quiet 2>/dev/null ||
    xz -dc "$img" 2>/dev/null | cpio -idm --quiet
  ))
}
unpack_initrd "$INITRD" "$WORK/unpacked"

# Normalize non-usrmerged initramfs layouts (/lib/modules -> /usr/lib/modules)
if [[ ! -d "$WORK/unpacked/usr/lib/modules" && -d "$WORK/unpacked/lib/modules" ]]; then
  mkdir -p "$WORK/unpacked/usr/lib"
  mv "$WORK/unpacked/lib/modules" "$WORK/unpacked/usr/lib/modules"
  ln -snf ../usr/lib/modules "$WORK/unpacked/lib/modules"
fi

KVER=$(ls -1 "$WORK/unpacked/usr/lib/modules" | head -1)
echo "      Detected kernel version: $KVER ($MODE)"

[[ -n "$MODS_DIR" ]] || MODS_DIR="$SCRIPT_ROOT/coreboot-mods/prebuilt/$KVER"
echo "[2/4] Preparing Coreboot framebuffer modules..."
mkdir -p "$WORK/unpacked/usr/lib/modules/$KVER/extra"
if [[ -d "$MODS_DIR" ]]; then
  cp -f "$MODS_DIR"/*.ko "$WORK/unpacked/usr/lib/modules/$KVER/extra/"
elif [[ -d "$SCRIPT_ROOT/coreboot-mods" && -d "/lib/modules/$KVER/build" ]]; then
  CB_BUILD="$WORK/cb_build"
  mkdir -p "$CB_BUILD"
  cp -rf "$SCRIPT_ROOT/coreboot-mods"/* "$CB_BUILD/"
  make -C "/lib/modules/$KVER/build" M="$CB_BUILD" modules >/dev/null 2>&1 || true
  strip --strip-debug "$CB_BUILD"/*.ko 2>/dev/null || true
  cp -f "$CB_BUILD"/*.ko "$WORK/unpacked/usr/lib/modules/$KVER/extra/" 2>/dev/null || true
else
  echo "      Warning: no prebuilt or host headers for $KVER coreboot-mods; continuing without injection." >&2
fi

echo "[3/4] Pruning unused GPU/NIC firmware and splitting into rd_vendor.img + rd_init.img..."
rm -rf "$WORK/unpacked/usr/lib/firmware"/{amdgpu,i915,xe,nvidia,radeon,qed,cxgb4,netronome,mellanox,cypress,mrvl,ath*,brcm,mediatek,ti-connectivity,iwlwifi*,intel/ibt*,intel/vpu*,intel/sof*,intel/qat*,qat*,cavium,qcom,LENOVO,dell,HP} 2>/dev/null || true
rm -rf "$WORK/unpacked/usr/lib/modules/$KVER/kernel/drivers/gpu/drm"/{amd,i915,xe,nouveau,radeon,vmwgfx,qxl,ast,mgag200} 2>/dev/null || true

# Stage Moonstone sensor sources + firmware, and inject prebuilt/host sensor modules when available (non-fatal)
SENSOR_MODS_DIR="$SCRIPT_ROOT/sensor-mods"
if [[ -d "$SENSOR_MODS_DIR" ]]; then
  mkdir -p "$WORK/unpacked/usr/local/share/googlebook-sensors" "$WORK/unpacked/usr/lib/firmware/intel/ish"
  cp "$SENSOR_MODS_DIR"/{Makefile,cros_ec.h,cros_ec_ishtp.c} "$WORK/unpacked/usr/local/share/googlebook-sensors/"
  cp -a "$SENSOR_MODS_DIR/firmware" "$WORK/unpacked/usr/local/share/googlebook-sensors/"
  cp "$SENSOR_MODS_DIR/firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin" "$WORK/unpacked/usr/lib/firmware/intel/ish/"
  for module in intel_ish_ipc cros_ec_ishtp cros_ec_sensors cros_ec_lid_angle cros_ec_keyb; do
    if dependencies=$(modprobe --show-depends --set-version "$KVER" "$module" 2>/dev/null); then
      while read -r kind path _rest; do
        [[ $kind == insmod && -f "$path" ]] || continue
        relative=${path#*/modules/$KVER/}
        [[ $relative != "$path" ]] && install -D -m644 "$path" "$WORK/unpacked/usr/lib/modules/$KVER/$relative"
      done <<<"$dependencies"
    fi
  done
  if [[ -f "$SENSOR_MODS_DIR/prebuilt/$KVER/cros_ec_ishtp.ko" ]]; then
    install -D -m644 "$SENSOR_MODS_DIR/prebuilt/$KVER/cros_ec_ishtp.ko" \
      "$WORK/unpacked/usr/lib/modules/$KVER/extra/cros_ec_ishtp.ko"
  fi
fi
depmod -b "$WORK/unpacked" -a "$KVER" 2>/dev/null || depmod -b "$WORK/unpacked/usr" -a "$KVER" 2>/dev/null || true

mv "$WORK/unpacked/usr/lib/modules" "$WORK/split_init/usr/lib/modules"

pack_ramdisks() {
  (cd "$WORK/unpacked" && find . -print0 | cpio --null -o -H newc --quiet | zstd -19 -T0 > "$OUT_DIR/rd_vendor.img")
  (cd "$WORK/split_init" && find . -print0 | cpio --null -o -H newc --quiet | zstd -19 -T0 > "$OUT_DIR/rd_init.img")
  VSZ=$(wc -c < "$OUT_DIR/rd_vendor.img")
  ISZ=$(wc -c < "$OUT_DIR/rd_init.img")
}
pack_ramdisks

# Live ISOs sometimes bundle sound/wireless/media modules in initrd; trim if rd_init exceeds 8 MiB
if (( ISZ > 8380416 )); then
  rm -rf "$WORK/split_init/usr/lib/modules/$KVER/kernel"/{sound,drivers/media,drivers/bluetooth,drivers/net/wireless,drivers/infiniband,drivers/isdn} 2>/dev/null || true
  pack_ramdisks
fi

echo "      rd_vendor.img: $VSZ bytes (limit 33488896)"
echo "      rd_init.img  : $ISZ bytes (limit 8380416)"

(( VSZ <= 33488896 && ISZ <= 8380416 )) || {
  echo "Installer ramdisks exceed Slot B capacity; no boot images written." >&2
  exit 1
}

echo "[4/4] Packing boot_b.img, init_boot_b.img, and vendor_boot_b.img..."
printf 'androidboot.googleboot=1\n' > "$WORK/bootconfig"

GPU_ARGS="xe.enable_psr=0 modprobe.blacklist=i915"
case "$MODE" in
  fedora-live)
    if [[ "$SOURCE" == http://* || "$SOURCE" == https://* ]]; then
      STAGE_ARGS="root=live:${SOURCE%/}/LiveOS/squashfs.img rd.live.image rd.neednet=1 ip=dhcp"
    else
      STAGE_ARGS="root=live:CDLABEL=${ISO_LABEL} rd.live.image"
    fi
    ;;
  fedora-stage2)
    GPU_ARGS="modprobe.blacklist=i915,xe"
    if [[ "$SOURCE" == http://* || "$SOURCE" == https://* ]]; then
      STAGE_ARGS="inst.stage2=${SOURCE} inst.text inst.sshd"
    else
      STAGE_ARGS="inst.stage2=hd:LABEL=${ISO_LABEL} inst.text inst.sshd"
    fi
    ;;
  arch-live)
    if [[ "$SOURCE" == http://* || "$SOURCE" == https://* ]]; then
      STAGE_ARGS="archisobasedir=arch archiso_http_srv=${SOURCE%/}/ ip=:::::eth0:dhcp cow_spacesize=4G"
    else
      STAGE_ARGS="archisobasedir=arch archisolabel=${ISO_LABEL} cow_spacesize=4G"
    fi
    ;;
  ubuntu-live)
    if [[ "$SOURCE" == http://*/*.iso || "$SOURCE" == https://*/*.iso ]]; then
      STAGE_ARGS="boot=casper ip=dhcp url=${SOURCE}"
    elif [[ "$SOURCE" == http://* || "$SOURCE" == https://* ]]; then
      STAGE_ARGS="boot=casper ip=dhcp fetch=${SOURCE%/}/casper/filesystem.squashfs"
    else
      STAGE_ARGS="boot=casper"
    fi
    ;;
  debian-live)
    if [[ "$SOURCE" == http://* || "$SOURCE" == https://* ]]; then
      STAGE_ARGS="boot=live ip=dhcp fetch=${SOURCE%/}/live/filesystem.squashfs"
    else
      STAGE_ARGS="boot=live"
    fi
    ;;
esac

CMDLINE="${STAGE_ARGS} rd.driver.pre=coreboot_table,framebuffer-coreboot clk_ignore_unused ${GPU_ARGS} console=tty0 bootconfig ${EXTRA_CMDLINE}"

"$MKBOOTIMG" --header_version 4 --kernel "$VMLINUZ" --cmdline "" \
  --os_version 17.0.0 --os_patch_level 2026-09 -o "$OUT_DIR/boot_b.img"

"$MKBOOTIMG" --header_version 4 --ramdisk "$OUT_DIR/rd_init.img" \
  --os_version 17.0.0 --os_patch_level 2026-09 -o "$OUT_DIR/init_boot_b.img"

"$MKBOOTIMG" --header_version 4 --pagesize 0x800 --vendor_cmdline "$CMDLINE" \
  --vendor_bootconfig "$WORK/bootconfig" \
  --ramdisk_type platform --ramdisk_name '' --vendor_ramdisk_fragment "$OUT_DIR/rd_vendor.img" \
  --vendor_boot "$OUT_DIR/vendor_boot_b.img"

ls -lh "$OUT_DIR"/{boot_b.img,init_boot_b.img,vendor_boot_b.img}
echo "=== Done! Flash with: ./scripts/flash-slotb.sh $OUT_DIR ==="
