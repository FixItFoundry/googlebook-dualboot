# Bare-Metal Windows 11 & ChromeOS EC (`crosecbus`) on the Acer Googlebook 14 (`Moonstone` / Intel Panther Lake)

This document details the architecture and driver analysis for booting **bare-metal Windows 11** alongside **Android 17 (Slot A)** and **Linux (Slot B)** on the **Acer Googlebook 14 (`GP714-91N`, `Moonstone`, Intel Panther Lake `x86_64`)** using **`googleboot-menu`** and a **`kexec` UEFI Bridge**, without modifying the laptop's SPI flash ROM.

---

## 1. Why a `kexec` UEFI Bridge is Required (and How It Works)

### The Firmware Constraint
- Standard PC motherboards ship with **UEFI firmware** in SPI flash, which provides `EFI_BOOT_SERVICES`, `EFI_RUNTIME_SERVICES`, and an `EFI_GRAPHICS_OUTPUT_PROTOCOL` (GOP) display. When GRUB on a standard PC runs `chainloader /EFI/Microsoft/Boot/bootmgfw.efi`, GRUB simply calls the motherboard's `EFI_BOOT_SERVICES->LoadImage()` and `StartImage()`.
- The Acer Googlebook 14 (`Moonstone`) ships with **Coreboot + Depthcharge** in SPI flash. Depthcharge only boots **Android Boot Image v4** partitions (`boot_a`/`boot_b`) containing a Linux `bzImage` kernel and ramdisk, and passes hardware state via the **Coreboot table** (`LB_TAG_FRAMEBUFFER`, `LB_TAG_MEMORY`) plus ACPI tables.
- Flashing a full UEFI ROM (`MrChromebox edk2`) to the SPI flash would replace Depthcharge and break native **Slot A Android 17 (Aluminum OS)** verified boot.

### The Solution: RAM-Resident UEFI Bridge (`uefi-bridge.bzImage`)
By placing **`googleboot-menu`** (Linux `7.2.11` + `framebuffer-coreboot.ko` + `kexec`) in **Slot B**, we can chainload a **RAM-resident UEFI firmware payload** (`Das U-Boot x86_64` built for Coreboot with `CONFIG_EFI_LOADER=y`, or `Tianocore EDK2 UefiPayloadPkg`) via `kexec`:

```
Power On (Slot B Active)
  └── Depthcharge boots Slot B (Linux 7.2.11 + googleboot-menu)
        └── User selects [Windows 11] in the 2880x1800 Graphical Menu
              └── `kexec -l /boot/uefi-bridge.bzImage && kexec -e`
                    └── U-Boot x86_64 (Coreboot payload) starts in < 0.5s:
                          ├── Reads Coreboot table (LB_TAG_FRAMEBUFFER 2880x1800)
                          ├── Initializes UEFI Boot/Runtime Services + EFI GOP Display
                          ├── Scans NVMe & USB (CONFIG_NVME_PCI + CONFIG_USB_XHCI_PCI)
                          └── Executes `/EFI/Microsoft/Boot/bootmgfw.efi` -> Bare-Metal Windows 11
```

This gives you **GRUB-style one-click chainloading into bare-metal Windows 11** while keeping the factory Coreboot + Depthcharge SPI flash and Slot A Android 17 100% intact.

---

## 2. Source Audit: [`coolstar/crosecbus`](https://github.com/coolstar/crosecbus) on Intel Panther Lake (`Moonstone`)

We audited the source code of CoolStar's Windows ChromeOS Embedded Controller bus driver ([`coolstar/crosecbus`](https://github.com/coolstar/crosecbus)) against the Linux `7.0`/`7.2` `cros_ec_lpcs.ko` driver (`drivers/platform/chrome/cros_ec_lpc.c`) and the Acer Googlebook 14 (`Moonstone`) hardware architecture.

### Finding 1: `crosecbus` Already Supports Panther Lake's EC Transport (`ACPI\GOOG0004`)
1. **Identical ACPI Device ID (`ACPI\GOOG0004`)**:
   - On Moonstone, Coreboot exposes the ChromeOS Embedded Controller in the DSDT as `ACPI\GOOG0004`.
   - `crosecbus/crosecbus.inf` binds directly to `ACPI\GOOG0004`:
     ```ini
     [Standard.NT$ARCH$]
     %CrosEcBus.DeviceDesc%=CrosEcBus_Device, ACPI\GOOG0004
     ```
   - Upstream Linux `7.2.11` (`cros_ec_lpcs.ko`) uses the exact same `acpi*:GOOG0004:*` binding with **zero** Panther Lake-specific kernel patches.

2. **Identical x86 eSPI / LPC I/O Port Protocol (`comm-lpc.c` & `comm-mec_lpc.c`)**:
   - On Intel Panther Lake, just like Meteor Lake, Alder Lake, and Tiger Lake, the EC communicates with the host CPU over the Intel **eSPI-to-LPC bridge** mapped to the fixed ChromeOS x86 I/O ports defined in `crosecbus/ec_commands.h`:
     - `0x200` (`EC_LPC_ADDR_HOST_DATA`) & `0x204` (`EC_LPC_ADDR_HOST_CMD`)
     - `0x800..0x8FF` (`EC_LPC_ADDR_HOST_PACKET` / Protocol v3 packet window)
     - `0x900..0x9FF` (`EC_LPC_ADDR_MEMMAP`, starting with `"EC"` signature at `0x900 + 0x20`)
   - In `comm_init_lpc()` (`crosecbus/comm-lpc.c`), the driver:
     1. Verifies that I/O ports `0x200` and `0x204` are not both `0xFF`.
     2. Checks if the EC uses Microchip MEC EMI indirect addressing (`comm_init_lpc_mec()` at `0x800..0x807`) and checks for `"EC"` at `EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID`.
     3. If not MEC, falls back to direct linear LPC/eSPI byte I/O (`ec_lpc_read_bytes` / `ec_lpc_write_bytes` used by Nuvoton `NPCX` and ITE `IT8xxx2` ChromeOS ECs) and negotiates **Protocol v3** (`EC_HOST_CMD_FLAG_VERSION_3`).
   - Because this eSPI I/O port window is architectural across all modern x86 ChromeOS/Googlebook boards, **`crosecbus` does not require protocol reverse engineering for Panther Lake**.

3. **Interrupt Handling (`ConnectInterrupt`) on Panther Lake**:
   - In [`crosecbus.inf`](https://github.com/coolstar/crosecbus/blob/master/crosecbus/crosecbus.inf), `ConnectInterrupt` defaults to `0`:
     ```ini
     [CrosEcBus_AddReg]
     ; Set to 1 to connect the first interrupt resource found, 0 to leave disconnected
     HKR,Settings,"ConnectInterrupt",0x00010001,0
     ```
   - This means `crosecbus` initializes and communicates with the EC (`EC_CMD_GET_VERSION`, `EC_CMD_GET_FEATURES`, battery/thermal memmap reads, backlight, and host commands) **even before** the Intel Panther Lake GPIO controller driver is loaded in Windows.
   - Once Intel's **Panther Lake Serial IO GPIO (`iaLPSS2_GPIO2_PTL`)** driver is installed in Windows, `ConnectInterrupt` can be enabled (`1`) if MKBP host event interrupts (`GpioInt` on `GOOG0004`) are desired.

---

## 3. Complete Windows 11 Bare-Metal Driver Matrix for Googlebook 14 (`Moonstone`)

| Subsystem | Hardware ID / Interface | Windows 11 Driver Source | Status |
| :--- | :--- | :--- | :--- |
| **GPU / Display (`2880x1800` OLED)** | `PCI\VEN_8086&DEV_B080` (Intel Xe2 / Panther Lake Graphics) | Standard Intel Arc / Core Ultra (Panther Lake) Windows 11 DCH Driver (+ UEFI GOP pre-driver fallback) | Native |
| **Wi-Fi 7 & Bluetooth** | Intel BE211 (`PCI\VEN_8086&DEV_...` CNVio3) | Standard Intel Wi-Fi 7 BE211 Windows 11 Driver | Native |
| **NVMe Storage & USB4 / Thunderbolt** | Standard PCIe Gen4 NVMe + Intel USB4 / XHCI | Built-in Windows 11 `stornvme.sys` + `usbxhci.sys` | Native |
| **Embedded Controller (Battery / Thermal / Charger)** | `ACPI\GOOG0004` (eSPI/LPC `0x200`/`0x800`/`0x900`) | [`coolstar/crosecbus`](https://github.com/coolstar/crosecbus) + `crosec` battery/thermal child drivers | Compatible (LPC v3) |
| **Internal Keyboard** | `ACPI\GOOG000A` / i8042 PS/2 (`0x60`/`0x64`) | CoolStar `croskeyboard` (or standard PS/2 with `ACPI\GOOG000A` INF match) | Compatible |
| **Serial IO (I2C / GPIO for Touchpad)** | Intel Panther Lake LPSS I2C & GPIO (`PCI\VEN_8086&DEV_...`, `INTC10E2`) | Intel Panther Lake Serial IO Windows 11 Driver (`iaLPSS2_I2C_PTL`, `iaLPSS2_GPIO2_PTL`) + `crostouchpad` (`I2C HID`) | Requires Intel PTL Serial IO INF |
| **Audio (SOF SoundWire / I2S)** | Intel Panther Lake Smart Sound Technology (SST) | CoolStar SOF Audio / USB-C or Bluetooth Audio until PTL SST topology is mapped | Work in progress |

---

## 4. Recommended Triple-Boot NVMe Partition Layout (1 TB / 2 TB Drive)

When setting up **Android 17 + Linux + Bare-Metal Windows 11** on the internal NVMe (`nvme0n1`):

| Partition | Label | Size | Filesystem | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| `nvme0n1p1`–`p30` | *(Stock Googlebook)* | `~476 GiB` | Android A/B + `super` + `userdata` | **Slot A: Android 17 (Aluminum OS)** + **Slot B: `googleboot-menu` (`boot_b`/`init_boot_b`/`vendor_boot_b`)** |
| `nvme0n1p31` | `linux_boot` | `2 GiB` | `ext4` | Linux `/boot` + `/boot/googlebook-theme/` + `/boot/uefi-bridge.bzImage` |
| `nvme0n1p32` | `linux_root` | `250 GiB` | `LUKS2` (`btrfs`) | **Fedomakase / Arch / Ubuntu Linux** (`subvol=root`, `subvol=home`) |
| `nvme0n1p33` | `ESP` | `512 MiB` | `FAT32` (`EF00`) | **Windows EFI System Partition** (`/EFI/Microsoft/Boot/bootmgfw.efi`) |
| `nvme0n1p34` | `Windows` | `~200 GiB` | `NTFS` (`0700`) | **Bare-Metal Windows 11 (`C:\`)** |
