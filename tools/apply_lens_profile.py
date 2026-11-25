#!/usr/bin/env python3
"""Apply config/lens_profile.json to the vendored Techart protocol templates.

Run after tools/import_techart.py and whenever the selected lens changes.
The upstream parser, GPIO/UART HAL and capability bitmap are untouched.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'config' / 'lens_profile.json'
IDENTITY = ROOT / 'config' / 'protocol_identity.json'
VENDOR = ROOT / 'lib' / 'ukiyo_techart' / 'src'


class ProfileError(ValueError):
    pass


def load_profile(path: pathlib.Path) -> dict:
    try:
        p = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ProfileError(f'Cannot read profile {path}: {exc}') from exc
    keys = {'profile_id', 'lens_model', 'sony_lens_id', 'focal_length_mm',
            'aperture_max_f', 'aperture_min_f', 'aperture_now_f'}
    if set(p) != keys:
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
    mm = p['focal_length_mm']
    if type(mm) is not int or not 1 <= mm <= 500:
        raise ProfileError('focal_length_mm must be an integer from 1 to 500')
    try:
        wide = float(p['aperture_max_f'])
        narrow = float(p['aperture_min_f'])
        reported = float(p['aperture_now_f'])
    except (TypeError, ValueError) as exc:
        raise ProfileError('aperture values must be numbers') from exc
    if not all(map(math.isfinite, (wide, narrow, reported))):
        raise ProfileError('aperture values must be finite')
    if not (1.0 <= wide <= reported <= narrow <= 64.0):
        raise ProfileError('Require 1 <= max aperture opening <= now <= min aperture opening <= 64')
    if reported > 9.0:
        raise ProfileError('Techart EM_F_BCD nameplate table supports up to f/9 for NOW; adapt table before increasing')
    return p


def load_identity(path: pathlib.Path) -> dict:
    """Validate the project-wide firmware and E-mount protocol identity.

    Camera-menu firmware is a single packed BCD byte (major.minor);
    the E-mount protocol revision is a 16-bit little-endian version
    word (major + two decimal minor digits). Neither carries the full
    semantic project version.
    """
    try:
        p = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ProfileError(f'Cannot read protocol identity {path}: {exc}') from exc
    expected = {'project_version', 'lens_firmware_version', 'emount_protocol_version'}
    if set(p) != expected or not all(isinstance(p[key], str) for key in expected):
        raise ProfileError(f'Protocol identity requires exactly these string keys: {sorted(expected)}')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', p['project_version']):
        raise ProfileError('project_version must be MAJOR.MINOR.PATCH')
    firmware = re.fullmatch(r'([0-9])\.([0-9])', p['lens_firmware_version'])
    if not firmware:
        raise ProfileError('lens_firmware_version must be one-digit MAJOR.MINOR (packed BCD)')
    protocol = re.fullmatch(r'([0-9]{1,2})\.([0-9]{2})', p['emount_protocol_version'])
    if not protocol:
        raise ProfileError('emount_protocol_version must be MAJOR.TWO_DIGIT_MINOR')
    # Sony E-mount protocol grades such as 1.08, 1.20, 1.31 are
    # represented as 0x0108, 0x0120, 0x0131, respectively.
    major = int(protocol.group(1))
    if major > 0xFF:
        raise ProfileError('emount_protocol_version major exceeds 8-bit field')
    minor_digits = protocol.group(2)
    p['lens_firmware_bcd'] = (int(firmware.group(1)) << 4) | int(firmware.group(2))
    p['emount_protocol_u16'] = (
        (major << 8) | (int(minor_digits[0]) << 4) | int(minor_digits[1])
    )
    return p


def aperture_to_av(value: float) -> int:
    # Techart emount.h: 4096 + 256 * AV, AV = 2 log2(f-number).
    # Rounded to nearest integer representable by the wire protocol.
    return round(4096 + 512 * math.log2(value))


def set_macro(header: str, name: str, value: int, explanation: str) -> str:
    pattern = re.compile(rf'(?m)^(#define\s+{re.escape(name)}\s+)\S+[^\n]*$')
    replaced, n = pattern.subn(lambda m: f'{m.group(1)}0x{value:04X}u /* {explanation} */'
                                if name.startswith('EM_APERTURE_')
                                else f'{m.group(1)}{value}u /* {explanation} */', header)
    if n != 1:
        raise ProfileError(f'Expected one macro {name}, found {n}')
    return replaced


def replace_array(text: str, symbol: str, modify) -> str:
    pat = re.compile(r'(uint8_t\s+' + re.escape(symbol) +
                     r'\s*\[\s*EA9_LEN_[0-9A-Fa-f]+\s*\]\s*=\s*\{)(.*?)(\};)', re.S)
    matches = list(pat.finditer(text))
    if len(matches) != 1:
        raise ProfileError(f'Expected one array {symbol}, found {len(matches)}')
    match = matches[0]
    # Strip comments so line-offset annotations aren't parsed as payload bytes.
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', match.group(2), flags=re.S)
    entries = [entry.strip() for entry in body.split(',') if entry.strip()]
    if not all(re.fullmatch(r'0[xX][0-9A-Fa-f]{1,2}', x) for x in entries):
        raise ProfileError(f'{symbol}: unexpected nonliteral byte in upstream template')
    data = [int(x, 16) for x in entries]
    modify(data)
    # The checksum is recomputed by the Techart sender, but generate a correct
    # stored checksum as well; see E-mount-protocol-RE/docs/frame_format.md.
    checksum = sum(data[1:-3]) & 0xffff
    data[-3], data[-2] = checksum & 0xff, checksum >> 8
    rows = ['    ' + ', '.join(f'0x{x:02x}' for x in data[i:i+12]) + ','
            for i in range(0, len(data), 12)]
    note = '\n    /* Generated from config/lens_profile.json by apply_lens_profile.py */\n'
    return text[:match.start()] + match.group(1) + note + '\n'.join(rows) + '\n' + match.group(3) + text[match.end():]


def apply_to_sources(header: str, packets: str, profile: dict, identity: dict) -> tuple[str, str]:
    name = profile['lens_model'].encode('ascii')
    lens_id = profile['sony_lens_id']
    focal_mm10 = profile['focal_length_mm'] * 10
    aperture_values = {
        'MAX': (profile['aperture_max_f'], 'widest aperture; f-number minimum'),
        'MIN': (profile['aperture_min_f'], 'narrowest aperture; f-number maximum'),
        'NOW': (profile['aperture_now_f'], 'reported current aperture'),
    }

    header = set_macro(header, 'EM_FOCAL_MM10', focal_mm10, 'Ukiyo lens profile')
    for key, (f_number, explanation) in aperture_values.items():
        header = set_macro(header, 'EM_APERTURE_' + key,
                           aperture_to_av(f_number), explanation)

    def patch_07(data: list[int]):
        if len(data) != 43 or data[:6] != [0xf0, 0x2b, 0, 2, 0, 7] or data[-1] != 0x55:
            raise ProfileError('em_msg_07 structure differs from verified Techart template')
        # Message 0x07, absolute frame offsets 8/9 and 12.
        protocol = identity['emount_protocol_u16']
        data[8], data[9] = protocol & 0xff, protocol >> 8  # pl[2..3], u16 LE
        data[12] = identity['lens_firmware_bcd']  # pl[6], packed BCD
        data[15], data[16] = lens_id & 0xff, lens_id >> 8  # pl[9..10]

    def patch_3f(data: list[int]):
        if len(data) != 74 or data[:6] != [0xf0, 0x4a, 0, 2, 0, 0x3f] or data[-1] != 0x55 or data[6] != 0:
            raise ProfileError('em_msg_3F structure differs from verified Techart template')
        data[7:71] = list(name.ljust(64, b'\x00'))  # payload[1..64]

    packets = replace_array(packets, 'em_msg_07', patch_07)
    packets = replace_array(packets, 'em_msg_3F', patch_3f)
    return header, packets


def main() -> int:
    argp = argparse.ArgumentParser(description=__doc__)
    argp.add_argument('--config', type=pathlib.Path, default=CONFIG)
    argp.add_argument('--identity', type=pathlib.Path, default=IDENTITY)
    argp.add_argument('--verify-only', action='store_true', help='Validate profile and source format; do not edit')
    args = argp.parse_args()
    try:
        p = load_profile(args.config)
        identity = load_identity(args.identity)
        hdr_file = VENDOR / 'emount.h'
        packets_file = VENDOR / 'emount_packets.c'
        hdr, packets = apply_to_sources(hdr_file.read_text(), packets_file.read_text(), p, identity)
        if not args.verify_only:
            # Write only after all validation succeeds. Avoid half-applied templates.
            hdr_file.write_text(hdr)
            packets_file.write_text(packets)
        print('LENS_PROFILE=' + p['profile_id'])
        print('LENS_MODEL=' + p['lens_model'])
        print('SONY_LENS_ID=' + str(p['sony_lens_id']) + ' (experimental; validate EXIF)')
        print('FOCAL_MM=' + str(p['focal_length_mm']))
        print('APERTURE_MAX_F=' + str(p['aperture_max_f']))
        print('APERTURE_MIN_F=' + str(p['aperture_min_f']))
        print('APERTURE_NOW_F=' + str(p['aperture_now_f']) + ' (fixed until sensing)')
        print('PROJECT_VERSION=' + identity['project_version'])
        print('LENS_FIRMWARE=' + identity['lens_firmware_version']
              + ' (0x%02X BCD; camera display to be tested)' % identity['lens_firmware_bcd'])
        print('EMOUNT_PROTOCOL=' + identity['emount_protocol_version']
              + ' (0x%04X; EXIF effect unverified)' % identity['emount_protocol_u16'])
        print('SOURCE_CHECK=PASS')
        return 0
    except (OSError, ProfileError) as exc:
        print('LENS_PROFILE_ERROR=' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
