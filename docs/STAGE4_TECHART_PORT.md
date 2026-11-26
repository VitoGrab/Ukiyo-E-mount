# v0.4.0 — Techart EA9 protocol-core port to XIAO SAMD21

> Historical development record. Commands, paths and status labels below describe the original stage, not the current v0.47 firmware. See the README for the current workflow.

## Status

**Experimental integration package, not a camera-validated release.** The original Techart `emount.c` and `emount_packets.c` are imported from the MIT-licensed project, not replaced with another hand-written state machine. The importer rejects unexpected GPIO code changes. The port has not been built on the target PlatformIO toolchain or tested against the actual Sony A6000 at the time of packaging.

Core sources: https://github.com/weiziqian/E-mount-impl-on-Techart-EA9

The v0.3.x diagnostic, offline session simulation and physical-bench tests are retained. Do not run old bench tests while attached to a camera.

## Hardware mapping — unchanged

| Sony E contact | Signal | Wire | XIAO |
|---|---|---|---|
| 3 | LOGIC_GND | Black | GND |
| 4 | BODY_VD_LENS | Orange | D8 (input) |
| 5 | LOGIC_VCC | Red | D0/A0 **via an external 100k/100k divider ONLY** |
| 6 | LENS_CS_BODY | Purple | D10 (output only in active mode, with series resistance) |
| 7 | RXD (lens -> body) | Green | D6 (UART TX only in active mode, with series resistance) |
| 8 | TXD (body -> lens) | Blue | D7 (UART RX) |
| 9 | BODY_CS_LENS | Yellow | D9 (input) |
| 10 | LENS_XDETECT | Brown | 680 ohm to Sony E pin 3 |

Sony E contacts 1 and 2 are not connected. The LOGIC_VCC wire must **never** be connected to XIAO `3V3`.

### VCC sense divider

```text
Sony E contact 5 (red) --- 100 kΩ ---+--- XIAO D0/A0
                                     |
                                   100 kΩ
                                     |
Sony E contact 3 (black) -------------+--- XIAO GND
```

Expected D0 voltage when the camera's LOGIC_VCC is 3.15 V: around 1.575 V. Firmware's threshold with a 10-bit, 3.3 V ADC reference is 370 counts (~2.39 V at pin 5). This threshold has **not** been calibrated on the user's physical board. The ADC reading and estimated supply voltage are printed by `s`. Independently measure voltage at D0 before connecting it.

**Power-down caution:** an externally USB-powered MCU can back-power an unpowered camera through GPIO even with a software guard. Use appropriate current-limiting and verify OFF-state behavior experimentally. This is not a certified hot-plug-safe interface. Do not attempt active TX if you cannot verify both the divider and the output-line electrical protection. The firmware does not substitute for a hardware-level isolator.

## Import upstream code

The package includes the importer rather than pretending that a text-only reimplementation is Techart's original stack. The XIAO-side integration code is included in this patch; the upstream sources are retrieved with Git on your LXC host:

```bash
cd /root/projects/Ukiyo-E-mount
python3 tools/import_techart.py
# Alternative using an already cloned source:
# python3 tools/import_techart.py --source /path/to/E-mount-impl-on-Techart-EA9
/opt/platformio/bin/pio run
```

The importer copies the MIT license and source commit ID. It imports `emount.c`, `emount_packets.c`, `optical_file.c`, headers and applies small, **audited** changes to board-specific register access, startup power-loss exits, and optics constants. If a required upstream expression is different, the import fails instead of guessing.

When imported, `include/ukiyo-e-mount/vendor_status.h` becomes `UKIYO_TECHART_VENDOR_PRESENT 1`. If the importer has not run, all original diagnostic commands compile, but the active `e` command is rejected.

## Lens constants

The importer modifies upstream `src/emount.h` macros:

```c
#define EM_FOCAL_MM10    580u     // 58.0 mm
#define EM_APERTURE_MAX  0x1200u  // f/2.0
#define EM_APERTURE_MIN  0x1200u  // fixed reported value, no iris control
#define EM_APERTURE_NOW  0x1200u  // fixed reported value
```

These are compile-time fields spliced into Techart's packet templates by `emount_packets.c`.

**Not yet changed or validated:** the upstream Techart AF-related capability declarations and optical-file data. Do not interpret the current software as a verified *manual-focus-only* identity, or its LensModel as Helios. The first objective is to port and observe the original working protocol path, then compare capability messages against LexOptical and native manual lenses. Do **not** blindly delete bits from message 0x01; the camera may depend on a message to finish initialization. The AF motor is absent and not used.

## Monitor commands

The old commands remain. All commands are echoed with `t_ms` and `t_us`. The new short command is `e`:

| Short | Long | Behavior |
|---|---|---|
| `l` | `camera listen` | Passive v0.3.x capture, D6/D10 high-Z |
| `p` | `camera probe` | Old diagnostic pin sequence, no UART TX |
| `e` | `camera emulate` | Arm Techart protocol; no drive until A0 detects LOGIC_VCC |
| `s` | `camera status` | All counters and A0 voltage estimate |
| `t` | `camera trace` | Body-CS transitions (up to 32), with µs timestamps |
| `x` | `camera stop` | Release UART and GPIO, abort active session |
| `h` | `help` | Short and long commands |

At boot, the device is high impedance. Active emulation never starts automatically. After `e`, the modem waits up to 30 seconds for power sense; once the camera supply appears, the original Techart `em_init()` runs. It blocks while waiting for the first BODY_CS window; the importer adds supply-loss exits. During that short blocking interval, the USB monitor cannot process another command, so `x` is not an emergency-stop mechanism. Disconnecting the camera supply must be able to return all outputs to high impedance, and actual electrical isolation should be verified before trusting this behavior.

Typical test order: `x`, `s` (check A0), camera OFF, `e`, camera ON, wait for `TECHART_MODE=RUNNING_PROTOCOL_ENGINE`, then `s`, `t`, `x` after camera OFF. If the A0 sensor is absent, only the old passive modes are supported.

## Porting details and limitations

- Original Techart `emount.c` and packet tables are preserved except for narrowly enumerated peripheral-access substitutions, fail-safe startup checks, and the three optics constants.
- Original Techart request queue, class-based frame handlers, checksum algorithm, 0x0C baud negotiation and status 0x05/0x06 logic remain in place.
- The original EA9 board's direct PAxx register addresses **cannot** run on a XIAO board; the XIAO HAL maps D6 TX, D7 RX, D8 VD, D9 BODY_CS, D10 LENS_CS.
- UART HAL initially uses Arduino `Serial1`, not the original EA9 SERCOM+DMA driver. `Serial1`'s ring-buffer depth, ISR priority and throughput at 1.5 Mbaud must be validated on actual hardware. It is a known risk, not equivalent to the original DMA setup.
- The Techart flash-write diagnostic is disabled (`EA9_NO_LOG`) because writing its absolute EA9 addresses would be unsafe on XIAO. USB counter logging is retained, outside interrupts.
- The original Techart shutdown/standby behavior remains largely intact, but USB-powered operation can differ from the original camera-powered adapter. Repeated mounting and power cycles are **not yet validated**.
- No camera communication or EXIF success is claimed until tested.

## Attribution and license

Techart core copyright (c) 2026 Aylwin Wei, MIT license. The import copies the complete license into `lib/ukiyo_techart/LICENSE`; preserve it when redistributing derived code. Techart acknowledges the work of LexOptical and Entropy512. This project similarly credits these upstream contributors in its README.
