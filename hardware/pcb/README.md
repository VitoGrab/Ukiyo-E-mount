# Custom PCB

Status: **TBD — not yet designed.**

This directory will contain the contact PCB design, schematic, PCB source files, electrical BOM and manufacturing files once they exist. Electrical design and camera-powered operation must be validated before fabrication and release.

## Candidate: repurposed Sony SELP1650 contact assembly

Status: **Under evaluation — not selected or validated for the final adapter.**

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
- Evaluate a matching FPC/ZIF connector on an interconnect PCB first. If unavailable, investigate a mechanically retained custom contact interface or a soldered flex termination with strain relief.
- Confirm insulation, electrical continuity, absence of shorts and safe power handling before connecting to a camera.

The custom PCB and final mounting remain TBD. This candidate does not replace the existing design requirements.
