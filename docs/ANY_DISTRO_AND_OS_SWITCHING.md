# Installing Any Operating System & Seamless OS Switching on the Acer Googlebook 14 (`Moonstone`)

The Acer Googlebook 14 (`GP714-91N`, Intel Panther Lake `x86_64`) uses **Coreboot + Depthcharge** with **Android Boot Image v4** partitions instead of standard UEFI (`OVMF`/`edk2`).

Despite this firmware design, the dual-boot architecture in this repository is **not limited to Fedomakase / Fedora 44**—it can boot **any `x86_64` Linux distribution** (Arch Linux, Vanilla Omarchy, Ubuntu, Debian, CachyOS, NixOS, Gentoo, etc.).

---

## Part 1: Opening This Setup to Any Operating System

There are **two ways** to support any Linux distribution on Slot B (`boot_b` / `init_boot_b` / `vendor_boot_b`):

### Approach A: Universal Slot B Kernel Packer (Direct Boot)

[`bin/fedomakase-googlebook-slotb`](../bin/fedomakase-googlebook-slotb) works on any Linux distribution installed on `nvme0n1p31` (`/boot`) and `nvme0n1p32` (`/`).

Whenever your distro installs or updates a kernel:
1. It compiles (or copies) the two tiny Coreboot modules (`coreboot_table.ko` and `framebuffer-coreboot.ko`, ~35 KB total) against the target kernel's headers (`/lib/modules/$KVER/build`).
2. It builds a slim initramfs (`<= 40 MiB`) split across `vendor_boot_b` (`32 MiB`) and `init_boot_b` (`8 MiB`), keeping the massive `/usr/lib/firmware/xe/*` GPU blobs on the root filesystem (`/sysroot`) where they are loaded after root mount.
3. It packs `boot_b.img`, `init_boot_b.img`, and `vendor_boot_b.img` with [`bin/mkbootimg`](../bin/mkbootimg) and writes them directly to `/dev/nvme0n1p4`, `/dev/nvme0n1p6`, and `/dev/nvme0n1p8`.

#### Automatic Kernel Upgrade Hooks by Distro

- **Fedora / RHEL / CentOS (`/etc/kernel/install.d/99-googlebook-slotb.install`)**:
  Automatically installed by `fedomakase-googlebook-slotb`.
- **Arch Linux / Vanilla Omarchy / CachyOS (`/etc/pacman.d/hooks/99-googlebook-slotb.hook`)**:
  ```ini
  [Trigger]
  Operation = Install
  Operation = Upgrade
  Type = Path
  Target = usr/lib/modules/*/vmlinuz

  [Action]
  Description = Packing Googlebook Slot B Android Boot Images...
  When = PostTransaction
  Exec = /usr/local/bin/fedomakase-googlebook-slotb
  ```
- **Ubuntu / Debian (`/etc/kernel/postinst.d/zz-googlebook-slotb`)**:
  ```bash
  #!/bin/sh
  KVER="$1"
  exec /usr/local/bin/fedomakase-googlebook-slotb "$KVER"
  ```

---

### Approach B: Slot B as a Universal `kexec` Bootloader (Zero Distro Modifications)

If you want to boot **completely unmodified** Linux distributions or standard live USB installers without worrying about:
- the `32 MiB + 8 MiB` Slot B ramdisk partition size limit, or
- compiling `coreboot_table.ko` and `framebuffer-coreboot.ko` for every new distro kernel,

you can flash a **permanent tiny Linux + `kexec` bootloader** (LinuxBoot / Petitboot style) into Slot B (`boot_b` + `vendor_boot_b`):

```
Power On (Slot B Active)
  └── Depthcharge loads Slot B Mini-Kernel (7.2.11 + framebuffer-coreboot.ko)
        ├── Lights up 2880x1800 OLED display via Coreboot Framebuffer
        ├── Displays TUI Boot Menu:
        │     1. Boot Installed Linux on NVMe (reads /boot/loader/entries/*.conf or grub.cfg)
        │     2. Boot Live USB Installer (scans any plugged-in USB drive for ISO/EFI/boot)
        │     3. Reboot into Android 17 (flips GPT slot to Slot A & reboots)
        └── Runs `kexec -l <vmlinuz> --initrd=<full-size-initramfs.img> --command-line=... && kexec -e`
```

#### Why `kexec` Solves Both the Size Limit and the Display Driver Problem:
1. **No Initramfs Size Limit**: The target OS's kernel and full-size `initramfs` (`60–150 MiB`) stay on `/dev/nvme0n1p31` (`linux_boot`, `2 GiB` ext4) or on the USB thumb drive. Slot B never needs to be reflashed when you update or switch distros.
2. **No Custom Kernel Modules Needed in Target OS**:
   Depthcharge passes the display framebuffer inside the Coreboot table (`LB_TAG_FRAMEBUFFER`), which standard distro kernels ignore (`# CONFIG_GOOGLE_FRAMEBUFFER_COREBOOT is not set`).
   However, once the Slot B mini-kernel loads `framebuffer-coreboot.ko`, Linux knows the physical framebuffer address, resolution (`2880x1800`), and stride (`11520`). When `kexec` loads the target distro's kernel, `kexec` populates the standard x86 zero-page `boot_params.screen_info` (`VIDEO_TYPE_VLFB = 0x23`), which **every** standard Linux kernel's built-in `sysfb` + `simpledrm` driver supports natively out of the box!

---

## Part 2: How to Easily Switch Between Operating Systems

Because Depthcharge selects **Slot A (Android 17)** vs. **Slot B (Linux)** using the GPT attribute flags on `boot_a`/`vbmeta_a` (`p3`/`p15`) and `boot_b`/`vbmeta_b` (`p4`/`p16`), you can switch operating systems in several ways:

### 1. Switching from Linux → Android 17

From a terminal (or bound to a desktop shortcut / Waybar power menu):

- **Permanent Switch to Android 17** (boots Android until you switch back to Slot B):
  ```bash
  sudo googlebook-switch-slot android --reboot
  # or using the shorthand symlink:
  sudo boot-android --reboot
  ```

- **One-Shot Boot into Android 17** (`android-once`):
  ```bash
  sudo googlebook-switch-slot android-once --reboot
  ```
  This sets **Slot A** to `Priority=15, Tries=1, Successful=0` (`0x001f000000000000`) while keeping **Slot B (Linux)** at `Priority=14, Tries=0, Successful=1` (`0x010e000000000000`). Depthcharge decrements `Tries` to `0` when booting Slot A; if Slot A is not re-marked successful, the next reboot automatically returns to Linux.

### 2. Switching from Android 17 → Linux

Depending on whether you have root/ADB enabled in Android or are using hardware keys:

1. **Via Fastboot (No Android Root Required)**:
   - Reboot and hold **Volume Up** (or select **Fastboot** on the bootloader screen).
   - From any computer or phone over USB-C:
     ```bash
     fastboot --set-active=b && fastboot reboot
     ```
2. **Directly on the Laptop Inside Android 17 (Termux + Wireless Debugging / Root)**:
   - **If Rooted (`Magisk` / `KernelSU` / `apatch`)**:
     Run inside Termux on Android:
     ```bash
     su -c 'bootctl set-active-boot-slot 1 && svc power reboot'
     ```
   - **Without Root (Termux + Android Wireless Debugging)**:
     Pair Termux `adb` once with Android's built-in **Developer Options → Wireless Debugging** (`localhost`), then run a 1-click Termux Widget shortcut on your Android home screen:
     ```bash
     adb connect localhost:$PORT && adb shell reboot bootloader
     ```
3. **Via Slot B Boot Menu (Zero Commands Needed in Android)**:
   - Keep **Slot B** as your default slot (`Successful=1, Priority=14`) and include a 3-second boot menu in Slot B (`1. Linux [default]`, `2. Android 17`). When you boot Android via one-shot (`Tries=1, Successful=0`), simply clicking **Restart** inside Android drops you right back into the Slot B menu!
