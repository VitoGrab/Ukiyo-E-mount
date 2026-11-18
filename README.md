# 浮世E-mount (ukiyo-e-mount)

Open-hardware Sony E-mount lens emulator based on XIAO SAMD21 board bridging vintage manual optics and modern digital cameras.

### Objective

Emulate a manual Sony E-mount lens for a Sony cameras, with a stable, fixed
focal length and an identifiable lens name in EXIF metadata. Start with 
a popular Helios 44M-4 lens - manual-focus 58 mm f/2 lens profile and expand 
to other lenses including zoom lenses.

## ROADMAP

### Development stages

| Stage | Deliverable | Exit criterion |
|---|---|---|
| 0. Board validation | XIAO USB upload, monitoring, 750 kbaud D6-D7 loopback | Tests pass without camera |
| 1. Protocol workbench | Frame encoder/parser, checksum, imported 0x07 identity fixture, host regression tests | Tests pass without camera |
| 2. Safe bus bring-up | Electrical validation, body CS capture, clock/baud, shutdown, high-Z pins when off | Observed handshake and no backfeeding |
| 3. Manual-lens emulation | Techart init/status replies, A6000 capability and 0x0C speed handling | Camera keeps lens powered and shoots |
| 4. EXIF verification | 58 mm and manual aperture profile, identity experiments | ARW FocalLength, LensInfo, LensModel inspected with ExifTool |
| 4.5 More cameras compatibility | Checking compatibility with other Sony cameras | Camera keeps lens powered and shoots |
| 5. Standalone power | Safe power switching from LOGIC_VCC or regulated motor supply | Works without USB and after repeated mounts |
| 6. Mechanical integration | Non-destructive adapter fit, contact PCB if needed | Secure fit, no shorts, optics unaffected |
|---|---EXTRAS---|---|
| 7. Aperture sensing | Analog lever sensor / optional button mode | Consistent aperture values |
| 8. Extras | I2C sensors, lens position, optional ranging/AF experiments | Separate proof-of-concept tests |

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
- Handle 0x16 shutdown cleanly. Keeping outputs active across shutdown may
  prevent correct remount/boot.
- Frame parser uses declared length, checks the checksum, and ignores padding
  after the terminator. It must reject truncated frames.
- No autofocus/motor capabilities should be advertised until implemented.
- Reserve D4/D5 for I2C and D0/D1 for optional aperture controls.

### Projects which made this possible

- Techart EA9 E-mount reference firmware (MIT):
  https://github.com/weiziqian/E-mount-impl-on-Techart-EA9
- E-mount physical layer, framing and A6000 session captures:
  https://github.com/weiziqian/E-mount-protocol-RE
- Legacy NEX-7 implementation:
  https://github.com/LexOptical/E-Mount


### Links to technical documentation of the board
- PlatformIO board definition:
  https://docs.platformio.org/en/latest/boards/atmelsam/seeed_xiao.html
- Official XIAO SAMD21 documentation:
  https://wiki.seeedstudio.com/Seeeduino-XIAO/

### References for E-mount PCB
- Mechanical PCB photo reference:
  https://www.pdmovie.com/products/smartfusion
