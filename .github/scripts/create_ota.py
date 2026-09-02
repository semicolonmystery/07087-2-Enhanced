#!/usr/bin/env python3
"""
create_ota.py — turn the .s37 pushed into ota/ into a Zigbee OTA image.

Runs Simplicity Commander to compress the build into an LZMA .gbl, then wraps
that in the 56-byte Zigbee OTA header plus one Upgrade Image sub-element.

The result is written to dist/ and published as a GitHub Release asset — it is
deliberately NOT committed back into the repo. Serving the image and index from
raw.githubusercontent.com meant Z2M kept reading a stale index for up to five
minutes (Fastly sends Cache-Control: max-age=300 on raw URLs); release assets
plus the /releases/latest/download/ alias avoid that entirely.

The raw .s37 is git rm'd so the repo never accumulates build output.

Version comes from app_config.h. FW_OTA_FILE_VERSION must stay a plain hex
literal there — it is matched with a regex, not evaluated.
"""
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OTA_DIR = REPO_ROOT / "ota"
DIST_DIR = REPO_ROOT / "dist"
APP_CONFIG_PATH = REPO_ROOT / "Zigbee-Remote-_TYZB01_7qf81wty" / "app_config.h"

MANUFACTURER_CODE = 0x1002          # DIY-Immax
IMAGE_TYPE = 0x0000
HEADER_STRING = "TS1001_TYZB01_7qf81wty_Enhanced"


def parse_app_config():
    content = APP_CONFIG_PATH.read_text(encoding="utf-8")

    major_match = re.search(r'#define\s+FW_VERSION_MAJOR\s+(\d+)', content)
    minor_match = re.search(r'#define\s+FW_VERSION_MINOR\s+(\d+)', content)
    patch_match = re.search(r'#define\s+FW_VERSION_PATCH\s+(\d+)', content)
    file_ver_match = re.search(r'#define\s+FW_OTA_FILE_VERSION\s+0x([0-9a-fA-F]+)', content)

    if not all([major_match, minor_match, patch_match, file_ver_match]):
        print("Failed to parse version from app_config.h")
        sys.exit(1)

    return {
        "major": int(major_match.group(1)),
        "minor": int(minor_match.group(1)),
        "patch": int(patch_match.group(1)),
        "file_version": int(file_ver_match.group(1), 16),
    }


def create_ota_header(manufacturer_code, image_type, file_version, string_id, image_size):
    magic = 0x0BEEF11E
    header_version = 0x0100
    header_length = 56
    field_control = 0
    stack_version = 2
    header_string = string_id.encode('utf-8').ljust(32, b'\0')

    return struct.pack(
        "<I H H H H H I H 32s I",
        magic, header_version, header_length, field_control,
        manufacturer_code, image_type, file_version, stack_version,
        header_string, image_size
    )


def create_ota_tag(tag_id, tag_data):
    tag_length = len(tag_data)
    tag_header = struct.pack("<H I", tag_id, tag_length)
    return tag_header + tag_data


def build_ota_file(gbl_data, manufacturer_code, image_type, file_version, string_id):
    tag_data = create_ota_tag(0x0000, gbl_data)  # Upgrade Image tag
    total_size = 56 + len(tag_data)
    header_data = create_ota_header(manufacturer_code, image_type, file_version,
                                    string_id, total_size)
    return header_data + tag_data


def emit_outputs(**kwargs):
    """Expose values to later workflow steps."""
    out = os.environ.get("GITHUB_OUTPUT")
    for key, value in kwargs.items():
        print(f"{key}={value}")
    if out:
        with open(out, "a", encoding="utf-8") as fh:
            for key, value in kwargs.items():
                fh.write(f"{key}={value}\n")


def main():
    build_files = (sorted(OTA_DIR.glob("*.s37"))
                   + sorted(OTA_DIR.glob("*.hex"))
                   + sorted(OTA_DIR.glob("*.bin")))
    if not build_files:
        print("No .s37, .hex, or .bin files found in ota/. Nothing to release.")
        emit_outputs(released="false")
        return
    if len(build_files) > 1:
        print(f"Expected exactly one build file in ota/, found {len(build_files)}: "
              f"{[f.name for f in build_files]}")
        sys.exit(1)

    build_file = build_files[0]
    ver = parse_app_config()
    version = f"{ver['major']}.{ver['minor']}.{ver['patch']}"
    print(f"Parsed version: v{version} (0x{ver['file_version']:08X})")

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    out_name = f"TS1001_TYZB01_7qf81wty_Enhanced-v{version}.ota"
    out_path = DIST_DIR / out_name
    gbl_path = DIST_DIR / "temp.gbl"

    print(f"Processing {build_file.name}...")
    res = subprocess.run(
        ["commander", "gbl", "create", str(gbl_path),
         "--app", str(build_file), "--compress", "lzma"],
        capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Commander failed:\n{res.stderr}\n{res.stdout}")
        sys.exit(1)

    ota_data = build_ota_file(
        gbl_path.read_bytes(),
        manufacturer_code=MANUFACTURER_CODE,
        image_type=IMAGE_TYPE,
        file_version=ver["file_version"],
        string_id=HEADER_STRING,
    )
    out_path.write_bytes(ota_data)
    print(f"Created {out_name} ({len(ota_data)} bytes)")
    gbl_path.unlink()

    # The raw build output never stays in the repo.
    subprocess.run(["git", "rm", "-f", str(build_file)], check=False)
    if build_file.exists():
        build_file.unlink()

    emit_outputs(released="true",
                 version=version,
                 tag=f"v{version}",
                 ota_path=str(out_path.relative_to(REPO_ROOT)).replace("\\", "/"),
                 ota_name=out_name)


if __name__ == "__main__":
    main()
