# Persistent profile Flash layout

Status: v0.411 development. Read-only startup loading and guarded USB CDC programming are implemented; hardware validation is pending.

## Address map (SAMD21G18A)

| Region | Start | End inclusive | Size |
|---|---:|---:|---:|
| Bootloader | 0x00000 | 0x01FFF | 8 KiB |
| Application | 0x02000 | 0x3DFFF | 240 KiB |
| Reserved profile region | 0x3E000 | 0x3FFFF | 8 KiB |

The device has 256 KiB of internal Flash, 64-byte pages and 256-byte erase rows. A v1 image is 559 bytes (nine pages, three erase rows). Only the first image at the profile start is considered active; there is no multi-profile menu or file system. The remainder of the reservation is unused.

The project linker script is derived from the [Seeed XIAO variant linker](https://github.com/Seeed-Studio/ArduinoCore-samd/blob/master/variants/XIAO_m0/linker_scripts/gcc/flash_with_bootloader.ld) and retains the original LGPL notice. Both PlatformIO environments select it. No section is emitted in the reserved region; absolute linker symbols identify its bounds. An explicit assertion protects the initialized RAM data load image as well as the application text region.

At first full initialization, the loader tries the Flash image once per MCU reset. A missing or invalid image falls back to the embedded reference, explicit RAM staging takes priority, and short wake does not revalidate or copy the profile. Validation runs before the existing bus initialization, never inside a reply handler. The 0x01 bitmap is unchanged. Cold-start timing has not yet been measured with a programmed image.

## USB programming

Physically detach the camera (do not merely switch it off), leave the LOGIC_VCC sense divider connected, and power XIAO from USB. Close any serial monitor. Use the application CDC port, never the bootloader port.

```sh
python3 tools/upload_lens_profile.py --port /dev/ttyACM0 --detached --info
python3 tools/upload_lens_profile.py --port /dev/ttyACM0 --detached profiles/hanimex-135-f2-8.bin
python3 tools/upload_lens_profile.py --port /dev/ttyACM0 --detached --read /tmp/ukiyo-profile.bin
```

The tool requires pyserial; use the PlatformIO Python environment if it is unavailable. It sends `x` to disable auto-arm, then checks NVM information. The device refuses profile commands if either camera mode is active or A0 detects voltage. Before programming it requires 20 consecutive low A0 samples and checks geometry, absolute linker bounds, EEPROM fuse and region lock. The fuse must indicate no EEPROM reservation; it never changes fuses or unlocks regions. A0 is a voltage interlock, not proof of physical detachment.

The entire image is received in RAM as sequential 24-byte hex chunks, then validated before erasing anything. Only the first three rows of the reserved region are erased. The old header is erased first; page zero is programmed last. Interrupted writes may destroy the old profile but an incomplete image should fail validation and select the embedded fallback on reset. The firmware reads back and validates the programmed bytes; the host independently reads back all 559 bytes and compares them with the source. A failed or ambiguous commit is not automatically retried. Identical images skip the erase/write cycle. Reset/power-cycle before reconnecting the camera; the tool leaves emulation OFF.

The commands `profile info`, `profile begin`, `profile data <offset-hex> <hex-bytes>`, `profile commit`, `profile read <offset-hex>` and `profile abort` use the existing USB monitor. Incomplete transfers time out after 10 seconds of inactivity or cancel on a different monitor command/disconnect. No binary protocol, additional GUI or bootloader modification is required.

## Validation still required

- Verify the exact installed Seeed framework and PlatformIO linker invocation, ELF load ranges, map symbols, and final BIN size on both environments.
- Read the SAMD21 NVM User Row BOOTPROT and EEPROM reservation fuses on the actual XIAO; confirm the proposed slot is not reserved for EEPROM emulation. Do not change fuses or enable writes if there is a conflict. The firmware checks EEPROM and geometry before writes; confirm actual fuse values through `--info` before the first programming attempt.
- Check NVM lock regions and page/row geometry on hardware.
- Inspect the actual SAM-BA/UF2 bootloader and BOSSA upload command. A normal application erase may include the profile region even though it is absent from the linked image. The existing firmware upload workflow is not yet profile-preserving. Do not claim persistence across firmware upgrades until proven.
- Validate the implemented USB-only, bus-idle upload path and readback on actual hardware. Do not erase/write while the camera bus is active.
- Verify the erased, malformed, valid and interrupted-write cases, and measure cold handshake latency on a6000.

References: [Microchip SAM D21 memory architecture](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/memory-architecture/), [SAMD21 NVMCTRL](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/nvmctrl/), [PlatformIO XIAO board definition](https://github.com/platformio/platform-atmelsam/blob/develop/boards/seeed_xiao.json).

The maintainer confirmed compilation of the initial read-only v0.411 build. No actual Flash contents, fuse values, program operation or upload erase range have been measured yet.
