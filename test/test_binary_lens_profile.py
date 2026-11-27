"""End-to-end generator and real C packet loader regression."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from apply_lens_profile import CONFIG, load_profile
from generate_lens_profile import build_profile

@unittest.skipUnless(shutil.which('cc'), 'Native C compiler unavailable')
class BinaryProfileTests(unittest.TestCase):
    def test_generator_and_loader(self):
        # Use exact AV-equivalent f-numbers for the C fixture.
        p = load_profile(CONFIG)
        def fixture(mm, model, lens_id, maximum, now):
            return build_profile(dict(p, focal_length_min_mm=mm,
                focal_length_max_mm=mm, focal_length_default_mm=mm,
                lens_model=model, sony_lens_id=lens_id,
                aperture_max_f=maximum, aperture_max_tele_f=maximum,
                aperture_now_f=now))
        first = fixture(135, 'Test 135mm', 42, 2 ** 1.5, 4)
        second = fixture(35, '35mm', 0, 2, 2)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            exe = tmp / 'test'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra',
                            '-Iinclude', '-Iinclude/emount',
                            'src/emount/emount_packets.c',
                            'test/host_runtime_lens_profile_test.c',
                            '-o', str(exe)], cwd=ROOT, check=True)
            paths = []
            for name, data in [('135', first), ('35', second)]:
                path = tmp / (name + '.bin')
                path.write_bytes(data)
                paths.append(str(path))
            # Repair both checksums after corrupting semantic fields: the
            # loader must reject them even when CRC is correct.
            for name, start, length, offset, value in (
                ('wrong-id', 16, 105, 5, 6),
                ('zoom', 16 + 148, 210, 25, 0x85),
                ('identity', 16 + 105, 43, 11, 99),
                ('focal-mismatch', 16 + 377, 44, 10, 1),
            ):
                invalid = bytearray(second)
                invalid[start + offset] = value
                struct.pack_into('<H', invalid, start + length - 3,
                                 sum(invalid[start + 1:start + length - 3]) & 0xffff)
                struct.pack_into('<I', invalid, 8, zlib.crc32(invalid[16:]))
                path = tmp / (name + '.bin')
                path.write_bytes(invalid)
                paths.append(str(path))
            subprocess.run([str(exe), *paths], cwd=ROOT, check=True)
