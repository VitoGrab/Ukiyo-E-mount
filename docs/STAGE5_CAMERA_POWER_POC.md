# Stage 5 — camera power proof of concept

Status: planning / measurement only. Baseline v0.411 at `d48da295069a5bd005a6cd30e2d14f7a220f58a7`. No firmware, bootloader, fuse or wiring change has been made on this branch.

## Evidence and working hypothesis

The maintainer previously operated XIAO from a powerbank without a USB data host, observed approximately 3.2 V at Sony contact 5, and estimates a 2–3 s interval with logic supply present before communication. Logic supply may remain present for one or two minutes after camera shutdown. These observations motivate direct logic-rail power as the first candidate, not an established current budget or worst-case cold-start margin.

The upstream [E-mount pinout](https://github.com/weiziqian/E-mount-protocol-RE) identifies contact 5 as 3.15 V LOGIC_VCC, contact 3 as LOGIC_GND, contact 2 as motor LENS_POWER (5 V or battery voltage, negotiation not established), and contact 1 as motor ground. A manual lens emulator should not depend on motor power appearing before initialization. The maintainer reports that the motor-power conductor is parked on an isolated breadboard row, not connected to XIAO.

[Seeed forum guidance](https://forum.seeedstudio.com/t/can-i-connect-an-external-3-3v-power-to-the-3v3-pin/272174) says external supply at 3V3 is possible but recommends regulated VIN. The [linked user report](https://forum.seeedstudio.com/t/xiao-samd21-undervoltage-200mv-output-on-pin-10-with-digitalwrite-high/273514) describes a 3.3 V buck-powered board; it does not establish safe simultaneous USB and external power or a Sony rail's capacity. Review the actual XIAO schematic/revision and regulator behavior. If its LDO is XC6206, [Torex](https://www.torex-europe.com/products/voltage-regulators/low-iq/xc6206/) specifies operating input up to 6 V; never feed unknown motor rail into VIN.

## First measurements, no firmware changes

- Keep the existing A0 100k/100k logic-supply divider and USB-powered v0.411 baseline. A0 is both a communication supply gate and a profile programming safety interlock; do not repurpose it for motor-power sensing.
- Use a high-impedance multimeter (DC V) or appropriate isolated measurement setup to measure contact 5 against contact 3, and contact 2 against its motor-ground contact 1. Confirm ground relationship before sharing the MCU reference. Do not connect the uncharacterized rail to MCU power or ADC. Measure during mount/power-on, initial handshake, active shooting, idle, sleep, wake and shutdown. A multimeter may miss short transitions; use a scope or suitably rated high-impedance probe for timing.
- If telemetry is useful, add a separate optional A1 ADC channel on this branch, not A0. Example divider 100k top / 33k bottom yields about 1.24 V at 5 V, 1.84 V at 7.4 V and 2.98 V at 12 V; verify actual maximum/transients and ADC acquisition/reference before connection. For an externally USB-powered board only. Preserve A0 qualification and profile interlocks; avoid ADC sampling in the handshake path. No unpowered MCU input connection.
- Characterize XIAO separately using a current-limited regulated bench supply at the measured logic-rail voltage with USB disconnected and camera wires removed. Record cold-start peak and steady current, board rail stability, regulator reverse-feed behavior and timing; compare with any credible camera rail current rating. Do not test rail capacity by shorting or driving the camera into current limit.
- If adequate current headroom and protection are established, evaluate contact 5 as the candidate direct 3V3 supply path, with USB physically disconnected, independent current limiting/reverse-current protection as required, common logic ground and verified power-off GPIO behavior. Do not tie camera and USB supplies together. Keep the existing divider; ADC readings then track a ratio to MCU VDD and do not independently demonstrate absolute voltage or brownout margin.
- Capture cold rail rise, BODY_CS/VD and first valid reply with external instrumentation; compare fallback and stored profiles. Distinguish power-on reset from warm USB/powerbank tests. Keep the working USB recovery path.

## Decision

Prefer contact 5 for the first protected power POC, subject to measured current margin, reverse-feed checks and cold-start timing. Contact 2 is initially a measurement-only alternative, not an assumed available supply. No final supply design or Stage 5 acceptance is claimed.
