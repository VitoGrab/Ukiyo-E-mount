"""Host protocol regression using a fake USB CDC peer; no hardware access."""
import pathlib
import os
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from upload_lens_profile import Device, UploadError, validate_image, lens_model, wait_until_armed, choose_profile, main, SIZE, CHUNK

class FakeSerial:
    def __init__(self, corrupt=False, reject=False, armed=True):
        self.incoming = bytearray()
        self.stored = b''
        self.replies = []
        self.commands = []
        self.corrupt = corrupt
        self.reject = reject
        self.armed = armed

    def write(self, data):
        command = data.decode('ascii').strip()
        self.commands.append(command)
        parts = command.split()
        if command == 'x':
            self.replies.extend(('EMULATOR_MODE=OFF_ALL_CAMERA_OUTPUTS_HIGH_Z', 'CAMERA_NOT_ACTIVE'))
        elif command == 'profile info':
            self.replies.extend(('PROFILE_EEPROM=7', 'PROFILE_VALID=0', 'PROFILE_INFO_END'))
        elif command == 'profile reset':
            self.replies.append('PROFILE_RESET_ACCEPTED')
        elif command == 's':
            self.replies.extend(('MODE=ARMED', 'AUTO_ARM=ENABLED') if self.armed else
                                ('MODE=OFF', 'AUTO_ARM=DISABLED'))
        elif command == 'profile abort':
            self.replies.append('PROFILE_ABORTED')
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

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def reset_input_buffer(self):
        self.replies.clear()

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

    def test_dynamic_picker(self):
        catalog = [{'profile_id': 'test-new-lens', 'lens_model': 'New lens'}]
        with patch('upload_lens_profile.prepare_profiles', return_value=catalog) as prep, \
             patch('builtins.input', return_value='1'):
            self.assertEqual(choose_profile().name, 'test-new-lens.bin')
        prep.assert_called_once_with()

    def test_select_cancel_before_usb(self):
        args = ['upload_lens_profile.py', '--port', '/dev/test', '--select']
        with patch.object(sys, 'argv', args), \
             patch('upload_lens_profile.choose_profile', return_value=ROOT / 'profiles/helios-44m-4.bin'), \
             patch('builtins.input', return_value='no'), \
             patch.dict(sys.modules, {'serial': None}):
            self.assertEqual(main(), 0)

    def test_first_catalog_entry_and_pagination(self):
        with patch('upload_lens_profile.shutil.get_terminal_size', return_value=os.terminal_size((80, 12))), \
             patch('builtins.input', side_effect=['n', 'p', '1']):
            self.assertEqual(choose_profile().name, 'helios-44m-4.bin')

    def test_select_y_upload_leaves_bus_off(self):
        peer = FakeSerial()
        serial = SimpleNamespace(Serial=lambda *a, **kw: peer)
        args = ['upload_lens_profile.py', '--port', '/dev/test', '--select']
        with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), \
             patch('upload_lens_profile.choose_profile', return_value=ROOT / 'profiles/helios-44m-4.bin'), \
             patch('builtins.input', return_value='Y'), patch('upload_lens_profile.time.sleep'):
            self.assertEqual(main(), 0)
        self.assertEqual(peer.stored, (ROOT / 'profiles/helios-44m-4.bin').read_bytes())
        self.assertNotIn('profile reset', peer.commands)
        self.assertEqual(peer.commands[0], 'x')

    def test_success(self):
        peer = FakeSerial()
        device = Device(peer)
        device.stop()
        self.assertIn('PROFILE_EEPROM=7', device.info())
        device.upload(self.image)
        self.assertEqual(peer.stored, self.image)
        self.assertEqual(device.read(), self.image)
        self.assertEqual(len(self.image), SIZE)

    def test_reset_after_verified_upload(self):
        peer, rebooted = FakeSerial(), FakeSerial()
        serial = SimpleNamespace(Serial=lambda *a, **kw: next(connections))
        connections = iter((peer, rebooted))
        args = ['upload_lens_profile.py', '--port', '/dev/test', '--detached', '--reset',
                str(ROOT / 'profiles/hanimex-135-f2-8.bin')]
        with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), \
             patch('upload_lens_profile.time.sleep'):
            self.assertEqual(main(), 0)
        self.assertEqual(peer.commands[-1], 'profile reset')
        self.assertTrue(peer.commands[-2].startswith('profile read '))
        self.assertEqual(rebooted.commands, ['s'])

    def test_failed_readback_never_resets(self):
        peer = FakeSerial(corrupt=True)
        serial = SimpleNamespace(Serial=lambda *a, **kw: peer)
        args = ['upload_lens_profile.py', '--port', '/dev/test', '--detached',
                str(ROOT / 'profiles/hanimex-135-f2-8.bin')]
        with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), \
             patch('upload_lens_profile.time.sleep'):
            self.assertEqual(main(), 1)
        self.assertNotIn('profile reset', peer.commands)

    def test_info_does_not_reset(self):
        peer = FakeSerial()
        serial = SimpleNamespace(Serial=lambda *a, **kw: peer)
        args = ['upload_lens_profile.py', '--port', '/dev/test', '--detached', '--info']
        with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), \
             patch('upload_lens_profile.time.sleep'):
            self.assertEqual(main(), 0)
        self.assertEqual(peer.commands, ['x', 'profile info'])

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
