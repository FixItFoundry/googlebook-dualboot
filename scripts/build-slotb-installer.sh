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
export PATH="/usr/sbin:/sbin:$PATH"

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
elif VMLINUZ=$(first_file "$ISO_DIR"/install.amd/vmlinuz "$ISO_DIR"/linux) &&
     INITRD=$(first_file "$ISO_DIR"/install.amd/gtk/initrd.gz "$ISO_DIR"/install.amd/initrd.gz "$ISO_DIR"/initrd.gz); then
  MODE="debian-installer"
else
  echo "Error: Unrecognized ISO layout in $ISO_DIR" >&2
  exit 1
fi

ISO_LABEL="${SOURCE#LABEL=}"
if [[ "$SOURCE" == "usb" || "$SOURCE" == "auto" || -z "$SOURCE" ]]; then
  ISO_LABEL=$(grep -rhoE '(CDLABEL|LABEL|archisolabel)=[^ "]+' "$ISO_DIR"/{EFI,boot,isolinux,loader,arch} 2>/dev/null | head -1 | cut -d= -f2 | sed 's/\\x20/ /g' || true)
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
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$img" "$WORK/early.cpio" "$WORK/stripped.img" <<'PY' 2>/dev/null || true
import re, sys
data = open(sys.argv[1], "rb").read()
pos = 0
while data[pos:pos+6] in (b"070701", b"070702"):
    m = re.search(b"TRAILER!!!\x00+", data[pos:])
    if not m:
        break
    pos += m.end()
    while pos < len(data) and data[pos] == 0:
        pos += 1
if pos > 0 and pos < len(data):
    open(sys.argv[2], "wb").write(data[:pos])
    open(sys.argv[3], "wb").write(data[pos:])
PY
    if [[ -s "$WORK/early.cpio" ]]; then
      (cd "$dst" && cpio -idm --quiet < "$WORK/early.cpio" 2>/dev/null || true)
      stream="$WORK/stripped.img"
    fi
  elif [[ -x /usr/lib/dracut/skipcpio ]]; then
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

# Register early Coreboot framebuffer modules across initramfs-tools (/conf/modules),
# mkinitcpio (/config), and systemd/dracut (/etc/modules-load.d)
[[ -d "$WORK/unpacked/conf" ]] && printf "coreboot_table\nframebuffer-coreboot\n" >> "$WORK/unpacked/conf/modules"
[[ -f "$WORK/unpacked/config" ]] && printf '\nMODULES="${MODULES:+$MODULES }coreboot_table framebuffer-coreboot"\n' >> "$WORK/unpacked/config"
mkdir -p "$WORK/unpacked/etc/modules-load.d"
printf "coreboot_table\nframebuffer-coreboot\n" > "$WORK/unpacked/etc/modules-load.d/coreboot-fb.conf"

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
for fwdir in "$WORK/unpacked/usr/lib/firmware" "$WORK/unpacked/lib/firmware"; do
  [[ -d "$fwdir" && ! -L "$fwdir" ]] || continue
  find "$fwdir" -mindepth 1 -maxdepth 1 \
    ! -name 'rtl_nic' ! -name 'intel' ! -name 'regulatory.db*' \
    -exec rm -rf {} + 2>/dev/null || true
  if [[ -d "$fwdir/intel" ]]; then
    find "$fwdir/intel" -mindepth 1 -maxdepth 1 ! -name 'ish' -exec rm -rf {} + 2>/dev/null || true
  fi
done

# Prune Stage 1 userspace bloat not needed before mounting Stage 2 / Live squashfs
rm -rf "$WORK/unpacked/usr/lib64/python3"* \
       "$WORK/unpacked/usr/lib/python3"* \
       "$WORK/unpacked/usr/lib64/libpython3"* \
       "$WORK/unpacked/usr/bin/python3"* \
       "$WORK/unpacked/usr/share/X11" \
       "$WORK/unpacked/usr/share/fonts" \
       "$WORK/unpacked/usr/lib64/ossl-modules/fips.so" \
       "$WORK/unpacked/usr/lib/udev/prefixdevname" \
       "$WORK/unpacked/usr/lib/kbd/consolefonts/partialfonts" \
       "$WORK/unpacked/usr/bin"/{pdata_tools,lvm,unbound,unbound-checkconf,unbound-control,vi,nvme,btrfs,btrfsck,btrfstune,ldconfig,xfs_repair,xfs_db,mdadm,mdmon,iscsiadm,iscsid,nft,dd_extract,dd_list} \
       "$WORK/unpacked/usr/lib64"/{libstdc++.so*,librpm*.so*,libsqlite3.so*,libnftables.so*,libfreeblpriv3.*,libisns.so*} 2>/dev/null || true

if [[ -d "$WORK/unpacked/usr/lib/kbd/keymaps/xkb" ]]; then
  find "$WORK/unpacked/usr/lib/kbd/keymaps/xkb" -type f ! -name 'us*' -delete 2>/dev/null || true
fi
if [[ -d "$WORK/unpacked/usr/lib/kbd/consolefonts" ]]; then
  find "$WORK/unpacked/usr/lib/kbd/consolefonts" -type f \
    ! -name 'latarcyrheb*' ! -name 'eurlatgr*' ! -name 'default*' -delete 2>/dev/null || true
fi
for pks in "$WORK/unpacked/usr/bin/parse-kickstart" "$WORK/unpacked/usr/lib/dracut/modules.d/80anaconda/parse-kickstart"; do
  if [[ -f "$pks" ]]; then
    cat > "$pks" <<'PKSEOF'
#!/usr/bin/bash
TMPDIR="/tmp"
if [ "$1" = "--tmpdir" ]; then
    TMPDIR="$2"
    shift 2
fi
ksfile="$1"
[ -f "$ksfile" ] || exit 0
cp -f "$ksfile" "$TMPDIR/ks.cfg.parsed"
echo "parsed_kickstart=\"$TMPDIR/ks.cfg.parsed\"" >> "$TMPDIR/ks.info"
grep -Eq "^[[:space:]]*text([[:space:]]|$)" "$ksfile" 2>/dev/null && echo "inst.text"
exit 0
PKSEOF
    chmod 755 "$pks"
  fi
done

# Prune non-Moonstone kernel drivers from Stage 1 initramfs
KMOD="$WORK/unpacked/usr/lib/modules/$KVER/kernel"
rm -rf "$KMOD/drivers"/{ata,bluetooth,crypto,firewire,gpu,infiniband,isdn,media,mmc,mtd,nvdimm,parport,pcmcia,soundwire,staging,target,ufs,watchdog} \
       "$KMOD/drivers/block"/{mtip32xx,null_blk,rnbd,zram,brd.ko*,floppy.ko*,nbd.ko*,pktcdvd.ko*,rbd.ko*,ublk_drv.ko*,xen-blkfront.ko*,zloop.ko*} \
       "$KMOD/drivers/md"/{bcache,persistent-data,raid*.ko*,dm-raid.ko*,dm-cache*.ko*,dm-thin*.ko*,dm-integrity.ko*,dm-multipath.ko*,dm-verity.ko*,dm-zoned.ko*,dm-clone.ko*,dm-era.ko*,dm-writecache.ko*,dm-log*.ko*,dm-bio-prison.ko*,dm-delay.ko*,dm-dust.ko*,dm-ebs.ko*,dm-flakey.ko*,dm-historical*.ko*,dm-io-affinity.ko*,dm-queue-length.ko*,dm-round-robin.ko*,dm-service-time.ko*,dm-switch.ko*,dm-unstripe.ko*,dm-zero.ko*,faulty.ko*} \
       "$KMOD/drivers/net"/{bonding,caif,can,dsa,fddi,fjes,hamradio,hippi,hyperv,ieee802154,mctp,pcs,ppp,slip,team,vmxnet3,vxlan,wan,wireless,wwan,xen-netback,geneve.ko*,macsec.ko*,macvlan.ko*,net_failover.ko*,ntb_netdev.ko*,sungem_phy.ko*,tun.ko*,veth.ko*,virtio_net.ko*,vrf.ko*,wireguard.ko*,xen-netfront.ko*} \
       "$KMOD/fs"/{afs,ceph,cifs,dlm,exfat,f2fs,fuse,gfs2,jffs2,jfs,lockd,netfs,nfs,nfs_common,nfsd,nilfs2,ntfs3,ocfs2,pstore,smb,ubifs,xfs} \
       "$KMOD/net"/{8021q,bluetooth,bridge,can,ceph,dccp,dsa,ieee802154,l2tp,mac80211,mpls,netfilter,nfc,openvswitch,rds,rfkill,rxrpc,sched,sctp,smc,sunrpc,tipc,vmw_vsock,wireless,xdp,xfrm} \
       "$KMOD/sound" 2>/dev/null || true

if [[ -d "$KMOD/drivers/hid" ]]; then
  find "$KMOD/drivers/hid" -maxdepth 1 -type f -name 'hid-*.ko*' \
    ! -name 'hid-generic*' ! -name 'hid-multitouch*' ! -name 'hid-sensor*' ! -name 'hid-google*' \
    -delete 2>/dev/null || true
fi
if [[ -d "$KMOD/drivers/net/ethernet" ]]; then
  find "$KMOD/drivers/net/ethernet" -mindepth 1 -maxdepth 1 ! -name 'realtek' -exec rm -rf {} + 2>/dev/null || true
fi
if [[ -d "$KMOD/drivers/net/phy" ]]; then
  find "$KMOD/drivers/net/phy" -maxdepth 1 -type f -name '*.ko*' \
    ! -name 'libphy*' ! -name 'fixed_phy*' ! -name 'mdio*' -delete 2>/dev/null || true
fi
if [[ -d "$KMOD/drivers/scsi" ]]; then
  find "$KMOD/drivers/scsi" -mindepth 1 -maxdepth 1 \
    ! -name 'device_handler' ! -name 'libsas' ! -name 'sd_mod*' ! -name 'sr_mod*' \
    ! -name 'scsi_mod*' ! -name 'scsi_common*' ! -name 'sg*' ! -name 'virtio_scsi*' \
    -exec rm -rf {} + 2>/dev/null || true
fi
if [[ -d "$KMOD/fs/nls" ]]; then
  find "$KMOD/fs/nls" -maxdepth 1 -type f -name '*.ko*' \
    ! -name 'nls_utf8*' ! -name 'nls_iso8859*' ! -name 'nls_cp437*' ! -name 'nls_ascii*' \
    -delete 2>/dev/null || true
fi

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

# Decompress individually compressed modules (.ko.xz/.ko.zst/.ko.gz -> .ko) so outer zstd deduplicates across all modules
find "$WORK/unpacked/usr/lib/modules/$KVER" -type f -name '*.ko.xz' -exec xz -d {} + 2>/dev/null || true
find "$WORK/unpacked/usr/lib/modules/$KVER" -type f -name '*.ko.zst' -exec zstd -d --rm -q {} + 2>/dev/null || true
find "$WORK/unpacked/usr/lib/modules/$KVER" -type f -name '*.ko.gz' -exec gzip -d {} + 2>/dev/null || true
strip --strip-debug "$WORK/unpacked/usr/lib/modules/$KVER/extra"/*.ko 2>/dev/null || true

if [[ -d "$WORK/unpacked/var/lib/dracut/hooks/cmdline" ]]; then
  cat > "$WORK/unpacked/var/lib/dracut/hooks/cmdline/00-googlebook-early.sh" <<EOF
#!/usr/bin/bash
modprobe coreboot_table 2>/dev/null || insmod /lib/modules/$KVER/extra/coreboot_table.ko 2>/dev/null || true
modprobe framebuffer-coreboot 2>/dev/null || insmod /lib/modules/$KVER/extra/framebuffer-coreboot.ko 2>/dev/null || true
EOF
  chmod 755 "$WORK/unpacked/var/lib/dracut/hooks/cmdline/00-googlebook-early.sh"
fi
if [[ -d "$WORK/unpacked/var/lib/dracut/hooks/pre-pivot" ]]; then
  cat > "$WORK/unpacked/var/lib/dracut/hooks/pre-pivot/98-googlebook-setup.sh" <<EOF
#!/usr/bin/bash
if [ -d /sysroot/usr ]; then
    mkdir -p /sysroot/usr/lib/modules/$KVER/extra 2>/dev/null || true
    cp -f /lib/modules/$KVER/extra/*.ko /sysroot/usr/lib/modules/$KVER/extra/ 2>/dev/null || true
    chroot /sysroot depmod -a $KVER 2>/dev/null || true
fi
EOF
  chmod 755 "$WORK/unpacked/var/lib/dracut/hooks/pre-pivot/98-googlebook-setup.sh"
fi

depmod -b "$WORK/unpacked" -a "$KVER" 2>/dev/null || depmod -b "$WORK/unpacked/usr" -a "$KVER" 2>/dev/null || true
if command -v python3 >/dev/null 2>&1; then
  python3 - "$WORK/unpacked/usr/lib/modules/$KVER" <<'PY' 2>/dev/null || true
import os, re, struct, sys

mdir = sys.argv[1]
if not os.path.isdir(mdir):
    sys.exit(0)

# Collect existing .ko relative paths and module names
rel_by_mod = {}
for root, _, files in os.walk(mdir):
    for f in files:
        if f.endswith(".ko"):
            rel = os.path.relpath(os.path.join(root, f), mdir).replace("\\", "/")
            mod = f[:-3].replace("-", "_")
            rel_by_mod[mod] = rel

dep_path = os.path.join(mdir, "modules.dep")
dep_bin_path = os.path.join(mdir, "modules.dep.bin")
need_rebuild = not os.path.exists(dep_bin_path)
if os.path.exists(dep_bin_path):
    raw = open(dep_bin_path, "rb").read()
    if b".ko.xz" in raw or b".ko.zst" in raw or b".ko.gz" in raw or (
        "coreboot_table" in rel_by_mod and b"coreboot_table" not in raw
    ):
        need_rebuild = True

if need_rebuild and os.path.exists(dep_path):
    def strip_ext(p):
        return re.sub(r"\.ko\.(xz|zst|gz)$", ".ko", p)

    dep_lines = []
    dep_index = {}
    seen = set()
    for line in open(dep_path, errors="replace").read().splitlines():
        if ":" not in line:
            continue
        lhs, rhs = line.split(":", 1)
        lhs = strip_ext(lhs.strip())
        mod = os.path.basename(lhs)[:-3].replace("-", "_")
        if mod not in rel_by_mod:
            continue
        lhs = rel_by_mod[mod]
        deps = []
        for d in rhs.split():
            d = strip_ext(d.strip())
            dmod = os.path.basename(d)[:-3].replace("-", "_")
            if dmod in rel_by_mod:
                deps.append(rel_by_mod[dmod])
        val = f"{lhs}: {' '.join(deps)}" if deps else f"{lhs}:"
        dep_lines.append(val)
        dep_index[mod] = [(0, val)]
        seen.add(mod)

    extra_deps = {
        "coreboot_table": [],
        "framebuffer_coreboot": ["coreboot_table"],
        "cros_ec_ishtp": ["intel_ishtp", "cros_ec", "cros_ec_proto"],
    }
    for mod, reqs in extra_deps.items():
        if mod in rel_by_mod and mod not in seen:
            deps = [rel_by_mod[r] for r in reqs if r in rel_by_mod]
            val = f"{rel_by_mod[mod]}: {' '.join(deps)}" if deps else f"{rel_by_mod[mod]}:"
            dep_lines.append(val)
            dep_index[mod] = [(0, val)]

    open(dep_path, "w").write("\n".join(dep_lines) + ("\n" if dep_lines else ""))

    def build_kmod_index(entries):
        trie = {}
        for k, vals in entries.items():
            if not k:
                continue
            node = trie
            for ch in k.encode():
                node = node.setdefault("c", {}).setdefault(ch, {})
            node["v"] = vals
        buf = bytearray(b"\xb0\x07\xf4\x57\x00\x02\x00\x01\x00\x00\x00\x00")
        def emit(node):
            pref = bytearray()
            while "v" not in node and len(node.get("c", {})) == 1 and node is not trie:
                ch, nxt = next(iter(node["c"].items()))
                pref.append(ch)
                node = nxt
            childs = node.get("c", {})
            c_offs = {ch: emit(childs[ch]) for ch in sorted(childs)}
            pos = len(buf)
            flags = 0
            if pref:
                flags |= 0x80000000
                buf.extend(pref + b"\x00")
            if childs:
                flags |= 0x20000000
                first, last = min(c_offs), max(c_offs)
                buf.extend(bytes([first, last]))
                for ch in range(first, last + 1):
                    buf.extend(struct.pack(">I", c_offs.get(ch, 0)))
            if "v" in node:
                flags |= 0x40000000
                vals = node["v"]
                buf.extend(struct.pack(">I", len(vals)))
                for prio, v in vals:
                    buf.extend(struct.pack(">I", prio) + v.encode() + b"\x00")
            return flags | pos
        root_off = emit(trie) if trie else 0
        struct.pack_into(">I", buf, 8, root_off)
        return bytes(buf)

    open(dep_bin_path, "wb").write(build_kmod_index(dep_index))

    for fname, extra_aliases in (
        ("modules.alias", [
            ("acpi*:BOOT0000:*", "coreboot_table"),
            ("acpi*:GOOGCB00:*", "coreboot_table"),
            ("coreboot:t00000012", "framebuffer_coreboot"),
            ("corebootdrm", "framebuffer_coreboot"),
            ("framebuffer_coreboot", "framebuffer_coreboot"),
            ("ishtp:{7B7154D0-56F4-4BDC-B0D8-9E7CDAE0D6A0}", "cros_ec_ishtp"),
        ]),
        ("modules.symbols", []),
    ):
        fpath = os.path.join(mdir, fname)
        if not os.path.exists(fpath):
            continue
        kept_lines = []
        idx = {}
        for line in open(fpath, errors="replace").read().splitlines():
            if not line.startswith("alias "):
                kept_lines.append(line)
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            alias_key, mod = parts[1], parts[2].replace("-", "_")
            if mod in rel_by_mod:
                kept_lines.append(line)
                lst = idx.setdefault(alias_key, [])
                lst.append((len(lst), mod))
        for alias_key, mod in extra_aliases:
            if mod in rel_by_mod:
                kept_lines.append(f"alias {alias_key} {mod}")
                lst = idx.setdefault(alias_key, [])
                if all(m != mod for _, m in lst):
                    lst.append((len(lst), mod))
        open(fpath, "w").write("\n".join(kept_lines) + "\n")
        open(fpath + ".bin", "wb").write(build_kmod_index(idx))
PY
fi

mv "$WORK/unpacked/usr/lib/modules" "$WORK/split_init/usr/lib/modules"

pack_ramdisks() {
  (cd "$WORK/unpacked" && find . -print0 | cpio --null -o -H newc --quiet | (zstd --ultra -22 -T0 2>/dev/null || zstd -19 -T0) > "$OUT_DIR/rd_vendor.img")
  (cd "$WORK/split_init" && find . -print0 | cpio --null -o -H newc --quiet | (zstd --ultra -22 -T0 2>/dev/null || zstd -19 -T0) > "$OUT_DIR/rd_init.img")
  VSZ=$(( $(wc -c < "$OUT_DIR/rd_vendor.img") ))
  ISZ=$(( $(wc -c < "$OUT_DIR/rd_init.img") ))
}
pack_ramdisks

# Both rd_vendor.img and rd_init.img are unpacked into the same / rootfs by the kernel before /init starts.
# If one partition overflows while the other has spare capacity, balance directories across the split.
if (( ISZ > 8380416 && VSZ < 33488896 )); then
  for d in "$WORK/split_init/usr/lib/modules/$KVER/kernel/drivers"/*; do
    [[ -d "$d" ]] || continue
    mkdir -p "$WORK/unpacked/usr/lib/modules/$KVER/kernel/drivers"
    mv "$d" "$WORK/unpacked/usr/lib/modules/$KVER/kernel/drivers/"
    pack_ramdisks
    (( ISZ <= 8380416 || VSZ >= 33488896 )) && break
  done
elif (( VSZ > 33488896 && ISZ < 8380416 )); then
  for rel in usr/lib/kbd usr/share usr/libexec; do
    [[ -d "$WORK/unpacked/$rel" ]] || continue
    mkdir -p "$(dirname "$WORK/split_init/$rel")"
    mv "$WORK/unpacked/$rel" "$WORK/split_init/$rel"
    pack_ramdisks
    (( VSZ <= 33488896 || ISZ >= 8380416 )) && break
  done
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
      STAGE_ARGS="archisobasedir=arch archiso_http_srv=${SOURCE%/}/ ip=::::::dhcp cow_spacesize=4G"
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
  debian-installer)
    STAGE_ARGS="vga=normal fb=true"
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
