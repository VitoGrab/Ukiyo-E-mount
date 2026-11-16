# Planned E-mount to XIAO wiring

**NOT A PERMISSION TO CONNECT THE CAMERA WITH v0.1.0.** This firmware is a
bench-only protocol workbench. E-mount contact numbering is left-to-right
when looking at a lens from the rear.

| Sony E contact | Name | XIAO | Notes |
|---|---|---|---|
| 1 | LENS_GND | NC | Motor return; not needed for basic manual lens |
| 2 | LENS_POWER | NC | Motor 5V / battery; negotiation of the voltage is not known (yet) |
| 3 | LOGIC_GND | GND | Shared reference once safe wiring is established |
| 4 | BODY_VD_LENS | D8 / PA07 | Input: periodic frame sync |
| 5 | LOGIC_VCC | Power subsystem TBD | ~3.15V; not directly XIAO 3V3 output |
| 6 | LENS_CS_BODY | D10 / PA06 | MCU output, LOW at safe idle |
| 7 | RXD (lens to body) | D6 / PB08 | MCU UART TX |
| 8 | TXD (body to lens) | D7 / PB09 | MCU UART RX |
| 9 | BODY_CS_LENS | D9 / PA05 | Input |
| 10 | LENS_XDETECT | Resistor to LOGIC_GND, optional D3 | Only after electrical validation; body pull-up |

Reserved: D0 analog aperture, D1 button, D4/D5 I2C; other GPIO spare.

## Safety gates before camera connection

1. Measure/mechanically map all Meike pass-through pins with the camera OFF.
2. Inspect every clip for mechanical clearance, strain relief and shorts.
3. Measure LOGIC_VCC and logic states on the body using an insulated breakout.
4. Verify input protection and startup/shutdown behaviour. When XIAO is powered
   by USB with the camera OFF, never source current into camera signal pins.
5. In the real emulator, output CS/TX pins must be configured to safe states
   before lens detection is asserted. In passive sniffing all signal pins must
   be high-impedance inputs with *no internal pull-ups*.
6. Validate that Meike and native lens remain electrically continuous for
   a passive sniffer; tapping wires must not be inserted in series through the
   MCU. Never route the motor power through a breadboard or XIAO.

## Power planning

Sony LOGIC_VCC is ~3.15 V, but XIAO labels its 3V3 terminal as an output of its
on-board regulator. Powering XIAO through that terminal while also connected
to USB is not approved. An isolated, reverse-current-safe circuit needs to be
validated before standalone operation. LENS_POWER may be 5V or battery voltage
and requires an appropriately rated regulator; no motor power is required for
a manual-only device.

## v0.1.0 runtime behaviour

- Boot: D3 and D6-D10 are digital inputs with no internal pull-up. No lens
  detection or output signalling. USB diagnostics only.
- `uart on` changes D6 to UART TX and D7 to UART RX for a **standalone
  loopback test only**; disconnect all E-mount wires before this command.
- `uart off` ends UART and restores all interface GPIO to high impedance.
