# Lens profiles

The firmware and lens profile are independent data formats. Version 0.410 introduced the binary profile loader and generator. Version 0.411 reserves a Flash region, reads one profile from it, and adds guarded USB CDC programming and native firmware-update retention. The maintainer has confirmed the implemented prime-profile workflow on XIAO/a6000, including automatic reset and retention during changed firmware upload.

## Generate a profile

Edit the single [profiles/profiles.json](../profiles/profiles.json) catalog, then run:

```sh
python3 tools/generate_lens_profile.py
python3 tools/generate_lens_profile.py --profile-id hanimex-135-f2-8
python3 tools/generate_lens_profile.py --verify-only
```

The default command generates all supported prime and constant-aperture zoom profiles as `profiles/<profile_id>.bin`. Stage 6A includes all four catalog zooms. Optional `--catalog` and `--output-dir` permit a separate user library. Duplicate or unsafe IDs are rejected. The generator does not modify firmware. The shared `base_packets.json` contains v0.49-derived templates. `python3 tools/prepare_lens_profiles.py --verify-only` checks all supported BINs, the fallback header and firmware identity; without the flag it prepares all of them, writing only changed files. The interactive profile uploader prepares the catalog before showing its live list.


| Setting | Meaning |
|---|---|
| profile_id | Lowercase kebab-case identifier |
| lens_model | ASCII model string, 1–63 bytes |
| sony_lens_id | 16-bit lens ID (0 is the tested reference) |
| lens_type | prime and constant-aperture zoom supported; zoom hardware validation pending (issue #5) |
| focal_length_min_mm, focal_length_max_mm, focal_length_default_mm | Equal integer focal lengths, 1–500 mm |
| focal_length_source | fixed for primes; zoom_range for zooms with catalog default |
| aperture_max_f | Maximum opening at the shortest focal length |
| aperture_max_tele_f | Maximum opening at the longest focal length; equal for primes |
| aperture_min_f | Narrowest opening |
| aperture_now_f | Fixed reported f-number, independent of mechanical aperture |

The physical catalog aperture bounds (f/1–f/64, reported aperture no greater than f/9) are retained. The Stage 6C Arsat aperture-zoom range probe advertises raw 0x0F00–0x1E00 (f/0.707–f/128) with initial raw 0x0F00; the v1 firmware validator permits this exact extended combination for zooms only. The previous probe remains loadable. This exception is not a persistent runtime mode identifier. There is no iris control or sensing.

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
python3 tools/prepare_lens_profiles.py --verify-only
python3 -m unittest discover -s test -p 'test_*.py'
pio run -e seeed_xiao
```

The Python suite includes a native C loader test when `cc` is available. It covers the full prime catalog, two alternate synthetic lenses, CRC and semantic corruption, fallback and preservation of 0x01. On camera verify MF, focus aids, shooting, EXIF, sleep/wake and remount. The v0.49 runtime milestone passed these existing host and maintainer-reported a6000 hardware checks on 2026-11-27; the v1 image loader and independent profile switching were subsequently verified on a6000 (see below).

## Remaining work

The Flash region and USB CDC programming are implemented in v0.411; see [Flash layout and programming](PROFILE_FLASH.md). Verify both linker/map configurations, bootloader erase behavior, interrupted writes and cold-start timing; regress native row-local firmware uploads in both environments. The original full-erase upload invalidated the stored profile; BOSSA 1.9 with explicit application offset and row-local erase successfully programmed a changed firmware image while retaining the Hanimex profile. Programming/readback and profile switching have passed on the maintainer's XIAO/a6000. Stage 6A zoom support is experimental and hardware acceptance is pending (issue #5).

## Hardware evidence (2026-11-27)

Maintainer-reported JPEG EXIF results from a Sony a6000 after a fresh firmware upload, independent Hanimex profile upload and return to Helios. No firmware rebuild was required for the profile changes.

| Capture | Lens model | Focal length | Maximum aperture | EXIF FNumber | Focus mode |
|---|---|---:|---:|---:|---|
| DSC04860.JPG | HELIOS 44M-4 58mm f2 | 58 mm | f/2 | f/6.3 | Manual |
| DSC04861.JPG | HANIMEX AUTOMATIC 135mm f2.8 | 135 mm | f/2.8 | f/2.8 | Manual |
| DSC04862.JPG | HANIMEX AUTOMATIC 135mm f2.8 | 135 mm | f/2.8 | f/22 | Manual |
| DSC04863.JPG | HELIOS 44M-4 58mm f2 | 58 mm | f/2 | f/2.5 | Manual |

All four report the expected LensSpec, LensInfo, LensModel, Composite LensID, focal length and full-frame lens format. The Hanimex EXIF f/22 demonstrates that the body can record a selected aperture within the advertised range, not that the physical iris was measured or actuated. The first JPEG includes LensFirmwareVersion Ver.04.008 and protocol 1.08; later JPEGs omit those tags and report LensMount2 as Unknown. The reason is unresolved (see issue #3); this evidence does not establish the new firmware version from EXIF alone. No raw files were independently inspected.

**Result:** Independent profile upload and Helios -> Hanimex -> Helios metadata switching passed. The maintainer subsequently confirmed the backup/restore update wrapper at `426802f`, then confirmed native retention at `741c387` (`PROFILE_RESTORE=ALREADY_PRESENT`, `PROFILE_FLASH_RETAINED=PASS`). The temporary backup/restore workflow has therefore been removed from ordinary firmware uploads. A subsequent changed-image upload failed on BOSSA 1.7, then passed with the BOSSA 1.9 correction at `862451d`; Hanimex remained valid. The Pentacon 200 mm f/4 profile was programmed, independently read back after automatic reset, and confirmed on camera. The Rubinar 500 mm f/5.6 profile correctly prevented aperture changes on camera. Timing, failure injection, production-environment and other camera regression remain separate tests.

## Stage 6A extension (v0.5.1)

The existing 559-byte v1 packet layout now supports constant-aperture zoom profiles: nonzero ordered tele BCD endpoint in 0x08 and matching endpoint maximum-aperture BCD values. The focal pair in 0x05/0x28/0x35 remains equal to the stored default within the range. No new JSON fields, header fields, Flash layout or USB transfer format. Variable-aperture zooms are deferred. Older v0.5.0 firmware rejects these zoom images. See [Stage 6 plan](STAGE6_MANUAL_ZOOM.md).
