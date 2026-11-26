# Hardware

This directory describes the intended physical Ukiyo-E-mount adapter. The integrated hardware is still under development; this is a preliminary parts list, not a build-ready release.

## Required hardware

| Component | Quantity | Status | Reference |
| --- | --- | --- | --- |
| Seeed Studio XIAO SAMD21 | 1 | Selected; firmware tested on this board | [Manufacturer product page](https://www.seeedstudio.com/Seeeduino-XIAO-Arduino-Microcontroller-SAMD21-Cortex-M0-p-4426.html) · [Technical documentation](https://wiki.seeedstudio.com/Seeeduino-XIAO/) |
| Specific mechanical lens-to-Sony E (NEX) adapter | 1 | Selected reference listing; exact listing designation and direct item URL to be confirmed | [Reference listing (AliExpress share URL)](https://a.aliexpress.com/_EwfrSLc) |
| Custom PCB and mechanical mounting inside the adapter | 1 assembly | TBD | [PCB](pcb/README.md) · [Mechanical integration](mechanical/README.md) |

The reference adapter is a specific mechanical design, not an arbitrary lens adapter with the same mount designation. Internal dimensions and contact-board mounting clearances must be verified before substituting another model. The AliExpress reference is provisional until the original product ID and mount designation have been confirmed.

For the microcontroller, use the SAMD21 model, not another XIAO family member. The final assembly's connector/header and clearance requirements are TBD.

The PCB, its electrical component list, mounting method and camera-powered operation have not yet been finalized. Do not interpret this list as a complete fabrication or installation guide.

## Optional development hardware

The breadboard test setup is separate from the final adapter and is not required for the intended finished product. See [development/README.md](development/README.md).
