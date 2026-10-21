# Googlebook 14 (`Moonstone`) Dual-Boot: Android 17 + Fedomakase Linux

Complete dual-boot toolkit, kernel modules, installer patches, and step-by-step guide for running **Android 17 / Aluminum OS (Slot A)** alongside **Fedomakase Linux (Fedora 44 + Omarchy Quattro Hyprland, Slot B)** on the **Acer Googlebook 14 (`GP714-91N`, `Moonstone` / `Google_Fatcat`, Intel Panther Lake `x86_64`)**.

> [!IMPORTANT]
> **Hardware Validation Notice:** This repository and its partition layout, Depthcharge A/B slot tooling, and Coreboot/Panther Lake kernel patches have **only been validated on the Acer Googlebook 14 (`GP714-91N`, board `Moonstone` / `Google_Fatcat`)**. Other Googlebook / Aluminum OS boards may use different partition numbering, display panels, or EC/audio topologies and are not yet validated.

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
| **Slot A** | Googlebook OS / Android 17 (Factory OS) | `boot_a` (`p13`), `init_boot_a` (`p18`), `vendor_boot_a` (`p20`), `vbmeta_a` (`p15`) | `super` (`p3`) + `userdata` (`p1`) |
| **Slot B** | Fedomakase (Fedora 44 + Omarchy) | `boot_b` (`p14`, 64 MiB), `init_boot_b` (`p19`, 8 MiB), `vendor_boot_b` (`p21`, 32 MiB), `vbmeta_b` (`p16`) | `p11` (`ext4` `/boot`, 2 GiB) + `p32` (`LUKS2` $\rightarrow$ `Btrfs` `root` & `home` subvolumes) |

Depthcharge selects the active boot slot by reading the ChromeOS/Depthcharge GPT attribute flags on `vbmeta_a` (`p15`) and `vbmeta_b` (`p16`). All 31 factory Android partitions remain 100% untouched.

> [!WARNING]
> **Googlebook OS (Android 17) OTA Update Limitation:**
> Because this dual-boot methodology repurposes **Slot B** (`boot_b`, `init_boot_b`, `vendor_boot_b`, `vbmeta_b`) for Linux while keeping **Slot A** on stock, unrooted Googlebook OS (`user` build, `ro.debuggable=0`):
> - Whenever Googlebook OS applies an A/B OTA update (`update_engine`), Android writes the new OS into the inactive slot (**Slot B**) and switches the active boot slot to **Slot B**, overwriting `googleboot-menu`.
> - On an unrooted `user` build, neither `adb shell` (`uid=2000`) nor the built-in Debian Linux VM (`crosvm` virtual disk `/dev/vda`) has raw write access to `/dev/block/nvme0n1*`.
> - **Therefore, you cannot upgrade Googlebook OS via OTA and return to Linux without either:**
>   1. **Having a second device handy** (a Mac/Linux PC, or an Android phone over USB-C OTG / TCP Fastboot `tcp:<IP>:5554`) with [`bin/googlebook-restore-slotb`](bin/googlebook-restore-slotb), `adb`/`fastboot`, and a copy of your `slotb-backup/` images (`boot_b.img`, `init_boot_b.img`, `vendor_boot_b.img`, `vbmeta_b.img`), **OR**
>   2. **Rooting Googlebook OS (`init_boot_a`)**, which defeats the purpose of keeping an unrooted stock OS for seamless OTAs.
> - **Preventing Accidental Background OTAs While Traveling:** Disable *Automatic system updates* in Googlebook OS (**Settings $\rightarrow$ System $\rightarrow$ Developer options $\rightarrow$ Automatic system updates = OFF**, or via ADB: `adb shell settings put global ota_disable_automatic_update 1`) so Googlebook OS never applies an OTA in the background while you are away from your recovery device.

---

## 2. Recommended Workflow: USB-C Enclosure Clone & Install vs. Internal Install

When upgrading from the factory 512 GB NVMe to a 1 TB / 2 TB NVMe, there are two ways to perform the clone and installation:

### Option A (Recommended): Clone & Install While the New NVMe is in a USB-C Enclosure, Then Swap Internally
1. Keep the factory 512 GB NVMe inside the Googlebook (`nvme0n1`) and connect the new 1 TB / 2 TB NVMe via a USB-C NVMe enclosure (`/dev/sda`).
2. Boot the Fedomakase Netinst environment in Slot B (in RAM), clone `/dev/nvme0n1` $\rightarrow$ `/dev/sda`, relocate the backup GPT header (`sgdisk -e /dev/sda`), and run the Fedomakase installer targeting `/dev/sda`.
3. Once installation completes, power off, swap the 1 TB / 2 TB NVMe into the internal M.2 slot, and boot.

**Why Option A is cleaner and safer:**
- The original 512 GB NVMe is never modified and serves as an untouched hardware backup.
- No third computer or external Linux host is needed to clone the drives.
- Because `fedomakase-googlebook-slotb` and `crypttab`/`fstab` reference partitions strictly by **filesystem/LUKS UUID** (`UUID=...`), moving the drive from `/dev/sda` (USB-C) to `/dev/nvme0n1` (internal M.2) requires zero `fstab` or `crypttab` changes.

### Option B: Clone Externally, Swap Drive First, Then Install Internally
1. Clone the 512 GB NVMe to the 1 TB / 2 TB NVMe using an external dual-bay cloner or another Linux machine, and run `sgdisk -e` on the new drive.
2. Install the 1 TB / 2 TB NVMe into the Googlebook's internal M.2 slot (`nvme0n1`).
3. Flash the Slot B installer via Fastboot and install Fedomakase directly onto the unallocated space of `nvme0n1`.

---

## 3. Step-by-Step Installation Guide

### Prerequisites
- Acer Googlebook 14 (`Moonstone`) with OEM Unlock / Developer Mode enabled (`fastboot flashing unlock`).
- USB-C Ethernet adapter (recommended for Fastboot over TCP and fast package downloads) and a USB-C NVMe enclosure (if upgrading the SSD via Option A).
- `android-platform-tools` (`fastboot`, `mkbootimg`) installed on your host computer.

### Step 1: Boot the Slot B Netinst Installer
1. Build or use the pre-packed Slot B installer images (`boot_b.img`, `init_boot_b.img`, `vendor_boot_b.img`):
   ```bash
   ./scripts/build-slotb-installer.sh ./iso-extracted ./slotb-installer http://<YOUR_HOST_IP>:8080 ./coreboot-mods/6.19.10-300.fc44.x86_64
   ```
2. Start an HTTP server hosting the extracted ISO directory (which serves `images/install.img` and `omarchy-ks.cfg`):
   ```bash
   python3 -m http.server 8080 --bind 0.0.0.0 --directory ./iso-extracted
   ```
3. Boot the Googlebook into Fastboot mode and flash Slot B:
   ```bash
   ./scripts/flash-slotb.sh ./slotb-installer tcp:<GOOGLEBOOK_IP>
   ```

### Step 2: Clone Factory 512 GB NVMe to 1 TB / 2 TB NVMe & Expand GPT
Once the installer boots (accessible on screen or via `ssh root@<GOOGLEBOOK_IP>`), clone the internal 512 GB NVMe (`/dev/nvme0n1`) to the USB-C 1 TB / 2 TB NVMe (`/dev/sda`) and move the backup GPT header to the end of the 1 TB disk:
```bash
./scripts/clone-and-expand-nvme.sh /dev/nvme0n1 /dev/sda
```
> [!IMPORTANT]
> Running `sgdisk -e /dev/sda` is mandatory after block-cloning a 512 GB drive to a 1 TB+ drive. Without `sgdisk -e`, the GPT usable sector boundary (`LastUsableLBA`) remains capped at the 512 GB mark, hiding the upper 476+ GiB of unallocated space from `parted` and Anaconda.

### Step 3: Run the Interactive Fedomakase Installer
On the Googlebook display (`tty3`), complete the Gum TUI wizard:
1. Select target drive (`sda` when installing to USB-C enclosure, or `nvme0n1` when installing internally).
2. Enter your username, password, hostname, timezone, and LUKS2 encryption preference.
3. The patched [`omarchy-installer.sh`](patches/omarchy-installer.sh) automatically detects the 31-partition Googlebook layout and creates:
   - Partition `11` (`2 GiB`, `ext4`, mounted at `/boot`, reusing the empty 1 MiB `efi` slot number in the free space after `userdata` `p31`).
   - Partition `32` (remaining ~475 GiB, `LUKS2` $\rightarrow$ `Btrfs` with `root` and `home` subvolumes).
4. At the end of installation (`%post`), [`fedomakase-googlebook-slotb`](bin/fedomakase-googlebook-slotb) packs the installed Fedora kernel and split initramfs (`rd_vendor` + `rd_init`) into `boot_b` (`p14`), `init_boot_b` (`p19`), and `vendor_boot_b` (`p21`).

---

## 4. Switching Between Android 17 (Slot A) and Fedomakase Linux (Slot B)

### From Fedomakase Linux (No External Computer Required)
Use [`googlebook-switch-slot`](bin/googlebook-switch-slot) (or its alias `boot-android`):
```bash
# Check current active boot slot:
sudo googlebook-switch-slot status

# Reboot into Android 17 (Slot A):
sudo boot-android a --reboot

# Keep Slot B (Fedomakase Linux) active:
sudo googlebook-switch-slot b
```

### From Android 17 or Fastboot Bootloader Menu
1. Reboot and hold Volume Down / select **Bootloader (Fastboot)** on screen.
2. From any computer on USB or LAN:
   ```bash
   # Boot Fedomakase Linux (Slot B):
   fastboot --set-active=b && fastboot reboot

   # Boot Android 17 (Slot A):
   fastboot --set-active=a && fastboot reboot
   ```

### Restoring Slot B After a Googlebook OS (Android 17) OTA or Development Flash
When Googlebook OS (on Slot A) applies an A/B OTA update (`update_engine`), it writes the new Android build into **Slot B** (`boot_b`, `init_boot_b`, `vendor_boot_b`, `vbmeta_b`, `pvmfw_b`, `recovery_b`, `ota_recovery_b`, and `*_b` inside `super` `p3`) and switches the active slot to B. Meanwhile, [`googlebook-pack-slotb`](bin/googlebook-pack-slotb) automatically saves a backup copy of your Linux Slot B images in `/boot/slotb-backup/` on `p11` (unencrypted `ext4`). Copy `/boot/slotb-backup/*.img` to `./slotb-backup/` on your recovery computer or phone so it is ready whenever you install an OTA.

Run **[`googlebook-restore-slotb`](bin/googlebook-restore-slotb)** after any Googlebook OS OTA (once Android has booted into the updated slot and finished merging Virtual A/B snapshots) or `fastboot` flash:
1. **Unrooted Googlebook OS (via Mac/PC or Android Phone over Fastboot / ADB)**:
   - When invoked with `--adb` or `--fastboot` while `current-slot=b` (post-OTA), `googlebook-restore-slotb` stages your Linux `slotb-backup/*.img` into **Slot A** (`boot_a`, `init_boot_a`, `vendor_boot_a`, `vbmeta_a`) so the newly updated Android OTA in Slot B is not overwritten.
   - As soon as Linux boots on the laptop (with full root access to `/dev/nvme0n1`), `googlebook-gpu-init.service` automatically runs `/boot/slotb-backup/googlebook-restore-slotb`, which promotes the new Googlebook OS build from **Slot B $\rightarrow$ Slot A** (including `super` `p3` `LpMetadata` at offset `12288`), restores Linux (`googleboot-menu`) to **Slot B**, and sets **Slot B** active.
2. **Rooted Googlebook OS (`adb root` / `su`)**:
   - Directly promotes Slot B $\rightarrow$ Slot A and restores Linux to Slot B from `/dev/block/nvme0n1p11` (`/boot/slotb-backup`), or installs `/data/adb/service.d/99-googlebook-restore-slotb.sh` via `--install-hook` to run automatically on post-OTA boot.

```bash
# Option 1 (Recommended for unrooted Googlebook OS): From your Mac/PC over ADB or Fastboot:
./bin/googlebook-restore-slotb --adb --reboot
# Or directly from Fastboot (USB-C cable or TCP fastboot tcp:<IP>:5554):
./bin/googlebook-restore-slotb --fastboot ./slotb-backup --reboot

# Option 2: From an Android phone (USB-C OTG cable or TCP Fastboot):
# Install ./android-restore-app/bin/googlebook-slotb-restore.apk on your phone
# (see ./android-restore-app/README.md) and tap "Set Active Slot = B" or "Flash & Restore Linux Slot B".

# Option 3: On a rooted Googlebook OS (adb root / su), or to install the automatic root boot hook:
./bin/googlebook-restore-slotb --adb --install-hook --reboot
```

---

## 5. Technical Reference: Key Fixes & Engineering Notes

1. **Coreboot Framebuffer (`coreboot_table.ko` + `framebuffer-coreboot.ko`)**:
   - Upstream Fedora x86_64 kernels disable `CONFIG_GOOGLE_COREBOOT_TABLE` and `CONFIG_GOOGLE_FRAMEBUFFER_COREBOOT`. Without them, `simpledrm` has no framebuffer device when booted from Depthcharge, resulting in a black screen during initramfs/LUKS unlock.
   - Sources in [`coreboot-mods/`](coreboot-mods/) build out-of-tree against `kernel-devel` and are automatically rebuilt across kernel upgrades by [`fedomakase-googlebook-slotb`](bin/fedomakase-googlebook-slotb).
2. **Two-Archive Initramfs Split (`rd_vendor.img` + `rd_init.img`)**:
   - Depthcharge concatenates the `vendor_boot` ramdisk (`32 MiB` partition) and `init_boot` ramdisk (`8 MiB` partition) in memory, but aligns each fragment to a `2048`-byte page boundary with zero bytes (`0x00`).
   - Byte-slicing a single `.cpio.zst` archive across `vendor_boot_b` and `init_boot_b` corrupts the `zstd` stream at the 32 MiB boundary (`ZSTD_compressedBlock: Corrupted block detected`).
   - Instead, [`fedomakase-googlebook-slotb`](bin/fedomakase-googlebook-slotb) packs two **independent** `.cpio.zst` archives (`rd_vendor.img` containing userspace and `rd_init.img` containing `/usr/lib/modules/<kver>`), which the Linux kernel's `initramfs.c` unpacks sequentially while skipping the zero-padding between them.
3. **Controlling Terminal (`TIOCSCTTY`) Fix in `omarchy-installer.sh`**:
   - Anaconda's `%pre` runs without a controlling terminal on `/dev/tty3`, causing `gum`'s `/dev/tty` open to fail with `ENXIO`. [`patches/omarchy-installer.sh`](patches/omarchy-installer.sh) acquires `/dev/tty3` via `ioctl(0, TIOCSCTTY, 1)` before launching the TUI.
4. **SDDM Wayland Greeter & Staged Intel Xe2 (`xe.ko`) Initialization**:
   - Fixes SDDM's `[Wayland] CompositorCommand` (`Hyprland --config /usr/share/sddm/hyprland.lua`), configures LUKS post-unlock `[Autologin]` for `omarchy.desktop`, symlinks `/usr/share/omarchy/bin/*` into `/usr/bin/`, and loads `xe.ko` right before `sddm.service` once `/usr/lib/firmware/xe/*` is mounted on the Btrfs root filesystem.
5. **SoundWire Audio Bringup (`snd-soc-sdw-utils.ko` + `snd-soc-sof-sdw.ko`, RT1320 Calibration & ALSA UCM)**:
   - On `Moonstone` (`Google_Fatcat`), the Realtek `RT721` (jack) + `RT1320` (smart amp) combo shares SoundWire Link 3 (`sdw-master-0-3`), while the internal array microphone is a 2-channel PCH DMIC (`sof-ptl-dmic-2ch-id3.tplg`).
   - Upstream `soc_sdw_utils.c` lacked `.quirk = SOC_SDW_CODEC_MIC, .quirk_exclude = true` on `rt1320-aif2` (causing an unrouted `SDW0-Capture-SimpleJack` BE link and a 128-byte `card->components` `-ENOSPC` overflow), had an early `break;` in `asoc_sdw_mc_dailink_exit_loop()` leaving `rtd_init_done` stale across reloads, and lacked a `Google_Fatcat` DMI quirk entry (`SOC_SDW_PCH_DMIC`) in `sof_sdw.c`.
   - [`audio-mods/`](audio-mods/) builds patched `snd-soc-sdw-utils.ko` and `snd-soc-sof-sdw.ko` out-of-tree into `/lib/modules/<kver>/updates/`, installs the Android 17 `rt1320_*Google_Moonstone*.dat` calibration files to `/lib/firmware/realtek/rt1320/`, creates `/usr/share/alsa/ucm2/codecs/rt721+rt1320/init.conf`, and anchors the `SpeakerCodec1` regex in `/usr/share/alsa/ucm2/sof-soundwire/rt721.conf`. Both [`fedomakase-googlebook-slotb`](bin/fedomakase-googlebook-slotb) and [`googlebook-sysroot-sync`](bin/googlebook-sysroot-sync) keep these synced across kernel and `alsa-ucm` updates.

---

## 6. Installing Other Linux Distros, Graphical `kexec` Menu & Bare-Metal Windows 11

- **[`googleboot-menu/`](googleboot-menu/README.md)**: Graphical DRM/KMS + `kexec` universal bootloader (`2880x1800` OLED) with live-editable themes (`theme.conf`), built-in LUKS2 passphrase modal, hotplug USB ISO scanner, and One-Shot Android 17 slot switching.
- **[`docs/ANY_DISTRO_AND_OS_SWITCHING.md`](docs/ANY_DISTRO_AND_OS_SWITCHING.md)**: Booting **any Linux distribution** (Arch, Vanilla Omarchy, Ubuntu, Debian, NixOS, CachyOS) and switching between Android 17 (Slot A) and Linux (Slot B).
- **[`docs/WINDOWS_11_BARE_METAL_AND_CROSECBUS.md`](docs/WINDOWS_11_BARE_METAL_AND_CROSECBUS.md)**: Bare-metal Windows 11 chainloading via the RAM-resident **Das U-Boot `x86_64` UEFI Bridge** (`uefi-bridge.bzImage` -> `bootmgfw.efi`) and source-level analysis of **[`coolstar/crosecbus`](https://github.com/coolstar/crosecbus)** on Intel Panther Lake (`ACPI\GOOG0004`).
