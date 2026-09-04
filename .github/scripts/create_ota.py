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
    slot_start = re.search(r'#define\s+OTA_SLOT0_START\s+(\d+)', content)
    slot_end = re.search(r'#define\s+OTA_SLOT0_END\s+(\d+)', content)

    if not all([major_match, minor_match, patch_match, file_ver_match,
                slot_start, slot_end]):
        print("Failed to parse version/slot geometry from app_config.h")
        sys.exit(1)

    return {
        "major": int(major_match.group(1)),
        "minor": int(minor_match.group(1)),
        "patch": int(patch_match.group(1)),
        "file_version": int(file_ver_match.group(1), 16),
        "slot_start": int(slot_start.group(1)),
        "slot_end": int(slot_end.group(1)),
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


def flashed_size(path):
    """Highest flash address touched by a build file.

    Handles Motorola S-records (S1/S2/S3 - Studio emits S2 for this part, with
    24-bit addresses) and Intel HEX (record 00 data, 02 extended segment,
    04 extended linear). Getting the record type wrong here would silently
    report 0 and let an oversized app through the gate below, so the parser
    must fail loudly rather than return a small number.
    """
    top = 0
    seen_data = False
    base = 0
    with open(path, "r", encoding="ascii", errors="ignore") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            if line[0] == "S" and len(line) > 4:
                kind = line[1]
                if kind not in "123":
                    continue                      # S0 header, S7/S8/S9 term
                addr_len = {"1": 4, "2": 6, "3": 8}[kind]
                count = int(line[2:4], 16)
                addr = int(line[4:4 + addr_len], 16)
                nbytes = count - (addr_len // 2) - 1
                if nbytes > 0:
                    top = max(top, addr + nbytes)
                    seen_data = True
            elif line[0] == ":" and len(line) >= 11:
                count = int(line[1:3], 16)
                addr = int(line[3:7], 16)
                rectype = int(line[7:9], 16)
                if rectype == 0x00:
                    top = max(top, base + addr + count)
                    seen_data = True
                elif rectype == 0x02:             # extended segment address
                    base = int(line[9:13], 16) << 4
                elif rectype == 0x04:             # extended linear address
                    base = int(line[9:13], 16) << 16
    if not seen_data:
        sys.exit("could not parse any data records from %s - refusing to "
                 "release without a verified app size" % path)
    return top


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
    # The .s37 is the GBL input; the .hex (if pushed alongside it) is published
    # as-is so a Raspberry Pi can flash over the wire without Studio, which is
    # x86_64 only. Studio emits both from every build.
    s37_files = sorted(OTA_DIR.glob("*.s37")) + sorted(OTA_DIR.glob("*.bin"))
    hex_files = sorted(OTA_DIR.glob("*.hex"))
    if not s37_files:
        print("No .s37 or .bin file found in ota/. Nothing to release.")
        emit_outputs(released="false")
        return
    if len(s37_files) > 1:
        print(f"Expected exactly one .s37/.bin in ota/, found {len(s37_files)}: "
              f"{[f.name for f in s37_files]}")
        sys.exit(1)
    if len(hex_files) > 1:
        print(f"Expected at most one .hex in ota/, found {len(hex_files)}: "
              f"{[f.name for f in hex_files]}")
        sys.exit(1)

    build_file = s37_files[0]
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
         "--app", str(build_file), "--compress", "lz4"],
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

    # Two hard gates. Neither failure mode is caught anywhere else:
    #
    #  1. autogen/linkerfile.ld gives the app the whole 512 KB (ORIGIN 0x0,
    #     LENGTH 0x80000) and knows nothing about the OTA slot, so an app that
    #     grows past OTA_SLOT0_START links cleanly and then overwrites the slot
    #     on the device.
    #  2. The image must fit the slot, or the download dies near the end.
    slot_start, slot_end = ver["slot_start"], ver["slot_end"]
    slot_size = slot_end - slot_start
    app_bytes = flashed_size(build_file)
    print(f"app image {app_bytes} B vs slot start {slot_start} "
          f"(0x{slot_start:X}); .ota {len(ota_data)} B vs slot {slot_size} B")
    if app_bytes > slot_start:
        sys.exit(f"REFUSING: app is {app_bytes} bytes and would run past the OTA "
                 f"slot start at {slot_start} (0x{slot_start:X}) by "
                 f"{app_bytes - slot_start} bytes. The linker does NOT catch this.")
    if len(ota_data) > slot_size:
        sys.exit(f"REFUSING: .ota is {len(ota_data)} bytes but slot 0 holds only "
                 f"{slot_size}. Over by {len(ota_data) - slot_size}.")

    # Publish the wired-flash image under the same versioned name as the .ota.
    hex_name = ""
    hex_path = ""
    if hex_files:
        hex_out = DIST_DIR / f"TS1001_TYZB01_7qf81wty_Enhanced-v{version}.hex"
        hex_out.write_bytes(hex_files[0].read_bytes())
        hex_name = hex_out.name
        hex_path = str(hex_out.relative_to(REPO_ROOT)).replace("\\", "/")
        print(f"Staged {hex_name} ({hex_out.stat().st_size} bytes) for release")
    else:
        print("No .hex pushed — releasing the .ota only. "
              "Push the Studio .hex alongside the .s37 to publish a wired-flash image.")

    # The raw build output never stays in the repo.
    for f in [build_file] + hex_files:
        subprocess.run(["git", "rm", "-f", str(f)], check=False)
        if f.exists():
            f.unlink()

    emit_outputs(released="true",
                 version=version,
                 tag=f"v{version}",
                 ota_path=str(out_path.relative_to(REPO_ROOT)).replace("\\", "/"),
                 ota_name=out_name,
                 hex_path=hex_path,
                 hex_name=hex_name)


if __name__ == "__main__":
    main()
