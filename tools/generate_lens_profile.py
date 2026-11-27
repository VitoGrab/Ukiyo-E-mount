#!/usr/bin/env python3
"""Generate independent prime-lens BINs from a JSON catalog."""
from __future__ import annotations
import argparse
import json
import math
import pathlib
import struct
import sys
import zlib
from prepare_lens_profiles import (ROOT, CONFIG, ProfileError, load_catalog,
                                select_profile, validate_profile)

ORDER = [('05', 105), ('07', 43), ('08', 210), ('1b', 19),
         ('28', 44), ('35', 48), ('3f', 74)]
SIZE = 559
BASE = ROOT / 'profiles' / 'base_packets.json'
BCD_APERTURES = (0x10, 0x11, 0x12, 0x14, 0x16, 0x18, 0x20, 0x22,
                 0x25, 0x28, 0x32, 0x35, 0x40, 0x45, 0x50, 0x56,
                 0x63, 0x71, 0x80, 0x90)

def av(f):
    return round(4096 + 512 * math.log2(f))

def desc(v):
    return (v - 4096 + 16) // 32 + 8

def bcd(n):
    return ((n // 10 % 10) << 4) | n % 10

def put16(frame, offset, value):
    struct.pack_into('<H', frame, offset, value)

def build_profile(p):
    p = validate_profile(p)
    raw = json.loads(BASE.read_text(encoding='utf-8'))
    frames = {key: bytearray.fromhex(raw[key]) for key, _ in ORDER}
    for key, size in ORDER:
        frame = frames[key]
        if len(frame) != size or frame[0] != 0xf0 or frame[5] != int(key, 16):
            raise ProfileError(f'Invalid base frame {key}')
    mm = p['focal_length_default_mm']
    maximum, minimum, now = (av(p[k]) for k in
                              ('aperture_max_f', 'aperture_min_f', 'aperture_now_f'))
    for key, offset in (('05', 30), ('28', 10), ('35', 10)):
        put16(frames[key], offset, mm * 10)
        put16(frames[key], offset + 2, mm * 10)
    spec = frames['08']
    spec[22:26] = bytes((bcd(mm // 100), bcd(mm), 0, 0))
    spec[26] = BCD_APERTURES[min(19, ((maximum - 4096) * 3 + 128) // 256)]
    spec[27] = 0
    put16(spec, 6, maximum)
    put16(spec, 8, minimum)
    for key in ('05', '1b'):
        put16(frames[key], 6, now)
        put16(frames[key], 8, now)
    for offset in (50, 52, 57):
        frames['05'][offset] = desc(maximum)
    frames['05'][58] = desc(minimum)
    identity = frames['07']
    for offset in (8, 9, 11, 12):
        identity[offset] = 0
    put16(identity, 15, p['sony_lens_id'])
    frames['3f'][7:71] = p['lens_model'].encode('ascii').ljust(64, b'\0')
    for key, size in ORDER:
        frame = frames[key]
        put16(frame, 1, size)
        put16(frame, size - 3, sum(frame[1:-3]) & 0xffff)
    payload = b''.join(frames[key] for key, _ in ORDER)
    return struct.pack('<4sHHII', b'UKLP', 1, SIZE, zlib.crc32(payload), 0) + payload

def fallback_header(data):
    rows = ['    ' + ', '.join(f'0x{x:02x}' for x in data[i:i+12]) + ','
            for i in range(0, len(data), 12)]
    return ('#pragma once\n\n'
            '/* Generated fallback; regenerate only when changing the reference profile. */\n'
            'static const uint8_t ukiyo_default_profile[UKIYO_PROFILE_SIZE] = {\n'
            + '\n'.join(rows) + '\n};\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=pathlib.Path, default=CONFIG)
    parser.add_argument('--profile-id', help='Generate one supported profile instead of all primes')
    parser.add_argument('--output-dir', type=pathlib.Path,
                        help='Defaults to the catalog directory')
    parser.add_argument('--verify-only', action='store_true',
                        help='Compare existing BINs without writing')
    args = parser.parse_args()
    try:
        profiles = load_catalog(args.catalog)
        output_dir = args.output_dir or args.catalog.parent
        if args.profile_id is not None:
            selected = [select_profile(profiles, args.profile_id)]
        else:
            selected = [p for p in profiles.values() if p['lens_type'] == 'prime']
            for p in profiles.values():
                if p['lens_type'] == 'zoom':
                    print(f"SKIP={p['profile_id']} REASON=zoom unsupported (issue #5)")
                    if (output_dir / (p['profile_id'] + '.bin')).exists():
                        raise ProfileError(f"Unexpected zoom BIN: {p['profile_id']}")
        if not selected:
            raise ProfileError('No supported prime profiles selected')
        # Validate and encode everything before writing anything.
        artifacts = [(output_dir / (p['profile_id'] + '.bin'), build_profile(p))
                     for p in selected]
        if not args.verify_only:
            output_dir.mkdir(parents=True, exist_ok=True)
        for output, data in artifacts:
            if args.verify_only:
                if output.read_bytes() != data:
                    raise ProfileError(f'Stale profile: {output}')
            else:
                output.write_bytes(data)
            print(f'PROFILE={output} SIZE={len(data)} CRC32={struct.unpack_from("<I", data, 8)[0]:08X}')
        print(f"PROFILES_{'VERIFIED' if args.verify_only else 'GENERATED'}={len(artifacts)}")
        return 0
    except (OSError, ProfileError, ValueError) as exc:
        print(f'LENS_PROFILE_ERROR={exc}', file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
