# Lens profiles

Only manual prime lenses are currently supported. Each lens has one editable JSON source and one ready-to-program BIN. The initial Helios 44M-4 is the validated v0.49 reference lens; its new binary format still requires hardware regression.

Copy `helios-44m-4.json` to a new filename, edit the lens parameters, then run from the repository root:

```sh
python3 tools/generate_lens_profile.py profiles/my-lens.json
```

This produces `profiles/my-lens.bin` without changing firmware. `base_packets.json` is a shared generator input derived from the validated protocol templates, not an individual lens profile. No expert mode or per-lens packet directories are needed.

Independent upload is not implemented yet. See [format and validation](../docs/LENS_PROFILES.md).
