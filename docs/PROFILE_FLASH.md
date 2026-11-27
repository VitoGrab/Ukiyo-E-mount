# Persistent profile Flash layout

Status: v0.411 development. Independent profile programming and switching have passed on XIAO SAMD21 and Sony a6000. The maintainer also confirmed byte-for-byte native Flash retention during firmware upload without BOSSA full erase (commit `741c387`). The temporary backup/restore workflow has been removed from ordinary firmware uploads.

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
/opt/platformio/bin/python tools/upload_lens_profile.py --port /dev/ttyACM0 --detached --info
/opt/platformio/bin/python tools/upload_lens_profile.py --port /dev/ttyACM0 --detached profiles/hanimex-135-f2-8.bin
python3 tools/upload_lens_profile.py --port /dev/ttyACM0 --detached --read /tmp/ukiyo-profile.bin
```

For `--info`, the host reads back the validated Flash image and decodes the lens model directly from message 0x3F. A missing/invalid stored profile reports `PROFILE_LENS_MODEL=NONE` and indicates the embedded fallback. No firmware change or new protocol command is needed.

The VS Code task `Ukiyo: Upload Lens Profile` presents the supported prime profiles and requires explicit physical camera detachment. It uses the PlatformIO Python interpreter. When adding a supported catalog entry, also update the task's `lensProfile` picker (checked by a regression test), or use the CLI directly.

The tool requires pyserial; use the PlatformIO Python environment if it is unavailable. It sends `x` to disable auto-arm, then checks NVM information. The device refuses profile commands if either camera mode is active or A0 detects voltage. Before programming it requires 20 consecutive low A0 samples and checks geometry, absolute linker bounds, EEPROM fuse and region lock. The fuse must indicate no EEPROM reservation; it never changes fuses or unlocks regions. A0 is a voltage interlock, not proof of physical detachment.

The entire image is received in RAM as sequential 24-byte hex chunks, then validated before erasing anything. Only the first three rows of the reserved region are erased. The old header is erased first; page zero is programmed last. Interrupted writes may destroy the old profile but an incomplete image should fail validation and select the embedded fallback on reset. The firmware reads back and validates the programmed bytes; the host independently reads back all 559 bytes and compares them with the source. A failed or ambiguous commit is not automatically retried. Identical images skip the erase/write cycle. After successful host readback the uploader sends `profile reset`. The firmware checks that the bus is idle, the camera supply is absent, and Flash contains a valid profile, then acknowledges and calls CMSIS `NVIC_SystemReset()` after a short USB flush/delay. The host reopens the same application CDC port and checks `MODE=ARMED` and `AUTO_ARM=ENABLED` using the read-only status command. No profile write is retried if reset confirmation fails. `--info` and `--read` do not reset the MCU and still leave emulation OFF; reset/power-cycle before reconnecting after these read-only commands. The automatic reset path requires the updated firmware and awaits hardware regression.

The commands `profile info`, `profile begin`, `profile data <offset-hex> <hex-bytes>`, `profile commit`, `profile read <offset-hex>` and `profile abort` and `profile reset` use the existing USB monitor. Incomplete transfers time out after 10 seconds of inactivity or cancel on a different monitor command/disconnect. No binary protocol, additional GUI or bootloader modification is required.

## Validation still required

- Verify the exact installed Seeed framework and PlatformIO linker invocation, ELF load ranges, map symbols, and final BIN size on both environments.
- Retain the existing fuse/geometry/lock interlocks for every programming attempt; do not change fuses. See the measured values below.
- Regress the simplified firmware upload workflow in both build environments and whenever the BOSSA/PlatformIO package or bootloader changes. Native retention has passed on the maintainer's current installation.
- Verify malformed and interrupted-write cases and measure cold handshake latency on a6000. Erased-to-valid programming and two-lens switching have been demonstrated.

References: [Microchip SAM D21 memory architecture](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/memory-architecture/), [SAMD21 NVMCTRL](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/nvmctrl/), [PlatformIO XIAO board definition](https://github.com/platformio/platform-atmelsam/blob/develop/boards/seeed_xiao.json).

## Hardware evidence (2026-11-27)

Maintainer-reported XIAO SAMD21 application CDC upload using the v0.411 implementation, before the host-only diagnostics change in `20df55e`:

```text
PROFILE_BOOTPROT=7
PROFILE_EEPROM=7
PROFILE_LOCK=0xFFFF
PROFILE_VALID=0
PROFILE_UPLOAD=PASS (byte-for-byte readback verified)
```

A subsequent info command returned `PROFILE_VALID=1`. EEPROM is disabled and all lock regions were reported unlocked; no fuse changes were made. The maintainer then reported successful Helios -> Hanimex -> Helios switching on the a6000; see [EXIF evidence](LENS_PROFILES.md#hardware-evidence-2026-11-27).

The exact tested firmware hash was not supplied. The final uploader diagnostics patch has not been separately hardware-tested. The subsequent standard Ukiyo Upload task changed PROFILE_VALID from 1 to 0, demonstrating loss of the active profile. The maintainer subsequently confirmed the update wrapper at `426802f` works and reported `PROFILE_VALID=1` after firmware upload. The full update log and independent post-update lens-name comparison were not supplied. Interrupted writes and measured cold-start timing remain open.

## Firmware updates with native profile retention

The [PlatformIO SAM-BA builder](https://github.com/platformio/platform-atmelsam/blob/develop/builder/main.py) normally supplies `--erase`. The [Seeed SAM-BA monitor](https://github.com/Seeed-Studio/ArduinoCore-samd/blob/master/bootloaders/zero/sam_ba_monitor.c) implements its extended erase command from a starting address to the end of Flash. Neither `board_upload.maximum_size` nor the linker region constrains that command.

The project post script `tools/platformio_upload_wait.py` removes the explicit `--erase` (or standalone `-e`) flag from SAM-BA uploads, leaving write, verify, reset, port discovery and all other flags unchanged. Both XIAO environments inherit the script. [Upstream BOSSA D2xNvmFlash](https://github.com/shumatech/BOSSA/blob/master/src/D2xNvmFlash.cpp) enables automatic row erasing in the absence of a full erase.

### Native retention evidence (2026-11-27)

Maintainer-reported hardware test with commit `741c387`, using the temporary backup/restore wrapper as a safety net:

```text
PROFILE_VALID=1
PROFILE_RESTORE=ALREADY_PRESENT
PROFILE_FLASH_RETAINED=PASS
PROFILE_VALID=1
PROFILE_PRESERVED=PASS
CAMERA_EMULATION=ARMED
UPLOAD_RESULT=SUCCESS
```

The wrapper compared the post-upload image byte-for-byte against the pre-upload image before deciding not to restore it. This confirms retention without a profile write on the tested installation. The earlier backup/restore implementation (commit `426802f`) was successful but is no longer needed in ordinary uploads. Exact installed BOSSA version and production-environment regression were not supplied.

### Current workflow

All four ordinary Ukiyo firmware-upload tasks use the simplified `tools/upload_with_retry.py`: verify reference artifacts, build, upload through PlatformIO with retries. They do not open the application CDC port, request camera detachment, read the profile, create a recovery copy or issue profile writes. Initial firmware installation uses the same workflow. The firmware auto-arms on reset as before. Close any serial monitor before uploading.

```sh
python3 tools/upload_with_retry.py --environment seeed_xiao
```

The native PlatformIO upload target also uses the project post script. The reserved region is not hardware write-protected; other programmers, full erase commands, or incompatible uploader versions may still erase it. Old application bytes beyond the new firmware image may remain; this is not a full sanitization erase. Save any important custom profile separately (the explicit `--read` command remains available). Historical recovery snapshots are ignored by Git and are not removed by this change.

Only the profile-management task and CLI retain the physical-detachment confirmation and firmware-side interlocks. The absence of a firmware-upload prompt is not evidence that flashing with an electrically connected camera has been tested. Follow the existing hardware connection/power guidance.

Version remains v0.411.
