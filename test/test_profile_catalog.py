"""Catalog validation and checked-in binary regression."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_lens_profiles import CONFIG, ProfileError, load_catalog, select_profile
from generate_lens_profile import build_profile

class CatalogTests(unittest.TestCase):
    def test_artifacts(self):
        catalog = load_catalog()
        expected = set()
        for key, p in catalog.items():
            path = CONFIG.parent / (key + '.bin')
            with self.subTest(profile=key):
                expected.add(path)
                self.assertEqual(path.read_bytes(), build_profile(p))
                self.assertEqual(select_profile(catalog, key), p)
        self.assertEqual(set(CONFIG.parent.glob('*.bin')), expected)

    def test_arsat_aperture_probe(self):
        import struct
        catalog = load_catalog()
        default = catalog['arsat-m-80-200-4-5']
        probe = catalog['arsat-m-80-200-4-5-aperture']
        self.assertEqual(probe['focal_length_source'], 'zoom_aperture')
        self.assertEqual(default, dict(probe, profile_id=default['profile_id'],
                                       focal_length_source='zoom_range'))
        image = build_profile(probe)
        spec = 16 + 148
        self.assertEqual(struct.unpack_from('<HH', image, spec + 6), (4096, 7168))
        self.assertEqual(image[spec + 22:spec + 28], bytes.fromhex('00 80 02 00 10 10'))
        self.assertEqual(struct.unpack_from('<HH', image, 16 + 30), (800, 800))
        self.assertEqual(struct.unpack_from('<HH', image, 16 + 6), (4096, 4096))
        self.assertNotEqual(image, build_profile(default))

    def test_duplicate_and_unsafe_id(self):
        p = next(iter(load_catalog().values()))
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / 'profiles.json'
            for entries in ([p, p], [dict(p, profile_id='../escape')]):
                path.write_text(json.dumps(entries), encoding='utf-8')
                with self.assertRaises(ProfileError):
                    load_catalog(path)

    def test_cli_batch_and_selection(self):
        cmd = [sys.executable, str(ROOT / 'tools/generate_lens_profile.py')]
        with tempfile.TemporaryDirectory() as d:
            output = pathlib.Path(d)
            result = subprocess.run(cmd + ['--output-dir', d], capture_output=True, text=True, check=True)
            catalog = load_catalog()
            expected = {key + '.bin' for key in catalog}
            self.assertEqual({p.name for p in output.glob('*.bin')}, expected)
            self.assertIn(f'PROFILES_GENERATED={len(expected)}', result.stdout)
            subprocess.run(cmd + ['--output-dir', d, '--verify-only'], check=True, capture_output=True)
            zoom = next(key for key, p in catalog.items() if p['lens_type'] == 'zoom')
            subprocess.run(cmd + ['--output-dir', d, '--profile-id', zoom],
                           check=True, capture_output=True)
            single = output / 'single'
            subprocess.run(cmd + ['--output-dir', str(single), '--profile-id', 'helios-44m-4'],
                           check=True, capture_output=True)
            self.assertEqual([p.name for p in single.glob('*.bin')], ['helios-44m-4.bin'])
