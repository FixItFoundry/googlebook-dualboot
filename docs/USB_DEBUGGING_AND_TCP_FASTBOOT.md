# Acer Googlebook 14 (`Moonstone` / `Google_Fatcat`) — USB-C Debugging, Wireless ADB & Bootloader TCP Fastboot

**Target Hardware:** Acer Googlebook 14 (`GP714-91N`, codename `moonstone`, board `Google_Fatcat`, Intel Panther Lake / Core Ultra Series 3 `x86_64`)  
**Firmware Analyzed:**
- Stock OS build: `Acer/moonstone/moonstone:17/CL3B.260622.271.R1` / `CL3B.260622.276.R4` (`6.18.21-android17-5`)
- AP Firmware (`ap-ec-fw.zip` in `vendor_a.img`): `ap-moonstone.ro-16650-89-0.rw-16650-163-0.ecrw-16667-2-142.bin` (Coreboot `v1.9308_26_0.0.22` + Depthcharge payload)

---

## 1. How to Connect to adb or fastboot

Unlike phones or tablets, the Acer Googlebook 14's USB-C ports are **USB Host ports only** (no USB Device/Gadget controller is wired to them in silicon). Because both your PC and the Googlebook are USB hosts, plugging a standard USB-C-to-USB-C cable between them will **not** show up in `adb` or `fastboot`.

| Mode | Standard USB-C to USB-C Cable | Standard **USB 3.x USB-A (PC) to USB-C** Cable | Active **Host-to-Host Bridge Cable** (`067b:27a7` Prolific PL27A1 / `18d1:5084`) | **Wi-Fi ADB + USB-C Ethernet** (`--adb` / `tcp:<IP>:5554`) |
| :--- | :---: | :---: | :---: | :---: |
| **OS ADB** (Googlebook OS) | No | **Yes** (via Intel xHCI `DbC` `18d1:5087`) | **Yes** | **Yes (Wireless ADB)** |
| **Bootloader Fastboot** (`depthcharge`) | No | No (`depthcharge` has no `DbC` driver) | **Yes** | **Yes (TCP `:5554` over Ethernet)** |

- **Easiest Setup (No Special USB Cable Needed):** Use **Wireless Debugging** (`adb pair`) inside Googlebook OS + a standard **USB-C Ethernet adapter** (`tcp:<ETH_IP>:5554`) for bootloader Fastboot.
- **If you want wired USB ADB in Googlebook OS:** Use a **USB 3.x (5 Gbps+) USB-A (on PC) to USB-C (on Googlebook)** cable (uses Intel xHCI Debug Capability `18d1:5087` via `/vendor/bin/adb-usb-bridge`).
- **If you want wired USB Fastboot in the Bootloader:** Requires an active **USB 3.0 Host-to-Host bridge cable** (`067b:27a7` Prolific PL27A1/PL27A7 or `18d1:5084` Google ALink).

---

## 2. Recommended Workflow: Wireless Debugging (ADB) $\rightarrow$ TCP Fastboot (`tcp:<ETH_IP>:5554`)

You do **not** need a Prolific Host-to-Host cable to unlock the bootloader, flash the Slot B Linux installer, or recover Slot B after an Android OTA. All you need is **Wi-Fi** (for Android Wireless Debugging) and a **USB-C Ethernet adapter** (for Depthcharge TCP Fastboot on port `5554`):

1. **Enable OEM Unlocking & Wireless Debugging in Googlebook OS:**
   - Open *Settings $\rightarrow$ About device* and tap **Build number** 7 times.
   - Open *Settings $\rightarrow$ System $\rightarrow$ Developer options* and toggle on **OEM unlocking** and **Wireless debugging**.
   - Click **Wireless debugging $\rightarrow$ Pair device with pairing code** and run on your computer:
     ```bash
     adb pair <GOOGLEBOOK_WIFI_IP>:<PAIR_PORT>
     adb connect <GOOGLEBOOK_WIFI_IP>:<CONNECT_PORT>
     ```
2. **Connect a USB-C Ethernet Adapter:**
   - Plug a USB-C Ethernet adapter into the Googlebook on the same local network (Depthcharge bootloader Fastboot uses wired Ethernet on TCP port `5554`).
3. **Unlock the Bootloader (First Time Only):**
   ```bash
   # Find the wired eth0 IP before rebooting to bootloader:
   adb shell ip -4 -o addr show dev eth0
   adb reboot bootloader
   fastboot -s tcp:<GOOGLEBOOK_ETH_IP>:5554 flashing unlock
   ```
4. **Flash or Restore Slot B Automatically via `--adb` or `--fastboot`:**
   - Both [`scripts/flash-slotb.sh`](../scripts/flash-slotb.sh) and [`bin/googlebook-restore-slotb`](../bin/googlebook-restore-slotb) support `--adb`, which automatically queries the Googlebook's wired `eth0` IP over Wireless ADB, runs `adb reboot bootloader`, waits for `tcp:<ETH_IP>:5554` to come online, and flashes Slot B over TCP Fastboot:
     ```bash
     # Initial Slot B installer flash from booted Googlebook OS (Wireless ADB -> TCP Fastboot):
     ./scripts/flash-slotb.sh ./slotb-installer --adb

     # Post-OTA or manual Slot B restore from booted Googlebook OS (Wireless ADB -> TCP Fastboot):
     ./bin/googlebook-restore-slotb --adb ./slotb-backup --reboot

     # Or directly when the laptop is already sitting at the Bootloader (Fastboot) screen:
     ./bin/googlebook-restore-slotb --fastboot tcp:<GOOGLEBOOK_ETH_IP> ./slotb-backup --reboot
     ```

---

## 3. USB-C Topology & Port Capabilities (`/vendor/etc/usb/usb_ports_static_info.json`)

| Port | Sysfs Path | Physical Location | Capabilities | Linked Host USB Paths | Linked Device USB Paths | Speeds / Display |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`port0`** | `.../cros_ec_ucsi.3.auto/typec/port0` | Left panel, upper-left | `host_mode`, `dbc`, `typec_debug_accessory_mode` (CCD/SuzyQable) | `0000:00:0d.0/usb2/2-0:1.0/usb2-port1`<br>`0000:00:14.0/usb3/3-0:1.0/usb3-port1` | `[]` *(None)* | USB3 20 Gbps, USB2 480 Mbps, DP UHBR10 |
| **`port1`** | `.../cros_ec_ucsi.3.auto/typec/port1` | Left panel, lower-left (right of `port0`) | `host_mode`, `dbc` | `0000:00:0d.0/usb2/2-0:1.0/usb2-port4`<br>`0000:00:14.0/usb3/3-0:1.0/usb3-port2`<br>USB4: `0000:00:0d.3/domain1/1-0/usb4-port3` | `[]` *(None)* | USB4 40 Gbps, USB3 20 Gbps, DP UHBR20 |
| **`port2`** | `.../cros_ec_ucsi.3.auto/typec/port2` | Right panel | `host_mode`, `dbc` | `0000:00:0d.0/usb2/2-0:1.0/usb2-port2`<br>`0000:00:14.0/usb3/3-0:1.0/usb3-port5` | `[]` *(None)* | USB3 10 Gbps, USB2 480 Mbps, DP HBR3 |

---

## 4. Reverse-Engineered Bootloader (`depthcharge`) Fastboot Reference

Extracted from `/firmware/ap-ec-fw.zip` $\rightarrow$ `images/ap-moonstone.ro-16650-89-0.rw-16650-163-0.ecrw-16667-2-142.bin` $\rightarrow$ FMAP region `FW_MAIN_A` (`0x00a02000`) $\rightarrow$ CBFS `fallback/payload` (load address `0x30000000`).

### A. Transport Selection (`fastboot_setup` @ `0x300246d7`)
```asm
300246d9: e8 d2 0e 00 00    call 0x300255b0   ; fastboot_setup_usb()
300246de: 49 89 c4          mov  %rax,%r12
300246e1: 48 85 c0          test %rax,%rax
300246e4: 75 0d             jne  0x300246f3   ; if USB H2H bridge found, use USB
300246e6: e8 32 0b 00 00    call 0x3002521d   ; otherwise call fastboot_setup_tcp()
```

### B. Complete Depthcharge `fastboot_cmds` Table (`0x30060a20` in `src/fastboot/fastboot.c`)

| Index | Command Prefix | `has_args` | Separator | Handler Address | Notes |
| :---: | :--- | :---: | :---: | :--- | :--- |
| 0 | `continue` | `0` | `\0` | `0x30022a18` | Boot active slot |
| 1 | `upload` | `0` | `\0` | `0x30022c18` | Max `0x5b4` (1460) bytes from staged buffer |
| 2 | `download` | `1` | `:` | `0x3002333a` | Stage image in RAM |
| 3 | `erase` | `1` | `:` | `0x30023335` | Erase GPT partition |
| 4 | `flash` | `1` | `:` | `0x3002325a` | Flash raw or sparse image to GPT partition |
| 5 | `getvar` | `1` | `:` | `0x30025ef6` | Query bootloader variables (`all`, `current-slot`, `partition-size:<part>`, etc.) |
| 6–13 | `oem cmdline` / `oem bootconfig` | `0/1` | `' '` | `0x300230b0`..`0x30023895` | Read/modify kernel cmdline or bootconfig in `misc` |
| 14 | `oem logs` | `0` | `\0` | `0x300234d9` | Read firmware/cbmem logs |
| 17 | `oem set-successful` | `1` | `:` | `0x30023a92` | `oem set-successful:<partition>:<0\|1>` |
| 18 | `oem set-priority` | `1` | `:` | `0x300239f9` | `oem set-priority:<partition>:<0-15>` |
| 19 | **`oem sha256`** | `1` | `:` | `0x30022ce3` | **`oem sha256:<part_name>[:<offset>[:<len>]]`** |
| 20–21 | `reboot` | `0/1` | `-` | `0x300229da`..`0x30022a43` | `reboot`, `reboot-bootloader`, `reboot-recovery`, `reboot-fastboot` |
| 22 | `set_active` | `1` | `:` | `0x300237f6` | `set_active:a` / `set_active:b` |
| 23 | `snapshot-update` | `1` | `:` | `0x300233cb` | `snapshot-update[:cancel]` |

> **Note:** `fetch` and `boot` (RAM boot) are not implemented in Moonstone's Depthcharge `fastboot_cmds` table. [`bin/googlebook-restore-slotb`](../bin/googlebook-restore-slotb) uses `oem sha256:vendor_boot_b:<offset>:<len>` to check the on-device `vendor_boot_b` bootconfig digest in place of `fetch`.
