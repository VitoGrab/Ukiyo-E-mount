# Persistent profile Flash layout

Status: v0.411 development. Independent profile programming and switching have passed on XIAO SAMD21 and Sony a6000. The maintainer also confirmed byte-for-byte native Flash retention during firmware upload without BOSSA full erase (commit `741c387`). The subsequent changed-firmware upload failed verification with BOSSA 1.7; the project now selects BOSSA 1.9 with an explicit application offset. Changed-image upload and profile retention subsequently passed on the maintainer's XIAO.

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

The VS Code task `Ukiyo: Upload Lens Profile` runs `prepare_lens_profiles.py` through the uploader's `--select` mode, then presents the freshly prepared supported profiles in a numbered terminal menu. For a zoom, choose a default focal length in mm within the rated range (Enter = catalog default; q = cancel). The custom image is generated in RAM without replacing catalog files. Prime profiles have no additional prompt. The uploader then requests physical camera detachment confirmation [y/N]. It uses the PlatformIO Python interpreter. Catalog additions require no task configuration edits.

The tool requires pyserial; use the PlatformIO Python environment if it is unavailable. It sends `x` to disable auto-arm, then checks NVM information. The device refuses profile commands unless USB CDC is open and both camera modes are stopped. Before programming it requires 20 consecutive idle checks and checks geometry, absolute linker bounds, EEPROM fuse and region lock. The fuse must indicate no EEPROM reservation; it never changes fuses or unlocks regions. Since v0.5.0, A0 is not a programming interlock: direct 3V3 wiring makes it high even on USB alone. Physical camera detachment is mandatory; software cannot verify it from A0.

The entire image is received in RAM as sequential 24-byte hex chunks, then validated before erasing anything. Only the first three rows of the reserved region are erased. The old header is erased first; page zero is programmed last. Interrupted writes may destroy the old profile but an incomplete image should fail validation and select the embedded fallback on reset. The firmware reads back and validates the programmed bytes; the host independently reads back all 559 bytes and compares them with the source. A failed or ambiguous commit is not automatically retried. Identical images skip the erase/write cycle. Since v0.5.0 the default uploader leaves the bus OFF after verified readback; unplug USB fully before mounting the camera, so the next body-powered boot loads the stored profile. Optional `--reset` retains the legacy reset workflow for isolated-sense wiring only. In that mode the firmware checks that the bus is idle and Flash contains a valid profile, then acknowledges and calls CMSIS `NVIC_SystemReset()` after a short USB flush/delay. The host reopens the same application CDC port and checks `MODE=ARMED` and `AUTO_ARM=ENABLED` using the read-only status command. No profile write is retried if reset confirmation fails. `--info` and `--read` do not reset the MCU and still leave emulation OFF; reset/power-cycle before reconnecting after these read-only commands. The automatic reset path requires the updated firmware and passed hardware regression on XIAO/a6000.

The commands `profile info`, `profile begin`, `profile data <offset-hex> <hex-bytes>`, `profile commit`, `profile read <offset-hex>` and `profile abort` and `profile reset` use the existing USB monitor. Incomplete transfers time out after 10 seconds of inactivity or cancel on a different monitor command/disconnect. No binary protocol, additional GUI or bootloader modification is required.

## Validation still required

- Verify the exact installed Seeed framework and PlatformIO linker invocation, ELF load ranges, map symbols, and final BIN size on both environments.
- Retain the existing fuse/geometry/lock interlocks for every programming attempt; do not change fuses. See the measured values below.
- Regress the simplified firmware upload workflow in both build environments and whenever the BOSSA/PlatformIO package or bootloader changes. Identical-image native retention passed on BOSSA 1.7, but changed-image verification failed. The BOSSA 1.9 changed-image upload and Hanimex profile retention have now passed in the standard environment; production-environment and future toolchain regression remain open.
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

The project post script `tools/platformio_upload_wait.py` configures SAM-BA uploads with write, verify, reset, native USB and explicit application offset `0x2000`, without `--erase`. Both XIAO environments inherit the script and the BOSSA 1.9 package selection. [Upstream BOSSA 1.9.1 D2xNvmFlash](https://github.com/shumatech/BOSSA/blob/1.9.1/src/D2xNvmFlash.cpp) implements software row erasing in the absence of a full erase.

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

The wrapper compared the post-upload image byte-for-byte against the pre-upload image before deciding not to restore it. This confirms retention without a profile write on the tested installation. The earlier backup/restore implementation (commit `426802f`) was successful. This retention test did not establish that a changed firmware image could be programmed without a full erase.

### Changed-image failure and uploader correction

The subsequent v0.411 firmware with software reset compiled to 41900 bytes (655 pages, previously 652) but BOSSA 1.7.0 (`tool-bossac@1.10700.190624`) failed checksum verification. The installed Atmel SAM platform is 9.0.0. In the [Arduino BOSSA legacy NvmFlash implementation](https://github.com/arduino/BOSSA/blob/arduino/src/NvmFlash.cpp), `eraseAuto(bool)` is a no-op; the accelerated buffer-write path likewise lacks a preceding row erase. Identical-image retention is therefore not sufficient evidence of changed-image programming.

The [Atmel SAM 9.0.0 package manifest](https://github.com/platformio/platform-atmelsam/blob/v9.0.0/platform.json) explicitly supports `tool-bossac` in the 1.10900 series. The project now selects `platformio/tool-bossac@~1.10900.0` rather than the default 1.7 release. The post script explicitly sets `--offset 0x2000` (BOSSA 1.9 defaults to zero); never run a 1.9 upload without this offset on this board. The application limit and linker remain unchanged. The maintainer confirmed successful changed-image upload and profile retention on the target (see below). If the failed upload left the application unable to boot, enter the existing bootloader using the documented double-reset procedure and retry the normal task. Do not erase the entire chip.

### BOSSA 1.9 and automatic reset hardware acceptance

Maintainer-reported results on 2026-11-27, with firmware/tooling at commit `862451d`:

- Changed application image: 41900 bytes, 655 pages; BOSSA write completed in 0.996 s, verification in 0.463 s, PlatformIO upload in 6.477 s; `UPLOAD_RESULT=SUCCESS`.
- Post-update `--info`: `PROFILE_VALID=1`, `PROFILE_LENS_MODEL=HANIMEX AUTOMATIC 135mm f2.8`. No profile restoration was performed.
- VS Code `Ukiyo: Upload Lens Profile` selected `pentacon-200-4.bin`:

```text
PROFILE_VALID=1
PROFILE_UPLOAD=PASS (byte-for-byte readback verified)
PROFILE_LENS_MODEL=PENTACON 200mm f4
PROFILE_RESET=ACCEPTED
PROFILE_RESET=PASS
CAMERA_EMULATION=ARMED
```

- A subsequent independent `--info` returned `PROFILE_VALID=1` and `PROFILE_LENS_MODEL=PENTACON 200mm f4`. The maintainer confirmed the new profile on the camera.
- Rubinar 500 mm f/5.6 fixed-aperture profile was also checked on the camera; aperture remained f/5.6 and could not be changed.

The implemented prime-profile workflow is accepted on the tested XIAO/a6000 configuration. The exact installed BOSSA 1.9 package revision, production build hardware regression, full host test suite, cold-start timing, interrupted-write tests and other camera models remain outside this reported acceptance. Stage 6A constant-aperture zoom profiles use the same USB transfer format; camera validation is pending (issue #5).

### Current workflow

The single `Ukiyo: Build & Upload` task uses `tools/upload_with_retry.py`: prepare profiles and identity, build the selected default environment, upload through PlatformIO with retries. They do not open the application CDC port, request camera detachment, read the profile, create a recovery copy or issue profile writes. Initial firmware installation uses the same workflow. The firmware auto-arms on reset as before. Close any serial monitor before uploading.

```sh
python3 tools/upload_with_retry.py
```

The native PlatformIO upload target also uses the project post script. The reserved region is not hardware write-protected; other programmers, full erase commands, or incompatible uploader versions may still erase it. Old application bytes beyond the new firmware image may remain; this is not a full sanitization erase. Save any important custom profile separately (the explicit `--read` command remains available). Historical recovery snapshots are ignored by Git and are not removed by this change.

Only the profile-management task and CLI retain the physical-detachment confirmation and firmware-side interlocks. The absence of a firmware-upload prompt is not evidence that flashing with an electrically connected camera has been tested. Follow the existing hardware connection/power guidance.

Version remains v0.411.

### v0.5.0 direct-power maintenance correction

Direct LOGIC_VCC-to-3V3 wiring makes A0 high on USB power alone. Profile operations retain CDC, stopped-bus, layout, CRC, timeout and readback checks but no longer require A0 low. The runtime communication gate remains unchanged. The uploader waits up to 15 s for the initial stop acknowledgement because a USB-only boot can enter the existing bounded camera-init wait. The numbered catalog is paginated according to terminal height, preserving all 16 prime entries including Helios at 1. Default upload does not reset into a false camera-power detection. Always physically detach the camera before connecting USB; never attach the camera until USB is disconnected. Hardware verification of this correction remains pending.

Stage 6B uses the unchanged v0.5.1 USB protocol and verifies the complete custom image after programming. Upload and `--info` report `PROFILE_FOCAL_LENGTH_MM` in addition to the model. The value describes the stored image, not a measured optical setting.
