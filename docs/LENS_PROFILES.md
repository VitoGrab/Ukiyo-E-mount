# Lens profiles

The firmware and lens profile are independent data formats. Version 0.410 introduced the binary profile loader and generator. Version 0.411 reserves a Flash region and reads one profile from it; and adds guarded USB CDC programming. Hardware verification and firmware-update preservation remain pending.

## Generate a profile

Edit the single [profiles/profiles.json](../profiles/profiles.json) catalog, then run:

```sh
python3 tools/generate_lens_profile.py
python3 tools/generate_lens_profile.py --profile-id hanimex-135-f2-8
python3 tools/generate_lens_profile.py --verify-only
```

The default command generates all supported primes as `profiles/<profile_id>.bin`. Zoom records are preserved for issue #5 but never encoded by this version. A direct zoom selection fails. Optional `--catalog` and `--output-dir` permit a separate user library. Duplicate or unsafe IDs are rejected. The generator does not modify firmware. The shared `base_packets.json` contains v0.49-derived templates. `python3 tools/apply_lens_profile.py --verify-only` checks the Helios reference BIN, fallback header and firmware identity; without the flag it regenerates these reference artifacts only.


| Setting | Meaning |
|---|---|
| profile_id | Lowercase kebab-case identifier |
| lens_model | ASCII model string, 1–63 bytes |
| sony_lens_id | 16-bit lens ID (0 is the tested reference) |
| lens_type | prime supported; zoom reserved in catalog, rejected for generation (issue #5) |
| focal_length_min_mm, focal_length_max_mm, focal_length_default_mm | Equal integer focal lengths, 1–500 mm |
| focal_length_source | fixed for primes; zoom_range reserved |
| aperture_max_f | Maximum opening at the shortest focal length |
| aperture_max_tele_f | Maximum opening at the longest focal length; equal for primes |
| aperture_min_f | Narrowest opening |
| aperture_now_f | Fixed reported f-number, independent of mechanical aperture |

The existing aperture bounds (f/1–f/64, reported aperture no greater than f/9) are retained. There is no iris control or sensing.

## Binary format v1

All integers in the header are little-endian. The file is exactly 559 bytes.

| Offset | Size | Value |
|---:|---:|---|
| 0 | 4 | ASCII UKLP |
| 4 | 2 | Schema version 1 |
| 6 | 2 | Total length 559 |
| 8 | 4 | IEEE CRC32 of bytes 16–558 |
| 12 | 4 | Reserved, zero |
| 16 | 105 | 0x05 |
| 121 | 43 | 0x07 |
| 164 | 210 | 0x08 |
| 374 | 19 | 0x1B |
| 393 | 44 | 0x28 |
| 437 | 48 | 0x35 |
| 485 | 74 | 0x3F |

Each template includes framing, nominal length and a checksum. The sender continues to update runtime fields and checksums. The profile's 0x07 firmware and protocol identity bytes are zero placeholders; the engine inserts the identity generated from `config/protocol_identity.json` (protocol 1.08). Message 0x01 and its capability bitmap remain engine-owned. Native MF overrides remain engine-owned, as do session and transport behavior. Aperture-offset fields in 0x28/0x35 stay zero.

The loader checks header, CRC, exact frame order/length/class/ID/checksum, prime focal consistency, aperture consistency, selected native MF flags and null-padded ASCII name. CRC is not authentication; load only trusted profiles. Invalid staging leaves the previous profile unchanged; absent external data uses the built-in Helios fallback. Staging must occur while the bus is idle. The engine copies the selected template set at full initialization, before enabling the bus; short wake preserves the active set. Cold-start latency must be measured on hardware before camera-powered release.

## Regression

```sh
python3 tools/apply_lens_profile.py --verify-only
python3 -m unittest discover -s test -p 'test_*.py'
pio run -e seeed_xiao
```

The Python suite includes a native C loader test when `cc` is available. It covers the full prime catalog, two alternate synthetic lenses, CRC and semantic corruption, fallback and preservation of 0x01. On camera verify MF, focus aids, shooting, EXIF, sleep/wake and remount. The v0.49 runtime milestone passed these existing host and maintainer-reported a6000 hardware checks on 2026-11-27; the v1 image loader and independent profile switching were subsequently verified on a6000 (see below).

## Remaining work

The Flash region and USB CDC programming are implemented in v0.411; see [Flash layout and programming](PROFILE_FLASH.md). Verify both linker/map configurations, bootloader erase behavior, interrupted writes and cold-start timing; validate the new host backup/restore firmware-update workflow. Standard firmware flashing was confirmed to invalidate the stored profile. Programming/readback and profile switching have passed on the maintainer's XIAO/a6000. Zoom profiles remain unsupported (issue #5).

## Hardware evidence (2026-11-27)

Maintainer-reported JPEG EXIF results from a Sony a6000 after a fresh firmware upload, independent Hanimex profile upload and return to Helios. No firmware rebuild was required for the profile changes.

| Capture | Lens model | Focal length | Maximum aperture | EXIF FNumber | Focus mode |
|---|---|---:|---:|---:|---|
| DSC04860.JPG | HELIOS 44M-4 58mm f2 | 58 mm | f/2 | f/6.3 | Manual |
| DSC04861.JPG | HANIMEX AUTOMATIC 135mm f2.8 | 135 mm | f/2.8 | f/2.8 | Manual |
| DSC04862.JPG | HANIMEX AUTOMATIC 135mm f2.8 | 135 mm | f/2.8 | f/22 | Manual |
| DSC04863.JPG | HELIOS 44M-4 58mm f2 | 58 mm | f/2 | f/2.5 | Manual |

All four report the expected LensSpec, LensInfo, LensModel, Composite LensID, focal length and full-frame lens format. The Hanimex EXIF f/22 demonstrates that the body can record a selected aperture within the advertised range, not that the physical iris was measured or actuated. The first JPEG includes LensFirmwareVersion Ver.04.008 and protocol 1.08; later JPEGs omit those tags and report LensMount2 as Unknown. The reason is unresolved (see issue #3); this evidence does not establish the new firmware version from EXIF alone. No raw files were independently inspected.

**Result:** Independent profile upload and Helios -> Hanimex -> Helios metadata switching passed. Persistence through a firmware upgrade, timing and failure injection are separate tests.
