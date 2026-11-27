"""Host regression for the simplified row-local firmware upload workflow."""
import json
import pathlib
import sys
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import upload_with_retry as updater

class FirmwareUploadTests(unittest.TestCase):
    def test_upload_without_application_cdc(self):
        with patch.object(sys, "argv", ["upload_with_retry.py", "--environment", "seeed_xiao"]), \
             patch.object(updater, "resolve_pio", return_value="pio"), \
             patch.object(updater, "run", return_value=0) as run:
            self.assertEqual(updater.main(), 0)
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(calls), 3)
        self.assertIn("--verify-only", calls[0])
        self.assertEqual(calls[1], ["pio", "run", "-e", "seeed_xiao"])
        self.assertEqual(calls[2], ["pio", "run", "-e", "seeed_xiao", "-t", "upload"])

    def test_retry(self):
        with patch.object(sys, "argv", ["upload_with_retry.py", "--environment",
                                        "seeed_xiao_production", "--port", "/dev/test"]), \
             patch.object(updater, "resolve_pio", return_value="pio"), \
             patch.object(updater, "run", side_effect=[0, 0, 1, 0]) as run, \
             patch.object(updater.time, "sleep"):
            self.assertEqual(updater.main(), 0)
        self.assertEqual(run.call_args_list[-1].args[0],
                         ["pio", "run", "-e", "seeed_xiao_production", "-t", "upload",
                          "--upload-port", "/dev/test"])

    def test_task_separation(self):
        config = json.loads((ROOT / ".vscode/tasks.json").read_text())
        firmware = [t for t in config["tasks"] if t["label"].startswith("Ukiyo: Upload")
                    and t["label"] != "Ukiyo: Upload Lens Profile"]
        self.assertEqual(len(firmware), 4)
        for task in firmware:
            self.assertNotIn("cameraDetached", json.dumps(task))
            self.assertIn("upload_with_retry.py", json.dumps(task))
        profile = next(t for t in config["tasks"] if t["label"] == "Ukiyo: Upload Lens Profile")
        self.assertIn("${input:cameraDetached}", profile["args"])
        self.assertIn("upload_lens_profile.py", json.dumps(profile))
        choices = next(i for i in config["inputs"] if i["id"] == "lensProfile")
        catalog = json.loads((ROOT / "profiles/profiles.json").read_text())
        self.assertEqual({o["value"] for o in choices["options"]},
                         {p["profile_id"] for p in catalog if p["lens_type"] == "prime"})
