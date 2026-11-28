# Stage 6 — manual zoom profiles

Status: 6A Arsat identification/capture metadata PASS on a6000; 6B host-side default-focal selection implemented on `feature/manual-zoom` (v0.5.1), hardware and regression acceptance pending. Baseline: v0.5.0, commit `4ea6d53286bc0990f38eb57011043ad0ad3f1e12`. Target milestone: v0.6.0 after acceptance, not a version bump at entry. Tracking: [#5](https://github.com/VitoGrab/Ukiyo-E-mount/issues/5). Sensor-based ring measurement is separate future research ([#6](https://github.com/VitoGrab/Ukiyo-E-mount/issues/6)). Cross-body compatibility follows Stage 6.

## Reference hardware and scope

Sony a6000, XIAO SAMD21, the proven camera-powered prototype, and the maintainer's Arsat M 80–200 mm f/4.5. The existing catalog declares 80–200 mm, constant maximum f/4.5, minimum f/22, default 80 mm. The maintainer's equipment inventory (photography/photo-inventory.json, 2026-11-08 export) confirms f/4.5–f/22, half-stop detents through f/11, then f/16 and f/22. No AF, OSS, power zoom, physical zoom control, additional hardware or changes to the power/wake mechanism are in scope. Aperture and focal length are reported metadata unless physically measured. Preserve all 16 existing prime profiles, independent USB programming and the embedded Helios fallback.

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

Goal: use the body aperture dial as a virtual focal-length selector without additional electronics. Keep `zoom_default` available and unaffected. Do not enable this mode for primes.

### Separate physical optics, selector and capture metadata

- Preserve physical lens specifications independently of the selector: Arsat 80–200 mm, constant maximum f/4.5, physical minimum f/22. Keep the existing JSON schema and real aperture range. Compute virtual selector bounds in the preparation/generation tools rather than replacing physical aperture values in the catalog.
- Explore a deliberately wider virtual aperture range than the physical lens offers, using the actual limits and step positions accepted by a6000. Start by testing candidate f/1.0–f/64 limits and investigate sub-f/1.0 (e.g. f/0.85, negative photographic AV) only after confirming the wire encoding and body support, then narrow to the reliably usable range if necessary; these are experimental bounds, not confirmed body support. Identify which 0x08 and 0x05 fields control the selectable range versus lens nameplate aperture metadata. Keep these consistent where required by the body, and verify whether physical nameplate fields can remain intact.
- Select practical focal increments (prefer 5 mm where the number of distinct stable selector positions permits, otherwise 10 mm or a documented coarser mapping). Both endpoints must be reachable. Do not invent additional positions or make the mapping depend on a linear f-number scale. For Arsat, 80–200 mm needs 25 positions at 5 mm or 13 at 10 mm. Avoid redundant adjacent selector values where possible; verify all displayed positions and both dial directions.
- Maintain a separate capture aperture reference, initially the physical maximum aperture at the selected focal length (f/4.5 throughout Arsat's range). This is a nominal reference, not a measurement of the mechanical iris. A future calibrated aperture input can replace it.

### Protocol investigation and implementation

1. Evidence first: add bounded diagnostic capture of validated 0x03 aperture pair (payload 3–4, 5–6 and selector bit in 7), and capture-time 0x1B. Current firmware already samples 0x03's second value. On a6000 in M/A, test displayed aperture steps across the virtual range, half-press, full press, metering changes, sleep/wake, and P/S auto-exposure. Establish which values represent stable dial selection versus automatic iris drive. 0x1B is a capture-time command, not a reliable live dial event.
2. Map stable, validated dial selections monotonically to focal length. Derive the table from actual discrete AV positions. Preserve the stored default until a valid deliberate selection; reject out-of-range/transient values. Maintain a single coherent focal snapshot for 0x05 and 0x28/0x35 and freeze the capture value across a shot. Keep the 0x08 rated focal range unchanged.
3. Investigate independent capture aperture reporting. Determine whether replying with the nominal physical aperture in 0x1B, and any required consistent 0x05 status during capture, results in the desired EXIF FNumber without moving/resetting the body's selector or corrupting metering or release sequencing. Do not repurpose the known 0x28/0x35 aperture-offset fields. The body may record the commanded exposure aperture regardless of the lens response; separate capture EXIF is a hypothesis, not a guaranteed capability. Trace requests and responses and inspect ARW/JPG metadata, including LensInfo/LensSpec, FNumber, MaxApertureValue, FocalLength and available MakerNotes. Test repeated shots without dial movement, aperture-focal changes, half-press/full press, exposure consistency and selector restoration. Do not claim that the reported maximum aperture is the actual shooting aperture when the physical ring is stopped down.
4. If independent capture aperture reporting is not achievable without adverse effects, test an explicit focal-selection/lock gesture via the aperture dial followed by normal aperture reporting, or retain a documented opt-in selector mode with its metadata limitation. Gesture, entry/exit feedback and mode semantics require separate maintainer approval. Never silently overload prime aperture behavior. Prefer M/A; if automatic modes cannot be identified reliably on the wire, do not claim firmware can distinguish them.
5. Persist only the selected profile mode/default; changing focal length during shooting must not continuously write Flash. Hold selected focal length over short sleep/wake; on full reboot use the stored default until a valid selection. Keep sensor-driven modes outside Stage 6.

Gate 6C: repeatable selection throughout the focal range with practical 5 or 10 mm increments where supported; correct captured focal metadata; no accidental changes on shutter/auto exposure in supported operation; predictable wake/reset and no regression of 6B. Independently verify whether nominal physical maximum aperture can be reported at capture without exposure, selector or release regressions. If that experiment fails, document the limitation and agree on fallback before shipping. If dynamic selection itself fails, retain default mode as the supported implementation.

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

## 6A implementation notes (v0.5.1)

The v1 559-byte layout already carries both BCD focal endpoints and both nameplate maximum apertures. Constant-aperture zooms now use these existing fields, with equal default focal values in status and capture templates. The four catalog zooms are generated and selectable; variable-aperture zooms are deliberately rejected at this stage. No runtime focal selection, USB default prompt or virtual aperture range is introduced. Prime binaries are unchanged. Arsat at 80 mm passed the identification and capture-metadata test below. Remaining hardware and host/build regression checks are not yet documented.

### 6A hardware evidence — Arsat, Sony a6000

Maintainer reported three successful captures using the v0.5.1 Stage 6A test workflow (source reference `794f2d0e435f7c3faae5a4b62c4e4a3b9463befa`, catalog profile `arsat-m-80-200-4-5`, default 80 mm; generated catalog payload CRC32 `858E9FCD`). The following values are transcribed from the supplied ExifTool output, not independently extracted from the original JPEG files.

| JPEG | FNumber | MaxApertureValue | FocalLength | MinFocalLength | MaxFocalLength | LensZoomPosition | FocusMode |
|---|---:|---:|---:|---:|---:|---:|---|
| DSC04869.JPG | 22.0 | 4.5 | 80 mm | 80 mm | 200 mm | 0% | Manual |
| DSC04870.JPG | 8.0 | 4.5 | 80 mm | 80 mm | 200 mm | 0% | Manual |
| DSC04871.JPG | 4.5 | 4.5 | 80 mm | 80 mm | 200 mm | 0% | Manual |

All three: LensModel and composite LensID = ARSAT M 80-200mm f4.5; LensInfo = 80-200mm f/4.5; LensSpec = E 80-200mm F4.5; SonyMaxApertureValue = 4.5; FocalLengthIn35mmFormat = 120 mm; LensFormat = Full-frame; LensMount = E-mount; no AF points used. Generic/unknown Sony lens type and LensMount2 are unchanged in this evidence and are not failures of the model/range reporting. Aperture and focal length are reported settings, not optical measurements. The output confirms successful still captures, correct zoom identification, default focal length and independent reported aperture at three values. Peaking/Magnifier, prime regression, explicit power/wake checks, complete host tests and Production build are not separately confirmed by these excerpts.

6B follow-up: check LensZoomPosition at intermediate/tele defaults (135/200 mm) before changing any unknown zoom-related fields. The observed 0% at 80 mm does not establish how the body derives the percentage.

### 6B implementation — default focal length at profile upload

The existing Stage 6A firmware already validates a stored focal length anywhere inside the zoom range and loads the complete v1 image at initialization. The interactive uploader now returns a selected catalog record and, for zooms only, prompts for an integer focal length in mm (Enter = catalog default, q = cancel). It builds the personalized image in memory with the existing generator before opening USB CDC. Catalog JSON, checked-in BINs and firmware remain unchanged. Prime selection has no additional prompt. The existing detached confirmation and byte-for-byte Flash readback are retained. `PROFILE_FOCAL_LENGTH_MM` reports the nominal focal length encoded in the verified image, including when reading `--info`. No new mode metadata is necessary for the fixed/default behavior and no firmware rebuild is required on a board already running Stage 6A v0.5.1. Test Arsat at 80, 135, 175 and 200 mm; record LensZoomPosition, LensInfo/LensSpec, FocalLength, FNumber, and power-cycle persistence. Aperture-focal control remains deferred to 6C.
