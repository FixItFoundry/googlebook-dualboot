# Experimental Windows Boot Research

Windows 11 boot is not a supported feature of this toolkit. The previous guide presented a proposed UEFI bridge and driver compatibility as a completed installation path; those claims have been withdrawn pending hardware validation.

Moonstone boots through Coreboot and Depthcharge. A possible research direction is a RAM-resident UEFI payload that could launch Windows Boot Manager without replacing the SPI firmware. The included [`build-uefi-bridge.sh`](../googleboot-menu/scripts/build-uefi-bridge.sh) produces an experimental U-Boot **ELF** artifact, defaulting to `./uefi-bridge.elf`. It does not convert that ELF into a Linux bzImage or validate its use with `kexec`. Renaming an ELF to `.bzImage` does not supply that missing integration.

The scanner only offers the experimental Windows entry when a separately supplied `/boot/uefi-bridge.bzImage` exists. A successful U-Boot build alone cannot establish Windows boot, UEFI runtime-service support, display handoff, or driver compatibility.

The ChromeOS EC driver at [coolstar/crosecbus](https://github.com/coolstar/crosecbus) is a research reference. Matching an ACPI identifier or transport protocol is not enough to declare the Moonstone Windows keyboard, battery, touchpad, audio, or thermal controls supported.

There is no validated Windows partitioning recipe here. Preserve the factory partition labeled `userdata`; do not format p31 based on the previous version of this document. The Linux tools currently expect `/boot` on p11 and root on p32. Verify the complete disk layout and backups before any future Windows installation work.
