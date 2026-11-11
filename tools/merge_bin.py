#!/usr/bin/env python3
import binascii
import json
import os
import struct
import subprocess
import sys

def ensure_valid_otadata(file_path):
    if not os.path.exists(file_path):
        return
    with open(file_path, "rb") as f:
        data = f.read()
    if len(data) >= 32 and data[:4] == b"\xff\xff\xff\xff":
        sector = bytearray(data)
        struct.pack_into("<I", sector, 0, 1)  # ota_seq = 1
        crc = binascii.crc32(struct.pack("<I", 1), 0xffffffff) % (1 << 32)
        struct.pack_into("<I", sector, 28, crc)
        with open(file_path, "wb") as f:
            f.write(sector)

def merge_firmware(build_dir="build", output_path=None):
    flasher_args_path = os.path.join(build_dir, "flasher_args.json")
    if not os.path.exists(flasher_args_path):
        print(f"Error: {flasher_args_path} not found.")
        sys.exit(1)

    with open(flasher_args_path, "r") as f:
        cfg = json.load(f)

    chip = cfg.get("extra_esptool_args", {}).get("chip", "esp32s3")
    flash_mode = cfg.get("flash_settings", {}).get("flash_mode", "dio")
    flash_size = cfg.get("flash_settings", {}).get("flash_size", "16MB")
    flash_freq = cfg.get("flash_settings", {}).get("flash_freq", "80m")

    if not output_path:
        output_path = os.path.join(build_dir, "eos-with-bootloader.bin")

    # Ensure target parent directory exists
    parent_dir = os.path.dirname(output_path)
    if parent_dir:
        os.makedirs(parent_dir, exist_ok=True)

    pad_arg = "--fill-flash-size"
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", chip,
        "merge_bin",
        "-o", output_path,
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq,
        "--flash_size", flash_size,
        pad_arg, flash_size,
    ]

    flash_files = cfg.get("flash_files", {})
    for offset in sorted(flash_files.keys(), key=lambda x: int(x, 0)):
        relpath = flash_files[offset]
        file_path = os.path.join(build_dir, relpath)
        if "ota_data" in relpath:
            ensure_valid_otadata(file_path)
        if not os.path.exists(file_path):
            print(f"Error: Binary file {file_path} not found.")
            sys.exit(1)
        cmd.extend([offset, file_path])

    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"Successfully generated merged firmware: {output_path}")

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else None
    bdir = sys.argv[2] if len(sys.argv) > 2 else "build"
    merge_firmware(bdir, out)
