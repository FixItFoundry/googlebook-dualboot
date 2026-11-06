#!/usr/bin/env bash
# smoke-googlebook.sh
# Picked up automatically by smoketests-all.sh, which globs /usr/local/bin/smoke-*.sh.
#
# Most of this re-checks what install-googlebook-layer.sh already asserts at build time.
# That is deliberate: those assertions only cover the layer's own build, and this image
# tracks a base image that moves underneath it. A base rebuild that bumps the kernel
# without a matching rebuild here would leave modules that silently refuse to load, and on
# Moonstone that means Slot B boots to a black screen with nothing in the logs.
set -euo pipefail
echo "$(basename "$0") started"

fail=0
check() {
  desc=$1
  shift
  if "$@" >/dev/null 2>&1; then
    echo "  ok   $desc"
  else
    echo "  FAIL $desc"
    fail=1
  fi
}

KVER=$(ls /usr/lib/modules | head -1)
echo "image kernel: $KVER"

# --- out-of-tree modules match the kernel they will be loaded against -------------------
for ko in "/usr/lib/modules/$KVER/extra"/*.ko "/usr/lib/modules/$KVER/updates"/*.ko; do
  [ -f "$ko" ] || continue
  got=$(modinfo -F vermagic "$ko" | awk '{print $1}')
  if [ "$got" = "$KVER" ]; then
    echo "  ok   vermagic $(basename "$ko")"
  else
    echo "  FAIL vermagic $(basename "$ko"): '$got' != '$KVER'"
    fail=1
  fi
done

# --- and are reachable by name, which is how the initramfs and modules-load.d load them --
for mod in coreboot_table framebuffer-coreboot cros_ec_ishtp \
           snd-soc-sof-sdw snd-soc-sdw-utils; do
  check "modprobe resolves $mod" modinfo -k "$KVER" "$mod"
done

# --- firmware the initramfs pulls in by absolute path ----------------------------------
check "ISH firmware present" \
  test -f /usr/lib/firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin
check "RT1320 calibration present" \
  test -f /usr/lib/firmware/realtek/rt1320/rt1320_Google_Moonstone_sku1.dat

# --- Vivaldi top row. The /etc copy of hwdb.bin shadows /usr/lib, so this catches a base
# --- image that reintroduces one and silently disables the keymap again.
check "Moonstone hwdb entry resolves" \
  sh -c 'systemd-hwdb query "evdev:atkbd:dmi:bvnA:bvrB:bdC:br1:svnGoogle:pnMoonstone:pvr1" | grep -q KEYBOARD_KEY_91'

# --- dracut drop-ins that keep the Slot B ramdisks inside their 32 MiB / 8 MiB budget ---
check "Slot B dracut conf present" \
  test -f /usr/lib/dracut/dracut.conf.d/90-googlebook-slotb.conf
check "sensor dracut conf present" \
  test -f /usr/lib/dracut/dracut.conf.d/91-googlebook-sensors.conf
check "coreboot framebuffer autoload present" \
  test -f /usr/lib/modules-load.d/googlebook-coreboot-fb.conf
check "staged Xe2 modprobe rules present" \
  test -f /usr/lib/modprobe.d/googlebook-xe-staged.conf
check "/var/lib/googlebook tmpfiles entry present" \
  grep -q '/var/lib/googlebook' /usr/lib/tmpfiles.d/googlebook.conf

# --- the toolkit entry points -----------------------------------------------------------
for tool in googlebook-pack-slotb googlebook-restore-slotb googlebook-switch-slot \
            googlebook-sysroot-sync googlebook-slotb-sync googleboot-menu googleboot-scan \
            boot-android; do
  check "$tool is executable" test -x "/usr/local/bin/$tool"
done
check "googleboot-menu is a built binary" \
  sh -c 'file -b "$(readlink -f /usr/local/bin/googleboot-menu)" | grep -q ELF'
check "packer parses" bash -n /usr/local/share/googlebook-dualboot/bin/googlebook-pack-slotb
check "sync wrapper parses" bash -n /usr/local/bin/googlebook-slotb-sync

# --- the tools the packer shells out to -------------------------------------------------
for cmd in dracut mkbootimg kexec sgdisk zstd depmod cpio findmnt; do
  check "$cmd available" command -v "$cmd"
done

# --- units that keep Slot B in step with the deployment ----------------------------------
check "googlebook-slotb-sync.path enabled" \
  systemctl is-enabled googlebook-slotb-sync.path
check "googlebook-gpu-init.service enabled" \
  systemctl is-enabled googlebook-gpu-init.service

# --- runtime-only: these need a real deployment, not a container -------------------------
BOOTIMAGE=$(bootc status --booted --format json 2>/dev/null | jq -r '.spec.image // "null"' || echo null)
if [ "$BOOTIMAGE" = "null" ]; then
  echo "  --   not a bootc system, skipping deployment checks"
else
  echo "Running under bootc - image=$BOOTIMAGE"
  check "ostree deployment marker" test -f /run/ostree-booted
  check "an ostree boot entry carries ostree=" \
    sh -c 'grep -qs "ostree=" /boot/loader/entries/*.conf'
  check "slotb-sync state directory" test -d /var/lib/googlebook
fi

if [ "$fail" -ne 0 ]; then
  echo "$(basename "$0") FAILED"
  exit 1
fi
echo "$(basename "$0") finished"
