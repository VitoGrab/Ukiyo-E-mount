# Lens profiles

**Authoritative profile:** `config/lens_profile.json`

The selected lens profile is a separate configuration file. Do not edit Techart's imported packet byte arrays or `emount.h` by hand. Apply the profile after `tools/import_techart.py` and after every edit to the JSON:

```bash
python3 tools/apply_lens_profile.py --verify-only
python3 tools/apply_lens_profile.py
/opt/platformio/bin/pio run
```

The application changes only the vendored `emount.h` lens constants and the template bodies of `em_msg_07` (Sony Lens ID) and `em_msg_3F` (ASCII lens model). It does **not** modify `emount.c`, the GPIO/UART HAL, lens capabilities or session lifecycle. Template structure is checked before writing. The script can be run repeatedly without re-importing Techart.

| Setting | Effect |
|---|---|
| `profile_id` | Human-readable configuration identifier, not sent to the camera |
| `lens_model` | ASCII name sent in message `0x3F`; up to 63 bytes |
| `sony_lens_id` | 16-bit ID in message `0x07`, payload bytes 9–10 |
| `focal_length_mm` | Reported focal length, used in Techart packet templates |
| `aperture_max_f` | Widest aperture (f/2); transmitted as `EM_APERTURE_MAX` |
| `aperture_min_f` | Narrowest aperture (f/16); transmitted as `EM_APERTURE_MIN` |
| `aperture_now_f` | Present reported f-number, transmitted as `EM_APERTURE_NOW`; stays f/2 until an aperture sensor is added |

## Helios 44M-4

- Model: `HELIOS 44M-4 58mm f2`
- Focal length: 58 mm
- Advertised aperture range: f/2–f/16; current reported aperture: f/2
- No electronic iris control or aperture-position sensing
- The camera receives the full range, but `NOW` remains f/2 regardless of the mechanical aperture ring until sensing is implemented. The camera **may** offer aperture changes because a range is advertised; the adapter will not actuate the iris. EXIF will continue to report f/2 unless the camera overrides it. Verify this behavior on A6000 before relying on exposure automation.
- Sony/Techart protocol encodes `MAX=f/2` as `0x1200`, `MIN=f/16` as `0x1800`, and `NOW=f/2` as `0x1200`.
- `sony_lens_id=0` is an experiment to avoid the known `50531` collision with Sigma. ExifTool documents `0` as an unknown E-mount lens/other lens, but whether the A6000 writes that value and ExifTool's Composite LensID prefers LensModel must be tested on a fresh ARW. Camera handling of ID classes can vary.

## Validation on camera

Preserve an ARW from the working v0.4.0 / ID 50531 as a reference. After applying a profile and rebuilding, compare:

```bash
exiftool -G1 -a -s -n -LensType2 -LensMount -LensModel -LensInfo \
  -FocalLength -FNumber DSC_NEW.ARW
exiftool -G1 -a -s -LensID -LensModel -LensType2 DSC_NEW.ARW
```

Expected **to test**, not yet confirmed: `LensModel=HELIOS 44M-4 58mm f2`, `LensType2=0`, `FocalLength=58 mm`, `FNumber=2`. Also test whether the camera exposes or attempts control across f/2–f/16 even though no motor is present. Do not treat `Composite:LensID` as a literal field transmitted over the Sony E-mount protocol.

## Known remaining work

- `TECHART_PORT` monitor's hardcoded `OPTICS=58MM_F2_FIXED_REPORTED` happens to match this profile, but is not yet dynamically driven by the profile. This should be fixed when implementing session lifecycle v0.4.1.
- The Techart AF capability declarations are unchanged; the fixed aperture response already prevents A6000 aperture adjustment. True manual-only AF declaration is a separate protocol change to test.
- `tools/import_techart.py` resets vendored sources. After any re-import, run `tools/apply_lens_profile.py` again.
