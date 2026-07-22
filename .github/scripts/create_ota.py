#!/usr/bin/env python3
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OTA_DIR = REPO_ROOT / "ota"
APP_CONFIG_PATH = REPO_ROOT / "Zigbee-Remote-_TYZB01_7qf81wty" / "app_config.h"

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
        "file_version": int(file_ver_match.group(1), 16)
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
    tag_data = create_ota_tag(0x0000, gbl_data) # Upgrade Image tag
    total_size = 56 + len(tag_data)
    header_data = create_ota_header(manufacturer_code, image_type, file_version, string_id, total_size)
    return header_data + tag_data

def main():
    s37_files = list(OTA_DIR.glob("*.s37")) + list(OTA_DIR.glob("*.hex")) + list(OTA_DIR.glob("*.bin"))
    if not s37_files:
        print("No .s37, .hex, or .bin files found in ota/ directory. Exiting.")
        return

    ver_info = parse_app_config()
    print(f"Parsed version: v{ver_info['major']}.{ver_info['minor']}.{ver_info['patch']} (0x{ver_info['file_version']:08X})")
    
    out_name = f"TS1001-CUS-v{ver_info['major']}.{ver_info['minor']}.{ver_info['patch']}.ota"
    out_path = OTA_DIR / out_name

    for build_file in s37_files:
        print(f"Processing {build_file.name}...")
        gbl_path = OTA_DIR / "temp.gbl"
        
        # 1. commander gbl create
        cmd = [
            "commander", "gbl", "create", str(gbl_path),
            "--app", str(build_file),
            "--compress", "lzma"
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Commander failed:\n{res.stderr}\n{res.stdout}")
            sys.exit(1)
            
        # 2. wrap gbl into ota
        gbl_data = gbl_path.read_bytes()
        ota_data = build_ota_file(
            gbl_data,
            manufacturer_code=0x1002,
            image_type=0x0000,
            file_version=ver_info["file_version"],
            string_id="TS1001-CUS"
        )
        
        out_path.write_bytes(ota_data)
        print(f"Created {out_name}")
        
        # 3. cleanup
        gbl_path.unlink()
        subprocess.run(["git", "rm", "-f", str(build_file)], check=False)
        if build_file.exists():
            build_file.unlink()

if __name__ == "__main__":
    main()
