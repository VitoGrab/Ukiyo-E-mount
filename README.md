# 浮世E-mount (ukiyo-e-mount)

Open-hardware Sony E-mount lens emulator based on XIAO SAMD21 board bridging vintage manual optics and modern digital cameras.

## Hardware

See [hardware/README.md](hardware/README.md) for the target adapter parts, PCB and mechanical integration status, and the optional developer test setup.

### Objective

Emulate a manual Sony E-mount lens for a Sony cameras, with a stable, fixed
focal length and an identifiable lens name in EXIF metadata. Start with 
a popular Helios 44M-4 lens - manual-focus 58 mm f/2 lens profile and expand 
to other lenses including zoom lenses.

## ROADMAP

### Development stages

Stage numbers describe work packages; firmware versions identify actual code milestones. Compact project versions are retained (e.g. v0.411); planned work is not assigned a firmware version in advance. Current milestone: **v0.51**, direct-power profile-maintenance correction to the v0.5 standalone camera-powered prototype, functionally validated on Sony a6000 with the pre-release debug build. See [v0.5 release notes](docs/releases/v0.5.md).

| Stage | Deliverable | Exit criterion | Status |
|---|---|---|---|
| 0. Board validation | XIAO USB upload, monitoring, 750 kbaud D6/D7 loopback | Tests pass without camera | Completed |
| 1. Protocol workbench | Frame encoder/parser, checksum, identity fixture, host regression tests | Offline protocol tests pass | Completed |
| 2. Safe bus bring-up | Electrical validation, body CS capture, clock/baud, safe shutdown | Stable a6000 handshake and no unintended bus drive in tested setup | Completed on a6000 |
| 3. Native manual-lens emulation | Handshake, 1.5 Mbaud, native MF responses, focus aids | a6000 forces MF, takes pictures, Focus Magnifier and Peaking operate | Completed on a6000 (v0.42) |
| 4. EXIF verification | Helios 44M-4, 58 mm, f/2 nameplate and selectable reported aperture | ARW/JPG reports lens identity, focal length, aperture and MF | Completed on a6000 (v0.42) |
| 4.5. Body lifecycle capture | Trace 0x16, sleep/wake, loss of lens rail and remount | Short wake and full re-init validated | Completed on a6000 |
| 4.6. Body-driven autonomous operation | Shutdown, short wake at 1.5 Mbaud, supply-loss rearm | Repeated wake, full init, remount, shooting and EXIF | Completed for externally powered prototype (v0.46) |
| 4.6.1. Protocol identity | Single project version, E-mount protocol 1.08 | Generated wire identity independent of lens profile | Implemented; intermittent MakerNotes remain [issue #3](https://github.com/VitoGrab/Ukiyo-E-mount/issues/3) |
| 4.6.2. Powerbank autonomy | Operation without PC or USB data host | Full init, shooting and wake without monitor | Passed; powerbank auto-off limits long standby test |
| 4.7. Refactor and startup | Safe early pins, auto-arm, consolidated protocol engine and LED modes | Preserve externally powered a6000 baseline | Refactor passed (v0.47); absolute cold-start timing moves to Stage 5 |
| 4.8. Protocol correction | Zero aperture-offset fields in 0x28/0x35 | Preserve MF, EXIF and selectable aperture | Completed (v0.48) |
| 4.9. Runtime prime profiles | Independent runtime packet templates and firmware identity | Select/stage alternative profile without changing protocol engine | Completed (v0.49) |
| 4.10. Standalone binary profiles | Catalog, 16 prime BINs, shared templates, v1 format and fallback | Independent generated profile images | Implemented (v0.410); camera validation followed in v0.411 |
| 4.11. Persistent prime profiles | Reserved Flash, USB CDC programming, readback, reset, BOSSA 1.9 retention | Change prime profile without rebuilding; preserve it across changed firmware uploads | Passed on a6000 (v0.411); merged to main |
| 5. Standalone power | Direct LOGIC_VCC prototype and low-power IDLE0 sleep | Functional body-only startup, remount and sleep/wake | Passed on a6000; final electrical and production qualification open (v0.5) |
| 6. Manual zoom profiles | Focal range, default/current focal length and optical metadata | Validated zoom profile generation, programming and camera behavior | **Next** — [issue #5](https://github.com/VitoGrab/Ukiyo-E-mount/issues/5) |
| 7. Cross-body compatibility | a6500 and additional bodies | Verify mount, MF, EXIF, zoom, sleep/wake and remount | After zoom |
| 8. Mechanical integration | SELP1650 service contact assembly and internal carrier | Secure fit, no shorts, optics unaffected | Parallel hardware track; parts planned |
| 9. Optional extras | Aperture sensing, I2C sensors, other experiments | Separate proof-of-concept tests | Future, not required for core firmware |

The prime-lens firmware feature set is complete for the tested a6000 prototype. Zoom development comes before systematic cross-body testing. Mechanical work can proceed in parallel. No general E-mount compatibility or production-ready hardware claim is made.

### Stage 4.5: characterized body lifecycle (a6000; completed 2026-11-25)

Record the **actual bus traffic** rather than treating power as a session trigger:

- Start with camera off, attach lens, switch on, and capture the first valid frame and BODY_CS/VD activity.
- Capture a normal power-off, including the observed `0x16` shutdown, and check whether subsequent messages arrive while LOGIC_VCC remains high.
- Capture sleep and wake-up independently; determine whether the body re-issues `0x01`, uses another request, or resumes an existing session.
- Characterize keepalive `0x0A` frequency during active use, idle and sleep, including periods with no requests.
- Simulate removal/reconnection safely and characterize loss of communication without assuming that supply voltage falls.
- Collect timestamps, direction, class, ID and error counters with bounded, nonblocking tracing. Preserve working v0.4.2 as the hardware regression baseline.

The observation that approximately 3.2 V may persist for one or two minutes after camera power-off is **experimental evidence**, not a reliable session signal. A `0x0A` timeout must only indicate detachment after active/sleep cadence has been characterized.

### Stage 4.6: body-driven autonomous operation (core a6000 tests completed)

Target states: `LISTENING` -> `NEGOTIATING` -> `RUNNING` -> `LISTENING`, with explicit `SUSPENDED` (if required by sleep captures) and recoverable error paths.

- On boot, initialize high-impedance/passive listening without waiting for USB CDC or a power rise.
- Start or restart handshake on a **validated body initiation** (expected `0x01` for a cold session), using real frame/bus activity rather than A0 alone; a repeated `0x01` in any state starts a clean session.
- Answer every **valid, supported** body request even when unexpected in the current sequence; report unsupported/invalid requests safely instead of fabricating replies.
- Preserve working init/status replies, MF characteristics, Helios EXIF, and the negotiated 750 kbaud -> 1.5 Mbaud transition.
- On confirmed `0x16` shutdown, release driven lines, clear per-session queues/timers/status and return to passive listening. Where sleep is confirmed, suspend/resume instead of falsely disconnecting.
- Implement a configurable, evidence-based loss-of-traffic watchdog (initially considering `0x0A`) only in states where keepalive is expected. Reset parser, link state and baud appropriately before accepting a new session.
- Keep USB serial as optional, nonblocking diagnostics only: status, state transitions, trace counters and protocol errors. Unplugging USB/closing the monitor must not change behavior.
- Use compile-time LED modes: debug builds show VCC on the User LED, pulse RX when the body handshake completes, and pulse TX for protocol/transport errors. Normal frame traffic and keepalives do not trigger LEDs. Production builds keep all GPIO-controlled LEDs off.

**Autonomous acceptance tests:**

The essential USB-powered prototype lifecycle was validated on a6000: 0x16
shutdown, short wake via 0x0B at retained 1.5 Mbaud, repeated sleep/wake,
full cold 0x01 after lens-rail loss, photography and correct Helios EXIF.
USB monitoring remains optional. Pure voltage-independent cold boot and strict
USB timing guarantees remain hardening/cleanup tasks for Stage 4.7. Compile-time
debug/production LED modes are implemented; standalone camera-rail power remains Stage 5.
The numbered list below retains the complete long-term acceptance criteria.

1. Flash/reset XIAO -> `LISTENING` immediately, even with camera rail still high; no USB monitor required.
2. Attach or power on body -> first validated initiation (normally `0x01`) starts handshake and reaches `RUNNING`.
3. Continue normal camera use until confirmed `0x16` or supported keepalive-loss detection; release lines safely.
4. Camera power cycle, sleep/wake and reconnection all restore shooting without reflashing or entering a serial command.
5. USB monitoring remains strictly observational and can be attached/detached at any time.
6. Debug LEDs indicate VCC, completed handshake and protocol/transport errors without per-frame activity; production builds keep GPIO-controlled LEDs off.

### Stage 4.6.1: firmware and E-mount protocol identity

`config/protocol_identity.json` is the single source of truth for the compact project version and E-mount protocol revision.

- The current project version is `0.51`. Its first digit after the decimal point maps to the Sony firmware major byte and the remaining digits map to the suffix byte: `0.411` -> `pl[6]=0x04`, `pl[5]=0x0B`; `0.5` (or `0.50`) -> `pl[6]=0x05`, `pl[5]=0x00`.
- The suffix occupies one byte (0–255). The generated v0.51 identity is expected to render as `Ver.05.001`; the v0.5 identity was expected to render as `Ver.05.000`; the historical v0.411 identity was expected to render as `Ver.04.011`; `Ver.04.008` was observed in an earlier capture, not established as the current firmware identity.
- The a6000 test has also produced `Ver.04.003` after setting `pl[5]=0x03`, supporting the suffix mapping. The body-side reason that version MakerNotes appear only on some captures remains unresolved; retain [issue #3](https://github.com/VitoGrab/Ukiyo-E-mount/issues/3) as open.
- `emount_protocol_version` remains `1.08`. The 1.31 experiment was not necessary for the EXIF fields and is not the selected baseline.
- `tools/prepare_lens_profiles.py` derives both firmware bytes from `project_version`, generates firmware identity constants; the engine inserts them into message `0x07` and recalculates its checksum at initialization. There is no separate firmware-version override or duplicate version field.
- `--verify-only` checks that all supported BINs, fallback header and firmware identity header match their sources.
- Monitor startup, `h` help and status output use the generated `v0.51` label; there is no separate version command.

### Stages 4.6.2–4.7: powerbank validation and cleanup

**Stage 4.6.2 — powerbank test, before refactoring**

- Keep the current firmware and proven USB-powered a6000 behaviour unchanged.
- Power XIAO from a powerbank through its USB connector, with no PC connected and no USB CDC monitor.
- Verify cold start, passive listening, full 0x01 initialization, normal photography and lens metadata.
- Verify camera standby/sleep and repeated short wake through 0x0B, including multiple consecutive cycles.
- Confirm that disconnecting or reconnecting a PC is not required for operation. A monitor may be attached afterwards for diagnostics, but it must not be part of the tested operating path.
- Record any startup delay, missed first request, reset, brownout or wake failure before changing the code.

Powerbank testing was performed before Stage 4.7. Standby validation remains limited by powerbank auto-off; retain USB-powered lifecycle regression tests.

**Stage 4.7 — codebase refactor and startup-path optimization**

- Following the powerbank tests, consolidate mode/state handling, remove obsolete bench modes and misleading version banners, and use project-owned protocol names while retaining upstream license, copyright and provenance notices.
- Separate electrical drivers, frame parsing, request dispatch, session lifecycle, optical profiles and diagnostics; retain replay/regression tests for a6000 init, status, shutdown and wake traces.
- Prioritize the cold-start path: establish safe pin states and protocol receive readiness as early as possible; avoid USB CDC initialization, diagnostics, delays, unnecessary setup and nonessential subsystem initialization before the first valid body request.
- Explicitly preserve fast reception and handling of cold-init 0x01 and short-wake 0x0B. The generated version header is compile-time-only and must not add runtime startup work.
- Re-run the complete powerbank test after each startup-path change. Do not remove a bench facility until its regression coverage has been retained elsewhere.

### Stages 4.8–4.11: protocol correction and independently programmable prime profiles

The protocol correction in v0.48, runtime templates in v0.49, standalone BIN format in v0.410, and persistent USB-programmable profiles in v0.411 are implemented. The reserved region is 8 KiB at 0x3E000–0x3FFFF; the current format is a 559-byte v1 image with CRC32, validated before application, with embedded Helios fallback. See [lens profiles](docs/LENS_PROFILES.md) and [Flash programming and hardware evidence](docs/PROFILE_FLASH.md).

The maintainer confirmed native MF and EXIF on Helios/Hanimex, independent Pentacon programming and automatic reset/re-arm, Rubinar fixed f/5.6 behavior, and retention of Hanimex through a changed application upload using BOSSA 1.9 without full erase. The tested baseline was integrated at commit `28547ce`. The subsequent host-tool/task cleanup did not change firmware protocol or Flash programming logic.

### Stage 5 — camera-rail power (functional POC completed)

On 2026-11-27 the maintainer confirmed a6000 operation with Sony contact 5 connected directly to XIAO 3V3, the existing 100k/100k A0 divider and XDETECT resistor retained, and USB physically disconnected. Motor power remains unused. Body-only startup, sleep/wake and live lens exchange passed. IDLE0 CPU sleep and extinguishing the software-controlled LEDs during suspension also passed functional tests. See [measurements and evidence](docs/STAGE5_CAMERA_POWER_POC.md).

The v0.5 milestone closes the core prime-lens firmware feature work. An undervoltage LED warning and deeper sleep modes are deliberately deferred: production disables software-controlled LEDs and additional power features are not needed for the demonstrated operation. The current A0 estimate is ratiometric under direct power, not an absolute voltage measurement.

Disconnect the camera physically before USB firmware or profile programming. Never connect USB and camera LOGIC_VCC to the 3V3 rail simultaneously. Current draw, startup transients, reverse-feed protection, actual idle current savings, production-build hardware acceptance and final integrated mechanical hardware remain unqualified. This is a functional prototype milestone, not a safety-certified or production-ready design.

### VS Code development tasks

- `Ukiyo: Build & Upload` prepares profiles and firmware identity, builds the default environment from `platformio.ini`, and retries SAM-BA upload up to three times. No profile write or monitor launch.
- `Ukiyo: Select Build Type` sets `[platformio] default_envs` to Debug (status LEDs enabled) or Production (status LEDs disabled). The selection persists in the tracked INI; changing it is a local configuration edit.
- `Ukiyo: Upload Lens Profile` prepares the entire catalog, displays a numbered live list of supported lenses in the terminal, requires physical detachment confirmation and uploads only the selected BIN.
- `Ukiyo: Monitor` opens the serial monitor separately.
- `Ukiyo: Clean` cleans both build environments.

`python3 tools/prepare_lens_profiles.py --verify-only` remains available for CI/manual validation. Use the XIAO's documented double-reset procedure if automatic bootloader entry fails.


The PlatformIO post extra-script extends bootloader-port discovery to a fifteen-second maximum, with a three-second fallback when the original port remains present and no re-enumeration is observed. It also configures BOSSA 1.9 row-local erase with explicit application offset `0x2000`.

#### XIAO LED build modes

- `pio run -e seeed_xiao` builds the debug configuration.
- `pio run -e seeed_xiao_production` builds the production configuration.
- Add `-t upload` to either command to flash that exact build. The selected default environment is stored in `platformio.ini` by the build-type task.

Debug mode: User LED is solid while LOGIC_VCC is detected except during protocol sleep; RX gives a short pulse after the 0x0A handshake completes; TX gives a longer pulse for malformed frames, dropped requests, unknown/incomplete instruction records or UART receive-ring overflow. Normal frame traffic and keepalives do not trigger LEDs. Production mode holds the User LED off and keeps RX/TX high-impedance. The hardware Power LED is connected directly to 3V3 and cannot be disabled in firmware. See [docs/LED_MODES.md](docs/LED_MODES.md) for implementation details.

### Technical design decisions

- Keep the proven PlatformIO `seeed_xiao` board configuration.
- Use USB CDC only for diagnostics in early stages. Never put diagnostic
  printing in time-critical E-mount processing.
- Use `Serial1` hardware UART at D6/D7; 750 kbaud already passed loopback.
- Keep E-mount protocol processing independent of hardware GPIO/UART backends.
- Maintain the integrated E-mount protocol engine independently, retaining upstream attribution.
- a6000 handshake has nine init exchanges: 01, 07, 0C, 0B, 08, 09, 0D,
  10, 0A. Some other bodies require additional messages.
- Treat 0x0C as a real baud-rate transition. Do not acknowledge a speed change
  until the serial interface can switch to the negotiated rate.
- Treat 0x16 shutdown, wake and link loss as independent lifecycle events.
  Power present on A0 does not imply an active camera session; return to passive
  listening after a confirmed shutdown or disconnection.
- Frame parser uses declared length, checks the checksum, and ignores padding
  after the terminator. It must reject truncated frames.
- No autofocus/motor capabilities should be advertised until implemented.
- Keep camera-driven state transitions independent of USB monitor commands.
- Never answer malformed or unsupported messages speculatively; preserve the
  last validated camera exchanges for offline regression testing.
- Reserve D4/D5 for I2C and D0/D1 for optional aperture controls.

### Projects which made this possible

- Techart EA9 E-mount reference firmware (MIT):
  https://github.com/weiziqian/E-mount-impl-on-Techart-EA9
- E-mount physical layer, framing and A6000 session captures:
  https://github.com/weiziqian/E-mount-protocol-RE
- Legacy NEX-7 implementation:
  https://github.com/LexOptical/E-Mount


### AI-assisted development

This project has been developed with assistance from ChatGPT (OpenAI) for
protocol research, code and documentation. The human maintainer controls
hardware design choices, reviews and integrates changes, and performs and
reports physical camera tests. AI assistance does not replace independent
testing or third-party license and attribution requirements.

### Links to technical documentation of the board
- PlatformIO board definition:
  https://docs.platformio.org/en/latest/boards/atmelsam/seeed_xiao.html
- Official XIAO SAMD21 documentation:
  https://wiki.seeedstudio.com/Seeeduino-XIAO/

### References for E-mount PCB
- Mechanical PCB photo reference:
  https://www.pdmovie.com/products/smartfusion

### License and provenance

Ukiyo-E-mount firmware is distributed under the [MIT License](LICENSE). The integrated protocol engine contains code derived from Aylwin Wei's MIT-licensed Techart EA9 community firmware. See [upstream provenance](third_party/Techart-UPSTREAM.txt) and [original license](third_party/Techart-LICENSE). The engine is maintained in `src/emount/` and `include/emount/`; the legacy importer has been retired.

### v0.48 protocol correction

Messages `0x28` and `0x35` payload bytes 9–10 are aperture offsets, not current aperture. Keep them zero for the current profile, independent of the `0x08` request. Based on [upstream 51ce0a5](https://github.com/weiziqian/E-mount-impl-on-Techart-EA9/commit/51ce0a5716a148a3ff092fe1ffcd65678bac4371). Current aperture reporting and the `0x1B` response are unchanged. Hardware regression confirmed on a6000 with the runtime-profile branch on 2026-11-27.

### v0.49 runtime prime-lens profiles

The runtime staging milestone (commit `573a316`) passed profile generator verification, Python regression tests, native host runtime-profile tests, XIAO SAMD21 build and maintainer-reported a6000 hardware regression on 2026-11-27. The version bump changes the reported firmware identity only. Profile loading from persistent storage and independent profile programming remain unimplemented. Zoom lenses are not supported (issue #5).

### v0.410 standalone packet profiles (development)

[profiles/](profiles/) contains a single `profiles.json` catalog and distributable 559-byte binary prime-lens profiles named by profile ID. Regenerate all supported entries with `python3 tools/generate_lens_profile.py`, or select one with `--profile-id`, without editing firmware or rebuilding. Zoom records are retained but not encoded. The engine validates and stages the binary packet image; firmware identity and 0x01 remain firmware-owned. The v0.410 milestone used its embedded Helios fallback; see v0.411 below for read-only Flash loading. See [lens profile format](docs/LENS_PROFILES.md).

### v0.411 persistent prime-lens profiles

Both XIAO environments use a Seeed-derived linker script that reserves 8 KiB at `0x3E000..0x3FFFF`. The engine checks the reserved image once before the first full handshake and uses the existing Helios fallback if absent or invalid. USB CDC profile programming is implemented with an idle/disconnected-camera interlock, pre-write validation and byte-for-byte readback. The original full-erase upload invalidated this region. An initial no-full-erase upload retained the profile byte-for-byte (`741c387`), but a changed image failed verification with BOSSA 1.7. The project now selects BOSSA 1.9 with explicit offset `0x2000` and no full erase; the changed-image upload and Hanimex profile retention passed on hardware. Normal firmware uploads do not back up or restore profiles. See [Flash layout](docs/PROFILE_FLASH.md) for the remaining validation and hardware evidence.

To program a profile, physically detach the camera, connect XIAO via USB and use `/opt/platformio/bin/python tools/upload_lens_profile.py --port /dev/ttyACM0 --detached profiles/hanimex-135-f2-8.bin`. In v0.411 the uploader requested a normal MCU reset and checked re-arm. Since v0.51 it leaves the bus OFF by default after verified programming; unplug USB completely before mounting the camera. The next body-powered boot loads the new profile. Run `--info` first (includes the stored lens model decoded from 0x3F); see the Flash layout document for limitations. On 2026-11-27 the maintainer confirmed byte-for-byte Flash readback and independent Helios -> Hanimex -> Helios switching on a6000, including correct focal length, model, maximum aperture and manual focus. See [EXIF evidence](docs/LENS_PROFILES.md#hardware-evidence-2026-11-27). Pentacon programming, automatic reset/re-arm, and fixed f/5.6 aperture for Rubinar were also confirmed on hardware. This was the v0.411 milestone; extended regression tests remain open.

Firmware updates: use `Ukiyo: Build & Upload` and close any existing monitor. The wrapper builds and retries the standard SAM-BA upload with full erase disabled by the project post script. No camera-detachment prompt or profile operations are performed by the firmware-upload tasks. Use `Ukiyo: Upload Lens Profile` to select a prime lens and explicitly confirm the camera is physically disconnected. See [firmware update workflow](docs/PROFILE_FLASH.md#firmware-updates-with-native-profile-retention).

The optional legacy post-profile-upload reset requires v0.411 or later firmware and has passed hardware verification with isolated-sense wiring; use `--reset` only for that wiring. Read-only profile operations do not reset the MCU and leave emulation off.

### v0.5 standalone camera-powered prototype

See [release notes](docs/releases/v0.5.md) and [Stage 5 evidence](docs/STAGE5_CAMERA_POWER_POC.md). The profile format and E-mount protocol revision remain unchanged. Zoom is the next firmware work package, followed by additional body compatibility tests.

### v0.51 direct-power profile programming fix

A0 no longer blocks USB profile programming, because direct 3V3 wiring energizes the sense divider from USB. The bus must be stopped and the camera physically detached. The interactive picker uses `[y/N]` confirmation and terminal-height pagination. The default uploader leaves the verified profile in Flash and the emulator OFF until USB is unplugged; no automatic reset on direct-power wiring. Runtime handshake and sleep behavior are unchanged. See [profile programming](docs/PROFILE_FLASH.md).
