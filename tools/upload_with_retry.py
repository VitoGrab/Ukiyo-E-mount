#!/usr/bin/env python3
"""Build once, then retry XIAO SAM-BA uploads after transient reset failures."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIO = "/opt/platformio/bin/pio"
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--environment",
        required=True,
        choices=ALLOWED_ENVIRONMENTS,
        help="PlatformIO environment to build and upload",
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
    args = parser.parse_args()

    if not 1 <= args.attempts <= 5:
        parser.error("--attempts must be between 1 and 5")

    try:
        pio = resolve_pio(args.pio)
    except FileNotFoundError as exc:
        print("UPLOAD_ERROR=" + str(exc), file=sys.stderr)
        return 2

    verify = [
        sys.executable,
        str(ROOT / "tools" / "apply_lens_profile.py"),
        "--verify-only",
    ]
    print("=== VERIFY GENERATED LENS PROFILE ===", flush=True)
    result = run(verify)
    if result:
        print("UPLOAD_ABORTED=Lens profile verification failed", file=sys.stderr)
        return result

    build = [pio, "run", "-e", args.environment]
    print("=== BUILD " + args.environment + " ===", flush=True)
    result = run(build)
    if result:
        print("UPLOAD_ABORTED=Build failed; no upload was attempted", file=sys.stderr)
        return result

    upload = [pio, "run", "-e", args.environment, "-t", "upload"]
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
