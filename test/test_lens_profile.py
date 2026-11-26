"""Prime-only profile schema regression tests."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from apply_lens_profile import CONFIG, ProfileError, load_profile


class LensProfileTests(unittest.TestCase):
    def validate(self, **changes):
        profile = json.loads(CONFIG.read_text(encoding="utf-8"))
        profile.update(changes)
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "profile.json"
            path.write_text(json.dumps(profile), encoding="utf-8")
            return load_profile(path)

    def test_prime(self):
        profile = self.validate(focal_length_min_mm=135,
                                focal_length_max_mm=135,
                                focal_length_default_mm=135)
        self.assertEqual(profile["focal_length_default_mm"], 135)

    def test_zoom_rejected(self):
        with self.assertRaisesRegex(ProfileError, "Only prime"):
            self.validate(lens_type="zoom", focal_length_min_mm=80,
                          focal_length_max_mm=200, focal_length_default_mm=135)

    def test_unequal_prime_range_rejected(self):
        with self.assertRaisesRegex(ProfileError, "identical"):
            self.validate(focal_length_min_mm=35, focal_length_max_mm=85)

    def test_nonfixed_prime_rejected(self):
        with self.assertRaisesRegex(ProfileError, "fixed"):
            self.validate(focal_length_source="sensor")

    def test_invalid_focal_values(self):
        for value in (0, 501, True, 58.5, "58"):
            with self.subTest(value=value), self.assertRaises(ProfileError):
                self.validate(focal_length_default_mm=value)
