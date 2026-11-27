"""Catalog validation and checked-in binary regression."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from apply_lens_profile import CONFIG, ProfileError, load_catalog, select_profile
from generate_lens_profile import build_profile

class CatalogTests(unittest.TestCase):
    def test_artifacts(self):
        catalog = load_catalog()
        expected = set()
        for key, p in catalog.items():
            path = CONFIG.parent / (key + '.bin')
            with self.subTest(profile=key):
                if p['lens_type'] == 'zoom':
                    self.assertFalse(path.exists())
                    with self.assertRaises(ProfileError):
                        build_profile(p)
                    with self.assertRaises(ProfileError):
                        select_profile(catalog, key)
                else:
                    expected.add(path)
                    self.assertEqual(path.read_bytes(), build_profile(p))
        self.assertEqual(set(CONFIG.parent.glob('*.bin')), expected)

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
            primes = {key + '.bin' for key, p in catalog.items() if p['lens_type'] == 'prime'}
            self.assertEqual({p.name for p in output.glob('*.bin')}, primes)
            self.assertIn(f'PROFILES_GENERATED={len(primes)}', result.stdout)
            subprocess.run(cmd + ['--output-dir', d, '--verify-only'], check=True, capture_output=True)
            zoom = next(key for key, p in catalog.items() if p['lens_type'] == 'zoom')
            result = subprocess.run(cmd + ['--output-dir', d, '--profile-id', zoom],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            single = output / 'single'
            subprocess.run(cmd + ['--output-dir', str(single), '--profile-id', 'helios-44m-4'],
                           check=True, capture_output=True)
            self.assertEqual([p.name for p in single.glob('*.bin')], ['helios-44m-4.bin'])
