#!/usr/bin/env python3
"""
bump_version.py — given a release tag like v1.0.16, update
Zigbee-Remote-_TYZB01_7qf81wty/app_config.h's version fields in place.

Run by release.yml's version-bump job before building: the resulting commit
is what actually gets compiled and released, so the version fields the
firmware reports (and the .ota file version Z2M compares against) always
match the tag that triggered the release.

FW_BUILD is deliberately NOT derived from the tag's patch number — it's read
from the current file and incremented by 1. app_config.h's own comment on
FW_BUILD explains why: "a single byte cannot encode all three [major/minor/
patch] without collisions (1.0.10 and 1.1.0 would tie)." Deriving it from
PATCH would silently break the monotonic-build invariant the OTA spec relies
on the next time a minor/major version resets PATCH to 0 (e.g. v1.0.15 ->
v1.1.0 would try to set FW_BUILD back to 0, which create_ota.py's OTA clients
would then refuse as "not newer").
"""
import argparse
import datetime
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
APP_CONFIG_PATH = REPO_ROOT / "Zigbee-Remote-_TYZB01_7qf81wty" / "app_config.h"
# Mirrored copy of FW_OTA_FILE_VERSION. app.c has a compile-time #if that
# fails the build if these two ever disagree (ota-client-policy.c policy
# rejects downloads otherwise) — both must be bumped together, every time.
OTA_POLICY_CONFIG_PATH = (REPO_ROOT / "Zigbee-Remote-_TYZB01_7qf81wty" / "config"
                           / "ota-client-policy-config.h")

TAG_RE = re.compile(r'^v(\d+)\.(\d+)\.(\d+)$')


def read_field(text, name, pattern=r'(\d+)'):
    m = re.search(rf'#define\s+{name}\s+{pattern}', text)
    if not m:
        sys.exit(f"could not find #define {name} in {APP_CONFIG_PATH}")
    return m


def replace_field(text, name, new_value, pattern=r'\d+', path=APP_CONFIG_PATH):
    new_text, n = re.subn(
        rf'(#define\s+{name}\s+){pattern}',
        rf'\g<1>{new_value}',
        text, count=1)
    if n != 1:
        sys.exit(f"failed to replace #define {name} in {path}")
    return new_text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True,
                         help="release tag, e.g. v1.0.16")
    parser.add_argument("--date", default=None,
                         help="YYYYMMDD build date (default: today, UTC)")
    args = parser.parse_args()

    m = TAG_RE.match(args.tag)
    if not m:
        sys.exit(f"tag '{args.tag}' does not match vMAJOR.MINOR.PATCH")
    major, minor, patch = (int(g) for g in m.groups())

    date_code = args.date or datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d")

    # newline='' on both read and write: preserve the file's existing line
    # endings byte-for-byte (it's tracked as LF) regardless of what platform
    # this script runs on — Path.read_text/write_text would otherwise
    # translate to the host's os.linesep on write.
    with open(APP_CONFIG_PATH, "r", encoding="utf-8", newline="") as fh:
        text = fh.read()

    current_build = int(read_field(text, "FW_BUILD").group(1))
    stack_rel = int(read_field(text, "FW_STACK_REL").group(1))
    stack_build = int(read_field(text, "FW_STACK_BUILD").group(1))
    current_file_version = int(
        read_field(text, "FW_OTA_FILE_VERSION", pattern=r'0x([0-9a-fA-F]+)UL').group(1), 16)
    legacy_floor = int(
        read_field(text, "FW_OTA_LAST_LEGACY_VERSION", pattern=r'0x([0-9a-fA-F]+)UL').group(1), 16)

    new_build = current_build + 1
    new_file_version = ((major & 0xFF) << 24) | ((new_build & 0xFF) << 16) \
        | ((stack_rel & 0xFF) << 8) | (stack_build & 0xFF)

    # Mirror the #if guards already in app_config.h so a bad bump fails here,
    # in CI, with a clear message — not as a compile error three steps later.
    if new_file_version <= current_file_version:
        sys.exit(f"REFUSING: new FW_OTA_FILE_VERSION 0x{new_file_version:08X} "
                  f"is not greater than the current 0x{current_file_version:08X}. "
                  f"No device would accept this as an update.")
    if new_file_version <= legacy_floor:
        sys.exit(f"REFUSING: new FW_OTA_FILE_VERSION 0x{new_file_version:08X} "
                  f"does not exceed FW_OTA_LAST_LEGACY_VERSION 0x{legacy_floor:08X}.")

    version_string = f"{major}.{minor}.{patch}"

    text = replace_field(text, "FW_VERSION_MAJOR", major)
    text = replace_field(text, "FW_VERSION_MINOR", minor)
    text = replace_field(text, "FW_VERSION_PATCH", patch)
    text = replace_field(text, "FW_VERSION_STRING", f'"{version_string}"',
                          pattern=r'"[^"]*"')
    text = replace_field(text, "FW_DATE_CODE", f'"{date_code}"',
                          pattern=r'"[^"]*"')
    text = replace_field(text, "FW_BUILD", new_build)
    text = replace_field(text, "FW_OTA_FILE_VERSION", f"0x{new_file_version:08X}UL",
                          pattern=r'0x[0-9a-fA-F]+UL')

    with open(APP_CONFIG_PATH, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)

    # Mirrored copy — see the comment on OTA_POLICY_CONFIG_PATH above.
    with open(OTA_POLICY_CONFIG_PATH, "r", encoding="utf-8", newline="") as fh:
        policy_text = fh.read()
    policy_text = replace_field(
        policy_text, "EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION",
        f"0x{new_file_version:08X}", pattern=r'0x[0-9a-fA-F]+',
        path=OTA_POLICY_CONFIG_PATH)
    with open(OTA_POLICY_CONFIG_PATH, "w", encoding="utf-8", newline="") as fh:
        fh.write(policy_text)

    print(f"Bumped to v{version_string}")
    print(f"  FW_BUILD:            {current_build} -> {new_build}")
    print(f"  FW_DATE_CODE:        {date_code}")
    print(f"  FW_OTA_FILE_VERSION: 0x{current_file_version:08X} -> 0x{new_file_version:08X}")
    print(f"  ota-client-policy-config.h EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION "
          f"updated to match")


if __name__ == "__main__":
    main()
