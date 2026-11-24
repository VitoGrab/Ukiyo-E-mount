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
| 2. Safe bus bring-up | Electrical validation, body CS capture, clock/baud, high-Z shutdown | Stable a6000 handshake and no unintended bus drive | Validated on a6000; lifecycle improvements pending |
| 3. Native manual-lens emulation | Handshake, 1.5 Mbaud, native MF responses, focus aids | a6000 forces MF, takes pictures, Focus Magnifier and Peaking operate | Completed on a6000 (v0.4.2) |
| 4. EXIF verification | Helios 44M-4 profile, 58 mm, f/2 nameplate, configurable reported aperture | ARW reports correct lens identity, focal length, aperture and MF | Completed on a6000 (v0.4.2) |
| 4.5. Body lifecycle capture | Trace power-off (0x16), idle, sleep/wake, remount and hot-unplug with body still on | Captures distinguish shutdown, sleep and disconnection; 0x0A cadence established | **Next** |
| 4.6. Body-driven autonomous operation | Passive boot/listen, reactive handshake, re-arm, timeout recovery, LED state indicator | All six autonomous acceptance tests below pass without USB monitor | Planned |
| 4.7. Protocol and codebase cleanup | Consolidate mode/state handling, remove obsolete bench modes and Techart-specific naming from project-owned code | No regressions against v0.4.2 and autonomous lifecycle tests | Planned |
| 4.8. Additional body compatibility | Test a6500 after refactoring; retain a6000 regression suite | Mount, MF, EXIF, sleep/wake and remount pass on both bodies | Planned |
| 5. Standalone power | Validate XIAO supply from camera rail with regulated/protected power path | Repeated mounts and wake cycles work without USB or backfeeding | Planned |
| 6. Mechanical integration | Contact PCB and non-destructive mount/adapter packaging | Secure fit, no shorts, optics unaffected | Planned |
| 7. Aperture sensing (extra) | Analog lever sensor or manual control | Reliable reported aperture values | Future |
| 8. Extras | I2C sensors, lens position, optional ranging/AF experiments | Separate proof-of-concept tests | Future |

### Stage 4.5: characterize the body lifecycle

Record the **actual bus traffic** rather than treating power as a session trigger:

- Start with camera off, attach lens, switch on, and capture the first valid frame and BODY_CS/VD activity.
- Capture a normal power-off, including the observed `0x16` shutdown, and check whether subsequent messages arrive while LOGIC_VCC remains high.
- Capture sleep and wake-up independently; determine whether the body re-issues `0x01`, uses another request, or resumes an existing session.
- Characterize keepalive `0x0A` frequency during active use, idle and sleep, including periods with no requests.
- Simulate removal/reconnection safely and characterize loss of communication without assuming that supply voltage falls.
- Collect timestamps, direction, class, ID and error counters with bounded, nonblocking tracing. Preserve working v0.4.2 as the hardware regression baseline.

The observation that approximately 3.2 V may persist for one or two minutes after camera power-off is **experimental evidence**, not a reliable session signal. A `0x0A` timeout must only indicate detachment after active/sleep cadence has been characterized.

### Stage 4.6: body-driven autonomous operation

Target states: `LISTENING` -> `NEGOTIATING` -> `RUNNING` -> `LISTENING`, with explicit `SUSPENDED` (if required by sleep captures) and recoverable error paths.

- On boot, initialize high-impedance/passive listening without waiting for USB CDC or a power rise.
- Start or restart handshake on a **validated body initiation** (expected `0x01` for a cold session), using real frame/bus activity rather than A0 alone; a repeated `0x01` in any state starts a clean session.
- Answer every **valid, supported** body request even when unexpected in the current sequence; report unsupported/invalid requests safely instead of fabricating replies.
- Preserve working init/status replies, MF characteristics, Helios EXIF, and the negotiated 750 kbaud -> 1.5 Mbaud transition.
- On confirmed `0x16` shutdown, release driven lines, clear per-session queues/timers/status and return to passive listening. Where sleep is confirmed, suspend/resume instead of falsely disconnecting.
- Implement a configurable, evidence-based loss-of-traffic watchdog (initially considering `0x0A`) only in states where keepalive is expected. Reset parser, link state and baud appropriately before accepting a new session.
- Keep USB serial as optional, nonblocking diagnostics only: status, state transitions, trace counters and protocol errors. Unplugging USB/closing the monitor must not change behavior.
- Drive the onboard active-low LED from **state**, without high-frequency changes on every CS edge: off in `LISTENING`, short blink while initiating, steady on in `RUNNING`, off after shutdown/loss (optional bounded error pattern). LED and debug operations must not delay the bus.

**Autonomous acceptance tests:**

1. Flash/reset XIAO -> `LISTENING` immediately, even with camera rail still high; no USB monitor required.
2. Attach or power on body -> first validated initiation (normally `0x01`) starts handshake and reaches `RUNNING`.
3. Continue normal camera use until confirmed `0x16` or supported keepalive-loss detection; release lines safely.
4. Camera power cycle, sleep/wake and reconnection all restore shooting without reflashing or entering a serial command.
5. USB monitoring remains strictly observational and can be attached/detached at any time.
6. LED indicates connection lifecycle consistently without disrupting timing.

### Stages 4.7-5: cleanup, compatibility and power

- Remove obsolete interaction modes and misleading version banners once autonomous behavior is stable. Replace project-owned `techart` names with camera/protocol-neutral names while retaining upstream license, copyright and provenance notices.
- Separate electrical drivers, frame parsing, request dispatch, session lifecycle, optical profiles and diagnostics; add automated replay/regression tests for a6000 init, status, shutdown and wake traces.
- Test a6500 **after** refactoring, not as an implicit substitute for a6000 regression coverage.
- For camera-powered operation, the measured ability to boot XIAO from approximately 3.0 V at `3V3` is promising but **not yet a verified supply design**. Verify board rail direction, regulator backfeed, clock/flash operating limits, peak current, brownout/reset and safe transitions with/without USB before connecting a final power path.

### Technical design decisions

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
