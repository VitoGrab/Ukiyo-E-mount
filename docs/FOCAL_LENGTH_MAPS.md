# Aperture zoom focal-length maps

Status: maintainer-approved mapping, 2026-11-28. The two maps are stable user-facing conventions, not per-lens interpolation. The canonical machine-readable source is [focal_length_maps.json](../profiles/focal_length_maps.json); profile preparation generates and verifies [focal_length_maps.h](../include/emount/focal_length_maps.h). Aperture values are virtual controls, not physical iris measurements.

Both maps have 40 positions on the confirmed visible f/1–90, 1/3 EV grid. RAW is authoritative; the camera may round nominal f-number labels differently. Index is zero-based: `RAW = 0x1000 + floor(index * 256 / 3)`. Never reinterpret ramp values as settled positions. The extended-range test found that some out-of-display values are accepted on the wire, but these are deliberately excluded from the user-facing maps.

| Index | Selector (nominal f-number) | RAW | Standard (mm) | Telephoto (mm) |
|---:|---:|---:|---:|---:|
| 0 | f/1 | `0x1000` | 20 | 35 |
| 1 | f/1.1 | `0x1055` | 21 | 40 |
| 2 | f/1.2 | `0x10AA` | 24 | 50 |
| 3 | f/1.4 | `0x1100` | 25 | 60 |
| 4 | f/1.6 | `0x1155` | 28 | 70 |
| 5 | f/1.8 | `0x11AA` | 30 | 75 |
| 6 | f/2 | `0x1200` | 35 | 80 |
| 7 | f/2.2 | `0x1255` | 40 | 85 |
| 8 | f/2.5 | `0x12AA` | 45 | 90 |
| 9 | f/2.8 | `0x1300` | 48 | 100 |
| 10 | f/3.2 | `0x1355` | 50 | 105 |
| 11 | f/3.5 | `0x13AA` | 60 | 110 |
| 12 | f/4 | `0x1400` | 70 | 120 |
| 13 | f/4.5 | `0x1455` | 75 | 125 |
| 14 | f/5 | `0x14AA` | 80 | 135 |
| 15 | f/5.6 | `0x1500` | 85 | 150 |
| 16 | f/6.3 | `0x1555` | 90 | 160 |
| 17 | f/7.1 | `0x15AA` | 95 | 170 |
| 18 | f/8 | `0x1600` | 100 | 180 |
| 19 | f/9 | `0x1655` | 105 | 190 |
| 20 | f/10 | `0x16AA` | 110 | 200 |
| 21 | f/11 | `0x1700` | 115 | 210 |
| 22 | f/13 | `0x1755` | 120 | 230 |
| 23 | f/14 | `0x17AA` | 125 | 240 |
| 24 | f/16 | `0x1800` | 130 | 250 |
| 25 | f/18 | `0x1855` | 135 | 275 |
| 26 | f/20 | `0x18AA` | 140 | 280 |
| 27 | f/22 | `0x1900` | 145 | 300 |
| 28 | f/25 | `0x1955` | 150 | 320 |
| 29 | f/29 | `0x19AA` | 155 | 350 |
| 30 | f/32 | `0x1A00` | 160 | 375 |
| 31 | f/36 | `0x1A55` | 165 | 400 |
| 32 | f/40 | `0x1AAA` | 170 | 425 |
| 33 | f/45 | `0x1B00` | 175 | 450 |
| 34 | f/51 | `0x1B55` | 180 | 475 |
| 35 | f/57 | `0x1BAA` | 185 | 500 |
| 36 | f/64 | `0x1C00` | 190 | 525 |
| 37 | f/72 | `0x1C55` | 195 | 550 |
| 38 | f/81 | `0x1CAA` | 200 | 575 |
| 39 | f/90 | `0x1D00` | 210 | 600 |

## Profile selection and bounds

- `fixed`: prime, no map.
- `default_zoom` ([DZ]): selected integer default focal at upload; aperture is independent.
- `aperture_zoom` ([AZ]): `focal_length_map` is required; standard if maximum focal length is at most 210 mm, telephoto otherwise. Both optical endpoints and catalog default must appear exactly in the selected map. Unsupported ranges are rejected, not silently rounded. The generator advertises only the corresponding inclusive aperture subrange and initializes the aperture to the default's mapped value. The stored focal length remains fixed until dynamic reporting is implemented.
- AZ virtual limits are written consistently into 0x08 and 0x05; virtual 0x08 BCD maximum is capped at f/9 by the existing representation. The current three AZ records have starting apertures below this cap. The physical optical values in JSON remain separate, and independent capture aperture reporting is future work.
- v1 BINs do not carry a dedicated persistent mode or map identifier. The firmware checks matching map ranges for extended aperture validation; it must not use that heuristic to activate dynamic behavior. Introduce an integrity-protected explicit mode/map before runtime switching.

| Profile | Map | Physical range | Selector RAW | Positions |
|---|---|---|---|---:|
| Arsat [AZ] | standard | 80–200 | 0x14AA–0x1CAA | 25 |
| Sigma [TEST] [AZ] | standard | 21–35 | 0x1055–0x1200 | 6 |
| Takumar [TEST] [AZ] | telephoto | 135–600 | 0x14AA–0x1D00 | 26 |

Sigma and Takumar are test records, not claims of ownership or hardware validation. Sigma uses the 21–35 f/3.5–4.2 variant with f/22 as the wide-end physical minimum reference (the manufacturer lists f/22–27 across the range); the physical variable-aperture behavior is not implemented. Takumar uses the documented 135–600 f/6.7–45 specification. The AZ templates advertise virtual constant apertures and do not claim variable-aperture optical emulation. Normal DZ still rejects variable-aperture optics. Zoom focal validation extends to 600 mm; prime validation stays at 500 mm.

Sources: [Sigma lens catalogue](https://www.cameramanuals.org/booklets/sigma_lens_booklet.pdf), [Takumar lens specifications](https://m42lens.com/m42-lens-database/126-asahi-optical-co-s-m-c-takumar-zoom-135-600mm-f-6-7-45).
