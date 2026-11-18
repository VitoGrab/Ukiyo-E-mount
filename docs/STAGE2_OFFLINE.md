# Stage 2 — Offline protocol-session workbench

Status: **development preview v0.2.0**. No camera connection or E-mount electrical
interface is implemented. This does **not** complete the repository's Stage 2
hardware / bus-validation exit criterion.

## Purpose

Expand the v0.1.0 framing test to a protocol-level session engine that can be
exercised without the camera, extension rings, jumper wires or electronic load.

The engine is separated from Arduino and hardware in `include/ukiyo-e-mount/session.h`
and `src/session.cpp`. The program in `src/main.cpp` uses an in-memory simulated
camera to call it. All these operations use USB CDC for diagnostics only.

## Sources / attribution

- Original Techart LM-EA9 message templates, MIT-licensed:
  https://github.com/weiziqian/E-mount-impl-on-Techart-EA9
  (`src/emount_packets.c`, `src/emount.c`)
- Reverse-engineered E-mount framing and observed A6000 / a9 II startup sequences:
  https://github.com/weiziqian/E-mount-protocol-RE
- Existing v0.1.0 frame parser/encoder and fixture:
  https://github.com/VitoGrab/Ukiyo-E-mount

Templates are **Techart AF-adapter reference fixtures**, not a corrected
58 mm f/2 manual-focus lens profile. Their capability bitmap advertises
functions that we have **not** implemented. Do not connect this firmware to
an actual camera.

## Implemented

- Reuses the frame encoder/decoder and checksummed framing from v0.1.0.
- Pure C++ state machine for observed init exchanges, class `0x02`:
  - A6000: `01 07 0C 0B 08 09 0D 10 0A` (9 exchanges)
  - a9 II: `01 07 0C 0B 3F 3D 08 09 0D 10 0A` (11 exchanges)
- Techart-derived response payloads for those messages. The full 201-byte
  `0x08` descriptor is assembled from the original template's fixed fields.
- Virtual baud-rate transition from 750000 to 1500000 following `0x0C`.
  This never changes the physical UART. Optional `0x0C` can be skipped.
- `0x0A` initialization echo and `0x16` shutdown echo (including sequence).
- Rejects malformed checksums, invalid class, incorrect message order,
  unexpected request sizes, or insufficient response-buffer capacity without
  advancing the protocol state. Permits transfer-window padding beyond a frame.
- Preserves the existing `proto`, `status`, `uart on` and `uart off` commands.
- Safe at power-on: all E-mount pins are configured as inputs. The optional
  `uart on` command is for D6/D7 bench loopback ONLY; it does drive D6.

### What is NOT implemented

- Actual E-mount electrical interface: UART handling during body chip select,
  lens chip select, `BODY_VD_LENS`, detect and supply rails.
- Physical 750 kbaud → 1.5 Mbaud switch and its line-level handshake.
- Normal class `0x01` loop (`0x05`, `0x06`, `0x03`, `0x04`) at around 60 Hz.
- Actual M42 lens capabilities, focal length, aperture or custom EXIF name.
- Standalone powering from the camera, power-off pin release and safety tests.
- On-device compilation/upload verification (must be run in the user's LXC).

`request_payload_length()` uses observed A6000 sizes for the nine standard
startup requests. The synthetic optional `0x3F`/`0x3D` requests use a minimal
one-byte payload because their request sizes still require capture-level
verification. Do not treat these synthetic requests as real captured traffic.

## Deployment (inside existing PlatformIO repository)

Copy the four changed/new source files from the overlay archive into the root
of the checked-out repository. Also copy this documentation and the host test.
**Do not overwrite `platformio.ini`**: retain the working `/dev/ttyACM0` setup.

```bash
cd /root/projects/Ukiyo-E-mount
/opt/platformio/bin/pio run
/opt/platformio/bin/pio run -t upload
/opt/platformio/bin/pio device monitor -b 115200
```

In USB monitor, send each command followed by Enter:

```text
proto
session a6000
session a9ii
session faults
status
```

Expected key output for both session commands:

```text
SESSION_TEST=PASS
BUS_MODE=USB_ONLY_GPIO_HIGH_Z
```

Expected key output for `session faults`:

```text
BAD_ORDER_REJECTED=PASS
BAD_CHECKSUM_REJECTED=PASS
STATE_UNCHANGED=PASS
```

`session a6000` reports `SIM_ACCEPTED=9`; `session a9ii` reports
`SIM_ACCEPTED=11`. Both end at `SIM_STAGE=RUNNING` with virtual
`SIM_BAUD=1500000`.

## Host regression tests

If `g++` is installed in the LXC:

```bash
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude \
  src/frame.cpp src/session.cpp test/host_session_test.cpp \
  -o /tmp/ukiyo-session-tests
/tmp/ukiyo-session-tests
```

No Arduino or PlatformIO dependencies are needed for these host tests.

## Development cautions

- `0x3F` (lens name) is requested by the a9 II but not the A6000. Its
  availability does not prove an arbitrary name will appear in A6000 EXIF.
- The stored Techart checksums may be stale placeholders; `encode()`
  recomputes checksums for the actual payload. This is intentional.
- Do not infer compatibility with a physical body from a passing in-memory
  synthetic session.
- The Techart startup 0x01 capability bitmap declares AF and normal-loop
  features. Do not carry it directly into a live manual-lens firmware.

## Next actions

1. Validate the compiled preview on the XIAO in USB-only mode.
2. Introduce a genuine manual-focus 58 mm f/2 capability profile that does
   not claim unimplemented motors, with regression checks for all relevant
   payload fields.
3. Add isolated, hardware-neutral transport abstraction and tests, then a
   carefully controlled receive-only body-communication bench test.
4. Only then proceed with E-mount electrical wiring/handshake validation.
