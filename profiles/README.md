# Lens profiles

The single editable source is [profiles.json](profiles.json), an array of lens records. Each supported entry generates `<profile_id>.bin`. The initial catalog contains 16 primes and four reserved zoom entries supplied by the maintainer. Specifications are catalog inputs, not individually hardware-verified; check your exact lens variant.

Generate all supported profiles from the repository root:

```sh
python3 tools/generate_lens_profile.py
```

Generate one or verify the entire library:

```sh
python3 tools/generate_lens_profile.py --profile-id hanimex-135-f2-8
python3 tools/generate_lens_profile.py --verify-only
```

Zoom entries are retained for future support (issue #5) and skipped explicitly in batch mode; selecting one directly fails. No zoom BIN is produced. IDs must be unique lowercase kebab-case. The shared `base_packets.json` is the generator's protocol template input, not a lens record. Generation does not modify firmware.

The current firmware uses the embedded Helios fallback. Independent Flash upload is not implemented yet. See [format and validation](../docs/LENS_PROFILES.md).
