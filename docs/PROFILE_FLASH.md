# Persistent profile Flash layout

Status: v0.411 development, read-only. No NVM erase/program operations are implemented.

## Address map (SAMD21G18A)

| Region | Start | End inclusive | Size |
|---|---:|---:|---:|
| Bootloader | 0x00000 | 0x01FFF | 8 KiB |
| Application | 0x02000 | 0x3DFFF | 240 KiB |
| Reserved profile region | 0x3E000 | 0x3FFFF | 8 KiB |

The device has 256 KiB of internal Flash, 64-byte pages and 256-byte erase rows. A v1 image is 559 bytes (nine pages, three erase rows). Only the first image at the profile start is considered active; there is no multi-profile menu or file system. The remainder of the reservation is unused.

The project linker script is derived from the [Seeed XIAO variant linker](https://github.com/Seeed-Studio/ArduinoCore-samd/blob/master/variants/XIAO_m0/linker_scripts/gcc/flash_with_bootloader.ld) and retains the original LGPL notice. Both PlatformIO environments select it. No section is emitted in the reserved region; absolute linker symbols identify its bounds. An explicit assertion protects the initialized RAM data load image as well as the application text region.

At first full initialization, the loader tries the Flash image once per MCU reset. A missing or invalid image falls back to the embedded reference, explicit RAM staging takes priority, and short wake does not revalidate or copy the profile. Validation runs before the existing bus initialization, never inside a reply handler. The 0x01 bitmap is unchanged. Cold-start timing has not yet been measured with a programmed image.

## Before enabling writes

- Verify the exact installed Seeed framework and PlatformIO linker invocation, ELF load ranges, map symbols, and final BIN size on both environments.
- Read the SAMD21 NVM User Row BOOTPROT and EEPROM reservation fuses on the actual XIAO; confirm the proposed slot is not reserved for EEPROM emulation. Do not change fuses or enable writes if there is a conflict. The current change performs only bounded memory reads.
- Check NVM lock regions and page/row geometry on hardware.
- Inspect the actual SAM-BA/UF2 bootloader and BOSSA upload command. A normal application erase may include the profile region even though it is absent from the linked image. The existing firmware upload workflow is not yet profile-preserving. Do not claim persistence across firmware upgrades until proven.
- Implement a USB-only, bus-idle upload path with validation, readback, and a safe interrupted-write fallback. Do not erase/write while the camera bus is active.
- Verify the erased, malformed, valid and interrupted-write cases, and measure cold handshake latency on a6000.

References: [Microchip SAM D21 memory architecture](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/memory-architecture/), [SAMD21 NVMCTRL](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/nvmctrl/), [PlatformIO XIAO board definition](https://github.com/platformio/platform-atmelsam/blob/develop/boards/seeed_xiao.json).

No actual Flash contents, fuse values or upload erase ranges have been measured yet.
