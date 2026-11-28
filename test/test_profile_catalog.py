"""Catalog validation and checked-in binary regression."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_lens_profiles import (CONFIG, ProfileError, load_catalog, select_profile,
                                   load_focal_maps, render_focal_map_header, FOCAL_MAP_HEADER)
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
        self.assertEqual(probe['focal_length_source'], 'aperture_zoom')
        common = dict(probe, profile_id=default['profile_id'],
                      focal_length_source='default_zoom', lens_model=default['lens_model'])
        del common['focal_length_map']
        self.assertEqual(default, common)
        image = build_profile(probe)
        spec = 16 + 148
        self.assertEqual(struct.unpack_from('<HH', image, spec + 6), (0x14AA, 0x1CAA))
        self.assertEqual(image[spec + 22:spec + 28], bytes.fromhex('00 80 02 00 50 50'))
        self.assertEqual(struct.unpack_from('<HH', image, 16 + 30), (800, 800))
        self.assertEqual(struct.unpack_from('<HH', image, 16 + 6), (0x14AA, 0x14AA))
        self.assertEqual([image[16 + i] for i in (50, 52, 57, 58)], [45, 45, 45, 109])
        self.assertTrue(probe['lens_model'].endswith('[AZ]'))
        self.assertTrue(default['lens_model'].endswith('[DZ]'))
        self.assertNotEqual(image, build_profile(default))

    def test_focal_maps_and_edges(self):
        import struct
        maps = load_focal_maps()
        self.assertEqual(FOCAL_MAP_HEADER.read_text(), render_focal_map_header(maps))
        self.assertEqual(len(maps['standard']), 40)
        self.assertEqual(len(maps['telephoto']), 40)
        catalog = load_catalog()
        self.assertEqual(sum(p['lens_type'] == 'prime' for p in catalog.values()), 16)
        defaults = [p for p in catalog.values() if p['focal_length_source'] == 'default_zoom']
        self.assertEqual(len(defaults), 4)
        self.assertTrue(all(p['lens_model'].endswith('[DZ]') for p in defaults))
        for key, limits, focal, bcd in (
            ('sigma-21-35-3-5-4-2-aperture-test', (0x1055, 0x1200), 210, '00 21 00 35 11 11'),
            ('asahi-smc-takumar-135-600-6-7-aperture-test', (0x14AA, 0x1D00), 1350, '01 35 06 00 50 50'),
        ):
            with self.subTest(profile=key):
                p = catalog[key]
                image = build_profile(p)
                self.assertIn('[TEST]', p['lens_model'])
                self.assertEqual(struct.unpack_from('<HH', image, 164 + 6), limits)
                self.assertEqual(image[164 + 22:164 + 28], bytes.fromhex(bcd))
                self.assertEqual(struct.unpack_from('<HH', image, 16 + 30), (focal, focal))

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
