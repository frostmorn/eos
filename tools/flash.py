#!/usr/bin/env python3
"""
EOS Automated Flashing Helper
Supports seamless flashing in TinyUSB Device mode (303a:4001-4003) via 1200-baud CDC touch,
USB-Serial-JTAG mode (303a:1001), and ROM Download mode (303a:0009).
"""

import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board_detect


def main():
    chip = board_detect.get_target_chip()
    port = sys.argv[1] if len(sys.argv) > 1 else None
    pid = None

    if port:
        _, pid = board_detect.get_usb_ids(port)
    else:
        devs = [d for d in board_detect.enumerate_espressif_usb()
                if d["tty_ports"] and (d["chip"] == chip or d["chip"] is None)]
        for preferred in ("rom_download", "tinyusb_gadget", "debug_jtag"):
            for d in devs:
                if d["role"] == preferred:
                    port = d["tty_ports"][0]
                    pid = d["pid"]
                    break
            if port:
                break

    if not port:
        print("No Espressif USB port found. Falling back to idf.py flash...")
        sys.exit(subprocess.call(["idf.py", "flash"]))

    if pid in board_detect.TINYUSB_PIDS:
        print(f"Target on {port} is in TinyUSB mode (PID {pid}); triggering 1200-baud bootloader reset...")
        if not board_detect.trigger_1200_bootloader(port):
            print(f"Error: 1200-baud bootloader touch failed on {port}")
            sys.exit(1)
        entered_rom = False
        deadline = time.time() + 5.0
        while time.time() < deadline:
            time.sleep(0.25)
            for d in board_detect.enumerate_espressif_usb():
                if d["pid"] == board_detect.ROM_DOWNLOAD_PID and d["tty_ports"]:
                    port = d["tty_ports"][0]
                    pid = d["pid"]
                    entered_rom = True
                    break
            if entered_rom:
                break
        if not entered_rom:
            print("Error: Target did not re-enumerate in ROM download mode (303a:0009) within 5s.")
            sys.exit(1)

    build_dir = os.path.abspath("build")
    if not os.path.exists(os.path.join(build_dir, "flash_args")):
        print("Error: build/flash_args not found. Run 'make build' first.")
        sys.exit(1)

    before_mode = "no-reset" if pid == board_detect.ROM_DOWNLOAD_PID else "default-reset"
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", chip,
        "-p", port,
        "-b", "460800",
        "--before", before_mode,
        "--after", "watchdog-reset",
        "write-flash", "@flash_args",
    ]
    print(f"Flashing {chip} on {port} (before={before_mode})...")
    rc = subprocess.call(cmd, cwd=build_dir)
    sys.exit(rc)


if __name__ == "__main__":
    main()
