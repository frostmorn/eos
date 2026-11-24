#!/usr/bin/env python3
"""
EOS Automated Flashing Helper
Supports seamless flashing even when the target is running in TinyUSB Device mode (303a:4001)
by issuing a 1200-baud CDC touch to enter ESP-ROM Download Mode, then flashing via esptool
with watchdog-reset to avoid GPIO0 strapping lockups.
"""

import glob
import os
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    serial = None


def get_usb_ids(port):
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        vid = open(os.path.join(usb_dir, "idVendor")).read().strip().lower()
        pid = open(os.path.join(usb_dir, "idProduct")).read().strip().lower()
        return vid, pid
    except Exception:
        return None, None


def find_espressif_port():
    for port in sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*")):
        vid, pid = get_usb_ids(port)
        if vid == "303a":
            return port, pid
    return None, None


def get_target_chip():
    if os.path.exists("sdkconfig"):
        with open("sdkconfig") as f:
            data = f.read()
            if 'CONFIG_IDF_TARGET="esp32s3"' in data:
                return "esp32s3"
            if 'CONFIG_IDF_TARGET="esp32p4"' in data:
                return "esp32p4"
    return "esp32s3"


def trigger_1200_bootloader(port):
    if serial is None:
        subprocess.run(["stty", "-F", port, "1200"], check=False)
        return
    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 1200
        s.timeout = 0.2
        s.write_timeout = 0.5
        s.dtr = False
        s.rts = False
        s.open()
        s.close()
    except Exception:
        pass


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    pid = None
    if port:
        _, pid = get_usb_ids(port)
    else:
        port, pid = find_espressif_port()

    if not port:
        print("No Espressif USB port found. Falling back to idf.py flash...")
        sys.exit(subprocess.call(["idf.py", "flash"]))

    if pid in ("4001", "4002", "4003"):
        print(f"Target on {port} is in TinyUSB mode (PID {pid}); triggering 1200-baud bootloader reset...")
        trigger_1200_bootloader(port)
        deadline = time.time() + 5.0
        while time.time() < deadline:
            time.sleep(0.25)
            new_port, new_pid = find_espressif_port()
            if new_port and new_pid in ("1001", "0009"):
                port = new_port
                pid = new_pid
                break
        if pid not in ("1001", "0009"):
            print("Warning: Target did not re-enumerate in ROM download mode yet; attempting flash anyway...")

    chip = get_target_chip()
    build_dir = os.path.abspath("build")
    if not os.path.exists(os.path.join(build_dir, "flash_args")):
        print("Error: build/flash_args not found. Run 'make build' first.")
        sys.exit(1)

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", chip,
        "-p", port,
        "-b", "460800",
        "--before", "no-reset",
        "--after", "watchdog-reset",
        "write-flash", "@flash_args"
    ]
    print(f"Flashing {chip} on {port}...")
    rc = subprocess.call(cmd, cwd=build_dir)
    sys.exit(rc)


if __name__ == "__main__":
    main()
