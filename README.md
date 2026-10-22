#Acer Googlebook 14 (`Moonstone`) Dual-Boot Toolkit
<img width="1448" height="1086" alt="image" src="https://github.com/user-attachments/assets/fbbfb277-41df-4ff7-8bef-c04d6fe34f5b" />

Linux dual-boot tools, a graphical `kexec` boot menu, and Coreboot/Panther Lake hardware modules for the **Acer Googlebook 14**. The working configuration is Googlebook OS on Slot A and Linux on Slot B. Other distributions require their own validation; Windows boot is experimental and is not supported by the supplied bridge builder.

> [!IMPORTANT]
> **Hardware Validation Notice:** This repository and its partition layout, Depthcharge A/B slot tooling, and Coreboot/Panther Lake kernel modules have **only been validated on the Acer Googlebook 14 (`GP714-91N`, board `Moonstone` / `Google_Fatcat`)**. Other Googlebook / Aluminum OS boards may use different partition numbering, display panels, or EC/audio topologies and are not yet validated.
>
> **Fedomakase / Omarchy Branch:** For Fedomakase / Omarchy Quattro-specific Anaconda Kickstart patches and provisioning helpers, see the [`googlebook-db-Fedomakase`](https://github.com/FixItFoundry/googlebook-dualboot/tree/googlebook-db-Fedomakase) branch.

---

## 1. Hardware & Boot Architecture Overview

The Acer Googlebook 14 (`Moonstone`, board family `Google_Fatcat`) uses:
- **Firmware / Bootloader**: `Coreboot` (`Google_Moonstone.*`) + `Depthcharge` payload with **Android Boot Image v4** A/B slots (`boot_a/b`, `init_boot_a/b`, `vendor_boot_a/b`, `vbmeta_a/b`). There is no UEFI GOP or `efifb` table when booting via Depthcharge.
- **Display**: 14" 2880x1800 OLED (`eDP-1`). Early boot console and LUKS passphrase unlock require `CONFIG_GOOGLE_COREBOOT_TABLE` (`coreboot_table.ko`) and `CONFIG_GOOGLE_FRAMEBUFFER_COREBOOT` (`framebuffer-coreboot.ko`) to register the Coreboot framebuffer with Linux's built-in `simpledrm` driver, followed by staged handoff to the hardware-accelerated Intel Panther Lake Xe2 (`xe.ko`) driver once the root filesystem is mounted.
- **Storage**: Internal M.2 2280 NVMe (`nvme0n1`) with a 31-partition Android Dynamic (`super`) GPT layout.
- **Security / TPM2**: Google Ti50 / Cr50 TPM2 over DesignWare I2C (`tpm_tis_i2c_cr50`, `GOOG0005` on `i2c_designware.0`).

### How A/B Dual-Boot Works
| Slot | OS | Boot Partitions | Root Filesystem |
| :--- | :--- | :--- | :--- |
| **Slot A** | Googlebook OS | `boot_a` (`p13`), `init_boot_a` (`p18`), `vendor_boot_a` (`p20`), `vbmeta_a` (`p15`) | `super` (`p3`) + factory partition labeled `userdata` |
| **Slot B** | Linux (`googleboot-menu` + Target Distro) | `boot_b` (`p14`, 64 MiB), `init_boot_b` (`p19`, 8 MiB), `vendor_boot_b` (`p21`, 32 MiB), `vbmeta_b` (`p16`) | `p11` (`ext4` `/boot`, 2 GiB) + `p32` (`LUKS2` or plain `Btrfs`/`ext4` root) |

Depthcharge selects the active boot slot by reading the ChromeOS/Depthcharge GPT attribute flags on `vbmeta_a` (`p15`) and `vbmeta_b` (`p16`). The Linux setup repurposes the Slot B boot partitions and the verified Linux `/boot` slot. It does not preserve the factory layout byte-for-byte. Confirm partition labels and sector ranges on your own disk before partitioning; do not infer the location of `userdata` from an example partition number.

> [!WARNING]
> **Android OTA recovery is unsupported in this revision.** An A/B OTA can replace Linux in Slot B. The recovery tools now refuse Android or unrecognized Slot B images and never stage Linux into Slot A or rewrite `super` metadata. A second device alone does not make post-OTA recovery safe.
> Keep a complete disk backup and a tested factory recovery path before applying an OTA. Do not rely on the developer-options automatic-update toggle as a guarantee against every update path.
> The old automatic promotion hook has been removed because it could misidentify Linux and overwrite Android. See the recovery section before upgrading an existing installation.

---

## 2. Recommended Workflow: USB-C Enclosure Clone & Install vs. Internal Install

When upgrading from the factory 512 GB NVMe to a 1 TB / 2 TB NVMe, there are two ways to perform the clone and installation:

### Option A (Recommended): Clone & Install While the New NVMe is in a USB-C Enclosure, Then Swap Internally
1. Keep the factory 512 GB NVMe inside the Googlebook (`nvme0n1`) and connect the new 1 TB / 2 TB NVMe via a USB-C NVMe enclosure (`/dev/sda`).
2. Boot the Linux installer environment in Slot B (in RAM), clone `/dev/nvme0n1` $\rightarrow$ `/dev/sda`, relocate the backup GPT header (`sgdisk -e /dev/sda`), and run your Linux installer targeting `/dev/sda`.
3. Once installation completes, power off, swap the 1 TB / 2 TB NVMe into the internal M.2 slot, and boot.

**Why Option A is cleaner and safer:**
- The clone operation reads the original NVMe. Booting the Slot B installer already changes its Slot B boot partitions, so retain a separate factory backup if you need an unchanged factory image.
- No third computer or external Linux host is needed to clone the drives.
- Because [`googlebook-pack-slotb`](bin/googlebook-pack-slotb) and `crypttab`/`fstab` reference partitions strictly by **filesystem/LUKS UUID** (`UUID=...`), moving the drive from `/dev/sda` (USB-C) to `/dev/nvme0n1` (internal M.2) requires zero `fstab` or `crypttab` changes.

### Option B: Clone Externally, Swap Drive First, Then Install Internally
1. Clone the 512 GB NVMe to the 1 TB / 2 TB NVMe using an external dual-bay cloner or another Linux machine, and run `sgdisk -e` on the new drive.
2. Install the 1 TB / 2 TB NVMe into the Googlebook's internal M.2 slot (`nvme0n1`).
3. Flash the Slot B installer via Fastboot and install Linux directly onto the unallocated space of `nvme0n1`.

---

## 3. Step-by-Step Installation Guide

### Prerequisites
- Acer Googlebook 14 (`Moonstone`) with OEM Unlock / Developer Mode enabled (`fastboot flashing unlock`).
- USB-C Ethernet adapter (recommended for Fastboot over TCP and fast package downloads) and a USB-C NVMe enclosure (if upgrading the SSD via Option A).
- A Linux build host with Python 3, Bash, dracut, cpio, zstd, make, a C compiler, zlib development headers, kmod, util-linux, coreutils, GPT fdisk (`sgdisk`), and the target kernel's development headers/modules. The repository includes the unsigned-image `mkbootimg` path; optional GKI signing needs the upstream GKI companion module.
- Android platform tools (`adb`/`fastboot`) on a recovery host. Verified Fastboot restore additionally requires device support for `fetch`; unsupported devices are refused before flashing.

### Step 1: Boot the Slot B Installer
1. Build the Slot B installer images (`boot_b.img`, `init_boot_b.img`, `vendor_boot_b.img`):
   ```bash
   ./scripts/build-slotb-installer.sh ./iso-extracted ./slotb-installer http://<YOUR_HOST_IP>:8080 ./coreboot-mods/prebuilt/6.19.10-300.fc44.x86_64
   ```
2. Start an HTTP server hosting the extracted ISO directory:
   ```bash
   python3 -m http.server 8080 --bind 0.0.0.0 --directory ./iso-extracted
   ```
3. Boot the Googlebook into Fastboot mode and flash Slot B:
   ```bash
   ./scripts/flash-slotb.sh ./slotb-installer tcp:<GOOGLEBOOK_IP>
   ```

### Step 2: Clone Factory 512 GB NVMe to 1 TB / 2 TB NVMe & Expand GPT
Once the installer boots, clone the internal 512 GB NVMe (`/dev/nvme0n1`) to the USB-C 1 TB / 2 TB NVMe (`/dev/sda`) and move the backup GPT header to the end of the 1 TB disk:
```bash
./scripts/clone-and-expand-nvme.sh /dev/nvme0n1 /dev/sda
```
> [!IMPORTANT]
> Running `sgdisk -e /dev/sda` is mandatory after block-cloning a 512 GB drive to a 1 TB+ drive. Without `sgdisk -e`, the GPT usable sector boundary (`LastUsableLBA`) remains capped at the 512 GB mark, hiding the upper 476+ GiB of unallocated space from `parted` and OS installers.

### Step 3: Partition & Pack Slot B
1. Inspect `lsblk -o NAME,SIZE,FSTYPE,PARTLABEL,MOUNTPOINTS` and `sgdisk -p /dev/sda` (or the actual target disk). Preserve every factory data partition, especially the one labeled `userdata`. The current Moonstone tools expect Linux `/boot` on partition 11 and root on partition 32. Reuse partition 11 only after confirming that it is the disposable factory `efi` entry on this disk, and allocate its replacement extent and partition 32 in verified free space. Stop if the labels or layout differ; this guide does not automate partition creation.
2. Install Linux with that ext4 `/boot` and the chosen root filesystem. Mount the installed root at `/mnt/sysroot` and its **own** `/boot` at `/mnt/sysroot/boot`. Install the build dependencies above inside the target system before packing.
3. From the repository directory in the installer, enter the installed system. These commands assume the two mounts above already exist:

   ```bash
   TARGET_ROOT=/mnt/sysroot
   sudo mkdir -p "$TARGET_ROOT/usr/local/share/googlebook-dualboot" "$TARGET_ROOT/usr/local/bin"
   sudo cp -a . "$TARGET_ROOT/usr/local/share/googlebook-dualboot/"
   for path in dev proc sys run; do
     sudo mount --rbind "/$path" "$TARGET_ROOT/$path"
     sudo mount --make-rslave "$TARGET_ROOT/$path"
   done
   sudo chroot "$TARGET_ROOT" ln -sf /usr/local/share/googlebook-dualboot/bin/googlebook-pack-slotb /usr/local/bin/googlebook-pack-slotb
   sudo chroot "$TARGET_ROOT" /usr/local/bin/googlebook-pack-slotb
   # After packing succeeds or after diagnosing a failure:
   for path in run sys proc dev; do sudo umount -R "$TARGET_ROOT/$path"; done
   ```

   The packer reads the installed system's `/`, `/boot`, `/etc/crypttab`, and `/lib/modules`; do not run it directly against the live installer's root. Check its printed disk and partition targets before using this workflow on another layout.

---

## 4. Switching Between Googlebook OS / Android 17 (Slot A) and Linux (Slot B)

### From the Graphical `googleboot-menu` Bootloader
At every boot on Slot B, **`googleboot-menu`** renders a native `2880x1800` graphical boot selector where you can choose **Fedora / Linux** (with integrated LUKS2 passphrase unlock) or **Googlebook OS** (one-shot reboot into Slot A while keeping Slot B as the default next boot).

### From the Linux CLI (No External Computer Required)
Use [`googlebook-switch-slot`](bin/googlebook-switch-slot) (or its alias `boot-android`):
```bash
# Check current active boot slot:
sudo googlebook-switch-slot status

# Reboot into Googlebook OS / Android 17 (Slot A):
sudo boot-android a --reboot

# Keep Slot B (Linux) active:
sudo googlebook-switch-slot b
```

### From Fastboot Bootloader Menu
1. Reboot and hold Volume Down / select **Bootloader (Fastboot)** on screen.
2. From any computer on USB or LAN:
   ```bash
   # Boot Linux (Slot B):
   fastboot --set-active=b && fastboot reboot

   # Boot Googlebook OS / Android 17 (Slot A):
   fastboot --set-active=a && fastboot reboot
   ```

### Restoring an Existing Linux Slot B

The packer saves `boot_b.img`, `init_boot_b.img`, and `vendor_boot_b.img` in `/boot/slotb-backup/`. Copy this directory to your recovery host. Restoration validates all three backups and reads the existing Slot B vendor image to identify Linux from its actual bootconfig section. It does not infer the OS from `current-slot`.

```bash
# On Linux, with /boot mounted:
sudo /usr/local/bin/googlebook-restore-slotb

# From a host while the laptop is in Fastboot (requires fetch support):
./bin/googlebook-restore-slotb --fastboot ./slotb-backup --reboot
./bin/googlebook-restore-slotb --fastboot tcp:<IP>:5554 ./slotb-backup --reboot

# From ADB over USB, retain the same device serial when entering Fastboot:
./bin/googlebook-restore-slotb --adb ./slotb-backup --reboot
```

A damaged or unreadable vendor image, unsupported Fastboot read-back, or an Android OTA in Slot B causes restoration to stop before flashing. This deliberately limits recovery: there is no unsafe override. The legacy `--no-promote` flag is accepted but does not bypass any check; `--install-hook` is rejected. `vbmeta` image contents, Slot A images and Android dynamic-partition metadata are not rewritten.

The [phone app](android-restore-app/README.md) applies the same checks and uses Android's folder picker for backups. Only use “Set Slot B” when you know which OS is installed there; that action selects a slot and does not repair it.

**Existing installations:** rebuild Slot B from the updated source and update `/boot/slotb-backup/googlebook-restore-slotb` and any installed copies. The new initramfs installs a GPU startup service without automatic recovery and removes the old broad disk/sudo grants. On a rooted Android installation, remove the previously installed `/data/adb/service.d/99-googlebook-restore-slotb.sh` hook before rebooting Android. Old initramfs images, scripts and APKs retain the unsafe behavior until replaced. These are deployment steps, not actions performed by a source update alone.

---

## 5. Technical Reference: Key Fixes & Engineering Notes

1. **Coreboot Framebuffer (`coreboot_table.ko` + `framebuffer-coreboot.ko`)**:
   - Upstream distro x86_64 kernels often disable `CONFIG_GOOGLE_COREBOOT_TABLE` and `CONFIG_GOOGLE_FRAMEBUFFER_COREBOOT`. Without them, `simpledrm` has no framebuffer device when booted from Depthcharge, resulting in a black screen during initramfs/LUKS unlock.
   - Sources in [`coreboot-mods/`](coreboot-mods/) build out-of-tree against `kernel-devel` / `linux-headers` and are automatically rebuilt across kernel upgrades by [`googlebook-pack-slotb`](bin/googlebook-pack-slotb).
2. **Two-Archive Initramfs Split (`rd_vendor.img` + `rd_init.img`)**:
   - Depthcharge concatenates the `vendor_boot` ramdisk (`32 MiB` partition) and `init_boot` ramdisk (`8 MiB` partition) in memory, but aligns each fragment to a `2048`-byte page boundary with zero bytes (`0x00`).
   - Byte-slicing a single `.cpio.zst` archive across `vendor_boot_b` and `init_boot_b` corrupts the `zstd` stream at the 32 MiB boundary (`ZSTD_compressedBlock: Corrupted block detected`).
   - Instead, [`googlebook-pack-slotb`](bin/googlebook-pack-slotb) packs two **independent** `.cpio.zst` archives (`rd_vendor.img` containing userspace + `googleboot-menu`, and `rd_init.img` containing `/usr/lib/modules/<kver>`), which the Linux kernel's `initramfs.c` unpacks sequentially while skipping the zero-padding between them.
3. **Staged Intel Panther Lake Xe2 (`xe.ko`) Initialization & Vivaldi Keymap**:
   - [`googlebook-sysroot-sync`](bin/googlebook-sysroot-sync) installs `/etc/modprobe.d/googlebook-xe-staged.conf` (`enable_psr=0`), `googlebook-gpu-init.service` (loading `xe.ko` once `/usr/lib/firmware/xe/*` is mounted on the root filesystem), and `/etc/udev/hwdb.d/61-googlebook-moonstone.hwdb` + `/etc/keyd/cros.conf` for the top-row ChromeEC action keys.
4. **SoundWire Audio Bringup (`snd-soc-sdw-utils.ko` + `snd-soc-sof-sdw.ko`, RT1320 Calibration & ALSA UCM)**:
   - On `Moonstone` (`Google_Fatcat`), the Realtek `RT721` (jack) + `RT1320` (smart amp) combo shares SoundWire Link 3 (`sdw-master-0-3`), while the internal array microphone is a 2-channel PCH DMIC (`sof-ptl-dmic-2ch-id3.tplg`).
   - Upstream `soc_sdw_utils.c` lacked `.quirk = SOC_SDW_CODEC_MIC, .quirk_exclude = true` on `rt1320-aif2` (causing an unrouted `SDW0-Capture-SimpleJack` BE link and a 128-byte `card->components` `-ENOSPC` overflow), had an early `break;` in `asoc_sdw_mc_dailink_exit_loop()` leaving `rtd_init_done` stale across reloads, and lacked a `Google_Fatcat` DMI quirk entry (`SOC_SDW_PCH_DMIC`) in `sof_sdw.c`.
   - [`audio-mods/`](audio-mods/) builds patched `snd-soc-sdw-utils.ko` and `snd-soc-sof-sdw.ko` out-of-tree into `/lib/modules/<kver>/updates/`, installs the Android 17 `rt1320_*Google_Moonstone*.dat` calibration files to `/lib/firmware/realtek/rt1320/`, creates `/usr/share/alsa/ucm2/codecs/rt721+rt1320/init.conf`, and anchors the `SpeakerCodec1` regex in `/usr/share/alsa/ucm2/sof-soundwire/rt721.conf`. Both [`googlebook-pack-slotb`](bin/googlebook-pack-slotb) and [`googlebook-sysroot-sync`](bin/googlebook-sysroot-sync) keep these synced across kernel and `alsa-ucm` updates.

---

## 6. Other Distributions and Experimental Windows Research

- **[`googleboot-menu/`](googleboot-menu/README.md)**: Graphical DRM/KMS + `kexec` universal bootloader (`2880x1800` OLED) with live-editable themes (`theme.conf`), built-in LUKS2 passphrase modal, hotplug USB ISO scanner, and One-Shot Googlebook OS slot switching.
- **[`docs/ANY_DISTRO_AND_OS_SWITCHING.md`](docs/ANY_DISTRO_AND_OS_SWITCHING.md)**: Requirements and limits for other distributions and OS switching.
- **[`docs/WINDOWS_11_BARE_METAL_AND_CROSECBUS.md`](docs/WINDOWS_11_BARE_METAL_AND_CROSECBUS.md)**: Experimental Windows research. The builder emits a U-Boot ELF artifact, not a validated `kexec`-bootable bzImage.

## 7. Local Checks

Run `python3 -m unittest discover -s tests -v` and `./tests/check-menu.sh`. These use temporary files and mocked device commands. They do not flash a disk or establish hardware/OTA compatibility. See [tests/README.md](tests/README.md) for the Android validator check.
