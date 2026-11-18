# Stage 3.0 — Camera physical-link diagnostic (v0.3.0)

## Scope and release status

**PREVIEW / HOST TESTED / NOT TESTED ON A PHYSICAL CAMERA.**

This release adds a real GPIO/UART receive path for XIAO SAMD21 and a gated,
short-lived initial chip-select probe. It is **NOT YET a working E-mount lens**.
The existing `LensSession` replies are Techart autofocus fixtures. This release
never transmits them to a camera. A manual-lens profile and normal 0x05/0x06
status-loop implementation are required before claiming a complete handshake.

The previous USB protocol tests and physical bench tests remain unchanged.

### Features

- `camera listen`: 750 kbaud RX-only. Reads camera-to-lens data on Sony E pin 8
  and monitors Sony E pins 4 and 9. Lens CS (pin 6) remains high impedance.
- `camera probe`: same RX-only data path; **explicitly** raises lens CS (Sony E
  pin 6) for the initial pin handshake, releases it to high impedance ~50 ms after the first falling
  edge of body CS, or times out after 3 seconds. **No UART responses.**
- CS-window-delimited frame decode with F0/length/checksum/55 validation,
  counters and USB diagnostics. Handles leading/trailing non-frame bytes.
- `camera status` and `camera stop`, which releases all GPIOs and shuts UART.
- In camera mode, all old `bench` and `uart on` commands are rejected until
  `camera stop`. No bench stimulus is transmitted into a connected camera.
- Cold boot: all E-mount GPIO high impedance.

## Physical connections for this release

The pin numbering is **rear view of the lens, left to right**. Verify with a
continuity meter on the actual Meike adapter. Do not trust illustration
perspective or generated diagrams.

### Meike breakout harness — fixed wire colors

The physical Meike extension-ring breakout has **all Sony E contacts 3–10 wired**
with the colors below. Pins 1–2 are deliberately not wired. These colors are
project-specific conventions, not Sony's official harness standard.

**A wire installed on Meike is not necessarily connected to XIAO.** Keep
unused free ends insulated individually; in particular, the red LOGIC_VCC
lead must not be connected directly to XIAO's `3V3` or `5V` pins.

| Sony E pin | Function | Harness color | XIAO / circuit | `camera listen` | `camera probe` |
| --- | --- | --- | --- | --- | --- |
| 1 | LENS_GND | Not wired | Not connected | NC | NC |
| 2 | LENS_POWER | Not wired | Not connected | NC | NC |
| 3 | LOGIC_GND | **Black** | GND | Required | Required |
| 4 | BODY_VD_LENS | **Orange** | D8 | Recommended | Recommended |
| 5 | LOGIC_VCC (about 3.15 V) | **Red** | **Insulated, not connected** | NC | NC |
| 6 | LENS_CS_BODY | **Purple** | D10 | **Do not connect to XIAO** | Required |
| 7 | RXD (lens -> body) | **Green** | D6 (UART TX) | **Do not connect to XIAO** | **Do not connect to XIAO** |
| 8 | TXD (body -> lens) | **Blue** | D7 (UART RX) | Required | Required |
| 9 | BODY_CS_LENS | **Yellow** | D9 | Required | Required |
| 10 | LENS_XDETECT | **Brown** | 680-ohm resistor to pin 3 (black / LOGIC_GND) | Only if needed for detection | Required for experimental probe |

The brown detection wire is intended to be grounded **through a 680-ohm
resistor**, not connected directly to an XIAO GPIO or the `3V3` rail. The
resistor value follows the chosen experimental detection configuration; it
is not an assertion that all Sony lenses use this value.

All continuity and adjacent-contact short tests must be made with the camera
disconnected and powered off. Restore the required USB ground and input
connections only after verifying the harness against the actual ring.

- XIAO power: **USB-C only**; do not feed its `3V3` or `5V` header pin from Sony.
- No connection between LENS_POWER and LOGIC_VCC.
- The resistor connects Sony E pin **10** to Sony E pin **3** (logic ground).
- In this version there is no benefit in wiring Sony E pin **7** to D6: the
  firmware explicitly keeps D6 an INPUT after enabling Serial1 receive.
- For future two-way communication, D6 (TX) -> Sony E 7 and D10 -> Sony E 6.

### Hardware prerequisites

1. Remove **all** physical bench jumpers: D6-D7, D1-D8, D2-D9, D10-D3.
2. Camera switched off; verify Meike continuity 1:1, no adjacent-pin shorts.
3. Check with a meter that Sony E `LOGIC_VCC` is near the expected 3.15 V
   when energized. Verify logic levels and the absence of >3.3 V on UART/GPIO.
4. Attach signal wires only with the camera off, using a secure connection.
5. Provide strain relief and insulation for adjacent spring contacts.
6. Prefer a USB connection to an isolated bench host / LXC setup as already
   tested; **USB ground is tied to the camera logic ground** in this wiring.
7. Before trying `camera probe`, verify that the body can tolerate a 3.3 V
   logic output on pin 6 while the control line is driven; the body is not
   powered from XIAO. Do not attempt it if logic power is absent or uncertain.
8. Do not use these diagnostics with a native powered zoom lens attached in
   series unless a safe, independent inline tap is prepared.

## Serial commands

```text
camera listen
camera status
camera stop
camera probe
camera status
camera stop
```

Expected log examples (not a promise of camera behavior):

```text
CAMERA_MODE=LISTEN_HIGH_Z_NO_UART_TX
CAMERA_RX_CLASS=0x2 ID=0x1 SEQ=0 LEN=41
CAMERA_UART_TX=DISABLED
```

In `camera probe`, an initial CS handshake may yield a camera request, but the
camera should be expected to time out, because **v0.3.0 does not reply**.
Lack of a request does not automatically establish that the pin assignment or
protocol is wrong: detection/power sequence, camera timing and wiring must be
verified separately. The current device exposes only one RX direction.

## Tests

Tested with host-compiled C++ harness:

```bash
g++ -std=c++11 -Wall -Wextra -Werror \
  -Iinclude -Itest/host_stubs \
  src/frame.cpp src/session.cpp test/host_session_test.cpp -o /tmp/session-test
/tmp/session-test

g++ -std=c++11 -Wall -Wextra -Werror \
  -Iinclude -Itest/host_stubs \
  src/frame.cpp src/session.cpp src/bench.cpp test/host_bench_test.cpp -o /tmp/bench-test
/tmp/bench-test

g++ -std=c++11 -Wall -Wextra -Werror \
  -Iinclude -Itest/host_stubs \
  src/frame.cpp src/camera_link.cpp test/host_camera_link_test.cpp -o /tmp/camera-test
/tmp/camera-test
```

The camera host test also injects a valid 0x07 frame into a mocked CS window.

PlatformIO target build and hardware tests must be performed on the LXC 109
XIAO bench. The USB->LXC `/dev/ttyACM0` alias belongs to deployment config,
not the firmware source.

## Next gate: v0.3.x active lens replies

Before enabling active UART TX to the real camera:

- Replace every Techart AF fixture with a coherent **manual** lens capability,
  identity, descriptor and status profile. Avoid advertising motor features.
- Implement and verify chip-select pulse timing, baud-switch handshake, 0x16
  sleep/wake handling, and 0x05/0x06 periodic reports. The camera may require
  full protocol handling for normal operation, even if EXIF is the only goal.
- Test physical TX with an independent 3.3 V UART/logic analyzer before
  attaching the camera. No camera connection while using bench drivers.
- Use a real meter/logic analyzer to establish that GPIO states are electrically
  safe. The v0.2 physical loopback is not a camera compatibility test.

## Sources

- https://github.com/weiziqian/E-mount-protocol-RE
- https://github.com/weiziqian/E-mount-impl-on-Techart-EA9
- https://github.com/LexOptical/E-Mount
- https://wiki.seeedstudio.com/Seeeduino-XIAO/
