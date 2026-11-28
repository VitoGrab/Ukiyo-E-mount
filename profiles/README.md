# Lens profiles

The single editable source is [profiles.json](profiles.json), an array of lens records. Each supported entry generates `<profile_id>.bin`. The initial catalog contains 16 primes and four constant-aperture zoom entries supplied by the maintainer. Specifications are catalog inputs, not individually hardware-verified; check your exact lens variant.

Prepare all supported profiles, embedded fallback and firmware identity from the repository root (also performed by the Build & Upload and interactive Upload Lens Profile tasks):

```sh
python3 tools/prepare_lens_profiles.py
```

Generate one or verify the entire library:

```sh
python3 tools/generate_lens_profile.py --profile-id hanimex-135-f2-8
python3 tools/generate_lens_profile.py --verify-only
```

Stage 6A generates the four constant-aperture zoom BINs and makes them selectable in the uploader. Catalog BINs retain the catalog default. Stage 6B permits an individual integer focal length during interactive upload without editing the catalog or rebuilding firmware. Dynamic in-camera selection is deferred (issue #5). Variable-aperture zooms are not supported in this stage. IDs must be unique lowercase kebab-case. The shared `base_packets.json` is the generator's protocol template input, not a lens record. Generation does not modify firmware.

The firmware uses the embedded Helios fallback if no valid image is found in the reserved Flash region. Independent USB CDC programming, Helios/Hanimex switching, Pentacon programming with automatic reset, and Rubinar fixed-aperture behavior were validated by the maintainer on a6000; see [Flash programming](../docs/PROFILE_FLASH.md). See [format and validation](../docs/LENS_PROFILES.md).

## Optical specification review (2026-11-27)

The initial 20 records were cross-checked against catalogues and published lens data. The current prime values correspond to documented variants; no optical values were silently changed. Verify the aperture ring of the physical lens before programming.

| Profile | Reference / review notes |
|---|---|
| helios-44m-4, helios-44-2 | [Zenith lens guide](https://www.cameramanuals.org/russian_pdf/zenith_guide-2.pdf); 58 mm f/2–16. |
| hanimex-135-f2-8 | Variant-dependent: [Automatic f/16](https://m42lens.com/m42-lens-database/425-hanimex-automatic-135mm-f-2-8-16) and [Automatic MC Macro f/22](https://m42lens.com/m42-lens-database/429-hanimex-automatic-mc-macro-135mm-f-2-8-22). Keep maintainer's f/22 entry until the physical lens is checked. |
| jupiter-9-85-2 | [M42 Jupiter-9](https://mattsclassiccameras.com/lenses/jupiter-9-85mm/); f/2–16. |
| asahi-super-takumar-50-1-4, asahi-super-takumar-200-4 | [Pentax SL manual](https://cameramanuals.org/pentax_pdf/pentax_sl.pdf); f/1.4–16 and f/4–22. |
| carl-zeiss-jena-pancolar-50-1-8 | [M42 Auto MC variant](https://m42lens.com/m42-lens-database/627-carl-zeiss-jena-ddr-pancolar-auto-mc-50mm-f-1-8-22). |
| industar-50-2 | [Zenith guide](https://www.cameramanuals.org/russian_pdf/zenith_guide-2.pdf); f/3.5–16. |
| carl-zeiss-jena-sonnar-135-3-5 | [M42 zebra](https://m42lens.com/m42-lens-database/1523-carl-zeiss-jena-sonnar-135mm-f-3-5-22-zebra); f/3.5–22. |
| tair-11a-135-2-8 | [JAPB data sheet](https://japb.net/gear/gear-review-index/ds_tair-11a/); f/2.8–22. |
| carl-zeiss-jena-flektogon-35-2-4 | [Lens review](https://phillipreeve.net/blog/review-carl-zeiss-jena-35mm-2-4-flektogon/); f/2.4–22. |
| mir-1b-37-2-8 | [M42 Lens Database](https://m42lens.com/m42-lens-database/103-mir1b-37mm-f2816); f/2.8–16. |
| tair-3s-300-4-5 | [Fotosniper variant](https://www.pentaxforums.com/userreviews/tair-3s-300mm-f4-5-fotosnaiper-12-ensemble.html); f/4.5–22. |
| carl-zeiss-jena-sonnar-180-2-8 | [M42 f/22 variant](https://m42lens.com/m42-lens-database/507-carl-zeiss-jena-sonnar-180mm-f-2-8-22); [Pentacon 6 f/32 variant](https://japb.net/gear/gear-review-index/ds_carlzeissjena-sonnar-180f28/). |
| pentacon-200-4 | [Auto/electric](https://japb.net/gear/gear-review-index/ds_pentacon-200f4/); f/4–22. |
| rubinar-500-5-6 | [Fixed-aperture mirror lens](https://ayton.id.au/wiki/doku.php?id=photo%3Arubinar500mmf5.6); f/5.6 throughout. |
| carl-zeiss-vario-sonnar-80-200-4 | The intended M42 lens is the Carl Zeiss Jena variant, not the Contax/Yashica T* lens. [Related Vario-Prakticar](https://japb.net/gear/gear-review-index/ds_carlzeissjena-vario-prakticar-80-200/) stops down to f/32; exact M42 endpoint is not independently confirmed. Preserve current f/22 until verified. |
| vivitar-series-1-70-210-3-5 | [Kiron variant](https://japb.net/gear/gear-review-index/ds_vivitar_70-210f35/); constant f/3.5; [f/22 endpoint](https://thenoisyshutter.com/2023/03/17/legacy-lens-review-vivitar-series-1-70-210-f3-5-macro-zoom/). |
| asahi-smc-takumar-85-210-4-5 | [Pentax ES manual](https://www.manualowl.com/m/Pentax/ES/Manual/184049?page=41); f/4.5–22. |
| arsat-m-80-200-4-5 | [M42 Arsat review](https://radojuva.com/en/2011/01/obzor-arsat-80-200-granit-11/) and [related Arsat H specifications](https://ussrphoto.com/modern/Wiki/default.asp?ContentID=1294&ParentID=2&WikiCatID=28); constant f/4.5, f/22 endpoint. |

The review confirms nominal lens specifications, not the accuracy of Sony E-mount reporting. Zoom BINs are experimental until camera validation.

Stage 6C adds one experimental Arsat aperture-zoom record (`arsat-m-80-200-4-5-aperture`). The physical optical record is identical to the default zoom; the generator now advertises an experimental f/0.707–f/128 aperture range only in the aperture-zoom BIN (previous f/1–f/64 range was tested on a6000). The current focal length remains 80 mm until dynamic selection is implemented and validated. The v1 BIN does not carry an independent runtime mode flag. Existing IDs and non-experimental BINs are unchanged; the interactive picker labels default and aperture zoom separately.

### Approved selector maps

The 23-entry catalog now contains 16 primes, four default zooms ([DZ], `default_zoom`), and three aperture-zoom profiles ([AZ], `aperture_zoom`), of which Sigma 21–35 and Takumar 135–600 are marked [TEST]. The previous experimental Arsat BIN is replaced with the constrained 80–200 mm standard map subrange. See [the approved maps and profile rules](../docs/FOCAL_LENGTH_MAPS.md). The current zoom remains fixed at the catalog default in AZ; test labels are not claims of hardware acceptance. The physical values in the catalog are unchanged for the existing lenses. DZ profile IDs are unchanged.
