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

[[ -x "$SCRIPT_DIR/bin/googleboot-menu" ]] || make -C "$SCRIPT_DIR"

mkdir -p "$RD_DIR/usr/local/bin" \
         "$RD_DIR/boot/googlebook-theme/icons" \
         "$RD_DIR/etc/systemd/system/initrd.target.wants"

cp -f "$SCRIPT_DIR/bin/googleboot-menu" "$RD_DIR/usr/local/bin/googleboot-menu"
cp -f "$SCRIPT_DIR/bin/googleboot-scan" "$RD_DIR/usr/local/bin/googleboot-scan"
cp -f "$SCRIPT_DIR/../bin/googlebook-switch-slot" "$RD_DIR/usr/local/bin/googlebook-switch-slot"
cp -f "$SCRIPT_DIR/theme/theme.conf" "$RD_DIR/boot/googlebook-theme/theme.conf"
chmod 755 "$RD_DIR/usr/local/bin/googleboot-menu" \
          "$RD_DIR/usr/local/bin/googleboot-scan" \
          "$RD_DIR/usr/local/bin/googlebook-switch-slot"

if command -v kexec >/dev/null 2>&1; then
  cp -f "$(command -v kexec)" "$RD_DIR/usr/sbin/kexec"
  chmod 755 "$RD_DIR/usr/sbin/kexec"
fi

cat > "$RD_DIR/etc/systemd/system/googleboot-menu.service" << 'EOF'
[Unit]
Description=GoogleBook Moonstone Graphical kexec Boot Menu
DefaultDependencies=no
After=systemd-udevd.service systemd-modules-load.service
Before=cryptsetup.target initrd-switch-root.target

[Service]
Type=oneshot
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/tty1
ExecStart=/usr/local/bin/googleboot-menu
RemainAfterExit=yes

[Install]
WantedBy=initrd.target
EOF

ln -sf ../googleboot-menu.service "$RD_DIR/etc/systemd/system/initrd.target.wants/googleboot-menu.service"
echo "==> Installed googleboot-menu and googleboot-menu.service into $RD_DIR"
