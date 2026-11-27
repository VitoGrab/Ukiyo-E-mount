#!/usr/bin/env python3
"""Build and retry XIAO SAM-BA firmware uploads using row-local erase.

The PlatformIO post script disables explicit full erase. No application CDC
commands or lens-profile reads/writes are needed for a firmware update.
"""
from __future__ import annotations

import argparse
import configparser
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIO = "/opt/platformio/bin/pio"
from select_build_type import ENVIRONMENTS as ALLOWED_ENVIRONMENTS, default_environment


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--environment",
        choices=ALLOWED_ENVIRONMENTS,
        help="Override platformio.ini default_envs for this upload",
    )
    parser.add_argument(
        "--attempts",
        type=int,
        default=3,
        help="Maximum upload attempts after the initial build (default: 3)",
    )
    parser.add_argument(
        "--pio",
        default=os.environ.get("PLATFORMIO_CLI", DEFAULT_PIO),
        help="PlatformIO CLI path (default: /opt/platformio/bin/pio)",
    )
    parser.add_argument("--port", help="Override the configured PlatformIO upload port")
    args = parser.parse_args()

    if not 1 <= args.attempts <= 5:
        parser.error("--attempts must be between 1 and 5")

    try:
        pio = resolve_pio(args.pio)
        args.environment = args.environment or default_environment()
    except (OSError, ValueError, configparser.Error) as exc:
        print("UPLOAD_ERROR=" + str(exc), file=sys.stderr)
        return 2

    prepare = [
        sys.executable,
        str(ROOT / "tools" / "prepare_lens_profiles.py"),
    ]
    print("=== PREPARE LENS PROFILES ===", flush=True)
    result = run(prepare)
    if result:
        print("UPLOAD_ABORTED=Lens profile preparation failed", file=sys.stderr)
        return result

    build = [pio, "run", "-e", args.environment]
    print("=== BUILD " + args.environment + " ===", flush=True)
    result = run(build)
    if result:
        print("UPLOAD_ABORTED=Build failed; no upload was attempted", file=sys.stderr)
        return result

    upload = [pio, "run", "-e", args.environment, "-t", "upload"]
    if args.port:
        upload.extend(["--upload-port", args.port])
    for attempt in range(1, args.attempts + 1):
        print(
            "=== UPLOAD_ATTEMPT=%d/%d ENV=%s ==="
            % (attempt, args.attempts, args.environment),
            flush=True,
        )
        result = run(upload)
        if result == 0:
            print("UPLOAD_RESULT=SUCCESS", flush=True)
            return 0

        if attempt < args.attempts:
            print(
                "UPLOAD_RETRY=%d; a fresh PlatformIO 1200-baud reset "
                "and bootloader-port discovery will be attempted."
                % (attempt + 1),
                file=sys.stderr,
                flush=True,
            )
            time.sleep(1.0)

    print(
        "UPLOAD_RESULT=FAILED_AFTER_%d_ATTEMPTS" % args.attempts,
        file=sys.stderr,
    )
    return result or 1


if __name__ == "__main__":
    raise SystemExit(main())
