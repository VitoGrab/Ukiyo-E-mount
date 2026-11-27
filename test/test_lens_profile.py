"""Prime-only profile schema regression tests."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_lens_profiles import CONFIG, ProfileError, load_profile, validate_profile


class LensProfileTests(unittest.TestCase):
    def validate(self, **changes):
        profile = dict(load_profile(CONFIG))
        profile.update(changes)
        return validate_profile(profile)

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

    def test_matching_prime_endpoint_apertures(self):
        profile = self.validate(aperture_max_f=1.8,
                                aperture_max_tele_f=1.8)
        self.assertEqual(profile["aperture_max_f"], 1.8)

    def test_unequal_prime_endpoint_apertures_rejected(self):
        with self.assertRaisesRegex(ProfileError, "identical wide and tele"):
            self.validate(aperture_max_f=2.0, aperture_max_tele_f=2.8)

    def test_invalid_endpoint_apertures_rejected(self):
        for value in (0, -1, float("nan"), float("inf"), None):
            with self.subTest(value=value), self.assertRaises(ProfileError):
                self.validate(aperture_max_tele_f=value)
