#!/bin/bash
# install-googlebook-layer.sh
# Build-time assembly for the Googlebook bootc layer. Runs inside the image, not on a
# booted system: this is the only moment when /usr is writable, so everything that
# googlebook-pack-slotb would otherwise install at pack time has to be placed here.
set -euxo pipefail

KVER="$(rpm -q --qf '%{VERSION}-%{RELEASE}.%{ARCH}' kernel-core)"
TOOLKIT=/usr/local/share/googlebook-dualboot

# --- out-of-tree modules built in the builder stage against this exact kernel ---------
install -d -m0755 "/usr/lib/modules/${KVER}/extra" "/usr/lib/modules/${KVER}/updates"
install -m0644 /tmp/googlebook-mods/extra/*.ko   "/usr/lib/modules/${KVER}/extra/"
install -m0644 /tmp/googlebook-mods/updates/*.ko "/usr/lib/modules/${KVER}/updates/"

# --- firmware the initramfs and the real root both need -------------------------------
# intel_ish_ipc requests the ISH blob before the real root is mounted, so 91-googlebook-
# sensors.conf also pulls this exact path into the initramfs via install_items.
install -D -m0644 "${TOOLKIT}/sensor-mods/firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin" \
  /usr/lib/firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin
install -d -m0755 /usr/lib/firmware/realtek/rt1320
install -m0644 "${TOOLKIT}"/audio-mods/firmware/realtek/rt1320/*.dat \
  /usr/lib/firmware/realtek/rt1320/

depmod -a "${KVER}"

# Fail the build rather than ship a kernel whose modules will not load. A module whose
# vermagic does not match is simply refused at modprobe time, and on this machine that
# means coreboot_table never loads, simpledrm never binds, and Slot B boots to a black
# screen with nothing in the logs to say why.
for ko in "/usr/lib/modules/${KVER}/extra"/*.ko "/usr/lib/modules/${KVER}/updates"/*.ko; do
  got="$(modinfo -F vermagic "$ko" | awk '{print $1}')"
  [[ "$got" == "$KVER" ]] || { echo "FATAL: $ko vermagic '$got' != image kernel '$KVER'" >&2; exit 1; }
done
for want in coreboot_table framebuffer-coreboot; do
  modinfo -k "$KVER" "$want" >/dev/null \
    || { echo "FATAL: $want not resolvable for $KVER after depmod" >&2; exit 1; }
done

# --- ALSA UCM: anchor the SpeakerCodec1 regex so rt721 does not also match rt7210 etc. --
if [[ -f /usr/share/alsa/ucm2/sof-soundwire/rt721.conf ]]; then
  sed -i 's/Regex "(rt721(-sdca)?)"/Regex "^(rt721(-sdca)?)$"/' \
    /usr/share/alsa/ucm2/sof-soundwire/rt721.conf
fi

# --- googleboot-menu binary from the builder stage ------------------------------------
# install-into-initramfs.sh tries `make -B` first and falls back to this prebuilt binary,
# which is the path taken here because the runtime image has no compiler.
install -D -m0755 /tmp/googlebook-mods/bin/googleboot-menu \
  "${TOOLKIT}/googleboot-menu/bin/googleboot-menu"

# --- command-line entry points --------------------------------------------------------
install -d -m0755 /usr/local/bin
for tool in googlebook-pack-slotb googlebook-restore-slotb googlebook-switch-slot \
            googlebook-sysroot-sync mkbootimg; do
  [[ -f "${TOOLKIT}/bin/${tool}" ]] && ln -sf "${TOOLKIT}/bin/${tool}" "/usr/local/bin/${tool}"
done
ln -sf "${TOOLKIT}/googleboot-menu/bin/googleboot-menu" /usr/local/bin/googleboot-menu
ln -sf "${TOOLKIT}/googleboot-menu/bin/googleboot-scan" /usr/local/bin/googleboot-scan
ln -sf /usr/local/bin/googlebook-switch-slot /usr/local/bin/boot-android
# COPY keeps the source modes, so the toolkit's own scripts are already 0755; this only
# makes sure nothing in the tree is unreadable to a non-root user.
chmod -R a+rX "${TOOLKIT}"

# --- hwdb: fold the Moonstone Vivaldi keymap into the image's binary index -------------
# udev looks for /etc/udev/hwdb.bin before /usr/lib/udev/hwdb.bin, and the base image
# ships a pre-built copy in /etc. Regenerating only the /usr index leaves that stale copy
# shadowing it, so the Moonstone keymap never resolves - verified with `systemd-hwdb
# query`. Drop the /etc copy and keep one current index in /usr, which is where
# image-provided content belongs and saves ~14 MiB. An admin who later runs
# `systemd-hwdb update` regenerates /etc/udev/hwdb.bin from every source file, ours
# included, so that path stays correct too.
rm -f /etc/udev/hwdb.bin
systemd-hwdb --usr update
systemd-hwdb --usr --root=/ query "evdev:atkbd:dmi:bvnA:bvrB:bdC:br1:svnGoogle:pnMoonstone:pvr1" \
  | grep -q KEYBOARD_KEY_91 \
  || { echo "FATAL: Moonstone hwdb entry does not resolve after update" >&2; exit 1; }

systemctl enable googlebook-gpu-init.service googlebook-slotb-sync.path
systemctl set-default graphical.target

# /var must be empty in a bootc image; tmpfiles.d/googlebook.conf recreates what we need.
find /var -mindepth 1 -delete
