#!/usr/bin/env python3
"""Prepare the complete supported lens library, embedded fallback and firmware identity."""
from __future__ import annotations
import argparse
import json
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'profiles' / 'profiles.json'
DEFAULT_PROFILE_ID = 'helios-44m-4'
DEFAULT_BIN = CONFIG.parent / (DEFAULT_PROFILE_ID + '.bin')
IDENTITY = ROOT / 'config' / 'protocol_identity.json'
VERSION_HEADER = ROOT / 'include' / 'ukiyo-e-mount' / 'version.h'

class ProfileError(ValueError):
    pass


def read_json(path: pathlib.Path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ProfileError(f'Cannot read profile {path}: {exc}') from exc


def validate_profile(p: dict, *, allow_zoom: bool = True) -> dict:
    keys = {'profile_id', 'lens_model', 'sony_lens_id', 'lens_type', 'focal_length_min_mm',
            'focal_length_max_mm', 'focal_length_default_mm', 'focal_length_source',
            'aperture_max_f', 'aperture_max_tele_f', 'aperture_min_f', 'aperture_now_f'}
    if not isinstance(p, dict) or set(p) != keys:
        raise ProfileError(f'Expected exactly these keys: {sorted(keys)}')
    if not isinstance(p['profile_id'], str) or not re.fullmatch(r'[a-z0-9][a-z0-9-]*', p['profile_id']):
        raise ProfileError('profile_id must be lowercase kebab-case')
    try:
        name = p['lens_model'].encode('ascii')
    except (AttributeError, UnicodeError) as exc:
        raise ProfileError('lens_model must be ASCII') from exc
    if not name or len(name) > 63 or b'\x00' in name:
        raise ProfileError('lens_model must be 1–63 non-NUL ASCII bytes')
    lens_id = p['sony_lens_id']
    if type(lens_id) is not int or not 0 <= lens_id <= 65535:
        raise ProfileError('sony_lens_id must be 0..65535 integer')
    kind = p['lens_type']
    if kind != 'prime' and not (allow_zoom and kind == 'zoom'):
        raise ProfileError('Unsupported lens type (or zoom disabled)')
    if kind == 'prime' and p['focal_length_source'] != 'fixed':
        raise ProfileError('Prime lenses require focal_length_source=fixed')
    if kind == 'zoom' and p['focal_length_source'] != 'zoom_range':
        raise ProfileError('Catalog zoom entries require focal_length_source=zoom_range')
    focal_keys = ('focal_length_min_mm', 'focal_length_max_mm', 'focal_length_default_mm')
    for key in focal_keys:
        mm = p[key]
        if type(mm) is not int or not 1 <= mm <= 500:
            raise ProfileError(f'{key} must be an integer from 1 to 500')
    if kind == 'prime' and len({p[key] for key in focal_keys}) != 1:
        raise ProfileError('Prime lenses require identical minimum, maximum and default focal lengths')
    if kind == 'zoom' and not (p['focal_length_min_mm'] < p['focal_length_max_mm'] and
                              p['focal_length_min_mm'] <= p['focal_length_default_mm'] <= p['focal_length_max_mm']):
        raise ProfileError('Zoom requires a nonempty range containing the default focal length')
    if any(type(p[k]) not in (int, float) for k in ('aperture_max_f', 'aperture_max_tele_f', 'aperture_min_f', 'aperture_now_f')):
        raise ProfileError('aperture values must be numbers')
    try:
        wide = float(p['aperture_max_f'])
        tele = float(p['aperture_max_tele_f'])
        narrow = float(p['aperture_min_f'])
        reported = float(p['aperture_now_f'])
    except (TypeError, ValueError) as exc:
        raise ProfileError('aperture values must be numbers') from exc
    if not all(map(math.isfinite, (wide, tele, narrow, reported))):
        raise ProfileError('aperture values must be finite')
    if kind == 'prime' and wide != tele:
        raise ProfileError('Prime lenses require identical wide and tele maximum apertures')
    if not (1.0 <= wide <= reported <= narrow <= 64.0):
        raise ProfileError('Require 1 <= max aperture opening <= now <= min aperture opening <= 64')
    if not 1.0 <= tele <= narrow:
        raise ProfileError('Tele maximum aperture outside supported range')
    if kind == 'zoom' and wide != tele:
        raise ProfileError('Stage 6A supports constant-aperture zooms only')
    if reported > 9.0:
        raise ProfileError('EM_F_BCD nameplate table supports up to f/9 for NOW; adapt table before increasing')
    return p

    
def load_catalog(path: pathlib.Path = CONFIG) -> dict:
    data = read_json(path)
    if not isinstance(data, list) or not data:
        raise ProfileError('Profile catalog must be a nonempty JSON array')
    profiles = {}
    for p in data:
        p = validate_profile(p, allow_zoom=True)
        key = p['profile_id']
        if key in profiles:
            raise ProfileError(f'Duplicate profile_id: {key}')
        profiles[key] = p
    return profiles


def select_profile(profiles: dict, profile_id: str) -> dict:
    if profile_id not in profiles:
        raise ProfileError(f'Unknown profile_id: {profile_id}')
    return validate_profile(profiles[profile_id])


def load_profile(path: pathlib.Path = CONFIG, profile_id: str = DEFAULT_PROFILE_ID) -> dict:
    return select_profile(load_catalog(path), profile_id)


def parse_project_version(value: str) -> tuple[int, int]:
    """Map project 0.MINOR.PATCH to Sony's two firmware-version bytes.

    Examples: 0.4.11 -> (4, 11); 0.5.0 -> (5, 0).
    Project major is reserved at zero; minor and patch each fit one byte.
    ExifTool renders these bytes as Ver.%.2x.%.3d.
    """
    match = re.fullmatch(r'0\.([1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})', value) if isinstance(value, str) else None
    if not match:
        raise ProfileError('project_version must use 0.MINOR.PATCH notation (e.g. 0.5.0)')
    major, minor = map(int, match.groups())
    if major > 255 or minor > 255:
        raise ProfileError('Sony firmware version components must fit one byte (0..255)')
    return major, minor


def load_identity(path: pathlib.Path) -> dict:
    """Validate the single project version and E-mount protocol identity."""
    try:
        p = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ProfileError(f'Cannot read protocol identity {path}: {exc}') from exc
    expected = {'project_version', 'emount_protocol_version'}
    if not isinstance(p, dict) or set(p) != expected or not all(isinstance(p[key], str) for key in expected):
        raise ProfileError(f'Protocol identity requires exactly these string keys: {sorted(expected)}')
    firmware_major, firmware_minor = parse_project_version(p['project_version'])
    protocol = re.fullmatch(r'([0-9]{1,2})\.([0-9]{2})', p['emount_protocol_version'])
    if not protocol:
        raise ProfileError('emount_protocol_version must be MAJOR.TWO_DIGIT_MINOR')
    # Sony E-mount protocol grades such as 1.08, 1.20, 1.31 are
    # represented as 0x0108, 0x0120, 0x0131, respectively.
    major = int(protocol.group(1))
    if major > 0xFF:
        raise ProfileError('emount_protocol_version major exceeds 8-bit field')
    minor_digits = protocol.group(2)
    p['lens_firmware_major_byte'] = firmware_major
    p['lens_firmware_minor_byte'] = firmware_minor
    p['emount_protocol_u16'] = (
        (major << 8) | (int(minor_digits[0]) << 4) | int(minor_digits[1])
    )
    return p


def render_version_header(identity: dict) -> str:
    """Render the compile-time version label used by all monitor output."""
    version = identity['project_version']
    return (
        '#pragma once\n'
        '\n'
        '// Generated by tools/prepare_lens_profiles.py from config/protocol_identity.json.\n'
        f'#define UKIYO_PROJECT_VERSION "{version}"\n'
        f'#define UKIYO_PROJECT_VERSION_LABEL "v{version}"\n'
        f'#define UKIYO_FIRMWARE_MAJOR {identity["lens_firmware_major_byte"]}u\n'
        f'#define UKIYO_FIRMWARE_MINOR {identity["lens_firmware_minor_byte"]}u\n'
        f'#define UKIYO_PROTOCOL_U16 0x{identity["emount_protocol_u16"]:04X}u\n'
    )


def prepare_profiles(*, verify_only=False):
    """Validate and encode the entire catalog before touching generated files.

    Return the supported catalog entries in their source order for the picker.
    Only changed files are written; never delete user-created BINs.
    """
    from generate_lens_profile import build_profile, fallback_header
    catalog = load_catalog()
    selected = list(catalog.values())
    expected = {CONFIG.parent / (p['profile_id'] + '.bin'): build_profile(p)
                for p in selected}
    if DEFAULT_BIN not in expected:
        raise ProfileError('Missing supported embedded fallback profile')
    expected[VERSION_HEADER] = render_version_header(load_identity(IDENTITY)).encode()
    expected[ROOT / 'include/emount/default_lens_profile.h'] = fallback_header(expected[DEFAULT_BIN]).encode()
    changed = 0
    for path, data in expected.items():
        if path.exists() and path.read_bytes() == data:
            continue
        if verify_only:
            raise ProfileError(f'Stale generated file: {path}')
        path.write_bytes(data)
        changed += 1
    print(f"PROFILES_{'VERIFIED' if verify_only else 'PREPARED'}={len(selected)} FILES_CHANGED={changed}", flush=True)
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true', help='Check without writing')
    args = parser.parse_args()
    try:
        prepare_profiles(verify_only=args.verify_only)
        print('SOURCE_CHECK=PASS')
        return 0
    except (OSError, ValueError) as exc:
        print(f'LENS_PROFILE_ERROR={exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
