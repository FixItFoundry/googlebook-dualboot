#!/bin/bash
# install-into-initramfs.sh
# Injects the compiled googleboot-menu binary, googleboot-scan backend, kexec binary,
# and systemd initrd service into an unpacked Slot B rd_vendor directory.
#
# Usage:
#   ./scripts/install-into-initramfs.sh /path/to/unpacked_rd_vendor

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RD_DIR="${1:?Usage: $0 /path/to/unpacked_rd_vendor}"

make -B -C "$SCRIPT_DIR" 2>/dev/null || [[ -x "$SCRIPT_DIR/bin/googleboot-menu" ]]

mkdir -p "$RD_DIR/usr/local/bin" \
         "$RD_DIR/boot/googlebook-theme/icons" \
         "$RD_DIR/etc/systemd/system/sysinit.target.wants" \
         "$RD_DIR/etc/systemd/system/initrd.target.wants"

cp -f "$SCRIPT_DIR/bin/googleboot-menu" "$RD_DIR/usr/local/bin/googleboot-menu"
cp -f "$SCRIPT_DIR/bin/googleboot-scan" "$RD_DIR/usr/local/bin/googleboot-scan"
for name in googlebook-switch-slot googlebook-restore-slotb; do
  cp -f "$SCRIPT_DIR/../bin/$name" "$RD_DIR/usr/local/bin/$name"
  chmod 755 "$RD_DIR/usr/local/bin/$name"
done
cp -f "$SCRIPT_DIR/theme/theme.conf" "$RD_DIR/boot/googlebook-theme/theme.conf"
chmod 755 "$RD_DIR/usr/local/bin/googleboot-menu" \
          "$RD_DIR/usr/local/bin/googleboot-scan"

if command -v kexec >/dev/null 2>&1; then
  cp -f "$(command -v kexec)" "$RD_DIR/usr/sbin/kexec"
  chmod 755 "$RD_DIR/usr/sbin/kexec"
fi

# Do not carry the old whole-disk write grant into the rebuilt initramfs.
rm -f "$RD_DIR/etc/udev/rules.d/99-googlebook-slotb.rules" "$RD_DIR/etc/sudoers.d/googlebook-slotb"

# Prevent plymouth from grabbing DRM master (/dev/dri/card0) before googleboot-menu
rm -f "$RD_DIR/usr/lib/systemd/system/sysinit.target.wants/plymouth-start.service" \
      "$RD_DIR/etc/systemd/system/sysinit.target.wants/plymouth-start.service" 2>/dev/null || true

# Ensure systemd-cryptsetup waits for googleboot-menu and skips re-prompting if already unlocked (any LUKS UUID/name)
CRYPT_DROPIN_DIR="$RD_DIR/etc/systemd/system/systemd-cryptsetup@.service.d"
mkdir -p "$CRYPT_DROPIN_DIR"
cat > "$CRYPT_DROPIN_DIR/skip-if-unlocked.conf" << 'EOF'
[Unit]
After=googleboot-menu.service
ConditionPathExists=!/dev/mapper/%i
EOF

cat > "$RD_DIR/etc/systemd/system/googleboot-menu.service" << 'EOF'
[Unit]
Description=GoogleBook Moonstone Graphical kexec Boot Menu
DefaultDependencies=no
After=systemd-udevd.service systemd-udev-trigger.service systemd-modules-load.service
Before=cryptsetup-pre.target cryptsetup.target initrd-switch-root.target dracut-initqueue.service

[Service]
Type=oneshot
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/tty1
ExecStart=/bin/sh -c 'modprobe nvme nvme_core ext4 btrfs vfat fat dm_crypt usb_storage uas 2>/dev/null || true; udevadm settle --timeout=3 2>/dev/null || sleep 1; for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /dev/dri/card0 ] && break; sleep 0.2; done; exec /usr/local/bin/googleboot-menu'
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target initrd.target
EOF

ln -sf ../googleboot-menu.service "$RD_DIR/etc/systemd/system/sysinit.target.wants/googleboot-menu.service"
ln -sf ../googleboot-menu.service "$RD_DIR/etc/systemd/system/initrd.target.wants/googleboot-menu.service"
echo "==> Installed googleboot-menu and googleboot-menu.service into $RD_DIR"
