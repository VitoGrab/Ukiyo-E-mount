"""Host-side compact version identity and packet checksum regression checks."""
from __future__ import annotations

import json
import pathlib
import re
import sys
import tempfile
import struct
import zlib
from pathlib import Path
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from apply_lens_profile import (  # noqa: E402
    CONFIG, DEFAULT_BIN, IDENTITY, VERSION_HEADER, ProfileError,
    load_identity, load_profile, parse_project_version, render_version_header,
)


from generate_lens_profile import build_profile, fallback_header, ORDER, SIZE

class ProtocolIdentityTests(unittest.TestCase):
    def setUp(self):
        self.profile = load_profile(CONFIG)
        self.identity = load_identity(IDENTITY)

    def test_current_config_matches_packet(self):
        data = build_profile(self.profile)
        packet = data[16 + 105:16 + 105 + 43]
        self.assertEqual(packet[8:10], b'\0\0')
        self.assertEqual(packet[11:13], b'\0\0')
        self.assertEqual(struct.unpack_from('<H', packet, 15)[0],
                         self.profile['sony_lens_id'])
        self.assertEqual(struct.unpack_from('<H', packet, 40)[0],
                         sum(packet[1:-3]) & 0xffff)
        self.assertEqual(len(data), SIZE)
        self.assertEqual(struct.unpack_from('<I', data, 8)[0], zlib.crc32(data[16:]))

    def test_compact_project_version_encoding(self) -> None:
        self.assertEqual(parse_project_version("0.42"), (0x04, 2))
        self.assertEqual(parse_project_version("0.50"), (0x05, 0))
        self.assertEqual(parse_project_version("0.410"), (0x04, 10))
        self.assertEqual(parse_project_version("0.4255"), (0x04, 255))

    def test_generated_version_header(self) -> None:
        expected = render_version_header(self.identity)
        self.assertEqual(expected, VERSION_HEADER.read_text(encoding='utf-8'))
        version = json.loads(IDENTITY.read_text(encoding="utf-8"))["project_version"]
        self.assertIn(f'#define UKIYO_PROJECT_VERSION "{version}"\n', expected)
        self.assertIn(f'#define UKIYO_PROJECT_VERSION_LABEL "v{version}"\n', expected)

    def test_regeneration_matches_checked_in_files(self):
        binary = build_profile(self.profile)
        self.assertEqual(binary, DEFAULT_BIN.read_bytes())
        self.assertEqual(fallback_header(binary),
                         (ROOT / 'include/emount/default_lens_profile.h').read_text())

    def test_profile_independent_of_firmware_version(self):
        before = build_profile(self.profile)
        self.identity['project_version'] = '0.4255'
        self.assertEqual(before, build_profile(self.profile))

    def test_invalid_project_versions(self) -> None:
        for value in ("0.4", "0.4.2", "4.02", "0.02", "0.4256", "0.4999", "1.42"):
            with self.subTest(value=value):
                with self.assertRaises(ProfileError):
                    parse_project_version(value)

    def test_invalid_identity_keys(self) -> None:
        identity = {"project_version": "0.42", "lens_firmware_version": "04.002",
                    "emount_protocol_version": "1.08"}
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "invalid.json"
            path.write_text(json.dumps(identity), encoding="utf-8")
            with self.assertRaises(ProfileError):
                load_identity(path)

    def test_invalid_protocol_version(self) -> None:
        identity = {"project_version": "0.42", "emount_protocol_version": "1.8"}
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "invalid.json"
            path.write_text(json.dumps(identity), encoding="utf-8")
            with self.assertRaises(ProfileError):
                load_identity(path)


if __name__ == "__main__":
    unittest.main()
