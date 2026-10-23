# Googlebook Slot B Restore (Android Phone App)

Restore Linux on your Acer Googlebook 14 (`Moonstone`) directly from an Android phone over **TCP Fastboot (`<IP>:5554`, recommended)** or an active Host-to-Host USB cable.

## Build & Install

```bash
# Build standalone APK (37 KB):
./android-restore-app/build-apk.sh

# Or bundle slotb-backup/*.img directly inside the APK:
./android-restore-app/build-apk.sh --bundle-images
```

Install `android-restore-app/bin/googlebook-slotb-restore.apk` on your phone.

## How to Restore Linux

1. **Load your backup images:** Copy `boot_b.img`, `init_boot_b.img`, and `vendor_boot_b.img` from `/boot/slotb-backup/` onto your phone and tap **Choose backup folder** (skip this step if you installed the `--bundle-images` APK).
2. **Enter Fastboot on the laptop:** Plug a USB-C Ethernet adapter into the Googlebook and boot into **Bootloader (Fastboot)** mode.
3. **Connect over LAN:** Select **TCP Fastboot (LAN)** in the app and enter the IP address shown on the laptop screen (e.g. `192.168.8.219:5554`).
4. **Flash:** Leave **Promote Android OTA (Slot B -> Slot A)** checked and tap **Flash & Restore Linux Slot B**.
   - If you just installed an Android system update, boot **Fedora** once when the menu appears — Linux will automatically move the updated Android system to Slot A and switch Slot B back to Linux.
