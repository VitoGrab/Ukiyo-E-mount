# 浮世E-mount (ukiyo-e-mount)

Open-hardware Sony E-mount lens emulator based on XIAO SAMD21 board bridging vintage manual optics and modern digital cameras.

### Objective

Emulate a manual Sony E-mount lens for a Sony cameras, with a stable, fixed
focal length and an identifiable lens name in EXIF metadata. Start with 
a popular Helios 44M-4 lens - manual-focus 58 mm f/2 lens profile and expand 
to other lenses including zoom lenses.

## ROADMAP

### Development stages

| Stage | Deliverable | Exit criterion | Status |
|---|---|---|---|
| 0. Board validation | XIAO USB upload, monitoring, 750 kbaud D6/D7 loopback | Tests pass without camera | Completed |
| 1. Protocol workbench | Frame encoder/parser, checksum, identity fixture, host regression tests | Offline protocol tests pass | Completed |
| 2. Safe bus bring-up | Electrical validation, body CS capture, clock/baud, safe shutdown | Stable a6000 handshake and no unintended bus drive | Completed on a6000 |
| 3. Native manual-lens emulation | Handshake, 1.5 Mbaud, native MF responses, focus aids | a6000 forces MF, takes pictures, Focus Magnifier and Peaking operate | Completed on a6000 (v0.4.2) |
| 4. EXIF verification | Helios 44M-4 profile, 58 mm, f/2 nameplate, configurable reported aperture | ARW reports correct lens identity, focal length, aperture and MF | Completed on a6000 (v0.4.2) |
| 4.5. Body lifecycle capture | Trace 0x16, sleep/wake, loss of lens rail and remount | Validated short wake, full re-init after removal and runtime capture | Completed on a6000 (2026-11-25) |
| 4.6. Body-driven autonomous operation | 0x16 electrical shutdown, short wake at 1.5 Mbaud, supply-loss rearm and optional USB trace | Repeated wake, full init, remount, shooting and EXIF | Completed for USB-powered a6000 prototype (2026-11-25); LED/boot refinements in 4.7 |
| 4.6.1. Protocol identity / EXIF experiment | One project version generates Sony firmware bytes; retain validated E-mount protocol 1.08 | Compare camera Version screen and ARW/JPG EXIF, retaining MF/wake behavior | Compact version 0.42 maps to 04.002; suffix 04.003 observed in a6000 EXIF; capture/session behaviour remains open in issue #3 |
| 4.6.2. Powerbank autonomy test | XIAO powered from a powerbank, with no PC/USB data connection | Full init, shooting and repeated 0x0B wake work without USB CDC host | Planned |
| 4.7. Codebase refactor and fast startup | Consolidate state handling, remove obsolete bench modes, minimize pre-ready startup work | Preserve the powerbank baseline; pins are ready early for 0x01 and 0x0B | Planned after 4.6.2 |
| 4.8. Additional body compatibility | Test a6500 after refactoring; retain a6000 regression suite | Mount, MF, EXIF, sleep/wake and remount pass on both bodies | Planned |
| 5. Standalone power | Validate XIAO supply from camera rail with regulated/protected power path | Repeated mounts and wake cycles work without USB or backfeeding | Planned |
| 6. Mechanical integration | Contact PCB and non-destructive mount/adapter packaging | Secure fit, no shorts, optics unaffected | Planned |
| 7. Aperture sensing (extra) | Analog lever sensor or manual control | Reliable reported aperture values | Future |
| 8. Extras | I2C sensors, lens position, optional ranging/AF experiments | Separate proof-of-concept tests | Future |

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

- The current project version is `0.46`. Its first digit after the decimal point maps to the Sony firmware major byte and the remaining digits map to the suffix byte: `0.46` -> `pl[6]=0x04`, `pl[5]=0x06`; `0.50` -> `pl[6]=0x05`, `pl[5]=0x00`.
- The suffix occupies one byte (0–255). ExifTool renders the current wire identity as `Ver.04.006`.
- The a6000 test has also produced `Ver.04.003` after setting `pl[5]=0x03`, supporting the suffix mapping. The body-side reason that version MakerNotes appear only on some captures remains unresolved; retain [issue #3](https://github.com/VitoGrab/Ukiyo-E-mount/issues/3) as open.
- `emount_protocol_version` remains `1.08`. The 1.31 experiment was not necessary for the EXIF fields and is not the selected baseline.
- `tools/apply_lens_profile.py` derives both firmware bytes from `project_version`, patches message `0x07`, and recalculates its checksum. There is no separate firmware-version override or duplicate version field.
- `--verify-only` checks that the generated packet sources and compile-time version header match the JSON configuration.
- Monitor startup, `h` help and status output use the generated `v0.46` label; there is no separate version command.

### Stages 4.6.2-5: powerbank validation, cleanup, compatibility and power

**Stage 4.6.2 — powerbank test, before refactoring**

- Keep the current firmware and proven USB-powered a6000 behaviour unchanged.
- Power XIAO from a powerbank through its USB connector, with no PC connected and no USB CDC monitor.
- Verify cold start, passive listening, full 0x01 initialization, normal photography and lens metadata.
- Verify camera standby/sleep and repeated short wake through 0x0B, including multiple consecutive cycles.
- Confirm that disconnecting or reconnecting a PC is not required for operation. A monitor may be attached afterwards for diagnostics, but it must not be part of the tested operating path.
- Record any startup delay, missed first request, reset, brownout or wake failure before changing the code.

**Stage 4.7 — codebase refactor and startup-path optimization**

- Only after the powerbank baseline passes, consolidate mode/state handling, remove obsolete bench modes and misleading version banners, and replace project-owned Techart-specific names while retaining upstream license, copyright and provenance notices.
- Separate electrical drivers, frame parsing, request dispatch, session lifecycle, optical profiles and diagnostics; retain replay/regression tests for a6000 init, status, shutdown and wake traces.
- Prioritize the cold-start path: establish safe pin states and protocol receive readiness as early as possible; avoid USB CDC initialization, diagnostics, delays, unnecessary setup and nonessential subsystem initialization before the first valid body request.
- Explicitly preserve fast reception and handling of cold-init 0x01 and short-wake 0x0B. The generated version header is compile-time-only and must not add runtime startup work.
- Re-run the complete powerbank test after each startup-path change. Do not remove a bench facility until its regression coverage has been retained elsewhere.

**Stage 4.8 — additional body compatibility**

- Test a6500 after refactoring, retaining a6000 as the regression baseline.
- Verify mount, MF, EXIF, sleep/wake and remount on both bodies.

**Stage 5 — camera-rail power**

- Only after the powerbank and startup-refactor stages pass, validate the XIAO supply from the camera rail with a regulated/protected power path.
- Verify rail direction, regulator backfeed, clock/flash operating limits, peak current, brownout/reset and safe transitions with/without USB before connecting the final power path.
- Do not connect Sony contact 5 directly to XIAO 3V3 or contact 2 to the XIAO without a regulator and protection review.

### Technical design decisions

### VS Code development tasks

The workspace tasks include separate debug and production builds/uploads, lens-profile apply/verification, serial monitoring and cleanup. Build tasks verify generated lens-profile sources before proceeding.

- `Ukiyo: Apply Lens Profile` runs `tools/apply_lens_profile.py`.
- `Ukiyo: Verify Lens Profile` runs the same tool with `--verify-only`.
- `Ukiyo: Upload` and `Ukiyo: Upload Production` use `tools/upload_with_retry.py`: build once, then retry the SAM-BA upload up to three times.
- The PlatformIO post extra-script extends bootloader-port discovery to a fifteen-second maximum, with a three-second fallback when the original port remains present and no re-enumeration is observed. It does not alter the 1200-baud reset touch or BOSSA transfer parameters.
- If automatic reset still fails, use the XIAO's documented double-reset procedure to enter its bootloader, then retry the upload task.

#### XIAO LED build modes

- `pio run -e seeed_xiao` builds the debug configuration (default).
- `pio run -e seeed_xiao_production` builds the production configuration.
- Add `-t upload` to either command to flash that exact build.

Debug mode: User LED is solid while LOGIC_VCC is detected; RX gives a short pulse after the 0x0A handshake completes; TX gives a longer pulse for malformed frames, dropped requests, unknown/incomplete instruction records or UART receive-ring overflow. Normal frame traffic and keepalives do not trigger LEDs. Production mode holds the User LED off and keeps RX/TX high-impedance. The hardware Power LED is connected directly to 3V3 and cannot be disabled in firmware. See [docs/LED_MODES.md](docs/LED_MODES.md) for implementation details.

- Keep the proven PlatformIO `seeed_xiao` board configuration.
- Use USB CDC only for diagnostics in early stages. Never put diagnostic
  printing in time-critical E-mount processing.
- Use `Serial1` hardware UART at D6/D7; 750 kbaud already passed loopback.
- Keep E-mount protocol processing independent of hardware GPIO/UART backends.
- Integrate Techart packet tables and session processing carefully rather than
  flashing the EA9 firmware image or directly copying its peripheral setup.
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
