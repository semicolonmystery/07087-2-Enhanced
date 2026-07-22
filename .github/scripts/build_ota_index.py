#!/usr/bin/env python3
"""Rebuild ota/index.json from the .ota files in the ota/ directory.

Scans every ota/*.ota file, parses its Zigbee OTA header, and writes a
pretty-printed JSON array (sorted ascending by fileVersion) describing each
image plus the raw GitHub URL it can be downloaded from. Run by
.github/workflows/ota-index.yml whenever a .ota file is pushed to main.
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OTA_DIR = REPO_ROOT / "ota"
INDEX_PATH = OTA_DIR / "index.json"

OTA_MAGIC = 0x0BEEF11E
RAW_URL_TEMPLATE = (
    "https://raw.githubusercontent.com/semicolonmystery/07087-2-Enhanced/"
    "main/ota/{filename}"
)
MODEL_ID = "TS1001_TYZB01_7qf81wty_Enhanced"


def parse_ota_header(data: bytes):
    """Parse the fixed portion of a Zigbee OTA file header.

    Layout (little-endian):
      uint32 magic            @ offset 0   (must be 0x0BEEF11E)
      uint16 manufacturerCode @ offset 10
      uint16 imageType        @ offset 12
      uint32 fileVersion      @ offset 14
    """
    if len(data) < 18:
        return None

    (magic,) = struct.unpack_from("<I", data, 0)
    if magic != OTA_MAGIC:
        return None

    (manufacturer_code,) = struct.unpack_from("<H", data, 10)
    (image_type,) = struct.unpack_from("<H", data, 12)
    (file_version,) = struct.unpack_from("<I", data, 14)

    return {
        "manufacturerCode": manufacturer_code,
        "imageType": image_type,
        "fileVersion": file_version,
    }


def build_entry(path: Path):
    data = path.read_bytes()
    header = parse_ota_header(data)
    if header is None:
        print(f"skipping {path.name}: not a valid Zigbee OTA file", file=sys.stderr)
        return None

    sha512 = hashlib.sha512(data).hexdigest()

    return {
        "fileVersion": header["fileVersion"],
        "fileSize": len(data),
        "manufacturerCode": header["manufacturerCode"],
        "imageType": header["imageType"],
        "sha512": sha512,
        "url": RAW_URL_TEMPLATE.format(filename=path.name),
        "modelId": MODEL_ID,
    }


def main():
    entries = []
    for path in sorted(OTA_DIR.glob("*.ota")):
        entry = build_entry(path)
        if entry is not None:
            entries.append(entry)

    entries.sort(key=lambda e: e["fileVersion"])

    INDEX_PATH.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {INDEX_PATH} with {len(entries)} entr{'y' if len(entries) == 1 else 'ies'}")


if __name__ == "__main__":
    main()
