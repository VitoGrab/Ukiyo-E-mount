#!/usr/bin/env python3
"""Select the default PlatformIO environment without rewriting other settings."""
import argparse
import configparser
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "platformio.ini"
ENVIRONMENTS = ("seeed_xiao", "seeed_xiao_production")


def default_environment(path=CONFIG):
    config = configparser.ConfigParser(interpolation=None)
    with path.open(encoding="utf-8") as source:
        config.read_file(source)
    env = config.get("platformio", "default_envs").strip()
    if env not in ENVIRONMENTS or not config.has_section("env:" + env):
        raise ValueError(f"Unsupported default_envs: {env}")
    return env


def select_environment(env, path=CONFIG):
    if env not in ENVIRONMENTS:
        raise ValueError(f"Unsupported build environment: {env}")
    # Validate the configuration and preserve the original INI formatting.
    previous = default_environment(path)
    if previous == env:
        return
    text = path.read_text(encoding="utf-8")
    section = re.search(r"(?m)^\[platformio\][ \t]*\n(?:(?!^\[).)*", text, re.S | re.M)
    if section is None:
        raise ValueError("Missing [platformio] section")
    updated, count = re.subn(r"(?m)^(default_envs[ \t]*=[ \t]*)[^\r\n]+",
                             lambda m: m.group(1) + env, section.group())
    if count != 1:
        raise ValueError("Expected exactly one default_envs setting")
    path.write_text(text[:section.start()] + updated + text[section.end():], encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("environment", choices=ENVIRONMENTS)
    args = parser.parse_args()
    try:
        select_environment(args.environment)
        print(f"BUILD_TYPE={default_environment()}")
        return 0
    except (OSError, ValueError, configparser.Error) as exc:
        print(f"BUILD_TYPE_ERROR={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
