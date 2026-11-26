# Stage 3.1 — Timestamped camera-CS handshake diagnostic

> Historical development record. Commands, paths and status labels below describe the original stage, not the current v0.47 firmware. See the README for the current workflow.

## Status

**Host-validated source; XIAO firmware compilation and physical A6000 measurements pending.**
This is still an **RX-only** diagnostic, not an E-mount lens emulator.

## Purpose

The A6000 test of v0.3.0 detected CS edges and VD pulses but no UART bytes.
The first diagnostic probe incorrectly drove LENS_CS immediately on arming.
This revision arms its output as high impedance, waits for actual BODY_CS
transitions and reproduces the two empirically observed delays in the
LexOptical NEX-7 implementation:

- First BODY_CS transition -> after **990 microseconds**, drive LENS_CS HIGH.
- Second BODY_CS transition -> after **305 microseconds**, drive LENS_CS LOW.
- After a further 2 ms, return LENS_CS to high impedance.

These values are **experimental**, not a Sony specification or proven A6000
requirements. Timing is implemented by nonblocking `micros()` polling;
report measured delays (`CS_HIGH_LATENCY_US`, `CS_LOW_LATENCY_US`). Software
polling does not guarantee these targets under USB or other interrupt load.
No USB diagnostic output is generated while the handshake is in progress.

For protection, the firmware:

- Drives **nothing** until the first observed BODY_CS edge.
- Times out if no edge arrives within 30 seconds of `p`.
- Releases LENS_CS if the second edge does not arrive within 50 ms of HIGH.
- Keeps hardware UART TX (XIAO D6) **as INPUT at all times**.
- Clears its output on `x` and on timing timeouts.
- Does not send Techart identity fixtures or any UART response to the body.

**Electrical caveat:** v0.3.1 still cannot measure `LOGIC_VCC`, so it cannot
independently guarantee that the camera's logic supply is present before
asserting HIGH on the USB-powered XIAO. Before connecting D10, verify the
probe physically with bench wiring, consider a current-limiting series
resistor on D10, and monitor body supply with a separate meter. Do not
connect D6, D0 or Sony E pin 5 to the camera in this firmware.

## Fixed Meike wiring convention

| Sony E contact | Signal | Wire color | XIAO / bench termination |
| --- | --- | --- | --- |
| 1 | LENS_GND | not wired | NC |
| 2 | LENS_POWER | not wired | NC |
| 3 | LOGIC_GND | black | GND |
| 4 | BODY_VD_LENS | orange | D8 |
| 5 | LOGIC_VCC | red | insulated, unconnected |
| 6 | LENS_CS_BODY | purple | D10, only for a verified probe setup |
| 7 | RXD, lens -> body | green | insulated, unconnected |
| 8 | TXD, body -> lens | blue | D7/RX |
| 9 | BODY_CS_LENS | yellow | D9 |
| 10 | LENS_XDETECT | brown | 680 ohm resistor to black/LOGIC_GND |

XIAO is powered only by USB. Clear all physical bench loopback jumpers first.

## Short commands

| Shortcut | Existing command | Meaning |
| --- | --- | --- |
| `l` | `camera listen` | Passive CS/VD/UART listener |
| `p` | `camera probe` | Arm two-edge CS handshake; turn on camera after arming |
| `s` | `camera status` | Counters, probe state, timestamps, measured delays |
| `t` | `camera trace` | First 16 BODY_CS edges with timestamps and logic states |
| `x` | `camera stop` | Release all E-mount GPIO |
| `h` | `help` | List commands |

Existing long commands and all bench commands remain supported. Every
received command is echoed as `[t_ms=... t_us=...] CMD=...` before execution.
The timestamps are monotonic since MCU boot, not wall-clock time. `millis()`
wraps in approximately 49.7 days; `micros()` wraps in approximately 71.6 min.

### Expected probe sequence

1. With the **camera powered off**, verify wiring, current limiting and the
   absence of shorts or backfeeding. Keep Sony E pin 5 unconnected to XIAO.
2. Connect the Meike adapter, run `p` and observe `PROBE_ARMED`.
3. Turn on the camera within 30 seconds.
4. After activity ceases, run `s` and `t`, then `x`.
5. Inspect `PROBE_STATE`, `CS_EDGES`, `VD_RISES`, `UART_BYTES`,
   `CS_HIGH_LATENCY_US` and `CS_LOW_LATENCY_US`.
6. If measured latencies greatly exceed 990/305 us, do not assume that the
   camera handshake was correctly timed. Further hardware-timer work may be
   required before moving to active UART replies.

In particular, no UART data after a successfully timed CS handshake is not
proof that the connection is defective: the real camera may require a different
start sequence or additional protocol responses.

## Voltage-monitor design note (later revision)

The red Sony E pin-5 wire can eventually be connected to XIAO D0/A0 for
monitoring the camera's 3.15 V `LOGIC_VCC` (relative to Sony E pin 3), **not
for powering XIAO**. First review XIAO's input tolerance, USB-powered I/O
injection current and the electrical schematic. Use input protection and a
high-impedance divider or other current-limiting interface, plus suitable
calibration. No analog connection or ADC firmware is included in v0.3.1.

## Source references

- LexOptical, `E-Mount.ino`, function `bodyCsChange()`:
  https://github.com/LexOptical/E-Mount/blob/master/E-Mount.ino
- Sony E-mount reverse engineering:
  https://github.com/weiziqian/E-mount-protocol-RE
- Seeed XIAO SAMD21 documentation:
  https://wiki.seeedstudio.com/Seeeduino-XIAO/
