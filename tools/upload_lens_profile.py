#!/usr/bin/env python3
"""Program or read one Ukiyo lens profile over the application USB CDC port."""
from __future__ import annotations
import argparse
import pathlib
import struct
import sys
import time
import zlib

from generate_lens_profile import ORDER, SIZE

CHUNK = 24

class UploadError(RuntimeError):
    pass

def validate_image(image: bytes):
    if len(image) != SIZE:
        raise UploadError(f'Expected {SIZE} bytes, got {len(image)}')
    magic, version, length, crc, reserved = struct.unpack_from('<4sHHII', image)
    if (magic, version, length, reserved) != (b'UKLP', 1, SIZE, 0):
        raise UploadError('Invalid profile header')
    if zlib.crc32(image[16:]) != crc:
        raise UploadError('Invalid profile CRC32')
    start = 16
    for key, n in ORDER:
        packet = image[start:start + n]
        if (packet[0] != 0xf0 or packet[-1] != 0x55 or
                packet[5] != int(key, 16) or
                struct.unpack_from('<H', packet, 1)[0] != n or
                struct.unpack_from('<H', packet, n - 3)[0] != sum(packet[1:-3]) & 0xffff):
            raise UploadError(f'Invalid packet {key}')
        start += n
    if start != SIZE:
        raise UploadError('Invalid packet sizes')
    return crc


def lens_model(image: bytes) -> str:
    """Decode 0x3F payload[1:65] from the verified binary profile."""
    validate_image(image)
    offset = 16
    for key, length in ORDER:
        if key == '3f':
            raw = image[offset + 7:offset + 71]
            name, separator, padding = raw.partition(b'\0')
            if not name or not separator or any(padding):
                raise UploadError('Invalid lens model field')
            try:
                return name.decode('ascii')
            except UnicodeError as exc:
                raise UploadError('Lens model is not ASCII') from exc
        offset += length
    raise UploadError('Profile has no 0x3F frame')


class Device:
    def __init__(self, serial, verbose=False):
        self.serial = serial
        self.verbose = verbose
        self.recent = []

    def receive(self):
        raw = self.serial.readline()
        line = raw.decode('ascii', errors='replace').strip()
        if raw:
            self.recent.append(line)
            self.recent = self.recent[-12:]
            if self.verbose:
                print(f'RX: {raw!r}', file=sys.stderr, flush=True)
        if line.startswith('UNKNOWN_COMMAND'):
            raise UploadError('Firmware does not recognize the command; flash the current branch build')
        return line

    def timeout(self, expected):
        return UploadError(f'Timed out waiting for {expected}; recent RX: {self.recent!r}')

    def send(self, line):
        if self.verbose:
            print(f'TX: {line}', file=sys.stderr, flush=True)
        data = (line + '\n').encode('ascii')
        if self.serial.write(data) != len(data):
            raise UploadError('Incomplete serial write')
        self.serial.flush()

    def expect(self, prefix, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.receive()
            if line.startswith('PROFILE_ERROR='):
                raise UploadError(line)
            if line.startswith(prefix):
                return line
        raise self.timeout(prefix)

    def stop(self):
        self.send('x')
        self.expect('EMULATOR_MODE=OFF_ALL_CAMERA_OUTPUTS_HIGH_Z')
        # Wait for camera_link::stop() as well, not just emulator::stop().
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            line = self.receive()
            if line in ('CAMERA_NOT_ACTIVE', 'CAMERA=STOPPED_ALL_GPIO_INPUT'):
                return
        raise self.timeout('camera stop completion')

    def info(self):
        self.send('profile info')
        lines = []
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            line = self.receive()
            if line.startswith('PROFILE_ERROR='):
                raise UploadError(line + ' ' + ' '.join(lines))
            if line == 'PROFILE_INFO_END':
                return lines
            if line.startswith('PROFILE_'):
                lines.append(line)
        raise self.timeout('profile info')

    def read(self):
        result = bytearray()
        for offset in range(0, SIZE, CHUNK):
            self.send(f'profile read {offset:04x}')
            line = self.expect('PROFILE_DATA=')
            try:
                chunk = bytes.fromhex(line.split('=', 1)[1])
            except ValueError as exc:
                raise UploadError('Malformed readback') from exc
            if len(chunk) != min(CHUNK, SIZE - offset):
                raise UploadError('Readback chunk has incorrect length')
            result.extend(chunk)
        return bytes(result)

    def upload(self, image):
        validate_image(image)
        self.send('profile begin')
        self.expect('PROFILE_READY')
        for offset in range(0, SIZE, CHUNK):
            chunk = image[offset:offset + CHUNK]
            self.send(f'profile data {offset:04x} {chunk.hex()}')
            reply = self.expect('PROFILE_NEXT=')
            if reply != f'PROFILE_NEXT={offset + len(chunk)}':
                raise UploadError(f'Unexpected transfer acknowledgement: {reply}')
        self.send('profile commit')
        # Never automatically retry a commit; it may have completed.
        self.expect('PROFILE_WRITTEN', timeout=15)
        actual = self.read()
        validate_image(actual)
        if actual != image:
            raise UploadError('Flash readback differs from input')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=pathlib.Path, nargs='?', help='BIN to upload')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--info', action='store_true', help='Display NVM layout and profile state')
    mode.add_argument('--read', type=pathlib.Path, metavar='BIN', help='Save a valid stored profile')
    parser.add_argument('--verbose', action='store_true', help='Show raw USB CDC command/response traffic')
    parser.add_argument('--port', required=True, help='Application USB CDC port (not bootloader)')
    parser.add_argument('--detached', action='store_true',
                        help='Confirm the camera is physically disconnected')
    args = parser.parse_args()
    if sum((args.profile is not None, args.info, args.read is not None)) != 1:
        parser.error('Choose a profile BIN, --info, or --read BIN')
    if not args.detached:
        parser.error('Physically disconnect the camera and specify --detached')
    if args.read and args.read.exists():
        parser.error('Readback destination already exists')
    try:
        data = args.profile.read_bytes() if args.profile else None
        if data is not None:
            validate_image(data)
        try:
            import serial
        except ImportError as exc:
            raise UploadError('pyserial is required (available in the PlatformIO Python environment)') from exc
        with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=3) as port:
            device = Device(port, verbose=args.verbose)
            # CDC opening is not a 1200-baud bootloader touch.
            time.sleep(0.3)
            port.reset_input_buffer()
            try:
                device.stop()
                info = device.info()
                for line in info:
                    print(line)
                if args.info:
                    states = [line for line in info if line.startswith('PROFILE_VALID=')]
                    if states == ['PROFILE_VALID=1']:
                        print('PROFILE_LENS_MODEL=' + lens_model(device.read()))
                    elif states == ['PROFILE_VALID=0']:
                        print('PROFILE_LENS_MODEL=NONE (embedded fallback on next initialization)')
                    else:
                        raise UploadError('Missing or ambiguous PROFILE_VALID response')
                if data is not None:
                    device.upload(data)
                    print('PROFILE_UPLOAD=PASS (byte-for-byte readback verified)')
                elif args.read:
                    image = device.read()
                    validate_image(image)
                    with args.read.open('xb') as output:
                        output.write(image)
                    print(f'PROFILE_READ={args.read}')
            except (OSError, UploadError, KeyboardInterrupt):
                # Abort an unfinished RAM transfer, not a completed Flash write.
                try:
                    device.send('profile abort')
                except Exception:
                    pass
                raise
        print('CAMERA_EMULATION=OFF; reset or power-cycle XIAO before reconnecting camera')
        return 0
    except (OSError, UploadError, KeyboardInterrupt) as exc:
        print(f'PROFILE_UPLOAD_ERROR={exc}', file=sys.stderr)
        print('Do not reconnect the camera until the device has been reset and checked.', file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
