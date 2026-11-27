"""Host regression for the simplified row-local firmware upload workflow."""
import json
import pathlib
import sys
import unittest
import tempfile
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import upload_with_retry as updater
from select_build_type import default_environment, select_environment

class FirmwareUploadTests(unittest.TestCase):
    def test_upload_without_application_cdc(self):
        with patch.object(sys, "argv", ["upload_with_retry.py", "--environment", "seeed_xiao"]), \
             patch.object(updater, "resolve_pio", return_value="pio"), \
             patch.object(updater, "run", return_value=0) as run:
            self.assertEqual(updater.main(), 0)
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(calls), 3)
        self.assertTrue(calls[0][1].endswith("prepare_lens_profiles.py"))
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

    def test_default_environment(self):
        with patch.object(sys, "argv", ["upload_with_retry.py"]), \
             patch.object(updater, "default_environment", return_value="seeed_xiao_production"), \
             patch.object(updater, "resolve_pio", return_value="pio"), \
             patch.object(updater, "run", return_value=0) as run:
            self.assertEqual(updater.main(), 0)
        self.assertEqual(run.call_args_list[-1].args[0],
                         ["pio", "run", "-e", "seeed_xiao_production", "-t", "upload"])

    def test_switch_default_preserves_ini(self):
        original = (ROOT / "platformio.ini").read_text()
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "platformio.ini"
            path.write_text(original)
            previous = default_environment(path)
            other = "seeed_xiao_production" if previous == "seeed_xiao" else "seeed_xiao"
            select_environment(other, path)
            self.assertEqual(default_environment(path), other)
            select_environment(previous, path)
            self.assertEqual(path.read_text(), original)

    def test_task_separation(self):
        config = json.loads((ROOT / ".vscode/tasks.json").read_text())
        tasks = {t["label"]: t for t in config["tasks"]}
        self.assertEqual(set(tasks), {"Ukiyo: Build & Upload", "Ukiyo: Select Build Type",
                                     "Ukiyo: Upload Lens Profile", "Ukiyo: Monitor", "Ukiyo: Clean"})
        self.assertNotIn("--environment", tasks["Ukiyo: Build & Upload"]["args"])
        self.assertIn("--select", tasks["Ukiyo: Upload Lens Profile"]["args"])
        self.assertNotIn("--detached", tasks["Ukiyo: Upload Lens Profile"]["args"])
        self.assertEqual([i["id"] for i in config["inputs"]], ["buildType"])
