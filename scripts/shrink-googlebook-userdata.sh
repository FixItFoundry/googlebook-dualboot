#!/bin/bash
# shrink-googlebook-userdata.sh
# Shrinks the stock Googlebook 'userdata' partition (the big ext4 data partition — index
# auto-detected by GPT label, e.g. p1 on ChromeOS-style layouts, p31 on Android-style ones)
# to 200 GiB (default) so the freed space can host Linux (Slot B) on the stock 512 GB SSD
# without buying a bigger NVMe.
#
# Only the partition labeled 'userdata' is touched; 'super' and all *_a / *_b boot/verity
# partitions remain untouched.
#
# Run this OFFLINE from the Slot B netinst RAM environment (scripts/build-slotb-installer.sh
# + scripts/flash-slotb.sh). userdata must NOT be mounted and the running system must not
# live on this disk. Data on userdata is preserved by resize2fs, but back up important files first.
#
# Usage:
#   sudo ./scripts/shrink-googlebook-userdata.sh /dev/nvme0n1 [target_gib]
# Example:
#   sudo ./scripts/shrink-googlebook-userdata.sh /dev/nvme0n1 200
set -euo pipefail

if (( EUID != 0 )); then
  echo "Error: run as root (sudo $0 /dev/nvme0n1 200)" >&2
  exit 1
fi

DISK="${1:-}"
TARGET_G="${2:-200}"   # GiB units; a bare integer. 200 -> partition of exactly 200 GiB

if [[ -z "$DISK" || ! -b "$DISK" ]]; then
  echo "Usage: sudo $0 <whole-disk> [200]" >&2
  echo "Example: sudo $0 /dev/nvme0n1 200   # 200 GiB userdata" >&2
  exit 1
fi
[[ "$TARGET_G" =~ ^(0|[1-9][0-9]*)$ ]] || { echo "Error: target must be a size in GiB, e.g. 200" >&2; exit 1; }
RESERVE_G=1   # GiB headroom between shrunken fs and final partition size
(( TARGET_G >= RESERVE_G + 1 )) || { echo "Error: target too small" >&2; exit 1; }
[[ -n "${FORCE:-}" ]] || [[ ! "$DISK" =~ ^/dev/(s|d)[a-z]+$ ]] || {
  echo "Error: refusing drive path $DISK — Googlebook user data lives on the internal NVMe (nvme0n1)." >&2
  exit 1
}

for tool in sgdisk partprobe lsblk findmnt e2fsck resize2fs dumpe2fs; do
  command -v "$tool" >/dev/null || { echo "Error: required tool '$tool' not found" >&2; exit 1; }
done

case "$(lsblk -lnno TYPE "$DISK" | head -1)" in
  disk|loop) : ;;  # whole disk (loop = resize-test medium; the netinst uses the real NVMe)
  *) echo "Error: $DISK is not a whole disk. Pass e.g. /dev/nvme0n1." >&2; exit 1 ;;
esac

run_fsck() {
  local rc=0
  e2fsck -f -p "$1" || rc=$?
  # e2fsck exit code 0 = no errors, 1 = filesystem errors corrected
  (( rc <= 1 ))
}

# Locate the 'userdata' partition by GPT label (device layout may put it at p1 or p31):
TABLE=$(sgdisk -p "$DISK")
NUM=$(awk '$NF=="userdata"{print $1; exit}' <<<"$TABLE")
if [[ -z "$NUM" ]]; then
  echo "Error: no partition labeled 'userdata' on $DISK. Full table:" >&2
  echo "$TABLE" >&2
  exit 1
fi
if [[ $(awk '$NF=="userdata"{c++} END{print c+0}' <<<"$TABLE") -gt 1 ]]; then
  echo "Error: multiple partitions labeled 'userdata' on $DISK — ambiguous, aborting." >&2
  exit 1
fi
echo "Found 'userdata' at partition index $NUM — proceeding."
echo "$TABLE"

PART=$(lsblk -rno PATH,TYPE "$DISK" | awk -v n="$NUM" '$2=="part"{name=$1; sub(/.*[^0-9]/,"",name); if (name+0==n){print $1; exit}}')
[[ -b "$PART" ]] || { echo "Error: cannot map partition $NUM to a device node on $DISK" >&2; exit 1; }

FSTARGET_G=$(( TARGET_G - RESERVE_G ))

echo "=== Googlebook userdata shrink ==="
echo "Disk        : $DISK"
echo "Partition $NUM: $PART"
echo "Target      : ${TARGET_G} GiB partition (fs ${FSTARGET_G} GiB)"

# [1/5] Identify the stock partition and write a recovery cheat-sheet
# Generous guard: only ever operate on the partition named 'userdata'.
INFO=$(sgdisk -i "$NUM" "$DISK")
LABEL=$(sed -n 's/^Partition name: .\(.*\).$/\1/p' <<<"$INFO")
if [[ "$LABEL" != "userdata" ]]; then
  echo "Error: partition $NUM on $DISK is labeled '${LABEL:-<none>}' (expected 'userdata'). Aborting — wrong disk?" >&2
  exit 1
fi
TYPE_GUID=$(awk '/^Partition GUID code:/{print $4; exit}' <<<"$INFO")
UNIQ_GUID=$(awk '/^Partition unique GUID:/{print $4; exit}' <<<"$INFO")
for g in "$TYPE_GUID" "$UNIQ_GUID"; do
  [[ "$g" =~ ^[0-9A-Fa-f]{8}(-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}$ ]] || {
    echo "Error: could not parse partition $NUM GUIDs ('$TYPE_GUID' / '$UNIQ_GUID') — aborting before any table change." >&2
    exit 1
  }
done
read -r START OLD_END < <(awk -v n="$NUM" '$1==n{print $2, $3}' <<<"$TABLE")
(( START > 0 )) || { echo "Error: could not read partition $NUM geometry" >&2; exit 1; }
(( START % 2048 == 0 )) || echo "Warning: start sector $START is not 1 MiB-aligned (proceeding, it came from the factory layout)"

# [2/5] Safety checks: offline filesystem, no Linux install present, not booted from this disk
if findmnt -rn -S "$PART" -o TARGET 2>/dev/null | grep -q .; then
  echo "Error: $PART is mounted ($(findmnt -rn -S "$PART" -o TARGET | tr '\n' ' ')). Run from the netinst RAM environment with userdata unmounted." >&2
  exit 1
fi
if [[ "$(findmnt -n -o SOURCE / 2>/dev/null)" == "$DISK"* ]]; then
  echo "Error: / is booted from $DISK — this must be the RAM netinst environment (iso/install.img → rootfs in RAM), not the internal drive." >&2
  exit 1
fi
# Guard against an install from this repo already present
REPO_LABELS=$(awk '$NF ~ /^(linux_boot|linux_root|ESP|Windows)$/ {print $NF}' <<<"$TABLE")
if [[ -n "$REPO_LABELS" ]]; then
  echo "Error: found partition(s) created by a dual-boot install: $REPO_LABELS — Linux is already installed. This script only prepares a first-time install." >&2
  exit 1
fi

FSTYPE=$(lsblk -ndo FSTYPE "$PART")
if [[ "$FSTYPE" != "ext4" ]]; then
  echo "Error: userdata filesystem is '${FSTYPE:-unknown}', not ext4." >&2
  echo "ext4 shrinks offline via resize2fs; f2fs/NTFS cannot be shrunk. Only option: destructive recreate (wipes Android data)." >&2
  exit 1
fi

OLD_BYTES=$(lsblk -bdno SIZE "$PART")
OLD_G=$(( OLD_BYTES / 1073741824 ))
NEW_BYTES=$(( TARGET_G * 1073741824 ))
if (( NEW_BYTES >= OLD_BYTES - 2147483648 )); then
  echo "Error: userdata is only ${OLD_G} GiB — shrinking to ${TARGET_G} GiB leaves no usable space for Linux." >&2
  exit 1
fi

# Persist the pre-shrink GPT backup & sector geometry inside userdata BEFORE e2fsck/resize2fs
# so s_lastcheck > s_mtime when resize2fs runs and no manual /tmp copy is needed.
PERSIST_MNT=$(mktemp -d)
trap 'umount "$PERSIST_MNT" 2>/dev/null || true; rmdir "$PERSIST_MNT" 2>/dev/null || true' EXIT
if mount "$PART" "$PERSIST_MNT" 2>/dev/null; then
  mkdir -p "$PERSIST_MNT/.googlebook-gpt-backup"
  { echo "# $(date -u '+%F %T')  $0 $DISK"; echo "$TABLE"; echo; echo "$INFO"; } > "$PERSIST_MNT/.googlebook-gpt-backup/userdata-original.txt"
  sgdisk --backup="$PERSIST_MNT/.googlebook-gpt-backup/gpt-pre-shrink.bin" "$DISK" >/dev/null 2>&1 || true
  sync
  umount "$PERSIST_MNT"
  echo "Saved persistent GPT backup on NVMe inside userdata: /.googlebook-gpt-backup/"
fi

echo "[1/5] Checking filesystem consistency (e2fsck -f -p)..."
run_fsck "$PART" || { echo "Error: e2fsck could not auto-repair userdata. Run it manually: e2fsck -f $PART" >&2; exit 1; }

read -r BS FREE_BLOCKS < <(dumpe2fs -h "$PART" 2>/dev/null | awk '/^Block size:/{bs=$3} /^Free blocks:/{fb=$3} END{print bs+0, fb+0}')
(( BS > 0 )) || { echo "Error: could not read block size via dumpe2fs" >&2; exit 1; }
FREE_BYTES=$(( FREE_BLOCKS * BS ))
FREE_G=$(( FREE_BYTES / 1073741824 ))
if (( FREE_BYTES * 10 < (OLD_BYTES - FSTARGET_G * 1073741824) * 11 )); then
  echo "Error: not enough free space inside userdata to shrink (${FREE_G} GiB free)." >&2
  echo "Free up Android storage (Settings > Storage) and re-run this script (~$(( (OLD_BYTES - FSTARGET_G*1073741824 - FREE_BYTES)/1073741824 + 2 )) GiB more needed)." >&2
  exit 1
fi

echo "[2/5] Shrinking ext4 filesystem to ${FSTARGET_G} GiB (this can take a while)..."
resize2fs "$PART" "${FSTARGET_G}G"
run_fsck "$PART" || { echo "Error: post-shrink fsck failed" >&2; exit 1; }

echo "[3/5] Truncating partition $NUM: start sector $START unchanged, end -> start + ${TARGET_G} GiB"
# One atomic sgdisk invocation: if any flag fails, nothing is saved (old userdata entry remains).
sgdisk --delete=$NUM --new=$NUM:${START}:+${TARGET_G}G --typecode=$NUM:${TYPE_GUID} \
       -u $NUM:${UNIQ_GUID} --change-name=${NUM}:userdata "$DISK"
partprobe "$DISK" 2>/dev/null || blockdev --rereadpt "$DISK" 2>/dev/null || true
udevadm settle 2>/dev/null || true

NEW_BYTES=$(lsblk -bdno SIZE "$PART")
echo "[4/5] Growing fs to fill exactly the new partition size..."
run_fsck "$PART"
resize2fs "$PART"

echo "[5/5] Verifying..."
sgdisk -i "$NUM" "$DISK"
lsblk -bno NAME,SIZE,FSTYPE,LABEL "$PART"
FREED_G=$(( (OLD_BYTES - NEW_BYTES) / 1073741824 ))
echo "=== Done! userdata is now exactly ${TARGET_G} GiB; ${FREED_G} GiB freed on $DISK ==="
cat <<'NEXT'

Next steps:
  1. Proceed directly to installing Linux into the newly freed unallocated space on this disk
     (e.g., choosing "Use Unallocated Free Space (Dual-Boot)" in Fedomakase, or creating p11 (2 GiB ext4 /boot)
     + p32 (root) in the freed space per README §3 Step 3), then pack Slot B with googlebook-pack-slotb.
  2. Pre-shrink GPT backup & original partition geometry have been saved automatically to
     userdata (/.googlebook-gpt-backup/).
NEXT
