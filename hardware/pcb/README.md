# Custom PCB

Status: **TBD — not yet designed.**

This directory will contain the contact PCB design, schematic, PCB source files, electrical BOM and manufacturing files once they exist. Electrical design and camera-powered operation must be validated before fabrication and release.

## Candidate: repurposed Sony SELP1650 contact assembly

Status: **Selected for the next mechanical prototype; procurement planned, not yet physically validated.**

The Sony SELP1650 service parts list identifies the rear contact assembly as `X-2585-594-1` (also `X25855941`), description `BOX ASSY, BL CONTACT`, reference 13. Reusing a manufactured contact assembly could avoid fabricating the camera-facing contact surfaces and provide existing screw mounting features. A separate interconnect PCB may still be required.

References:
- [Maintainer's eBay candidate](https://www.ebay.com/itm/406930206456) (listing not independently accessible during review).
- [Alternative replacement listing](https://www.ebay.com/itm/128018433077) (authenticity and equivalence unverified).
- [Sony service manual, parts list](https://www.manualslib.com/manual/2187730/Sony-E-3-5-5-6-Pz-16-50-Oss.html).
- [Spare part reference](https://fixpart.co.uk/product/sony-x25855941-circuit-board).
- [iFixit disassembly, step 3](https://www.ifixit.com/Guide/SELP1650+SELP+1650++Sony+Lens+Disassembly/95695): the rear contact flex uses a locking ZIF-style connection, with the exposed fingers facing up during reassembly.

### Open questions

- Measure the assembly dimensions, mounting hole spacing, contact height and optical clearance against the reference M42–NEX housing.
- Measure flex thickness, conductor count, contact center spacing and pad widths. Unequal pad widths do not necessarily imply unequal pitch. Connector manufacturer and part number remain unknown.
- Map each flex conductor to the camera-facing contacts by continuity testing; do not infer the pin order from pad geometry.
- Evaluate direct wire soldering to the exposed solder pads at the contact assembly first, bypassing the flex tail and ZIF connector. Confirm pad accessibility, continuity, insulation and strain relief on the actual part. A matching FPC/ZIF connector or custom interconnect remains a fallback, not a requirement.
- Confirm insulation, electrical continuity, absence of shorts and safe power handling before connecting to a camera.

The custom PCB and final mounting remain TBD. This candidate does not replace the existing design requirements.

### Preferred prototype approach

Investigate direct soldering of short wires to the exposed solder points of the original contact assembly. This may eliminate the need for the flex connector, donor SELP1650 mainboard and a custom contact/interconnect PCB. Do not remove the flex or solder until its electrical and mechanical role has been checked on the actual part. The final power interface and any additional components remain TBD pending camera-powered validation. A 3D-printed carrier is the preferred mechanical concept; see [Mechanical integration](../mechanical/README.md). This approach is proposed, not physically validated.

## Procurement decision — 2026-11-27

Purchase the SELP1650 service contact assembly separately (X-2585-594-1), potentially several units. Do not purchase a damaged donor lens solely for this purpose. Listing photos suggest accessible solder pads; verify on receipt before soldering. Direct wiring is preferred, with continuity mapping, strain relief and insulation. No order or physical fit is confirmed.
