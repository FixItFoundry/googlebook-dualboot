# Googlebook 14 (`Moonstone`) Slot B Restore — Android Phone Companion APK

Standalone, zero-dependency Android phone application ([`bin/googlebook-slotb-restore.apk`](bin/googlebook-slotb-restore.apk)) for switching boot slots or restoring Linux (`googleboot-menu`) on Slot B after an unrooted Googlebook OS (Android 17) OTA update—using only your Android phone over a **USB-C to USB-C cable (USB OTG)** or **TCP Fastboot (`tcp:<IP>:5554`)**.

---

## Features

1. **1-Tap Slot Switch (`Set Active Slot = B & Reboot`)**:
   - No `.img` files required. Sends `set_active:b` and `reboot` over USB-C or TCP when your Googlebook is at the Fastboot screen, returning directly to `googleboot-menu` / Linux.
2. **Post-OTA Safe Linux Restore (`Flash & Restore Linux Slot B`)**:
   - Queries `getvar:current-slot`:
     - If `current-slot=b` (meaning an Android OTA just wrote the new Googlebook OS into Slot B and switched active slot to B), the app stages your Linux backup images into **Slot A** (`boot_a`, `init_boot_a`, `vendor_boot_a`, `vbmeta_a`) and boots Linux via Slot A so the newly updated Android OTA in Slot B is untouched.
     - As soon as Linux boots on the laptop, `googlebook-gpu-init.service` automatically promotes the new Android OTA from **Slot B $\rightarrow$ Slot A** and restores Linux to **Slot B**.
     - If `current-slot=a` (or Post-OTA Safe Mode is unchecked), flashes `boot_b`, `init_boot_b`, `vendor_boot_b`, `vbmeta_b` directly to **Slot B** and sets Slot B active.
3. **Dual Transport Support**:
   - **USB-C Cable (OTG)**: Automatically detects the Googlebook's Fastboot USB interface (`VID 0x18d1`, class `0xFF/0x42/0x03`). Plugging the USB-C cable into your phone while the Googlebook is in Fastboot mode will automatically prompt to open **Googlebook Slot B Restore**.
   - **TCP Fastboot (`<IP>:5554`)**: Connects over Wi-Fi / LAN using Depthcharge's `FB01` TCP Fastboot protocol when the Googlebook has a USB-C Ethernet adapter connected.

---

## Installing & Loading Your Backup Images on Your Phone

1. Install the prebuilt APK onto your Android phone:
   ```bash
   adb install -r ./android-restore-app/bin/googlebook-slotb-restore.apk
   ```
2. Copy your Linux Slot B backup images (`boot_b.img`, `init_boot_b.img`, `vendor_boot_b.img`, `vbmeta_b.img` from `/boot/slotb-backup/` on `nvme0n1p11`) into your phone's `Download/slotb-backup/` folder:
   ```bash
   adb shell mkdir -p /sdcard/Download/slotb-backup
   adb push ./slotb-backup/*.img /sdcard/Download/slotb-backup/
   ```
   *(Alternatively, run `./android-restore-app/build-apk.sh --bundle-images` when `./slotb-backup/*.img` is populated to bake the 4 `.img` files directly inside the APK's `assets/` so no separate file push is needed.)*
