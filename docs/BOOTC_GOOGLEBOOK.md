# Running the Googlebook on a bootc Image

This covers moving the Acer Googlebook 14 (`GP714-91N`, board `Moonstone` / `Google_Fatcat`)
from a package-based Fedora install to an image-based [bootc](https://bootc-dev.github.io/bootc/)
one, and how Slot A/B switching works once you are there.

The image is built by [`bootc/Containerfile`](../bootc/Containerfile) on top of
`ghcr.io/dsmk/bootc-image-laptop`. Slot B boot itself is unchanged: Depthcharge still reads
`boot_b` / `init_boot_b` / `vendor_boot_b`, [`googleboot-menu`](../googleboot-menu/README.md)
still draws the selector, and [`googlebook-pack-slotb`](../bin/googlebook-pack-slotb) still
writes the images. What changes is *when* the hardware enablement is assembled.

---

## 1. Why This Needs a Different Shape

On a package-based install, `googlebook-pack-slotb` does two jobs at once: it builds and
installs the hardware enablement (the out-of-tree modules, the ISH and RT1320 firmware, the
ALSA UCM override) and then packs Slot B. On a bootc system the first job is impossible at
runtime and unnecessary at build time:

- **`/usr` is immutable.** `/usr/lib/ostree/prepare-root.conf` in the base image contains
  `[composefs] enabled = yes` and `[sysroot] readonly = true`. `/usr` is a composefs image,
  not a read-only bind mount, so it cannot be remounted `rw` at all. Every module,
  firmware blob and UCM file therefore has to be placed while the container image is being
  built. `/etc` and `/var` stay writable, which is why the dracut, modprobe and
  `modules-load.d` drop-ins still work the way they always did.

- **The kernel is pinned to the image.** This removes the toolkit's single worst failure
  mode. On the package-based path, a kernel update whose `kernel-devel` is missing makes the
  silenced build branch in `googlebook-pack-slotb` fall through quietly, and you only find
  out when Slot B boots to a black screen. Here the modules are compiled in a builder stage
  against the image's own `kernel-core`, and
  [`install-googlebook-layer.sh`](../bootc/install-googlebook-layer.sh) **fails the build**
  if any `.ko`'s `vermagic` does not match or if `coreboot_table` is not resolvable after
  `depmod`. A black-screen image cannot be published.

- **The root filesystem is named differently.** ostree identifies a deployment to the
  initramfs with an `ostree=/ostree/boot.<n>/<stateroot>/<bootcsum>/<serial>` kernel
  argument. `ostree-remount.service` is gated on `ConditionKernelCommandLine=ostree`, and
  the `ostree` dracut module does nothing without it. The package-based command line is
  built from `findmnt`, which on a deployment yields neither a usable `root=` (`/` is an
  overlay with no UUID of its own) nor the `ostree=` argument. So on these systems the
  packer reads the command line out of the Boot Loader Specification entry that ostree
  writes under `/boot/loader/entries`, which already carries `root=`, `ostree=` and any
  `rootflags=`, and appends only the Googlebook arguments on top.

`googlebook-pack-slotb` detects all of this at runtime by probing whether `/usr` is
writable and whether an ostree deployment is present, so the same script still behaves
exactly as before on the Fedora Server install you are migrating from.

### Which BLS entry is "the one"

libostree names entries `ostree-<n>[-<stateroot>].conf` where `n = n_deployments - index`,
writes `version` to that same number, and appends `:<index>` to `title`. Index 0 is the
deployment that boots next, so it gets the **highest** version and a title ending in `:0)`.
The packer matches that title marker first and falls back to the highest `version`.

Boot counting changes the filename (`ostree-2+3.conf`, `ostree-1+2-1.conf`), which is why
the selection keys off the file's contents rather than its name.

The kernel path in the `linux` field comes in two forms depending on ostree's
`enable_bootprefix`: root-relative (`/boot/ostree/<stateroot>-<bootcsum>/vmlinuz-<kver>`) or
relative to the `/boot` mount (`/ostree/...`). The packer tries the literal path first and
the `/boot`-prefixed form second, so it handles both. Some layouts also carry a
`/boot/boot -> .` self-symlink that makes the wrong form resolve anyway; this does not rely
on it.

Arguments the entry carries that the toolkit does not manage — `boot=UUID=…` among them —
are passed through untouched. Only the ones the packer sets itself (`rw`, `console=tty0`,
`quiet`, `rhgb`, `bootconfig`, `clk_ignore_unused`, `rd.driver.pre=`) are stripped before
being re-added, so nothing is specified twice.

### What has been verified, and what has not

Checked against a real ostree deployment and the built image:

- Entry selection picks the deployment whose `ostree=` matches the booted `/proc/cmdline`,
  on a live two-deployment ostree system, in both naming schemes and with boot counting.
- Kernel path resolution and `<kver>` extraction, in both `linux`-field forms.
- The derived command line keeps `root=`, `ostree=` and `rootflags=subvol=` and contains no
  duplicated arguments.
- All five out-of-tree modules build against the base image's kernel, carry a matching
  `vermagic`, and resolve through `modprobe` after `depmod`.
- The Moonstone hwdb entry resolves via `systemd-hwdb query`.

**Not yet verified:** an end-to-end boot of this image on the Googlebook. The Slot B pack
itself has only been exercised on the package-based Fedora Server install.

---

## 2. Building the Image

Build from the **repository root** — the layer needs the module sources and the boot menu
that live beside it:

```bash
podman build -f bootc/Containerfile -t ghcr.io/dsmk/bootc-image-googlebook:main .
podman push ghcr.io/dsmk/bootc-image-googlebook:main
```

To build against a different base:

```bash
podman build -f bootc/Containerfile \
  --build-arg BASE_IMAGE=ghcr.io/dsmk/bootc-image-base:main \
  -t ghcr.io/dsmk/bootc-image-googlebook:server .
```

The build will stop with `FATAL: ... vermagic ... != image kernel ...` if the base image's
kernel and the `kernel-devel` resolved for it ever drift apart. That is the intended
behaviour; do not work around it by relaxing the check.

---

## 3. Path A — Migrating an Existing Fedora Server Install in Place

This keeps your partitioning: ext4 `/boot` on partition 11, Btrfs root on partition 32, the
Android Slot B images on p14 / p19 / p21, and the 120 GiB `userdata` split. It is also
reversible, because `--replace alongside` leaves the old root's files in place and the old
Slot B images are a file copy away.

### Step 1 — Save the current Slot B images off the machine

**`bootc install to-existing-root` wipes `/boot`,** which is where `slotb-backup` lives.
Copy it somewhere else *first*; it is what gets you back into Fedora Server if the migration
does not boot.

```bash
# From your workstation:
scp -r googlebook:/boot/slotb-backup ./slotb-backup-fedora-server
```

### Step 2 — Install the image alongside the running system

```bash
sudo podman run --rm --privileged --pid=host \
  --security-opt label=disable \
  -v /var/lib/containers:/var/lib/containers \
  -v /dev:/dev \
  ghcr.io/dsmk/bootc-image-googlebook:main \
  bootc install to-existing-root
```

This creates `/ostree` on the existing root, deploys the image into it, and writes BLS
entries to `/boot`. It does **not** touch the bootloader the Googlebook actually uses —
Depthcharge reads the Android partitions, not `/boot` — so at this point the machine still
boots the old Fedora Server, and will keep doing so until you complete step 3.

Confirm the entry carries both arguments before going further:

```bash
grep -h '^options' /boot/loader/entries/*.conf
```

You want to see `root=UUID=…` **and** `ostree=/ostree/boot.…` on the same line. If
`ostree=` is absent, stop — packing would produce an image that drops to a dracut
emergency shell.

### Step 3 — Pack Slot B from inside the new deployment

The pack has to run *inside* the deployment, not from the Fedora Server host. The host's
dracut has no `ostree` module (that comes from the image), so an initramfs built there
cannot act on `ostree=` no matter how correct the command line is.

```bash
# head -1 takes whichever deployment sorts first, which is not necessarily the one the
# default boot entry uses. Check the count before trusting it; with more than one, pick the
# deployment deliberately (ostree admin status, or match the commit in ostree-<n>.conf).
ls -d /ostree/deploy/*/deploy/*.0
DEPLOY=$(ls -d /ostree/deploy/*/deploy/*.0 | head -1); echo "$DEPLOY"
STATEROOT=$(dirname "$(dirname "$DEPLOY")")        # /ostree/deploy/<stateroot>

# A bootc image ships an empty /var on purpose, and systemd-tmpfiles has not run in this
# deployment yet, so the directories dracut and the toolkit expect are simply absent.
# Create them before chrooting; the deployment is a plain directory tree until it boots.
sudo mkdir -p "$STATEROOT/var/tmp" "$STATEROOT/var/roothome" "$STATEROOT/var/lib"
sudo chmod 1777 "$STATEROOT/var/tmp"
sudo chmod 0700 "$STATEROOT/var/roothome"

for p in dev proc sys run; do
  sudo mount --rbind "/$p" "$DEPLOY/$p"
  sudo mount --make-rslave "$DEPLOY/$p"
done
sudo mount --bind "$STATEROOT/var" "$DEPLOY/var"
sudo mount --bind /boot            "$DEPLOY/boot"
sudo mount --bind /                "$DEPLOY/sysroot"

sudo chroot "$DEPLOY" /usr/local/bin/googlebook-pack-slotb
```

**Relabel `/etc` before rebooting.** An in-place conversion can leave files in `/etc` with no
SELinux label at all, and a confined domain cannot even `getattr` an `unlabeled_t` file — so
the failure arrives as "Permission denied" from a process running as root, which is easy to
misread as a mode or read-only problem:

```bash
sudo find /etc -context '*unlabeled_t*' 2>/dev/null   # see the extent
sudo restorecon -Rv /etc
```

Observed on a converted machine: `/etc/udev/hwdb.bin` was `unlabeled_t` while `/etc/udev`
itself was correctly `etc_t`, so `restorecon` without `-R` reported nothing to do. That one
file took out `systemd-hwdb-update.service`, which failed four times in three seconds and
left a 105-byte stub that udev then refused to parse — and udev does *not* fall back to
`/usr/lib/udev/hwdb.bin` when the `/etc` copy is unparseable, so the entire Vivaldi top row
went with it. `colord` was denied `read` on the same inode.

Check the output. It should name the boot entry it chose, show a command line containing
`ostree=`, and report both ramdisks inside their limits:

```
[googlebook-slotb] ostree deployment; default boot entry /boot/loader/entries/ostree-1.conf
[googlebook-slotb] cmdline: root=UUID=… ostree=/ostree/boot.1/fedora/…/0 rw console=tty0 …
[googlebook-slotb] rd_vendor: … bytes (limit 33488896), rd_init: … bytes (limit 8380416)
[googlebook-slotb] wrote boot_b.img -> /dev/nvme0n1p14
```

Then unmount, in reverse:

```bash
sudo umount "$DEPLOY/sysroot" "$DEPLOY/boot" "$DEPLOY/var"
for p in run sys proc dev; do sudo umount -R "$DEPLOY/$p"; done
sync
```

### Step 4 — Reboot

```bash
sudo reboot
```

Slot B is already active, so nothing needs switching. You should get the Acer logo →
Depthcharge → `googleboot-menu` → the Fedora entry → GNOME.

### Step 5 — Reclaim the old root (only once you are happy)

The old Fedora Server files are still on the Btrfs root, outside `/ostree`. Leave them
until you have rebooted into bootc successfully a few times — while they are there, writing
`slotb-backup-fedora-server` back to Slot B returns you to the old system. When you are
ready:

```bash
sudo ostree admin undeploy 1   # if an old non-ostree deployment is still listed
# then remove the stale top-level directories from the pre-bootc install
```

Inspect before deleting anything. `/ostree`, `/boot`, `/home`, `/var` and the deployment
roots must all stay.

### Rolling back to Fedora Server

If the migration does not boot, nothing is lost:

```bash
./bin/googlebook-restore-slotb --fastboot tcp:<ETH_IP> ./slotb-backup-fedora-server --reboot
```

or, from the Slot B installer, `dd` the three images back onto p14 / p19 / p21. The old root
subvolume is untouched, so Fedora Server comes straight back.

---

## 4. Path B — Installing From Scratch

Use this for a new or wiped disk. Follow
[README Step 1](../README.md#step-1-boot-the-slot-b-installer) and
[Step 2](../README.md#step-2-prepare-unallocated-space-1-tb--2-tb-nvme-upgrade-or-stock-512-gb-ssd)
unchanged to boot the Slot B installer and free up space, then — instead of running
anaconda — partition and install the image directly:

```bash
# ext4 /boot on partition 11, Btrfs root on partition 32, as the toolkit expects.
sudo mkfs.ext4 -L boot /dev/nvme0n1p11
sudo mkfs.btrfs -f -L fedora /dev/nvme0n1p32
sudo mount /dev/nvme0n1p32 /mnt
sudo btrfs subvolume create /mnt/root
sudo umount /mnt
sudo mount -o subvol=root /dev/nvme0n1p32 /mnt
sudo mkdir -p /mnt/boot
sudo mount /dev/nvme0n1p11 /mnt/boot

sudo podman run --rm --privileged --pid=host \
  --security-opt label=disable \
  -v /var/lib/containers:/var/lib/containers \
  -v /dev:/dev -v /mnt:/mnt \
  ghcr.io/dsmk/bootc-image-googlebook:main \
  bootc install to-filesystem --root-ssh-authorized-keys /dev/null /mnt
```

Then pack Slot B exactly as in Path A step 3, with `DEPLOY` under `/mnt/ostree/deploy/...`
and `/mnt` bind-mounted at `$DEPLOY/sysroot` instead of `/`.

`/boot` **must** be ext4 or vfat: `googleboot-scan` only considers devices that `blkid`
reports as one of those two when it looks for the boot partition.

---

## 5. Switching Between Googlebook OS / Android 17 (Slot A) and Linux (Slot B)

This is the bootc equivalent of [README section 4](../README.md#4-switching-between-googlebook-os--android-17-slot-a-and-linux-slot-b).
**The mechanism is identical** — slot switching is GPT attribute manipulation on `vbmeta_a`
/ `vbmeta_b`, which has nothing to do with how the Linux root filesystem is managed. The
only differences are where the tools live and the one extra step after an upgrade.

### From the graphical `googleboot-menu` bootloader

Unchanged. At every Slot B boot, `googleboot-menu` renders the `2880x1800` selector where
you choose **Fedora / Linux** (with LUKS2 unlock) or **Googlebook OS** (a one-shot reboot
into Slot A that keeps Slot B as the default next boot).

The menu binary is part of the image at
`/usr/local/share/googlebook-dualboot/googleboot-menu/bin/googleboot-menu`, symlinked to
`/usr/local/bin/googleboot-menu`. It is compiled in the builder stage, because the runtime
image has no compiler — `install-into-initramfs.sh` tries `make -B` first and falls back to
that prebuilt binary.

### From the Linux CLI

Identical commands; `/usr/local/bin` is populated by the image rather than by
`googlebook-sysroot-sync`:

```bash
# Check current active boot slot:
sudo googlebook-switch-slot status

# Reboot into Googlebook OS / Android 17 (Slot A):
sudo boot-android a --reboot

# One-shot into Android, falling back to Linux on the next boot:
sudo googlebook-switch-slot android-once --reboot

# Keep Slot B (Linux) active:
sudo googlebook-switch-slot b
```

`googlebook-switch-slot` needs `sgdisk`, which is in the base image, and writes GPT
attributes on `/dev/nvme0n1` — block device access, not a filesystem write, so the immutable
`/usr` makes no difference.

### From the Fastboot bootloader menu

Unchanged:

```bash
fastboot --set-active=b && fastboot reboot   # Linux (Slot B)
fastboot --set-active=a && fastboot reboot   # Googlebook OS / Android 17 (Slot A)
```

### What *is* different: Slot B has to follow image upgrades

On a package-based install, `/etc/kernel/install.d/95-googlebook-slotb.install` re-packs
Slot B whenever a kernel RPM lands. A bootc system never installs a kernel that way —
`bootc upgrade` swaps the whole `/usr` tree and writes a new BLS entry — so the trigger is
that entry changing instead:

| Unit | Role |
|---|---|
| `googlebook-slotb-sync.path` | Watches `/boot/loader.0/entries` and `/boot/loader.1/entries` |
| `googlebook-slotb-sync.service` | Re-packs Slot B when the fingerprint of those entries changes |

Both are enabled in the image. The `.path` unit deliberately watches the two real
directories rather than `/boot/loader/entries`: ostree flips the `/boot/loader` symlink
between `loader.0` and `loader.1` when it swaps boot versions, and an inotify watch set up
through the symlink would keep pointing at whichever directory was current when the watch
was created.

`googlebook-slotb-sync` hashes every boot entry plus the listing of `/boot/ostree`, compares
that against `/var/lib/googlebook/slotb.stamp`, and does nothing if they match — so a normal
boot costs no time. It writes the stamp only *after* the packer has verified its own block
writes, so an interrupted pack is retried on the next boot rather than being recorded as
done.

So a normal upgrade is:

```bash
sudo bootc upgrade          # stages the new image, writes a new BLS entry
journalctl -u googlebook-slotb-sync -f   # watch the re-pack; takes a couple of minutes
sudo reboot
```

Wait for the re-pack to finish before rebooting. If you reboot while Slot B still points at
the previous deployment you simply boot the previous deployment — ostree keeps it — so this
is a delay, not a failure. To force a pack by hand at any time:

```bash
sudo googlebook-slotb-sync --force
```

### Rollback

`bootc rollback` swaps the deployment order, which rewrites the BLS entries, which trips the
path unit, which re-packs Slot B against the rolled-back deployment:

```bash
sudo bootc rollback
journalctl -u googlebook-slotb-sync -f
sudo reboot
```

Keep `/boot/slotb-backup` copied off the machine regardless. It is the only recovery route
that does not depend on the system booting at all.

### Restoring Linux after an Android OTA

Unchanged from
[README](../README.md#restoring-linux-after-an-android-system-update-ota). An Android A/B
OTA writes the updated Android system into Slot B, replacing Linux:

```bash
# From Googlebook OS with Wireless Debugging enabled:
./bin/googlebook-restore-slotb --adb ./slotb-backup --reboot

# From the Bootloader (Fastboot) screen:
./bin/googlebook-restore-slotb --fastboot tcp:<ETH_IP> ./slotb-backup --reboot

# From inside Linux:
sudo /usr/local/bin/googlebook-restore-slotb
```

The self-healing step also still works: `googlebook-gpu-init.service` runs
`googlebook-restore-slotb --auto-post-ota` on each boot, which promotes a verified Android
OTA from Slot B to Slot A and puts Linux back in Slot B.

---

## 6. What the Image Contains

| Path | Contents |
|---|---|
| `/usr/lib/modules/<kver>/extra/` | `coreboot_table.ko`, `framebuffer-coreboot.ko`, `cros_ec_ishtp.ko` |
| `/usr/lib/modules/<kver>/updates/` | `snd-soc-sdw-utils.ko`, `snd-soc-sof-sdw.ko` |
| `/usr/lib/firmware/intel/ish/` | `ish_ptl_62b0f067_501de2f8.bin` |
| `/usr/lib/firmware/realtek/rt1320/` | Android 17 `rt1320_*Google_Moonstone*.dat` calibration files |
| `/usr/share/alsa/ucm2/codecs/rt721+rt1320/` | `init.conf`, plus the anchored `SpeakerCodec1` regex in `sof-soundwire/rt721.conf` |
| `/usr/lib/dracut/dracut.conf.d/` | `90-googlebook-slotb.conf`, `91-googlebook-sensors.conf` |
| `/usr/lib/modprobe.d/`, `/usr/lib/modules-load.d/` | Staged Xe2 load rules, coreboot framebuffer autoload |
| `/usr/lib/udev/hwdb.d/` | `61-googlebook-moonstone.hwdb` (folded into the image's `hwdb.bin`) |
| `/etc/keyd/cros.conf` | Vivaldi top-row remap, for a `keyd` installed from COPR; Fedora 44 does not package it, so on Fedora the top row is handled by the hwdb entry above plus the `EVIOCSKEYCODE` pass in `googlebook-gpu-init` |
| `/usr/local/share/googlebook-dualboot/` | The whole toolkit, including module sources and `googleboot-menu` |
| `/usr/local/bin/` | Symlinks for `googlebook-pack-slotb`, `-restore-slotb`, `-switch-slot`, `-sysroot-sync`, `googleboot-menu`, `googleboot-scan`, `boot-android` |

The toolkit lives in `/usr/local/share` rather than `/var/usrlocal` because on
`fedora-bootc` `/usr/local` is a real directory inside `/usr` — immutable, verity-backed
content that survives the base image's `find /var -mindepth 1 -delete`. That placement also
makes `googlebook-pack-slotb` see its own `SELF_DIR` as
`/usr/local/share/googlebook-dualboot` and skip the self-install step, which would otherwise
try to write to a read-only `/usr`.

---

## 7. Troubleshooting

**Black screen after the Acer logo, no boot menu.** The Coreboot framebuffer modules are
not loading. Check that `rd.driver.pre=coreboot_table,framebuffer-coreboot` is in the packed
command line and that the modules are in the deployment:

```bash
modinfo -F vermagic /usr/lib/modules/$(uname -r)/extra/coreboot_table.ko
grep -c coreboot /usr/lib/modules/$(uname -r)/modules.dep
```

Unlike the package-based path, these cannot be missing from a successfully built image —
the build fails first — so suspect the pack rather than the image.

**Dracut emergency shell, `Could not find ostree=` or a missing root.** The packed command
line lost its `ostree=` argument. Print what Slot B is actually carrying:

```bash
sudo /usr/local/share/googlebook-dualboot/bin/mkbootimg --help >/dev/null  # sanity
cat /proc/cmdline
grep -h '^options' /boot/loader/entries/*.conf
```

**Pack from the installed system, not from an Anaconda or live environment, when you have
the choice.** `googlebook-pack-slotb` runs dracut with `--hostonly`, which probes the
*running* system. In an Anaconda chroot that is the installer's hardware and its `/run`, not
the machine as it will boot. Measured on one machine, the same packer and the same image
produced 28,482,617 bytes from the installed Fedora Server and 32,092,814 bytes from the
Anaconda chroot, with about 5 MB shifted between the two ramdisks — and the Anaconda build
was the one that booted into the wrong OS. Use the installer environment only when the
system cannot boot at all.

**`bootloader-update.service` failed.** Expected, and masked by the image. It runs
`bootupctl update`, which manages GRUB inside an EFI System Partition. This machine has
neither — Depthcharge loads the kernel straight from the Android boot partitions that
`googlebook-pack-slotb` writes — so bootupd has nothing to act on, fails on every boot, and
buries real failures in `systemctl --failed`. It also logs a run of its own AVC denials
around `/tmp` and `lsblk`. If it is still failing after an upgrade:

```bash
sudo systemctl mask bootloader-update.service
```

**`systemd-hwdb-update.service` failed, or `smoke-googlebook.sh` reports
`FAIL Moonstone hwdb entry resolves`.** Both come from the same place. Check the label first:

```bash
sudo ausearch -m avc -ts boot | grep hwdb
ls -lZ /etc/udev/hwdb.bin
```

The repair is the `restorecon -Rv /etc` above, plus removing the `/etc` index, which this
image does not need and which is the specific file that has caused trouble:

```bash
sudo rm -f /etc/udev/hwdb.bin
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=input --action=change
sudo systemctl reset-failed systemd-hwdb-update.service
```

Removing it is not a workaround but the intended steady state: the build generates and
verifies `/usr/lib/udev/hwdb.bin`, udev prefers `/etc`'s copy only when it exists, and with
`/etc/udev/hwdb.d/` empty all three of the service's OR'd conditions go false — so it is
skipped rather than failing, and cannot leave a stub again. `googlebook-gpu-init` performs
this on every boot, rebuilding instead of removing when `/etc/udev/hwdb.d/` does hold local
entries, since those exist nowhere else.

**`ostree-prepare-root: Couldn't find specified OSTree root '/sysroot/ostree/boot.1/…':
No such file or directory`, after a `bootc upgrade`.** The deployment is fine; the path is
stale. ostree alternates its boot directory between `boot.0` and `boot.1` on every deployment
change and rewrites the entries to match, but Slot B's `ostree=` is baked into
`vendor_boot_b`'s `vendor_cmdline` at pack time. An upgrade that Slot B was not repacked for
therefore leaves it pointing at a directory that no longer exists.

`googleboot-scan` now detects this: before taking its direct switch-root shortcut it compares
the `ostree=` it booted with against the one in the selected boot entry, and falls through to
`kexec` when they disagree, since the entry's command line is correct by construction. Slot B
survives an un-repacked upgrade with one extra kexec and a message saying so. Repack to make
it direct again.

**To recover a machine already in this state**, boot installation media and repack. Pick the
deployment deliberately — after an upgrade there are two, and `ls … | head -1` is as likely
to pick the rollback as the one that boots:

```bash
SYSROOT=/mnt/sysroot                 # wherever the btrfs subvol is mounted
ENTRY=$(grep -l '^options .*ostree=' "$SYSROOT"/boot/loader/entries/*.conf | head -1)
OSTREE_ARG=$(sed -n 's/^options .*ostree=\([^ ]*\).*/\1/p' "$ENTRY" | head -1)
DEPLOY=$(readlink -f "$SYSROOT$OSTREE_ARG")   # the deployment that entry actually boots
echo "$DEPLOY"
```

Then bind-mount and chroot as in section 3 and run `googlebook-pack-slotb`.

**Slot B boots, but into the pre-migration OS instead of the bootc deployment.** This one
has no error message anywhere, which is what makes it confusing. `bootc install
to-existing-root` leaves the previous installation's files in the subvolume — it adds
`/ostree` beside them rather than replacing them — so an initramfs that switch-roots into
the plain subvolume lands in a complete, working, *old* userspace. Nothing fails; you simply
arrive somewhere else.

You are in the OS the boot landed in, so `journalctl -b` covers the whole boot, initramfs
included: journald flushes the initramfs ring buffer to the persistent journal after
switch-root. Establish which root you got first:

```bash
cat /proc/cmdline
findmnt -no SOURCE,FSTYPE / /usr
test -f /run/ostree-booted && echo "ostree boot" || echo "NOT an ostree boot"
bootc status 2>/dev/null | head -20
```

`/usr` of type `composefs` means the deployment; plain btrfs `subvol=root` with no
`/run/ostree-booted` means the subvolume. Then read the initramfs phase, which
`-o short-monotonic` makes easy to pick out (everything before roughly 5 s):

```bash
sudo journalctl -b -o short-monotonic | sed -n '1,250p'
sudo journalctl -b | grep -iE 'googleboot|ostree-prepare-root|switch.root|composefs'
ls -la /run/initramfs/
```

Stale boot entries are worth ruling out too — `googleboot-scan` reads
`/boot/loader/entries`, and a migration can leave the old entries beside `ostree-1.conf`:

```bash
grep -H '' /boot/loader/entries/*.conf
```

**If the journal has nothing for that phase**, the packed command line carries `quiet rhgb`
and the messages are gone. Repack verbose:

```bash
sudo GOOGLEBOOK_VERBOSE=1 chroot "$DEPLOY" /boot/googlebook-pack-slotb
```

That drops `quiet rhgb` and adds `rd.info systemd.show_status=1 systemd.log_level=info`. It
needs a repack because the command line lives in `vendor_boot_b`'s `vendor_cmdline` field,
not in a bootloader configuration — Depthcharge offers no prompt and there is no GRUB, so
there is nothing to edit at boot time. `rd.shell` is deliberately *not* included: on a bootc
image the root account is locked and the emergency shell cannot be opened.

**`ostree-prepare-root: Failed to mount composefs: composefs: failed to mount: No such
device`, then emergency mode.** Slot B reached switch-root — the framebuffer, the boot menu
and the disk all worked — and failed one step later, mounting `/usr`.

An ostree deployment mounts `/usr` as a composefs: an EROFS image composed through
overlayfs. libcomposefs does that with `fsopen("erofs")`, and `fsopen(2)` returns `ENODEV`
when the filesystem type is not registered. So "No such device" here does not mean a disk —
it means the running kernel has no `erofs`, because `erofs.ko` was not in the Slot B
initramfs. The package-based path never needed it, which is why this appears only after
migrating to bootc.

`/usr/lib/ostree/prepare-root.conf` in the base image is:

```
[composefs]
enabled = yes
```

`yes`, not `maybe`, so there is no fallback to the classic bind-mount deployment, and
`ostree-prepare-root` has no kernel-command-line override. The failure is fatal by design.

The fix is to pin the modules rather than rely on probing — `90-googlebook-slotb.conf` now
carries `erofs overlay loop` in `add_drivers`, and `googlebook-pack-slotb` refuses to pack an
ostree deployment whose initramfs lacks `erofs` or `overlay`. Re-copy the packer and re-run:

```bash
sudo cp bin/googlebook-pack-slotb /boot/googlebook-pack-slotb
sudo chroot "$DEPLOY" /boot/googlebook-pack-slotb
```

The `/etc/dracut.conf.d/90-googlebook-slotb.conf` write is unguarded, so this picks the new
list up on its own. Cost is about 200 KB in `rd_init`.

`50ostree` does call `instmods erofs overlay`, which is why this is not universal. Rather
than work out why that did not take on one particular machine, the modules are now named
along three independent paths, because each is cheap and the failure is unrecoverable in the
field:

- `add_drivers+=` — dracut's `instmods -c` batch at line 2124
- `filesystems+=` — a *separate* `instmods -c` batch at line 2138, so a failure in the first
  does not take the second with it (verified: with a deliberately unresolvable name in
  `add_drivers`, the `filesystems+=` copies still land)
- a pack-time refusal that lists the generated archive and checks both are in it

**The packer also now checks dracut's exit status.** It previously ran

```bash
dracut ... "$W/initramfs.img" >/dev/null
```

with no `||`, so a dracut that failed outright left a missing or truncated image and the pack
continued regardless. That is worth knowing when reading a "missing composefs module(s)"
error: if the listing came back empty, *every* module looks absent, and the real fault is
further up. The failure path now dumps the dracut log and runs a direct `dracut-install`
probe so the two cases are distinguishable.

**Emergency mode is useless on a bootc image.** `Cannot open access to console, the root
account is locked` — `sulogin` has nothing to authenticate against, because the base image
ships no root password. Plan on reading the failure from the screen, not from a shell.
Nothing is written to `/boot` either: `/run/initramfs/rdsosreport.txt` dies with the boot.
If a future failure needs more than the console shows, set a root password in the image
(`bootc/install-googlebook-layer.sh`) before packing, and remove it afterwards.

**`rd_vendor too large`, or an `initramfs size:` far above the Fedora Server figure.** The
Slot B budget is 32 MiB for `vendor_boot_b` and 8 MiB for `init_boot_b`. A package-based
Fedora Server pack came out at 32.0 MB, splitting to 24.5 + 6.0; an untrimmed bootc pack on
the same machine came out at **64.3 MB**, which does not fit.

The cause is not the Googlebook layer but fedora-bootc's own dracut drop-ins, which build a
deliberately generic initramfs: `30-bootc-standard.conf` adds `lvm`, `crypt` and `fips`,
`22-bootc-generic.conf` adds `virtiofs`, and `49-bootc-tpm2-tss.conf` plus
`50-bootc-clevis.conf` add the entire TPM2 and Clevis unlock stack. None of it applies to a
local NVMe Btrfs root.
[`92-googlebook-bootc.conf`](../bootc/configs/usr/lib/dracut/dracut.conf.d/92-googlebook-bootc.conf)
omits them while keeping `ostree`, `bootc`, `btrfs`, `dm`, `crypt` and
`systemd-cryptsetup`. Measured on the machine, that takes the pack from 64.3 MB to
**28.6 MB** — below the 32.0 MB a package-based Fedora Server pack produced on the same
hardware, because the omit list drops more than bootc adds:

```
[googlebook-slotb] initramfs size: 28582951 bytes
[googlebook-slotb] rd_vendor: 19484539 bytes (limit 33488896), rd_init: 7590459 bytes (limit 8380416)
```

`rd_vendor` lands at 58% of budget, `rd_init` at **91%** — about 790 KiB spare. `rd_init`
holds the early-userspace archive, so anything added to `add_dracutmodules`, a new
`/etc/crypttab` entry, or a base image that grows `systemd` will land there first. If a
later pack reports `rd_init too large`, that margin is where it went.

**The filename is load-bearing.** `googlebook-pack-slotb` writes its own
`/etc/dracut.conf.d/90-googlebook-slotb.conf`, and dracut resolves `conf.d` files by
basename with `/etc` winning over `/usr/lib` — verified, not assumed. Anything added to a
`90-` or `91-` file in the image is therefore silently discarded at pack time. Use a
basename the packer never writes.

To apply the omissions to a deployment packed with an older image, write the file into the
deployment's `/etc`, which is writable even though `/usr` is not:

```bash
sudo cp bootc/configs/usr/lib/dracut/dracut.conf.d/92-googlebook-bootc.conf \
        "$DEPLOY/etc/dracut.conf.d/"
```

The driver-level omit list is separate and lives in
[`90-googlebook-slotb.conf`](../bootc/configs/usr/lib/dracut/dracut.conf.d/90-googlebook-slotb.conf).

For reference, the `ostree` and `bootc` dracut modules that bootc adds on top of a
package-based initramfs come to **1.26 MiB uncompressed**, almost all of it
`usr/lib/bootc/initramfs-setup` (1.19 MiB):

| File | Bytes |
|---|---|
| `usr/lib/bootc/initramfs-setup` | 1,191,560 |
| `usr/lib64/libcomposefs.so.1.4.0` | 70,032 |
| `usr/lib/ostree/ostree-prepare-root` | 57,184 |
| service units and `prepare-root.conf` | ~1,800 |

That lands in `rd_vendor`, which is the roomier of the two — confirmed on the machine at
19,484,539 of 33,488,896 bytes with the omit list applied.

**dracut fails with `Invalid tmpdir '/var/tmp'`.** A bootc image ships an empty `/var`,
and `systemd-tmpfiles` creates `/var/tmp` (`q /var/tmp 1777 root root 30d`) and
`/var/roothome` (`d /var/roothome 0700 root root -`) only on a real boot — so neither
exists inside a deployment chroot or a container. The packer now passes `--tmpdir` to
dracut and no longer depends on `/var/tmp` at all; on an older copy of the packer, either
create the directories as in Path A step 3 or run it as:

```bash
sudo chroot "$DEPLOY" env TMPDIR=/tmp /usr/local/bin/googlebook-pack-slotb
```

**`ERROR: missing coreboot framebuffer module(s)`.** The packer refuses to write a Slot B
image that cannot drive the panel, because that failure gives you no display, no console
and no log. On an image-based system it almost always means the deployed image is the
*base* (`bootc-image-laptop`) rather than the Googlebook layer built from
`bootc/Containerfile` — the base carries no `coreboot_table.ko`. Check what is deployed:

```bash
DEPLOY=$(ls -d /ostree/deploy/*/deploy/*.0 | head -1)
cat "${DEPLOY}.origin" | grep -i image
ls "$DEPLOY"/usr/lib/modules/*/extra/
```

`GOOGLEBOOK_ALLOW_NO_FB=1` overrides the refusal, but only do that if you have another way
in — it will boot blind.

**`googlebook-slotb-sync` never fires.** It is conditioned on `/run/ostree-booted`, so it is
inert on a package-based system by design. On a deployment, check both units:

```bash
systemctl status googlebook-slotb-sync.path googlebook-slotb-sync.service
cat /var/lib/googlebook/slotb.stamp
```

---

## 8. Limits

- Only `ghcr.io/dsmk/bootc-image-laptop` (kernel `7.2.11-200.fc44.x86_64` at the time of
  writing) has been built and inspected. Other bases need their own build and boot test.
- Bazzite and Bluefin are **not** usable as bases as they stand: they ship no
  `kernel-devel` for their kernels, their `/usr/lib/modules/*/build` symlink dangles, and
  their squashfs is large enough to need its own partitioning plan. Building the modules
  would require adding their exact `kernel-devel` to the builder stage first.
- `bootc install to-existing-root` wipes `/boot`. Copy `slotb-backup` off the machine before
  running it.
- The Slot B boot path, the `googleboot-menu` kexec handoff and the Android OTA restore flow
  are unchanged from the package-based setup, but have not been re-validated end to end on
  a bootc deployment.
