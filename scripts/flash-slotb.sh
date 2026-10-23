#!/bin/bash
# flash-slotb.sh
# Flashes Fedomakase Linux Slot B images (boot_b.img, init_boot_b.img, vendor_boot_b.img)
# to the Acer Googlebook 14 (Moonstone) via Fastboot (USB or TCP) and sets Slot B active.
#
# Usage:
#   ./flash-slotb.sh <image-dir> [fastboot-target | --adb [adb-serial]]
# Examples:
#   ./flash-slotb.sh ./slotb-installer tcp:192.168.8.219 # TCP Fastboot over USB-C Ethernet
#   ./flash-slotb.sh ./slotb-installer --adb             # From Wireless/USB ADB -> auto TCP Fastboot
#   ./flash-slotb.sh ./slotb-installer                   # Active H2H USB Fastboot (or auto-ADB if connected)
set -euo pipefail

IMG_DIR="${1:-.}"
TARGET="${2:-}"
ADB_SERIAL="${3:-}"

FASTBOOT=$(command -v fastboot || echo "/opt/homebrew/bin/fastboot")
ADB=$(command -v adb || echo "/opt/homebrew/bin/adb")
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

if [[ "$TARGET" == "--adb" ]]; then
  ADB_ARGS=()
  [[ -n "$ADB_SERIAL" ]] && ADB_ARGS=(-s "$ADB_SERIAL")
  echo "=== Querying Googlebook LAN IP via ADB ==="
  LAN_IP=$("$ADB" "${ADB_ARGS[@]}" shell "ip -4 -o addr show scope global 2>/dev/null | awk '\$2 ~ /^(eth|en)/ {split(\$4,a,\"/\"); print a[1]; found=1; exit} !ip {split(\$4,b,\"/\"); ip=b[1]} END {if (!found && ip) print ip}'" 2>/dev/null | tr -d '\r\n ')
  if [[ -z "$LAN_IP" ]]; then
    echo "Error: could not determine Googlebook LAN IP over ADB. Connect a USB-C Ethernet adapter and retry." >&2
    exit 1
  fi
  echo "=== Rebooting Googlebook into Fastboot mode (TCP ${LAN_IP}:5554) ==="
  "$ADB" "${ADB_ARGS[@]}" reboot bootloader
  python3 - "$LAN_IP" <<'PY_WAIT'
import socket, sys, time
ip = sys.argv[1]
for _ in range(40):
    s = socket.socket()
    s.settimeout(0.5)
    try:
        s.connect((ip, 5554))
        s.close()
        sys.exit(0)
    except Exception:
        s.close()
        time.sleep(0.5)
sys.exit("Error: timed out waiting for TCP Fastboot at " + ip + ":5554")
PY_WAIT
  TARGET="tcp:$LAN_IP"
fi

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
