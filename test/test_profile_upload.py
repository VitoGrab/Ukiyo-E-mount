"""Host protocol regression using a fake USB CDC peer; no hardware access."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from upload_lens_profile import Device, UploadError, validate_image, lens_model, SIZE, CHUNK

class FakeSerial:
    def __init__(self, corrupt=False, reject=False):
        self.incoming = bytearray()
        self.stored = b''
        self.replies = []
        self.commands = []
        self.corrupt = corrupt
        self.reject = reject

    def write(self, data):
        command = data.decode('ascii').strip()
        self.commands.append(command)
        parts = command.split()
        if command == 'x':
            self.replies.extend(('EMULATOR_MODE=OFF_ALL_CAMERA_OUTPUTS_HIGH_Z', 'CAMERA_NOT_ACTIVE'))
        elif command == 'profile info':
            self.replies.extend(('PROFILE_EEPROM=7', 'PROFILE_VALID=0', 'PROFILE_INFO_END'))
        elif command == 'profile begin':
            self.incoming.clear()
            self.replies.append('PROFILE_READY')
        elif parts[:2] == ['profile', 'data']:
            assert int(parts[2], 16) == len(self.incoming)
            self.incoming.extend(bytes.fromhex(parts[3]))
            self.replies.append(f'PROFILE_NEXT={len(self.incoming) + int(self.reject)}')
        elif command == 'profile commit':
            self.stored = bytes(self.incoming)
            if self.corrupt:
                self.stored = self.stored[:-1] + bytes([self.stored[-1] ^ 1])
            self.replies.append('PROFILE_WRITTEN')
        elif parts[:2] == ['profile', 'read']:
            offset = int(parts[2], 16)
            self.replies.append('PROFILE_DATA=' + self.stored[offset:offset + CHUNK].hex())
        else:
            raise AssertionError(command)
        return len(data)

    def flush(self):
        pass

    def readline(self):
        return (self.replies.pop(0) + '\n').encode() if self.replies else b''

class UploadTests(unittest.TestCase):
    def setUp(self):
        self.image = (ROOT / 'profiles/hanimex-135-f2-8.bin').read_bytes()

    def test_lens_model_from_stored_packet(self):
        self.assertEqual(lens_model(self.image), 'HANIMEX AUTOMATIC 135mm f2.8')
        helios = (ROOT / 'profiles/helios-44m-4.bin').read_bytes()
        self.assertEqual(lens_model(helios), 'HELIOS 44M-4 58mm f2')
        with self.assertRaises(UploadError):
            lens_model(self.image[:-1])

    def test_success(self):
        peer = FakeSerial()
        device = Device(peer)
        device.stop()
        self.assertIn('PROFILE_EEPROM=7', device.info())
        device.upload(self.image)
        self.assertEqual(peer.stored, self.image)
        self.assertEqual(device.read(), self.image)
        self.assertEqual(len(self.image), SIZE)

    def test_unknown_command_reports_old_firmware(self):
        peer = FakeSerial()
        peer.replies.append('UNKNOWN_COMMAND (type help)')
        with self.assertRaisesRegex(UploadError, 'does not recognize'):
            Device(peer).expect('PROFILE_INFO_END')

    def test_invalid_image_before_transfer(self):
        peer = FakeSerial()
        with self.assertRaises(UploadError):
            Device(peer).upload(self.image[:-1])
        self.assertEqual(peer.commands, [])

    def test_bad_ack_never_commits(self):
        peer = FakeSerial(reject=True)
        with self.assertRaises(UploadError):
            Device(peer).upload(self.image)
        self.assertNotIn('profile commit', peer.commands)

    def test_corrupt_readback(self):
        peer = FakeSerial(corrupt=True)
        with self.assertRaises(UploadError):
            Device(peer).upload(self.image)
        self.assertEqual(peer.commands.count('profile commit'), 1)
