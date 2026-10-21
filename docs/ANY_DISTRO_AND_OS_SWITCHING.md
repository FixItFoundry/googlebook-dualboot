# Other Linux Distributions and OS Switching

The tested hardware is the Acer Googlebook 14 (Moonstone). The toolkit has Fedora-oriented assumptions: dracut, systemd, the target kernel's development headers, an ext4 `/boot` on partition 11, and Linux root on partition 32. Other distributions need their own installation and boot validation; a kernel that can be packed is not proof that the entire distribution works.

## Packing an Installed Kernel

Use [`googlebook-pack-slotb`](../bin/googlebook-pack-slotb) from inside the installed system, with its `/boot` mounted. Follow the [chroot procedure](../README.md#step-3-partition--pack-slot-b) instead of running against the live installer's root. The packer finds `boot_b`, `init_boot_b`, and `vendor_boot_b` by partition label on the selected disk. The current Moonstone map is p14, p19, and p21 respectively; inspect the disk before writing.

Install the repository under `/usr/local/share/googlebook-dualboot` and link its packer into `/usr/local/bin` as shown in the README. This preserves access to the module sources and menu installer. The packer installs `/etc/kernel/install.d/95-googlebook-slotb.install`; distributions that do not call `kernel-install` need their own tested integration. Do not copy the old `fedomakase-googlebook-slotb` examples: that executable is not part of this branch.

## Boot Menu Limits

The scanner understands BootLoaderSpec entries and selected `/boot/vmlinuz-*` layouts. It also detects several common live USB layouts. This is not a universal GRUB, UKI, NixOS, or distro configuration parser. Display handoff and `kexec` behavior need validation for each target kernel. The fallback command line assumes a Btrfs `root` subvolume; distributions with another layout should supply correct BootLoaderSpec entries.

## Switching Slots

```bash
sudo googlebook-switch-slot status
sudo googlebook-switch-slot android --reboot
sudo googlebook-switch-slot android-once --reboot
sudo googlebook-switch-slot linux --reboot
```

The one-shot setting depends on Android not marking Slot A successful again. Do not promise that every Android reboot returns automatically to Linux. From Fastboot, `fastboot --set-active=b` selects B; it does not prove that Linux is still installed there.

## Android Updates

Android OTA promotion and post-OTA Linux recovery are unsupported. An A/B OTA can replace Linux in Slot B. The updated restore tools require positive identification of Linux in B and refuse Android or unknown images; they never copy B over A or rewrite `super`. See the [recovery limitations and upgrade steps](../README.md#restoring-an-existing-linux-slot-b).
