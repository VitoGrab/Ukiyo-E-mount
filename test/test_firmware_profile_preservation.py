"""Host-only firmware update preservation regression."""
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import upload_with_retry as updater
from upload_lens_profile import UploadError

class FakeDevice:
    def __init__(self, image=None):
        self.image = image
        self.events = []
        self.info_override = None
        self.corrupt = False

    def info(self):
        self.events.append("info")
        if self.info_override is not None:
            return self.info_override
        return [f"PROFILE_VALID={int(self.image is not None)}"]

    def read(self):
        self.events.append("read")
        return self.image

    def upload(self, image):
        self.events.append("upload")
        self.image = image[:-1] if self.corrupt else image

    def send(self, command):
        self.events.append(command)

    def expect(self, prefix):
        self.events.append(prefix)

class PreservationTests(unittest.TestCase):
    def setUp(self):
        self.image = (ROOT / "profiles/hanimex-135-f2-8.bin").read_bytes()

    def test_save_and_restore(self):
        old = FakeDevice(self.image)
        image = updater.backup_profile(old)
        self.assertEqual(image, self.image)
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp) / "recovery"
            with patch.object(updater, "RECOVERY_DIR", directory):
                path = updater.save_recovery(image)
                self.assertEqual(path.read_bytes(), image)
        new = FakeDevice()
        updater.restore_profile(new, image)
        self.assertEqual(new.image, self.image)
        self.assertEqual(new.events.count("upload"), 1)
        self.assertIn("e", new.events)

    def test_already_present_skips_write(self):
        device = FakeDevice(self.image)
        updater.restore_profile(device, self.image)
        self.assertNotIn("upload", device.events)

    def test_blank_skips_write(self):
        device = FakeDevice()
        self.assertIsNone(updater.backup_profile(device))
        updater.restore_profile(device, None)
        self.assertNotIn("upload", device.events)
        self.assertIn("e", device.events)

    def test_bad_info_blocks_backup(self):
        device = FakeDevice()
        device.info_override = []
        with self.assertRaises(UploadError):
            updater.backup_profile(device)

    def test_corrupt_restore_never_rearms(self):
        device = FakeDevice()
        device.corrupt = True
        with self.assertRaises(UploadError):
            updater.restore_profile(device, self.image)
        self.assertNotIn("e", device.events)

    def test_unexpected_profile_not_overwritten_when_blank(self):
        device = FakeDevice(self.image)
        with self.assertRaises(UploadError):
            updater.restore_profile(device, None)
        self.assertNotIn("upload", device.events)
