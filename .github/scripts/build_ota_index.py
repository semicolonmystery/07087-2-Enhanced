#!/usr/bin/env python3
"""
build_ota_index.py — emit the Zigbee2MQTT OTA index for one .ota image.

The index carries a single entry: the newest image. Z2M only ever needs the
newest one — it compares the entry's fileVersion against what the device
reports and offers the upgrade if it is greater.

Every field is read back out of the .ota file's own 56-byte header rather than
re-derived from app_config.h, so the index cannot disagree with the image it
describes.

The index is published as a GitHub Release asset and Z2M is pointed at
    https://github.com/<owner>/<repo>/releases/latest/download/index.json
which GitHub redirects to the newest release's copy. That URL is what makes
updates visible immediately: the old raw.githubusercontent.com path is served
through Fastly with Cache-Control: max-age=300, so Z2M could keep reading a
stale index for minutes after a push.

  build_ota_index.py <in.ota> --url <download-url> \
      --model-id TS1001_TYZB01_7qf81wty_Enhanced \
      --manufacturer-name DIY-Immax [--out index.json]
"""
import argparse
import hashlib
import json
import struct
import sys

OTA_HDR = struct.Struct("<I5HIH32sI")   # matches create_ota.py
OTA_MAGIC = 0x0BEEF11E


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("ota")
    p.add_argument("--url", required=True)
    p.add_argument("--model-id", required=True)
    p.add_argument("--manufacturer-name", required=True)
    p.add_argument("--out")
    a = p.parse_args()

    with open(a.ota, "rb") as f:
        data = f.read()

    (magic, _hver, _hlen, _fctl, manuf, image_type, file_version,
     _zbver, hstr, total_size) = OTA_HDR.unpack(data[:OTA_HDR.size])

    # Refuse to publish an index for something that is not a valid OTA image.
    if magic != OTA_MAGIC:
        sys.exit(f"{a.ota}: bad OTA magic 0x{magic:08X}, expected 0x{OTA_MAGIC:08X}")
    if total_size != len(data):
        sys.exit(f"{a.ota}: header totalImageSize {total_size} != actual size {len(data)}")

    entry = {
        "fileVersion": file_version,
        "fileSize": total_size,
        "manufacturerCode": manuf,
        "imageType": image_type,
        "sha512": hashlib.sha512(data).hexdigest(),
        "url": a.url,
        "modelId": a.model_id,
        "manufacturerName": [a.manufacturer_name],
    }

    header_string = hstr.split(b"\x00")[0].decode(errors="replace")
    print(f"header: mfg={manuf} imageType={image_type} "
          f"fileVersion=0x{file_version:08X} size={total_size} "
          f"headerString={header_string!r}")

    out = json.dumps([entry], indent=2)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(out + "\n")
        print(f"wrote {a.out}")
    else:
        print(out)


if __name__ == "__main__":
    main()
