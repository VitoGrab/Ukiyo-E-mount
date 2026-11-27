# Stage 5 — camera power proof of concept

Status: functional direct-power POC PASS on Sony a6000 (maintainer report, 2026-11-27). Baseline v0.411 at `d48da295069a5bd005a6cd30e2d14f7a220f58a7`. The hardware test used the existing firmware, bootloader and fuses; a separate diagnostic firmware commit on this branch was not required for the test. Electrical qualification and final Stage 5 acceptance remain open.

## Evidence and working hypothesis

The maintainer previously operated XIAO from a powerbank without a USB data host, observed approximately 3.2 V at Sony contact 5, and estimates a 2–3 s interval with logic supply present before communication. Logic supply may remain present for one or two minutes after camera shutdown. These observations motivate direct logic-rail power as the first candidate, not an established current budget or worst-case cold-start margin.

The upstream [E-mount pinout](https://github.com/weiziqian/E-mount-protocol-RE) identifies contact 5 as 3.15 V LOGIC_VCC, contact 3 as LOGIC_GND, contact 2 as motor LENS_POWER (5 V or battery voltage, negotiation not established), and contact 1 as motor ground. A manual lens emulator should not depend on motor power appearing before initialization. The maintainer reports that the motor-power conductor is parked on an isolated breadboard row, not connected to XIAO.

[Seeed forum guidance](https://forum.seeedstudio.com/t/can-i-connect-an-external-3-3v-power-to-the-3v3-pin/272174) says external supply at 3V3 is possible but recommends regulated VIN. The [linked user report](https://forum.seeedstudio.com/t/xiao-samd21-undervoltage-200mv-output-on-pin-10-with-digitalwrite-high/273514) describes a 3.3 V buck-powered board; it does not establish safe simultaneous USB and external power or a Sony rail's capacity. Review the actual XIAO schematic/revision and regulator behavior. If its LDO is XC6206, [Torex](https://www.torex-europe.com/products/voltage-regulators/low-iq/xc6206/) specifies operating input up to 6 V; never feed unknown motor rail into VIN.

## Maintainer measurements — 2026-11-27

With the externally powered v0.411 prototype, the maintainer measured 4.87 V between Sony contacts 1 and 2 on the multimeter's 20 V DC range. LENS_POWER disappears when the a6000 enters sleep or is switched off. LOGIC_VCC remains present during sleep and temporarily after shutdown, consistent with previous observations. These are qualitative lifecycle observations and a steady-state voltage measurement; no transient waveform or current capacity was measured.

This strengthens the preference for LOGIC_VCC: using motor power alone would remove MCU supply during sleep, losing the retained protocol state needed by the existing 0x0B short-wake path. Do not assume every native lens subsystem remains powered during sleep; the communication subsystem must remain available or resume in time.

A conventional series diode would reduce the already-low logic supply and may introduce GPIO level mismatches. Prefer a low-drop reverse-current-blocking solution if required after electrical characterization; a diode is not a current limiter. Keep USB physically disconnected for an eventual direct-supply POC, and disconnect the external 3V3 supply path before attaching USB. Neither the current budget nor safe regulator reverse-feed behavior has been established.

## First measurements, no firmware changes

- Keep the existing A0 100k/100k logic-supply divider and USB-powered v0.411 baseline. A0 is both a communication supply gate and a profile programming safety interlock; do not repurpose it for motor-power sensing.
- Use a high-impedance multimeter (DC V) or appropriate isolated measurement setup to measure contact 5 against contact 3, and contact 2 against its motor-ground contact 1. Confirm ground relationship before sharing the MCU reference. Do not connect the uncharacterized rail to MCU power or ADC. Measure during mount/power-on, initial handshake, active shooting, idle, sleep, wake and shutdown. A multimeter may miss short transitions; use a scope or suitably rated high-impedance probe for timing.
- If telemetry is useful, add a separate optional A1 ADC channel on this branch, not A0. Example divider 100k top / 33k bottom yields about 1.24 V at 5 V, 1.84 V at 7.4 V and 2.98 V at 12 V; verify actual maximum/transients and ADC acquisition/reference before connection. For an externally USB-powered board only. Preserve A0 qualification and profile interlocks; avoid ADC sampling in the handshake path. No unpowered MCU input connection.
- Characterize XIAO separately using a current-limited regulated bench supply at the measured logic-rail voltage with USB disconnected and camera wires removed. Record cold-start peak and steady current, board rail stability, regulator reverse-feed behavior and timing; compare with any credible camera rail current rating. Do not test rail capacity by shorting or driving the camera into current limit.
- If adequate current headroom and protection are established, evaluate contact 5 as the candidate direct 3V3 supply path, with USB physically disconnected, independent current limiting/reverse-current protection as required, common logic ground and verified power-off GPIO behavior. Do not tie camera and USB supplies together. Keep the existing divider; ADC readings then track a ratio to MCU VDD and do not independently demonstrate absolute voltage or brownout margin.
- Capture cold rail rise, BODY_CS/VD and first valid reply with external instrumentation; compare fallback and stored profiles. Distinguish power-on reset from warm USB/powerbank tests. Keep the working USB recovery path.

## Decision

Prefer contact 5 for the first protected power POC, subject to measured current margin, reverse-feed checks and cold-start timing. Contact 2 is initially a measurement-only alternative, not an assumed available supply. No final supply design or Stage 5 acceptance is claimed.

## Load characterization observations

Maintainer reported 501 ADC counts (3232 mV estimated) with no additional load and 498 counts (3212 mV estimated) with 1 kOhm. With 470 Ohm the emulator entered Running at 276760 ms and aborted for qualified LOGIC_VCC loss at 276790 ms, then rearmed. This does not identify whether the rail physically fell, the ADC misread, or the body withdrew supply due to a failed session. Do not increase load or disable the electrical gate until independently measured. The current qualification thresholds are 370/310 counts and 3/12 ms. A diagnostic-only change keeps ADC sampling in Off mode and reports the raw sample at an active power-loss abort, without relaxing the output interlock. With USB power, use x to disable emulation before the passive measurement, s to inspect supply state, and measure contacts 5/3 directly; a body may deliberately switch off its rail when no lens responds. The maintainer subsequently identified the 470 Ohm failure as a mechanical connection problem; the corrected measurements below supersede that initial observation. Remove the added load before ordinary operation.

## Completed load characterization — 2026-11-27

Measurements used the existing 100k/100k divider and USB-powered XIAO ADC (nominal 3.3 V reference assumption). Currents are calculated from estimated rail voltage and nominal resistor values, not measured directly. No extra load was applied for the direct-power POC.

| Added load | Active ADC | Active estimated V | Active added mA | Sleep ADC | Sleep estimated V | Sleep added mA |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| None | 501 | 3.232 | 0 | 477 | 3.077 | 0 |
| 1 kOhm | 498 | 3.212 | 3.21 | — | — | — |
| 470 Ohm | 497 | 3.206 | 6.82 | — | — | — |
| 330 Ohm | 496 | 3.200 | 9.70 | — | — | — |
| 220 Ohm | 497 | 3.206 | 14.57 | 473 | 3.051 | 13.87 |
| 150 Ohm | 496 | 3.200 | 21.33 | 469 | 3.025 | 20.17 |
| 100 Ohm | 492 | 3.174 | 31.74 | 430 | 2.774 | 27.74 |

At 100 Ohm, sleep remained stable near 2.78 V and the camera woke normally with the short handshake while XIAO was externally powered. The sleep rail droops more than the active rail. The 50 Ohm test was deliberately not performed. These readings do not establish absolute rail accuracy, maximum current, or transient margin.

## Direct camera power — functional POC PASS, 2026-11-27

The maintainer disconnected USB and the camera, removed the load resistor, retained the A0 100k/100k divider and XDETECT resistor, and connected Sony contact 5 directly to XIAO 3V3, with the existing common logic ground and signal connections. The motor rail remained unused. The initial no-LED attempt was traced to a mechanical electrical connection fault; correcting the connection restored operation. USB-powered recovery also worked before the corrected attempt.

With the existing v0.411 firmware and no USB attached, the maintainer reported green and yellow LEDs illuminated, lens recognition, sleep and wake, removal/reinstallation while the camera was off followed by startup, and live lens exchange with the camera on. These are functional observations on the a6000, not quantified timing or endurance measurements. No firmware change was needed to achieve body-powered operation.

### Follow-up qualification

- Measure actual XIAO supply voltage at its 3V3/GND pads under camera-only power in active, sleep and wake states. The present A0 conversion assumes 3.3 V VDD and becomes ratiometric with direct power; it cannot independently measure absolute supply voltage.
- Characterize current draw, startup transients, cold-start margin, and regulator reverse-feed behavior when suitable equipment is available. Preserve physical USB isolation whenever the direct supply is connected.
- Consider independent internal ADC reference or scaled VDD measurement and a low-duty-cycle undervoltage warning, initially targeting 2.8 V with hysteresis. Verify ADC configuration and calibration, protect timing and the existing profile-programming interlock, and avoid confusing the existing LED meanings. This is a proposed firmware enhancement, not implemented here.
- Repeat photo/EXIF, aperture, sleep/wake and power-cycle tests with retained profiles; test production build and other bodies separately. Final hardware protection, mechanical mounting, and production qualification remain open.
