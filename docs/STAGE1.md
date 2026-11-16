# Eidolon v0.1.0 — Sony E-mount protocol workbench

An initial, **bench-only** PlatformIO prototype for Seeed XIAO SAMD21.
The current firmware DOES NOT implement a live E-mount lens and MUST NOT be
connected to a camera. It does not transmit Techart replies on E-mount pins.

## Included

- Independent Sony E-mount frame encoder/decoder with correct additive checksum.
- Real Techart EA9 message `0x07` payload as a test fixture (MIT upstream).
- Host regression tests for checksum, truncation, padding and frame ID.
- USB CDC shell for protocol tests and UART 750 kbaud loopback.
- Safe boot: all reserved camera-facing pins are inputs.
- Board map, staged roadmap, and requirements for later hardware bring-up.

## Setup on the VS Code Server LXC

The user already has PlatformIO in `/opt/platformio/bin/pio`, with USB
passthrough to LXC 109. Do not run `pio project init` again.

```bash
# Work in a NEW directory or back up existing src/main.cpp first.
cd /root/projects/eidolon
/opt/platformio/bin/pio run
/opt/platformio/bin/pio run -t upload
/opt/platformio/bin/pio device monitor -b 115200
```

Use your existing stable USB device path or specify `-p /dev/xiao/ttyXIAO`
if this path exists in your LXC. The bootloader can re-enumerate as a different
`ttyACM*` device during flashing.

## USB commands

- `help` — commands
- `status` — board uptime, UART mode and counters
- `proto` — encode a synthetic A6000-style `0x07` request and the Techart
  `0x07` reply fixture, parse both and reject a corrupted checksum
- `uart on` — D6 TX/D7 RX loopback at 750000 baud, **bench only**
- `uart off` — stop UART; restore GPIOs to high impedance

For `uart on`, connect only D6 to D7 and leave the camera completely disconnected.
A few seconds later, issue `status`; `UART_LOOPBACK_FRAMES` should increase.

## Host-only regression suite

```bash
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude \
  src/frame.cpp test/host_frame_test.cpp -o /tmp/eidolon-host-test
/tmp/eidolon-host-test
```

Expected result: `HOST_FRAME_TEST=PASS`.

## Upstream and license

- The E-mount frame format is documented by `weiziqian/E-mount-protocol-RE`.
- The 34-byte message 0x07 identity payload comes from
  `weiziqian/E-mount-impl-on-Techart-EA9/src/emount_packets.c` (MIT).
- A copy of the upstream MIT license is in `third_party/Techart-LICENSE`.
- This repository is a new bench harness, not a copy of the Techart firmware.

See `docs/ROADMAP.md` and `docs/WIRING.md` before connecting real hardware.
