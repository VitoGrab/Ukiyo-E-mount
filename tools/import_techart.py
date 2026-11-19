#!/usr/bin/env python3
"""Reproducibly vendor the MIT Techart protocol core for the XIAO port.

Source code is copied, not reimplemented. Exactly enumerated peripheral access
sites are adapted to the Ukiyo HAL. Unknown future upstream changes fail closed.
"""
from __future__ import annotations
import argparse
import pathlib
import re
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGET = ROOT / "lib" / "ukiyo_techart" / "src"
FILES = ["emount.c", "emount.h", "emount_packets.c", "emount_packets.h",
         "optical_file.c", "optical_file.h", "em_uart.h", "em_eic.h", "diag.h"]


def replace_exact(s: str, before: str, after: str, count: int) -> str:
    found = s.count(before)
    if found != count:
        raise RuntimeError(f"upstream changed: expected {count} occurrences of {before!r}, got {found}")
    return s.replace(before, after)


def prepare(source: pathlib.Path) -> None:
    missing = [s for s in FILES if not (source / "src" / s).is_file()]
    if missing:
        raise RuntimeError(f"Missing Techart source files in {source}: {missing}")
    if not (source / "LICENSE").is_file():
        raise RuntimeError("Techart LICENSE is required")

    raw = {name: (source / "src" / name).read_text() for name in FILES}
    engine = raw["emount.c"]
    # We cannot use the EA9's PA00/PA01/PA02/PA03/PA10 register addresses.
    # All substituted calls use the *Arduino* XIAO D6..D10 pin mapping.
    edits = {
        'PORT->Group[0].OUTSET.reg = (1u << EM_PIN_LENS_CS);': 'ukiyo_gpio_lens_cs(1);',
        'PORT->Group[0].OUTCLR.reg = (1u << EM_PIN_LENS_CS);': 'ukiyo_gpio_lens_cs(0);',
        'PORT->Group[0].PINCFG[EM_PIN_BODY_CS].reg = PORT_PINCFG_INEN;': 'ukiyo_gpio_body_cs_input();',
        'PORT->Group[0].PINCFG[EM_PIN_BODY_VD].reg = PORT_PINCFG_INEN;': 'ukiyo_gpio_body_vd_input();',
        'PORT->Group[0].OUTCLR.reg = (1u << EM_PIN_TX);': 'ukiyo_gpio_tx_low();',
        'PORT->Group[0].DIRSET.reg = (1u << EM_PIN_TX);': 'ukiyo_gpio_tx_output();',
        'PORT->Group[0].PINCFG[EM_PIN_TX].reg &= ~(uint8_t)PORT_PINCFG_PMUXEN;': 'ukiyo_gpio_tx_unmux();',
    }
    for old, new in edits.items():
        occurrences = engine.count(old)
        if occurrences == 0:
            raise RuntimeError(f"Techart emount.c no longer contains known peripheral call: {old}")
        engine = engine.replace(old, new)
    if re.search(r'PORT\s*->', engine):
        raise RuntimeError("Unported direct PORT access remains in emount.c")

    engine = engine.replace('#include "board.h"', '#include "board.h"\n#include "ukiyo_hal.h"', 1)
    a = engine.index('void em_init(void)')
    b = engine.index('/* Message 0x05 pl[77..80]', a)
    boot = engine[a:b]
    boot = replace_exact(boot, 'void em_init(void)\n{',
                         'void em_init(void)\n{\n\tem_t_init_done = 0;\n\tuint32_t ukiyo_boot_start_ms = millis();', 1)
    boot = replace_exact(boot,
        'while (!em_pin_level(EM_PIN_BODY_CS)) {',
        'while (!em_pin_level(EM_PIN_BODY_CS)) {\n'
        '\t\t\tif (!ukiyo_supply_ok() || (uint32_t)(millis() - ukiyo_boot_start_ms) > 10000u) { ukiyo_bus_abort(); return; }', 1)
    boot = replace_exact(boot,
        'while (em_pin_level(EM_PIN_BODY_CS)) {\n\t}',
        'while (em_pin_level(EM_PIN_BODY_CS)) {\n'
        '\t\tif (!ukiyo_supply_ok() || (uint32_t)(millis() - ukiyo_boot_start_ms) > 10000u) { ukiyo_bus_abort(); return; }\n\t}', 1)
    boot = replace_exact(boot, '\tdelay_ms(50);',
        '\tfor (uint8_t wait_i = 0; wait_i < 50; ++wait_i) {\n'
        '\t\tif (!ukiyo_supply_ok()) { ukiyo_bus_abort(); return; }\n'
        '\t\tdelay_ms(1);\n\t}', 1)
    engine = engine[:a] + boot + engine[b:]
    raw["emount.c"] = engine

    # The camera's reported metadata is set in Techart's own constant-driven
    # templates. Start with Helios 44M-4, 58mm f/2.0, fixed reported aperture.
    hdr = raw["emount.h"]
    hdr = replace_exact(hdr, '#define EM_FOCAL_MM10 500u', '#define EM_FOCAL_MM10 580u', 1)
    for key in ('MAX', 'MIN', 'NOW'):
        hdr, count = re.subn(rf'(#define\s+EM_APERTURE_{key}\s+)0x11C0u',
                             r'\g<1>0x1200u', hdr)
        if count != 1:
            raise RuntimeError(f"Cannot set EM_APERTURE_{key}, match count={count}")
    raw["emount.h"] = hdr
    raw["diag.h"] = '#ifndef EA9_NO_LOG\n#define EA9_NO_LOG 1\n#endif\n' + raw["diag.h"]

    TARGET.mkdir(parents=True, exist_ok=True)
    for name, content in raw.items():
        (TARGET / name).write_text(content)
    shutil.copy2(source / "LICENSE", ROOT / "lib" / "ukiyo_techart" / "LICENSE")
    commit = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'],
                            capture_output=True, text=True)
    sha = commit.stdout.strip() if commit.returncode == 0 else 'local-source-no-git-hash'
    (ROOT / "lib" / "ukiyo_techart" / "UPSTREAM.txt").write_text(
        'Upstream: https://github.com/weiziqian/E-mount-impl-on-Techart-EA9\n'
        f'Commit: {sha}\n'
        'License: MIT (see LICENSE)\n'
        'Modifications: GPIO abstraction, power-fail init exits, Helios 58mm f/2 constants.\n'
        'Original packet tables, parser, reply queue, status engine retained.\n'
    )
    # This file activates the implementation in the existing command handler.
    (ROOT / 'include' / 'ukiyo-e-mount' / 'vendor_status.h').write_text(
        '#pragma once\n#define UKIYO_TECHART_VENDOR_PRESENT 1\n')
    print('TECHART_IMPORT=PASS')
    print('UPSTREAM_COMMIT=' + sha)
    print('PORT_GPIO_ACCESSES=REPLACED')
    print('MANUAL_OPTICS=HELIOS_58MM_F2_FIXED_APERTURE')
    print('AF_CAPABILITY_BITMAP=UPSTREAM_UNVERIFIED')
    print('BUILD_VERIFICATION=STILL_REQUIRED')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=pathlib.Path,
                        help='An existing checked-out Techart repository; no network needed')
    parser.add_argument('--ref', default='main',
                        help='Git ref to clone when --source is omitted (pin a commit for release)')
    args = parser.parse_args()
    if args.source:
        prepare(args.source.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='techart-upstream-') as td:
            src = pathlib.Path(td) / 'upstream'
            subprocess.run(['git', 'clone', '--quiet',
                            'https://github.com/weiziqian/E-mount-impl-on-Techart-EA9.git',
                            str(src)], check=True)
            subprocess.run(['git', '-C', str(src), 'checkout', '--quiet', args.ref], check=True)
            prepare(src)


if __name__ == '__main__':
    main()
