#!/usr/bin/env python3
"""Build, preserve the selected profile, upload firmware, and verify restoration."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
import tempfile
from pathlib import Path

from upload_lens_profile import Device, UploadError, validate_image

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIO = "/opt/platformio/bin/pio"
DEFAULT_PORT = "/dev/ttyACM0"
RECOVERY_DIR = ROOT / ".ukiyo-recovery"
ALLOWED_ENVIRONMENTS = ("seeed_xiao", "seeed_xiao_production")


def resolve_pio(value: str) -> str:
    if os.path.sep in value:
        if Path(value).is_file():
            return value
    else:
        resolved = shutil.which(value)
        if resolved:
            return resolved

    fallback = shutil.which("pio")
    if fallback:
        return fallback
    raise FileNotFoundError(
        "PlatformIO CLI not found. Set PLATFORMIO_CLI or pass --pio."
    )


def run(command: list[str]) -> int:
    print("RUN: " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=ROOT, check=False).returncode


def profile_valid(device):
    lines = device.info()
    for line in lines:
        print(line, flush=True)
    values = [line for line in lines if line.startswith("PROFILE_VALID=")]
    if values not in (["PROFILE_VALID=0"], ["PROFILE_VALID=1"]):
        raise UploadError("Missing or ambiguous PROFILE_VALID response")
    return values == ["PROFILE_VALID=1"]


def backup_profile(device):
    if not profile_valid(device):
        print("PROFILE_BACKUP=NO_VALID_PROFILE", flush=True)
        return None
    image = device.read()
    validate_image(image)
    return image


def restore_profile(device, image, *, fresh=False):
    valid = profile_valid(device)
    if image is None:
        if valid and not fresh:
            raise UploadError("Unexpected valid profile after firmware upload; refusing to change it")
        print("PROFILE_RESTORE=NOT_REQUIRED", flush=True)
    else:
        if valid and device.read() == image:
            print("PROFILE_RESTORE=ALREADY_PRESENT", flush=True)
        else:
            device.upload(image)
            print("PROFILE_RESTORE=PASS (byte-for-byte verified)", flush=True)
        if not profile_valid(device) or device.read() != image:
            raise UploadError("Final profile verification failed")
        print("PROFILE_PRESERVED=PASS", flush=True)
    # The uploader stopped the application for maintenance. Restore the normal
    # auto-armed state only after all profile checks have passed.
    device.send("e")
    device.expect("EMULATOR_MODE=ARMED_WAITING_FOR_LOGIC_VCC_ON_A0")
    print("CAMERA_EMULATION=ARMED", flush=True)


def save_recovery(image):
    RECOVERY_DIR.mkdir(mode=0o700, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="profile-", dir=RECOVERY_DIR))
    path = directory / "profile.bin"
    with path.open("xb") as f:
        f.write(image)
        f.flush()
        os.fsync(f.fileno())
    # Persist the directory entry before permitting destructive firmware upload.
    for folder in (directory, RECOVERY_DIR, RECOVERY_DIR.parent):
        fd = os.open(folder, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    print(f"PROFILE_BACKUP={path}", flush=True)
    return path


def usb_identity(port):
    from serial.tools import list_ports
    for entry in list_ports.comports():
        if entry.device == port:
            # Some LXC USB passthrough configurations expose no USB metadata.
            return (entry.vid, entry.pid, entry.serial_number, entry.location)
    return (None, None, None, None)


def check_identity(port, expected):
    actual = usb_identity(port)
    if expected is not None:
        for old, new in zip(expected, actual):
            if old is not None and old != new:
                raise UploadError(f"USB application port identity changed: {expected!r} -> {actual!r}")


def open_application(port, expected=None, timeout=20):
    import serial
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        connection = None
        try:
            check_identity(port, expected)
            connection = serial.Serial(port, 115200, timeout=0.25,
                                       write_timeout=3, exclusive=True)
            time.sleep(0.3)
            connection.reset_input_buffer()
            device = Device(connection)
            device.stop()
            return connection, device
        except (OSError, UploadError) as exc:
            last_error = exc
            if connection is not None:
                connection.close()
            time.sleep(0.5)
    raise UploadError(f"Application CDC unavailable at {port}: {last_error}")


def flash_with_retry(pio, args):
    upload = [pio, "run", "-e", args.environment, "-t", "upload",
              "--upload-port", args.port]
    for attempt in range(1, args.attempts + 1):
        print(f"=== UPLOAD_ATTEMPT={attempt}/{args.attempts} ENV={args.environment} ===",
              flush=True)
        result = run(upload)
        if result == 0:
            return
        if attempt < args.attempts:
            print("Retrying firmware upload after transient bootloader failure",
                  file=sys.stderr, flush=True)
            time.sleep(1.0)
    raise UploadError(f"Firmware upload failed after {args.attempts} attempts")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", required=True, choices=ALLOWED_ENVIRONMENTS)
    parser.add_argument("--attempts", type=int, default=3)
    parser.add_argument("--pio", default=os.environ.get("PLATFORMIO_CLI", DEFAULT_PIO))
    parser.add_argument("--port", default=DEFAULT_PORT, help="Application CDC port")
    parser.add_argument("--detached", action="store_true", help="Camera physically disconnected")
    parser.add_argument("--fresh", action="store_true",
                        help="Explicitly skip profile backup for initial/recovery flashing; existing profile may be lost")
    args = parser.parse_args()
    if not 1 <= args.attempts <= 5:
        parser.error("--attempts must be between 1 and 5")
    if not args.detached:
        parser.error("Physically disconnect the camera and specify --detached")

    recovery = None
    upload_started = False
    try:
        pio = resolve_pio(args.pio)
        try:
            import serial
        except ImportError as exc:
            raise UploadError("Use the PlatformIO Python interpreter (pyserial required)") from exc

        verify = [sys.executable, str(ROOT / "tools/apply_lens_profile.py"), "--verify-only"]
        print("=== VERIFY GENERATED REFERENCE ===", flush=True)
        result = run(verify)
        if result:
            print("UPLOAD_ABORTED=Reference verification failed", file=sys.stderr)
            return result
        print(f"=== BUILD {args.environment} ===", flush=True)
        result = run([pio, "run", "-e", args.environment])
        if result:
            print("UPLOAD_ABORTED=Build failed; no upload was attempted", file=sys.stderr)
            return result

        image = None
        identity = None
        if args.fresh:
            print("PROFILE_BACKUP=EXPLICITLY_SKIPPED (--fresh)", flush=True)
        else:
            print("=== BACK UP ACTIVE PROFILE ===", flush=True)
            identity = usb_identity(args.port)
            connection, device = open_application(args.port, identity)
            with connection:
                image = backup_profile(device)
                if image is not None:
                    recovery = save_recovery(image)

        # Never automatically switch to fresh mode on a failed backup.
        upload_started = True
        flash_with_retry(pio, args)
        print("=== VERIFY / RESTORE ACTIVE PROFILE ===", flush=True)
        # Use the original explicit application port; do not scan other devices.
        connection, device = open_application(args.port, identity)
        with connection:
            restore_profile(device, image, fresh=args.fresh)
        if recovery is not None:
            try:
                recovery.unlink()
                recovery.parent.rmdir()
            except OSError as exc:
                print(f"PROFILE_BACKUP_CLEANUP_WARNING={exc}", file=sys.stderr)
        print("UPLOAD_RESULT=SUCCESS", flush=True)
        return 0
    except (OSError, UploadError, KeyboardInterrupt) as exc:
        print(f"UPLOAD_ERROR={exc}", file=sys.stderr)
        print(f"FIRMWARE_UPLOAD_STARTED={int(upload_started)}", file=sys.stderr)
        if recovery is not None:
            print(f"PROFILE_RECOVERY_FILE={recovery}", file=sys.stderr)
            print(f"Restore explicitly with tools/upload_lens_profile.py --port {args.port} --detached {recovery}",
                  file=sys.stderr)
        elif not upload_started:
            print("No firmware erase/upload attempted. For an old firmware or bootloader-only device use --fresh only if profile loss is acceptable.",
                  file=sys.stderr)
        print("Keep camera detached until the device is checked.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
