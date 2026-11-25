"""Host-only regression checks for Ukiyo lens identity packet generation."""
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
    CONFIG, IDENTITY, VENDOR, ProfileError, apply_to_sources, load_identity,
    load_profile,
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
    def test_identity_encoding(self) -> None:
        identity = load_identity(IDENTITY)
        self.assertEqual(identity['lens_firmware_bcd'], 0x04)
        self.assertEqual(identity['emount_protocol_u16'], 0x0131)

    def test_rebuild_is_idempotent_and_matches_vendored_packet(self) -> None:
        original_header = (VENDOR / 'emount.h').read_text()
        original_packets = (VENDOR / 'emount_packets.c').read_text()
        profile = load_profile(CONFIG)
        identity = load_identity(IDENTITY)
        rebuilt_header, rebuilt_packets = apply_to_sources(
            original_header, original_packets, profile, identity)
        self.assertEqual(rebuilt_header, original_header)
        self.assertEqual(rebuilt_packets, original_packets)
        self.assertEqual(
            apply_to_sources(rebuilt_header, rebuilt_packets, profile, identity),
            (rebuilt_header, rebuilt_packets))
        data = packet_bytes(rebuilt_packets, 'em_msg_07')
        self.assertEqual(len(data), 43)
        self.assertEqual(data[5], 0x07)
        self.assertEqual(data[8:10], [0x31, 0x01])
        self.assertEqual(data[12], 0x04)
        self.assertEqual(data[15:17], [0x00, 0x00])
        self.assertEqual(
            data[-3:-1], [sum(data[1:-3]) & 255, (sum(data[1:-3]) >> 8) & 255])

    def test_invalid_display_version_rejected(self) -> None:
        identity = json.loads(IDENTITY.read_text())
        identity['lens_firmware_version'] = '0.4.2'
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'invalid.json'
            path.write_text(json.dumps(identity))
            with self.assertRaises(ProfileError):
                load_identity(path)

    def test_invalid_protocol_version_rejected(self) -> None:
        identity = json.loads(IDENTITY.read_text())
        identity['emount_protocol_version'] = '1.3'
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'invalid.json'
            path.write_text(json.dumps(identity))
            with self.assertRaises(ProfileError):
                load_identity(path)


if __name__ == '__main__':
    unittest.main()
