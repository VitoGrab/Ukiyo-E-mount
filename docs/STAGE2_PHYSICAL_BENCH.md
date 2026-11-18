# Stage 2.1 — XIAO physical-pin workbench (v0.2.1)

**Status:** Bench-only development preview. **Never attach a camera, Meike ring,
Sony lens, or external power while executing a `bench` command.** USB-C supplies
all power. The 5V and 3V3 header pads MUST NOT be jumpered to GPIO.

This is an optional supplement to v0.2.0's in-memory session engine. It validates
physical GPIO operation, actual UART TX/RX echo and synthetic session payloads.
It is **not** a working E-mount lens emulator or a camera protocol timing test.

## Firmware base

The **cumulative** v0.2.1 overlay installs directly on the current v0.1.0
repository as well as on v0.2.0. Keep the existing `platformio.ini`.
This update modifies `src/main.cpp` and adds the files:

- `include/ukiyo-e-mount/bench.h`
- `src/bench.cpp`
- `test/host_bench_test.cpp`
- `test/host_stubs/Arduino.h`
- `docs/STAGE2_PHYSICAL_BENCH.md`

It still requires the v0.2.0 `session.h` / `session.cpp`, v0.1.0 frame codec and
existing PlatformIO board definition (`seeed_xiao`).

## Jumper connections

XIAO only, **disconnected from camera and Meike**, all at 3.3V GPIO levels.
Set up these four **independent** jumper wires after removing USB power:

| Wire | Direction in the mock | Purpose |
| --- | --- | --- |
| D6 (TX) -> D7 (RX) | output -> input | Physical serial loopback |
| D1 -> D8 | simulated body VD -> VD input | Verify BODY_VD_LENS input |
| D2 -> D9 | simulated body CS -> CS input | Verify BODY_CS_LENS input |
| D10 -> D3 | lens CS output -> observer input | Verify LENS_CS_BODY output |

`D1`, `D2` and `D3` are used for the bench fixture *only* and do **not** change
the final E-mount pin allocation. No resistor is required for these XIAO-to-XIAO
3.3V GPIO jumper tests. Verify there are no shorts between neighboring pads.
Do not wire any pin to `GND`, `3V3`, or `5V` for these tests.

## USB serial commands

All pins start in high-impedance mode on reboot. The commands explicitly enable
active GPIO and automatically return all selected pins to input mode at completion.

```text
status
proto
session a6000
session a9ii
bench gpio
bench a6000
bench a9ii
```

`bench gpio` uses the three GPIO jumpers, without transmitting UART bytes.
Output should include `BENCH_GPIO=PASS`. On failure, check and remove power
before adjusting jumper wires.

`bench a6000` / `bench a9ii` require **all four** jumpers. They:

1. Check digital GPIO loops.
2. Generate synthetic init requests for 9 / 11 protocol messages.
3. Pulse D1 and sample D8 (VD frame marker).
4. Hold D2 high and verify D9 during the request transfer.
5. Physically send request bytes from D6 to D7 one by one, decode them, and
   feed them to the LensSession class.
6. Hold D10 high and sample D3 during response transfer, then transmit the
   response bytes through the same UART self-loop, decode, and verify each reply.
7. Following `0x0C`, end / restart the UART at 1,500,000 bps (the initial rate
   is 750,000 bps). Both CS lines rise briefly during the simulated switch.
8. Release all GPIO to input at the end, on success or failure.

Expected summary for A6000:

```text
BENCH_GPIO=PASS
BENCH_ACCEPTED=9
BENCH_FINAL_BAUD=1500000
BENCH_BAUD_SWITCH=PASS
BENCH_SESSION=PASS
BENCH_MODE=SINGLE_UART_PHYSICAL_SELF_ECHO
TIMING_VALIDATION=NOT_PERFORMED
CAMERA_CONNECTION=FORBIDDEN
```

For a9 II, `BENCH_ACCEPTED=11`.

A single UART TX->RX jumper is **NOT** an independent camera simulator. The MCU
sends a synthetic request, receives its own bytes, computes a response, and
receives its own transmitted response. This verifies that frame handling can
run over the physical UART and tests real GPIO transitions. It cannot validate
camera response timing, line arbitration, simultaneous two-endpoint UART use,
voltage/power negotiation, continuous 210-byte transfers, or true chip-select
window timing. The test deliberately waits for every echoed byte, creating
inter-byte gaps. A logic analyser is required for timing measurements.

**Never connect the camera with these jumpers in place.** D1, D2 and D10 actively
drive other GPIOs and must not drive body pins. `LENS_XDETECT` (Sony E pin 10),
Sony motor supply and LOGIC_VCC are deliberately not connected.

## Building and testing

```bash
cd /root/projects/Ukiyo-E-mount
/opt/platformio/bin/pio run
/opt/platformio/bin/pio run -t upload
/opt/platformio/bin/pio device monitor -b 115200
```

The working `/dev/ttyACM0` passthrough configuration from LXC 109 is unchanged.

Optional host unit tests (outside PlatformIO):

```bash
g++ -std=c++11 -Wall -Wextra -Werror \
  -Iinclude -Itest/host_stubs \
  src/frame.cpp src/session.cpp src/bench.cpp test/host_bench_test.cpp \
  -o /tmp/ukiyo-host-bench
/tmp/ukiyo-host-bench
```

The host test uses simulated GPIO and UART wiring and checks success/failure
and cleanup paths. It is **not** a hardware test. A separate existing host test
continues to exercise the in-memory protocol session.

## Further improvement: independent mock camera on the same MCU

The ATSAMD21G18A provides more than one SERCOM peripheral. A second hardware
UART can be configured temporarily on `SERCOM2`, with D4/PA08 (TX/PAD0) and
D5/PA09 (RX/PAD1), while the actual lens UART remains D6/D7 (`SERCOM4`).
Then use two **crossed** TX/RX jumpers instead of D6->D7:

- Mock camera D4 TX -> lens D7 RX
- Lens D6 TX -> mock camera D5 RX

This requires an additional `Uart` instance, correct pin multiplexing, SERCOM2
interrupt service, and integration testing at 750 kbaud and 1.5 Mbaud. **It is
not implemented in v0.2.1.** D4/D5 are only repurposed for a bench fixture,
leaving the planned I2C sensor assignment unchanged in production.

See https://github.com/sigmdel/xiao_usarts for validated extra-UART examples,
and https://www.seeedstudio.com/xiao-series-page/playground/pinout/ for pin mapping.
