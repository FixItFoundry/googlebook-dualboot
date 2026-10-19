# `googleboot-menu` — Graphical DRM/KMS + `kexec` Universal Bootloader

`googleboot-menu` turns the Acer Googlebook 14 (`Moonstone`) Slot B partitions (`boot_b` + `init_boot_b` + `vendor_boot_b`) into a permanent, graphically customizable **Petitboot-style `kexec` bootloader** running at the full **`2880x1800` OLED** resolution via `framebuffer-coreboot.ko` + `simpledrm`.

![GoogleBoot Main Screen](theme/preview-main.png)

![GoogleBoot LUKS2 Unlock Modal](theme/preview-luks.png)

---

## Key Features

1. **Zero External GUI Library Bloat (~65 KB Binary)**:
   - Written in self-contained C (`src/googleboot-menu.c`) using raw Linux kernel DRM/KMS dumb-buffer ioctls (`/dev/dri/card0`), Signed Distance Field (SDF) anti-aliased rounded cards, radial lighting, and `evdev` (`/dev/input/event*`) input.
   - Requires only `libc`, `libm`, and `libz`—adding `< 70 KB` to the `32 MiB` `vendor_boot_b` ramdisk.
2. **Instant Local Graphical Theme Preview (`make preview`)**:
   - Compiles natively on both **macOS** and **Linux**.
   - Edit [`theme/theme.conf`](theme/theme.conf) and run `make preview` to render `2880x1800` PNG previews (`theme/preview-main.png` and `theme/preview-luks.png`) in under 200ms without rebooting.
   - On the laptop, `googleboot-menu` reads `/boot/googlebook-theme/theme.conf` live from `nvme0n1p31` (`linux_boot`), so you can tweak colors, card dimensions, titles, and timeouts from Linux without reflashing Slot B.
3. **Petitboot-Style OS Discovery (`bin/googleboot-scan`)**:
   - **Installed Linux Distros**: Automatically parses BootLoaderSpec (`/boot/loader/entries/*.conf`) and `/boot/vmlinuz-*` on `nvme0n1p31`, unlocks LUKS2 (`nvme0n1p32`) via the graphical passphrase modal, and `kexec`s into any distro's kernel + full-size initramfs (no 40 MiB ramdisk limit, and `kexec` passes `boot_params.screen_info` so standard distro kernels get display output without custom modules).
   - **Android 17 (Slot A One-Shot)**: Arms Slot A with `Priority=15, Tries=1, Successful=0` while keeping Slot B (`googleboot-menu`) at `Priority=14, Successful=1`. Rebooting from Android automatically returns to `googleboot-menu`.
   - **Windows 11 Bare-Metal (via UEFI Bridge)**: Chainloads `/boot/uefi-bridge.bzImage` (`Das U-Boot x86_64` built with `CONFIG_VIDEO_COREBOOT=y` + `CONFIG_EFI_LOADER=y` + `CONFIG_NVME=y`) via `kexec`, providing UEFI Boot/Runtime Services + GOP framebuffer to launch `/EFI/Microsoft/Boot/bootmgfw.efi` bare-metal without touching the laptop's SPI flash ROM.
   - **Live USB / ISO Hotplug (`R` to Rescan)**: Scans any plugged-in USB stick (`/dev/sd*`) for Fedora/Omarchy, Arch, or Ubuntu/Debian Live ISOs and `kexec`s directly into the live installer.

---

## Building & Previewing Themes

```bash
cd googleboot-menu
make preview
```

Outputs:
- `theme/preview-main.png` (`2880x1800`)
- `theme/preview-luks.png` (`2880x1800`)
