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
DEPLOY=$(ls -d /ostree/deploy/*/deploy/*.0 | head -1); echo "$DEPLOY"

for p in dev proc sys run; do
  sudo mount --rbind "/$p" "$DEPLOY/$p"
  sudo mount --make-rslave "$DEPLOY/$p"
done
sudo mount --bind /boot "$DEPLOY/boot"
sudo mount --bind /     "$DEPLOY/sysroot"

sudo chroot "$DEPLOY" /usr/local/bin/googlebook-pack-slotb
```

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
sudo umount "$DEPLOY/sysroot" "$DEPLOY/boot"
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

**`rd_vendor too large`.** The Slot B budget is 32 MiB for `vendor_boot_b` and 8 MiB for
`init_boot_b`. Packages that pull firmware or storage drivers into a host-only initramfs can
push it over. The omit list is in
[`bootc/configs/usr/lib/dracut/dracut.conf.d/90-googlebook-slotb.conf`](../bootc/configs/usr/lib/dracut/dracut.conf.d/90-googlebook-slotb.conf).

For reference, the `ostree` and `bootc` dracut modules that bootc adds on top of a
package-based initramfs come to **1.26 MiB uncompressed**, almost all of it
`usr/lib/bootc/initramfs-setup` (1.19 MiB):

| File | Bytes |
|---|---|
| `usr/lib/bootc/initramfs-setup` | 1,191,560 |
| `usr/lib64/libcomposefs.so.1.4.0` | 70,032 |
| `usr/lib/ostree/ostree-prepare-root` | 57,184 |
| service units and `prepare-root.conf` | ~1,800 |

That lands in `rd_vendor`, which had roughly 8.6 MiB of headroom on the Fedora Server pack
(24,472,219 of 33,488,896 bytes), so it fits — but the figure above is an uncompressed
measurement taken in a container, not a packed size from the machine. Check the packer's own
`rd_vendor:` / `rd_init:` line, which is authoritative.

**dracut fails with `Invalid tmpdir '/var/tmp'` or `dracut-install: ERROR: installing
'/root'`.** You are running the packer inside `podman run` rather than on a deployment. A
bootc image must ship an empty `/var`, and `systemd-tmpfiles` recreates `/var/tmp` (`q
/var/tmp 1777 root root 30d`) and `/var/roothome` (`d /var/roothome 0700 root root -`) only
on a real boot. Test on the machine.

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
