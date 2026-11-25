"""Host-side version identity and packet checksum regression checks."""
from __future__ import annotations

import json
import pathlib
import re
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from apply_lens_profile import (  # noqa: E402
    CONFIG, IDENTITY, VENDOR, ProfileError, apply_to_sources,
    load_identity, load_profile, parse_firmware_version,
)


def packet_bytes(source: str, symbol: str) -> list[int]:
    match = re.search(
        r'uint8_t\s+' + re.escape(symbol) +
        r'\s*\[EA9_LEN_[0-9A-Fa-f]+\]\s*=\s*\{(.*?)\};',
        source, flags=re.S,
    )
    if match is None:
        raise AssertionError(f'Cannot locate {symbol}')
    body = re.sub(r'/\*.*?\*/', '', match.group(1), flags=re.S)
    return [int(value, 16) for value in re.findall(r'0x[0-9a-fA-F]{2}', body)]


class ProtocolIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.profile = load_profile(CONFIG)
        self.identity = load_identity(IDENTITY)
        self.header = (VENDOR / 'emount.h').read_text(encoding='utf-8')
        self.packets = (VENDOR / 'emount_packets.c').read_text(encoding='utf-8')

    def assert_packet(self, source: str, *, protocol: int, major: int, minor: int) -> None:
        data = packet_bytes(source, 'em_msg_07')
        self.assertEqual(len(data), 43)
        self.assertEqual(data[:6], [0xf0, 0x2b, 0, 2, 0, 7])
        self.assertEqual(data[8:10], [protocol & 255, protocol >> 8])
        self.assertEqual(data[11:13], [minor, major])
        self.assertEqual(data[15:17], [0, 0])  # Helios Lens ID
        csum = sum(data[1:-3]) & 0xffff
        self.assertEqual(data[-3:-1], [csum & 255, csum >> 8])
        self.assertEqual(data[-1], 0x55)

    def test_verified_baseline_04000_at_protocol_108(self) -> None:
        self.assertEqual(self.identity['lens_firmware_version'], '04.000')
        self.assertEqual(self.identity['lens_firmware_major_bcd'], 0x04)
        self.assertEqual(self.identity['lens_firmware_minor_candidate'], 0)
        self.assertEqual(self.identity['emount_protocol_u16'], 0x0108)
        self.assert_packet(self.packets, protocol=0x0108, major=0x04, minor=0)

    def test_regeneration_matches_checked_in_files(self) -> None:
        result = apply_to_sources(self.header, self.packets, self.profile, self.identity)
        self.assertEqual(result, (self.header, self.packets))
        self.assertEqual(apply_to_sources(*result, self.profile, self.identity), result)

    def test_experimental_04002_and_checksum(self) -> None:
        identity = dict(self.identity)
        identity['lens_firmware_major_bcd'], identity['lens_firmware_minor_candidate'] = (
            parse_firmware_version('04.002'))
        header, packets = apply_to_sources(self.header, self.packets, self.profile, identity)
        self.assertEqual(header, self.header)
        self.assert_packet(packets, protocol=0x0108, major=0x04, minor=2)
        self.assertEqual(packet_bytes(packets, 'em_msg_07')[-3:-1], [0xcd, 0x02])
        self.assertEqual(apply_to_sources(header, packets, self.profile, identity), (header, packets))

    def test_sample_22_major(self) -> None:
        self.assertEqual(parse_firmware_version('22.000'), (0x22, 0))
        self.assertEqual(parse_firmware_version('04.255'), (0x04, 255))

    def test_invalid_versions(self) -> None:
        for value in ('0.4', '0.4.2', '4.002', '04.02', '04.256', '04.999',
                      '0A.002', '-1.002', '04.-01'):
            with self.subTest(value=value):
                with self.assertRaises(ProfileError):
                    parse_firmware_version(value)

    def test_invalid_protocol_version(self) -> None:
        identity = json.loads(IDENTITY.read_text(encoding='utf-8'))
        identity['emount_protocol_version'] = '1.8'
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'invalid.json'
            path.write_text(json.dumps(identity), encoding='utf-8')
            with self.assertRaises(ProfileError):
                load_identity(path)


if __name__ == '__main__':
    unittest.main()
