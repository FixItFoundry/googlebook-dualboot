# Moonstone sensor hub

`cros_ec_ishtp.c` and `cros_ec.h` are unmodified Linux v7.2.11 sources from `drivers/platform/chrome/` in https://github.com/gregkh/linux/tree/v7.2.11. Their SPDX licenses remain in the files. Only this bridge needs an out-of-tree build on Fedora 44; the IIO, sensor-hub and tablet-switch drivers ship with the kernel.

Build against the target kernel, never reuse a module across kernel versions:

```sh
make -C /lib/modules/$(uname -r)/build M="$PWD" modules
```

`firmware/intel/ish/ish_ptl_62b0f067_501de2f8.bin` was extracted read-only from this Moonstone's Android `vendor_a` on 2026-10-20. It identifies itself as `moonstone-ish-5_8_0-16667.2.138` and is 217088 bytes. SHA-256: `538c869adbe49b546041cc9e27b9ca898681dfa02c3f3728f87013554c99ce9c`. Preserve its DMI-specific filename; do not replace Intel's generic `ish_ptl.bin`. Firmware redistribution rights have not been established; this is a local hardware-validation copy.

The running Fedora 44 kernel `7.2.11-200.fc44.x86_64` loaded this firmware and the bridge successfully. Both accelerometers and the lid-angle IIO device appeared, and physical folding emitted tablet-mode on/off events. Keyboard suppression needs desktop handling on this build. After a Slot B rebuild and reboot, the firmware and bridge loaded automatically, both accelerometers and the lid-angle sensor returned, and the Hyprland watcher started at login. Later kernel versions and a full installer ISO build remain untested.
