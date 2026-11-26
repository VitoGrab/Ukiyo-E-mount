# v0.4.1 — Always-ready Techart port and startup diagnostics

> Historical development record. Commands, paths and status labels below describe the original stage, not the current v0.47 firmware. See the README for the current workflow.

## Verified baseline

On Sony A6000, v0.4.0 completed the bidirectional 750 kb/s -> 1.5 Mb/s handshake, maintained repeated 0x0A traffic, and wrote the lens identity to JPG/ARW. The HELIOS 44M-4 profile reports Sony Lens ID 0, focal length 58 mm and range f/2–f/16. Camera-selected f/5.6 was recorded as `ExifIFD:FNumber=5.6`, while `MaxApertureValue=2.0` and `LensModel=HELIOS 44M-4 58mm f2` remained correct.

## Changes in this version

1. **Automatic standby:** The device starts in `MODE=ARMED` during `setup()`. No USB monitor and no manual `e` are required. There is no 30-second arm timer. D6 and D10 remain high impedance until qualified LOGIC_VCC has been observed on A0.
2. **Repeatable power cycles:** Sustained loss of LOGIC_VCC aborts UART and releases lens-facing outputs. The emulator returns to persistent `ARMED` after the loss is confirmed. When LOGIC_VCC returns, it attempts a new Techart initialization. The protocol engine is still the imported Techart implementation; the XIAO HAL and outer power lifecycle are ours. Repeated complete camera sleep/wake cycles require further hardware testing because the original Techart core has additional session state.
3. **ADC deglitching:** A 100k/100k A0 voltage divider is retained. The sense filter samples at most once every 2 ms. Rising threshold: 370 counts for 3 ms. Falling threshold: 310 counts for 12 ms. These are experimentally motivated defaults, not universal Sony voltage limits. A confirmed loss still releases GPIO. The 12 ms delay is a software compromise and **does not provide electrical isolation** from an unpowered camera; keep the prototype's series resistors and do not hot-plug while powered.
4. **Quiet keepalive logging:** `0x0A` messages are still decoded and answered but no longer print repeatedly to USB. Non-`0x0A` request IDs and initial handshake completion are printed. Full cumulative counters and `REQUEST_ID_TRACE` remain available via `s`; the 32-entry trace still records only the first IDs, as in v0.4.0.
5. **Startup timing:** `s` reports `BOOT_SETUP_ENTRY_MS`, `BOOT_PINS_READY_MS`, `BOOT_SETUP_DONE_MS`, `LOGIC_VCC_LAST_ON_US`, `INIT_STARTED_MS`, `INIT_DONE_MS`, and `HANDSHAKE_DONE_MS`. The on-board LED is asserted in `setup()` rather than waiting for the first 500 ms blink. USB CDC initialization does not wait for a host. These timestamps are relative to Arduino's sketch clock and **do not measure bootloader startup**.

## Operating procedure

1. With camera and XIAO unpowered, mount the ring and verify the same wiring as v0.4.0: D8 VD, D9 BODY_CS, D10 LENS_CS (with series resistance), D7 UART RX, D6 UART TX (with series resistance), brown detect contact to GND through 680 Ω, and red LOGIC_VCC to A0 through the tested 100k/100k divider.
2. Power the XIAO through USB, leave the camera off, and enter `s`. Expect `MODE=ARMED`, `AUTO_ARM=ENABLED`, `LOGIC_VCC_DETECTED=NO`, and no active UART.
3. Switch the A6000 on **without sending `e`**. After startup enter `s`. Expect `MODE=RUNNING`, `HANDSHAKE_DONE=1`, the correct UART speed, and protocol counters increasing. Recurring `0x0A` lines should no longer flood the console.
4. Take a photograph. A request with ID `0x35` should be visible if it was the last received request when the monitor sampled it. Check EXIF focal length, aperture and lens name. No lens profile values are changed by this patch.
5. Switch the A6000 off. Check `s` and then switch it on again **without disconnecting USB**. Collect both statuses. If the second handshake fails, capture `s` and `t`; do not repeatedly issue `e` while the camera is still powered.
6. `x` explicitly disables automatic arming and releases the camera outputs. `e` resumes persistent arming. Legacy commands `l`, `p` and bench tests must be preceded by `x` and must never run against a connected live camera unless specifically instructed.

## Timing and limitations

- `BOOT_SETUP_ENTRY_MS` is called at the top of `setup()`. The SAMD21 UF2 bootloader and C runtime execute before that. A few-second delay between applying power and first LED activity can originate upstream of the sketch. To quantify it, measure **power rail to earliest GPIO transition** with a logic analyzer or oscilloscope; comparing `millis()` with wall time is insufficient.
- The current camera adapter is still a USB-powered prototype. Powering it only from the Sony body requires a separate power-supply design and hardware startup measurement. Do not connect Sony contact 5 directly to XIAO `3V3`, or contact 2 to the XIAO without a regulator and protection review.
- ADC filtering makes false power-loss decisions less likely. It does not substitute for a power-aware output driver or isolation during partial contact insertion/removal.
- The original Techart core retains internal counters and state across warm sessions. Repeated initialization is newly attempted here, not yet verified on physical XIAO and camera. The host test validates only the outer lifecycle, not the upstream protocol state after a camera shutdown.
- If a hardware bootloader delay dominates, optimizing `setup()` cannot remove it. Investigate the bootloader or a suitable standalone deployment only after timing measurements. Preserve the working USB recovery path until then.

## Build and regression tests

From the repository root, with the existing imported Techart library and applied Helios profile:

```sh
/opt/platformio/bin/pio run
/opt/platformio/bin/pio run -t upload
```

For host-side regressions (requires C++17):

```sh
g++ -std=c++17 -Iinclude test/host_supply_gate_test.cpp -o /tmp/ukiyo-supply-test && /tmp/ukiyo-supply-test
g++ -std=c++17 -Itest/host_fake_vendor -Itest/host_stubs -Iinclude src/techart_port.cpp test/host_techart_autostart_test.cpp -o /tmp/ukiyo-auto-test && /tmp/ukiyo-auto-test
```

These checks do not replace PlatformIO compilation or physical verification of startup, CS timing and a second A6000 session.
