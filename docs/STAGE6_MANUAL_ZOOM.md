# Stage 6 — manual zoom profiles

Status: planned. Baseline: v0.5.0, commit `4ea6d53286bc0990f38eb57011043ad0ad3f1e12`. Target milestone: v0.6.0 after acceptance, not a version bump at entry. Tracking: [#5](https://github.com/VitoGrab/Ukiyo-E-mount/issues/5). Sensor-based ring measurement is separate future research ([#6](https://github.com/VitoGrab/Ukiyo-E-mount/issues/6)). Cross-body compatibility follows Stage 6.

## Reference hardware and scope

Sony a6000, XIAO SAMD21, the proven camera-powered prototype, and the maintainer's Arsat M 80–200 mm f/4.5. The existing catalog declares 80–200 mm, constant maximum f/4.5, minimum f/22, default 80 mm; confirm marked aperture endpoints against the physical lens. No AF, OSS, power zoom, physical zoom control, additional hardware or changes to the power/wake mechanism are in scope. Aperture and focal length are reported metadata unless physically measured. Preserve all 16 existing prime profiles, independent USB programming and the embedded Helios fallback.

Work on a dedicated Stage 6 branch. Commit each substage independently and stop for maintainer hardware acceptance before the next behavior change.

## 6A — correct zoom identity

1. Inspect current upstream protocol documentation and captured native manual/zoom responses. Map 0x08 payload 16–19 to BCD shortest/longest focal lengths and 20–21 to endpoint maximum apertures. Keep 0x07 lens ID/firmware identity separate, 0x3F model name, and known native MF flags. Do not copy unidentified motor/OSS fields from power zoom lenses.
2. Extend catalog and binary generation/validation jointly: distinguish prime versus zoom, enforce ordered endpoints and a valid default, verify BCD, aperture endpoints, checksums, cross-frame focal consistency and rejection of malformed profiles. First use Arsat at the existing 80 mm default; do not introduce runtime control yet.
3. Preserve byte-for-byte v1 prime images. Prefer reuse of the existing packet layout where safe; if explicit mode metadata requires a new format, design a versioned, integrity-protected extension and keep v1 prime loading and upload supported. Decide the precise representation before coding; do not repurpose unprotected reserved bytes as configuration. Ensure Flash geometry, readback, USB chunking and size checks match the selected format.
4. Test generated packets offline and program Arsat through the normal USB workflow. On a6000 confirm successful native MF, Peaking/Magnifier, shooting, model name, zoom range in available EXIF/MakerNotes, and current focal length 80 mm. Record actual tags, including LensModel, LensInfo/LensSpec, FocalLength, FNumber, FocusMode; distinguish unavailable tags from wrong values. If the body does not expose a range, validate the transmitted descriptor before changing unknown flags.

Gate 6A: valid zoom identification and shooting at 80 mm, with unchanged prime behavior. Capture ARW/JPG evidence and exact firmware commit/profile checksum.

## 6B — default focal length

- Add explicit fixed/default behavior for a zoom profile (name proposed: `zoom_default`). Retain the catalog default (Arsat 80 mm) unless the user chooses otherwise.
- Allow selection of any integer focal length inside the range during profile preparation/upload (optional prompt; Enter uses catalog default). Generate an independent temporary/custom BIN without overwriting the distributed catalog or requiring a firmware rebuild. Verify the stored profile by readback.
- Maintain one coherent current-focal state; apply to 0x05 payload 24–27 and 0x28/0x35 payload 4–7. The two values are nominal/actual candidates, not the declared zoom range; report equal chosen values provisionally until evidence establishes a reason to differ. Leave 0x05 effective focal field and unknown 0x06 zoom fields alone unless justified by capture evidence.
- Keep full zoom descriptor 80–200 in 0x08 regardless of default. Preserve independent normal aperture control and fixed-aperture prime behavior. Current focal persists across power loss because it is part of the uploaded profile.
- Validate minimum, middle (135 mm), maximum, arbitrary in-range value and rejected out-of-range inputs. On a6000 shoot at 80/135/200 mm with at least two body aperture settings; compare EXIF, MF and unchanged zoom name/range. Test sleep/wake and full power cycle.

Gate 6B: default focal selectable without recompiling; correct focal length across status/capture replies, aperture independent, persistent profile retained. This is the fallback even if aperture-focal control is unsuitable.

## 6C — aperture-focal control (experimental, opt-in)

Goal: body aperture dial can select the reported focal length without additional electronics. Keep `zoom_default` available and unaffected. Do not enable this mode for primes.

1. Evidence first: add bounded diagnostic capture of validated 0x03 aperture pair (payload 3–4, 5–6 and selector bit in 7), and capture-time 0x1B. Current firmware already samples 0x03's second value. On a6000 in M/A, test every displayed aperture step, half-press, full press, metering changes, sleep/wake, and P/S auto-exposure. Establish which values represent stable dial selection versus automatic iris drive. 0x1B is a capture-time command, not a reliable live dial event.
2. First candidate: direct mapping of stable aperture selections to focal length, monotonically across the zoom range; Arsat f/4.5 -> 80 mm and f/22 -> 200 mm, intermediate steps mapped to useful focal values after actual dial capture (e.g. rounded 5 mm; exact table TBD). Use observed discrete AV positions, not a linear f-number assumption. Preserve a stable default until a valid deliberate selection. Reject out-of-range/transient values; update only on stable validated selection, not from every frame. Use a single focal snapshot for status/capture, avoid mixed pairs and mid-capture changes. Do not change 0x08 rated range.
3. Direct mode consumes the body aperture setting: it cannot independently encode the physical iris. Retain truthful semantics in documentation; body FNumber may reflect the selector rather than the actual mechanical aperture. Test exposure/metering effects before adoption. Prefer M/A operation; if automatic modes cannot be identified reliably on the wire, do not claim firmware can distinguish them. Automatic or capture-induced changes must not silently reselect focal length in the tested supported workflow.
4. If direct mapping is unreliable or the aperture trade-off unacceptable, evaluate an explicit focal-selection/lock gesture via the aperture dial, then normal aperture reporting after lock. Gesture, entry/exit feedback, mode semantics and timeout require separate maintainer approval and tests; do not invent unsupported body UI or silently overload normal prime aperture behavior.
5. Persist only the selected profile mode/default; changing focal length during shooting must not continuously write Flash. Hold selected focal length over short sleep/wake; on a full reboot use the stored default until a valid selection. Keep sensor-driven modes outside Stage 6.

Gate 6C: repeatable selection at minimum/middle/maximum and intermediate positions; correct captured focal metadata; no accidental changes on shutter/auto exposure in supported operation; predictable wake/reset and no regression of 6B. If the gate fails, retain default mode as the supported implementation and document the limitation rather than ship unstable dynamic control.

## 6D — extended regression and release

| Test | Coverage |
|---|---|
| Python profile and upload tests | All 16 primes plus four zoom catalog entries; valid/invalid images, mode/default bounds, picker, detached confirmation, USB write/readback and interrupted/invalid transfer behavior |
| Native C tests | v1 prime compatibility, zoom validation, focal packet updates, CRC/checksums, malformed ranges, mode isolation and capture consistency |
| Builds | Debug and Production; verify generated artifacts and Flash region limits |
| Arsat hardware | Full range, default and aperture-focal modes, independent aperture in default mode, EXIF, MF, sleep/wake, off/on and profile replacement |
| Other lenses | Maintainer tests other physically available lenses; include prime switching (Helios/Hanimex/Jupiter-9 as available), fixed aperture and additional zoom profiles as available; distinguish catalog/offline checks from camera or optical checks |
| Maintenance and autonomy | USB-only profile programming with camera detached; readback, unplug USB before body mount; flash retention, repeated body-only wake/remount; production LEDs off |

Do not change other catalog lenses' optical specifications without evidence. Verify all four zoom descriptors offline (Arsat, CZJ Vario-Sonnar, Vivitar Series 1, Asahi SMC Takumar); do not claim physical validation of untested lenses. Update release asset packaging to include all supported zoom BINs (not prime-only filter). Record exact firmware version, commit, profile ID/default/mode, camera, file names and ExifTool values. Publish v0.6.0 only after agreed acceptance. Stage 7 is systematic testing of a6500 and additional bodies.

## Sources

- [E-mount 0x08 descriptor](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x08.md)
- [0x05 status](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x05.md)
- [0x28 capture](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x28.md), [0x35](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x35.md)
- [0x03 body aperture commands](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x03.md), [0x1B capture aperture](https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x1B.md)
- [Existing profile format](LENS_PROFILES.md), [Flash programming](PROFILE_FLASH.md).
