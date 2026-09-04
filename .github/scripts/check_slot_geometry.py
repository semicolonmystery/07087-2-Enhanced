#!/usr/bin/env python3
"""
check_slot_geometry.py — fail loudly if the OTA slot geometry drifts between
the bootloader's .slcp (what `slc generate` compiles into the bootloader) and
app_config.h (what create_ota.py's release gates and the app itself use).

Why this exists: v1.0.15 moved the OTA slot from 0x44000/192KB to
0x40000/220KB (LZ4 needed a bigger slot than LZMA would have). The generated
bootloader config headers (config/btl_storage_slot_cfg.h, btl_storage_cfg.h)
were updated to match, but bootloader-storage-internal-single-512k.slcp's own
`configuration:` block was not — so a from-source build driven by the .slcp
would have silently regenerated the pre-fix, already-broken geometry. This
script is the standing check that stops that from happening again, silently,
the next time only one side of this gets edited.

Two modes:
  - Default: compares the checked-in .slcp against app_config.h. Needs no
    tools installed — cheap enough to run before slc-cli exists on a runner.
  - --generated-config <path/to/btl_storage_slot_cfg.h>: additionally
    compares what slc actually generated against app_config.h, catching any
    drift introduced by the SDK/slc-cli itself rather than by this repo.

Exit status is the only contract CI relies on: 0 means every source agrees,
non-zero means don't ship this build.
"""
import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
APP_CONFIG_PATH = REPO_ROOT / "Zigbee-Remote-_TYZB01_7qf81wty" / "app_config.h"
BOOTLOADER_SLCP_PATH = (REPO_ROOT / "bootloader-storage-internal-single-512k"
                         / "bootloader-storage-internal-single-512k.slcp")

# This project's part is Series 1 (EFR32MG13P732F512GM48), so the .slcp
# entries that matter are the ones conditioned on device_series_1. If the
# part ever changes to a Series 2 device, the sdid_2xx conditions (a
# different flash-base offset scheme) would need to be checked instead.
SLCP_CONDITION = "device_series_1"


def parse_app_config():
    text = APP_CONFIG_PATH.read_text(encoding="utf-8")
    start = re.search(r'#define\s+OTA_SLOT0_START\s+(\d+)', text)
    end = re.search(r'#define\s+OTA_SLOT0_END\s+(\d+)', text)
    if not (start and end):
        sys.exit(f"could not parse OTA_SLOT0_START/END from {APP_CONFIG_PATH}")
    return int(start.group(1)), int(end.group(1))


def parse_slcp_slot(text):
    """Extract SLOT0_START / SLOT0_SIZE / BTL_STORAGE_BASE_ADDRESS for
    SLCP_CONDITION, and the unconditioned SLOT0_SIZE default, from the
    .slcp's `configuration:` YAML block using plain regex — this file's
    structure is fixed enough (one repeated 3-line pattern) that pulling in
    a YAML dependency isn't worth it, matching create_ota.py's own approach
    of regex-parsing generated config rather than requiring extra tooling.
    """
    # Unconditioned default, e.g.: - {name: SLOT0_SIZE, value: '225280'}
    default_size = re.search(
        r"\{name:\s*SLOT0_SIZE,\s*value:\s*'(\d+)'\}", text)

    # Conditioned entries look like:
    #   - condition: [device_series_1]
    #     name: SLOT0_START
    #     value: '262144'
    cond_block = re.compile(
        r"-\s*condition:\s*\[" + re.escape(SLCP_CONDITION) + r"\]\s*\n"
        r"\s*name:\s*(\w+)\s*\n"
        r"\s*value:\s*'(\d+)'"
    )
    values = {}
    for m in cond_block.finditer(text):
        name, value = m.group(1), int(m.group(2))
        # Multiple entries can share a name only if they agree; first wins,
        # later ones are cross-checked.
        if name in values and values[name] != value:
            sys.exit(f"{BOOTLOADER_SLCP_PATH}: conflicting '{name}' values "
                      f"under condition [{SLCP_CONDITION}]: "
                      f"{values[name]} vs {value}")
        values[name] = value

    if not default_size:
        sys.exit(f"could not find an unconditioned SLOT0_SIZE in {BOOTLOADER_SLCP_PATH}")
    if "SLOT0_START" not in values:
        sys.exit(f"could not find SLOT0_START under condition [{SLCP_CONDITION}] "
                  f"in {BOOTLOADER_SLCP_PATH}")
    if "BTL_STORAGE_BASE_ADDRESS" not in values:
        sys.exit(f"could not find BTL_STORAGE_BASE_ADDRESS under condition "
                  f"[{SLCP_CONDITION}] in {BOOTLOADER_SLCP_PATH}")

    return {
        "SLOT0_START": values["SLOT0_START"],
        "SLOT0_SIZE": int(default_size.group(1)),
        "BTL_STORAGE_BASE_ADDRESS": values["BTL_STORAGE_BASE_ADDRESS"],
    }


def parse_generated_header(path):
    text = Path(path).read_text(encoding="utf-8")
    start = re.search(r'#define\s+SLOT0_START\s+(\d+)', text)
    size = re.search(r'#define\s+SLOT0_SIZE\s+(\d+)', text)
    if not (start and size):
        sys.exit(f"could not parse SLOT0_START/SLOT0_SIZE from {path}")
    return int(start.group(1)), int(size.group(1))


def check(label, checks):
    ok = True
    for name, expected, actual in checks:
        status = "OK  " if expected == actual else "FAIL"
        if expected != actual:
            ok = False
        print(f"  {status} {name}: expected {expected}, got {actual}")
    print(f"{'PASS' if ok else 'FAIL'}: {label}")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--generated-config",
        help="path to the slc-generated config/btl_storage_slot_cfg.h to "
             "additionally cross-check (optional; run after `slc generate`)")
    args = parser.parse_args()

    ota_start, ota_end = parse_app_config()
    expected_size = ota_end - ota_start
    print(f"app_config.h: OTA_SLOT0_START={ota_start} (0x{ota_start:X}), "
          f"OTA_SLOT0_END={ota_end} (0x{ota_end:X}), "
          f"implied size={expected_size} (0x{expected_size:X})")

    slcp = parse_slcp_slot(BOOTLOADER_SLCP_PATH.read_text(encoding="utf-8"))
    print(f".slcp [{SLCP_CONDITION}]: SLOT0_START={slcp['SLOT0_START']}, "
          f"SLOT0_SIZE={slcp['SLOT0_SIZE']}, "
          f"BTL_STORAGE_BASE_ADDRESS={slcp['BTL_STORAGE_BASE_ADDRESS']}")

    all_ok = check(".slcp vs app_config.h", [
        ("SLOT0_START", ota_start, slcp["SLOT0_START"]),
        ("SLOT0_SIZE", expected_size, slcp["SLOT0_SIZE"]),
        ("BTL_STORAGE_BASE_ADDRESS", ota_start, slcp["BTL_STORAGE_BASE_ADDRESS"]),
    ])

    if args.generated_config:
        gen_start, gen_size = parse_generated_header(args.generated_config)
        print(f"generated {args.generated_config}: SLOT0_START={gen_start}, "
              f"SLOT0_SIZE={gen_size}")
        all_ok &= check("generated config vs app_config.h", [
            ("SLOT0_START", ota_start, gen_start),
            ("SLOT0_SIZE", expected_size, gen_size),
        ])

    if not all_ok:
        sys.exit(
            "\nSlot geometry disagrees between sources. This is exactly the "
            "class of bug that shipped a broken OTA slot in the past "
            "(pre-v1.0.15): fix whichever side is stale before building — "
            "see docs/BUILD.md's flash memory map for the current ground "
            "truth.")
    print("\nSlot geometry is consistent.")


if __name__ == "__main__":
    main()
