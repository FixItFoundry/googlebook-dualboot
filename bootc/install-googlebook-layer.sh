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

# --- the dracut module that mounts /usr at boot ---------------------------------------
# /usr on this image is a composefs, and only ostree-prepare-root mounts it. It reaches the
# Slot B initramfs through the dracut "ostree" module, which 92-googlebook-bootc.conf names
# in add_dracutmodules and force_add_dracutmodules. If a base image ever stops shipping it,
# the failure is the quiet kind: every pack still succeeds, Slot B still boots, and
# switch-root lands in the bare subvolume running whatever userspace was there before, with
# nothing logged anywhere. Fail the build rather than ship that.
ls -d /usr/lib/dracut/modules.d/*ostree >/dev/null 2>&1 \
  || { echo "FATAL: no ostree dracut module in this image; Slot B would boot the wrong userspace" >&2; exit 1; }
test -x /usr/lib/ostree/ostree-prepare-root \
  || { echo "FATAL: /usr/lib/ostree/ostree-prepare-root missing; nothing would mount /usr" >&2; exit 1; }
grep -q 'force_add_dracutmodules.*ostree' /usr/lib/dracut/dracut.conf.d/92-googlebook-bootc.conf \
  || { echo "FATAL: 92-googlebook-bootc.conf no longer forces the ostree dracut module" >&2; exit 1; }

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

# bootupd manages GRUB inside an EFI System Partition. This machine has no ESP and no GRUB:
# Depthcharge loads the kernel directly from the Android boot partitions, which is what
# googlebook-pack-slotb writes. bootupctl update therefore has nothing to act on and fails
# on every boot, which is noise that hides real failures in `systemctl --failed`. Masking is
# the honest state - there is no bootloader here for it to update.
# Masked in /usr, not via `systemctl mask`, which writes /etc/systemd/system/<unit> -> /dev/null.
# This is the image's decision about this hardware, not local administrator state, so it
# belongs in /usr where it is carried by the image rather than by ostree's 3-way /etc merge.
# systemd treats a unit symlinked to /dev/null in either location as masked; an administrator
# can still override it from /etc. Verified: is-enabled reports "masked" with /etc untouched.
ln -sf /dev/null /usr/lib/systemd/system/bootloader-update.service

# gnome-software cannot manage this system, and trying costs a polkit prompt on every login.
# It ships no bootc plugin, so it reaches for rpm-ostreed instead: observed in the field as
#   rpm-ostree[...]: Initiated txn RefreshMd for client(id:gnome-software ... uid:1000)
# followed by metadata downloads from every repo in /etc/yum.repos.d - the 1Password repo and
# the PyCharm COPR included, which exist for build-time installs and have no business being
# fetched at login. Worse than the nuisance: the step past RefreshMd is package layering,
# which does not mix with bootc. The OS is updated with `bootc upgrade`, after which
# googlebook-slotb-sync.path repacks Slot B.
#
# Removing both package backends leaves appstream, flatpak and fwupd, so Software still
# installs applications and still reports firmware updates - which is the role it should have
# here. Globbed because the directory carries gnome-software's plugin ABI version
# (plugins-23 at the time of writing) and that moves with the base image.
rm -f /usr/lib64/gnome-software/plugins-*/libgs_plugin_dnf5.so \
      /usr/lib64/gnome-software/plugins-*/libgs_plugin_rpm-ostree.so
# Assert the ones that give Software its remaining purpose are still there, so a base image
# that reorganises the plugin directory fails the build instead of shipping a useless GUI.
for want in appstream flatpak fwupd; do
  ls /usr/lib64/gnome-software/plugins-*/libgs_plugin_${want}.so >/dev/null \
    || { echo "FATAL: gnome-software $want plugin missing after prune" >&2; exit 1; }
done
systemctl set-default graphical.target

# /var must be empty in a bootc image; tmpfiles.d/googlebook.conf recreates what we need.
find /var -mindepth 1 -delete
