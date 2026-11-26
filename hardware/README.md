# Hardware

This directory describes the intended physical Ukiyo-E-mount adapter. The integrated hardware is still under development; this is a preliminary parts list, not a build-ready release.

## Required hardware

| Component | Quantity | Status | Reference |
| --- | --- | --- | --- |
| Seeed Studio XIAO SAMD21 | 1 | Selected; firmware tested on this board | [Manufacturer product page](https://www.seeedstudio.com/Seeeduino-XIAO-Arduino-Microcontroller-SAMD21-Cortex-M0-p-4426.html) · [Technical documentation](https://wiki.seeedstudio.com/Seeeduino-XIAO/) |
| M42–NEX mechanical adapter (specific housing design) | 1 | Reference housing selected; integration pending | [Original purchased adapter](https://www.aliexpress.com/item/32882376567.html) · [Alternative listing](https://www.aliexpress.com/item/1005004573509701.html) |
| Custom PCB and mechanical mounting inside the adapter | 1 assembly | TBD | [PCB](pcb/README.md) · [Mechanical integration](mechanical/README.md) |

The reference adapter is a specific mechanical design, not an arbitrary lens adapter with the same mount designation. Internal dimensions and contact-board mounting clearances must be verified before substituting another model. The original purchased adapter is the mechanical reference. The alternative listing appears to offer the same housing design according to the project maintainer; dimensional interchangeability has not been independently verified. These listings are examples, not required sellers.

### Adapter identification

A photograph of the reference M42–NEX adapter will be added here by the project maintainer (TBD). Use the housing design and, once available, the dimensional requirements in [mechanical/README.md](mechanical/README.md) to identify suitable equivalents, rather than relying on seller names or product listings alone.

For the microcontroller, use the SAMD21 model, not another XIAO family member. The final assembly's connector/header and clearance requirements are TBD.

A repurposed Sony SELP1650 rear contact assembly (X-2585-594-1) is being evaluated as an alternative to a custom camera-facing contact PCB; see [PCB candidates](pcb/README.md#candidate-repurposed-sony-selp1650-contact-assembly). The PCB, its electrical component list, mounting method and camera-powered operation have not yet been finalized. Do not interpret this list as a complete fabrication or installation guide.

## Optional development hardware

The breadboard test setup is separate from the final adapter and is not required for the intended finished product. See [development/README.md](development/README.md).
