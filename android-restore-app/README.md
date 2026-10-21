# Googlebook Slot B Restore for Android Phones

This app selects Slot B or restores an **existing, positively identified Linux Slot B** over USB-C OTG or TCP Fastboot. It never stages Linux into Slot A or promotes Android OTA metadata. Post-OTA recovery is unsupported.

## Build and Install

Build on macOS with Java available using `./android-restore-app/build-apk.sh`, then install `android-restore-app/bin/googlebook-slotb-restore.apk`. The script uses the cached debug signing key; retain that key to update an existing installation without changing its signer. The bundled APK is built from this revision; older APKs retain the unsafe staging behavior.

## Choose Backups and Restore

1. Copy `boot_b.img`, `init_boot_b.img`, and `vendor_boot_b.img` from the laptop's `/boot/slotb-backup/` into a folder on your phone.
2. Tap **Choose backup folder** and select that subfolder using Android's system picker. Select the `slotb-backup` subfolder rather than the entire Downloads root. The app retains access across restarts.
3. Connect the laptop in Fastboot mode, select USB or TCP, and check status.
4. Tap **Flash & Restore Linux Slot B**. The app validates all three backups and partition capacities, then reads the existing `vendor_boot_b` header and bootconfig before any flash.

Fastboot `fetch` support is required. A missing permission, incomplete backup, unsupported read-back, damaged vendor image, or Android image in Slot B stops the restore before writing. The active-slot letter is never used to infer the installed OS. `vbmeta` images are not flashed.

With no selected folder, app-specific external storage and bundled `assets/slotb-backup/` images remain available. `./android-restore-app/build-apk.sh --bundle-images` includes files from the repository's `slotb-backup/` directory. With a selected folder, all images must come from that folder; missing images are not silently mixed with another backup set.

**Set Slot B & Reboot** only changes the selected boot slot. After an OTA, B may contain Android rather than Linux. The app does not establish a safe post-OTA recovery path.

The build and fixture tests do not replace testing on a phone and a Moonstone laptop. Keep a complete disk backup and tested factory recovery available.
