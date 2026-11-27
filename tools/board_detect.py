#!/usr/bin/env python3
"""
EOS Board, Chip, Port, and USB Gadget Discovery & Reset Utilities.

Provides hardware-agnostic discovery via Linux sysfs (/sys/bus/usb/devices)
without hardcoded ttyACM*, sd*, USB serials, or user-specific tool paths.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    serial = None

ESPRESSIF_VID = "303a"
TINYUSB_PIDS = tuple(f"40{i:02x}" for i in range(256))
ROM_DOWNLOAD_PID = "0009"
JTAG_PID = "1001"


def find_openocd():
    """Locate openocd binary via OPENOCD_BIN, PATH, or standard ~/.espressif/tools."""
    env_bin = os.environ.get("OPENOCD_BIN")
    if env_bin and os.path.isfile(env_bin) and os.access(env_bin, os.X_OK):
        return env_bin
    which_bin = shutil.which("openocd")
    if which_bin:
        return which_bin
    pattern = os.path.expanduser("~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd")
    matches = sorted(glob.glob(pattern), reverse=True)
    for m in matches:
        if os.path.isfile(m) and os.access(m, os.X_OK):
            return m
    return None


def get_active_board(repo_root="."):
    path = os.path.join(repo_root, ".active_board")
    if os.path.exists(path):
        try:
            with open(path) as f:
                val = f.read().strip()
                if val:
                    return val
        except OSError:
            pass
    return None


def get_target_chip(repo_root="."):
    sdkconfig = os.path.join(repo_root, "sdkconfig")
    if os.path.exists(sdkconfig):
        try:
            with open(sdkconfig) as f:
                data = f.read()
                if 'CONFIG_IDF_TARGET="esp32s3"' in data:
                    return "esp32s3"
                if 'CONFIG_IDF_TARGET="esp32p4"' in data:
                    return "esp32p4"
        except OSError:
            pass
    return "esp32p4"


def _read_sysfs(path, default=""):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def _infer_chip(pid, bcd, product):
    if bcd == "0101" or pid == "4003" or "Lilka" in product:
        return "esp32s3"
    if bcd == "0102" or pid == "4002" or "JC4880P4" in product:
        return "esp32p4"
    return None


def _find_usb_ttys(usb_dev_dir):
    ttys = []
    for tty_path in glob.glob(os.path.join(usb_dev_dir, "*", "tty", "tty*")):
        ttys.append("/dev/" + os.path.basename(tty_path))
    for tty_path in glob.glob(os.path.join(usb_dev_dir, "*", "ttyACM*")):
        ttys.append("/dev/" + os.path.basename(tty_path))
    return sorted(set(ttys))


def _find_usb_blocks(usb_dev_dir):
    blocks = []
    pattern = os.path.join(usb_dev_dir, "*", "host*", "target*", "*", "block", "*")
    for blk_path in sorted(glob.glob(pattern)):
        name = os.path.basename(blk_path)
        sectors = 0
        try:
            sectors = int(_read_sysfs(os.path.join(blk_path, "size"), "0"))
        except ValueError:
            sectors = 0
        parts = []
        for p in sorted(glob.glob(os.path.join(blk_path, f"{name}*"))):
            p_name = os.path.basename(p)
            if p_name != name and os.path.isdir(p):
                parts.append("/dev/" + p_name)
        blocks.append({
            "dev": "/dev/" + name,
            "name": name,
            "sectors": sectors,
            "size_bytes": sectors * 512,
            "partitions": parts,
        })
    return blocks


def enumerate_espressif_usb():
    """Return a list of dicts describing all connected Espressif (303a) USB devices."""
    devices = []
    for usb_dir in sorted(glob.glob("/sys/bus/usb/devices/*")):
        vid = _read_sysfs(os.path.join(usb_dir, "idVendor")).lower()
        if vid != ESPRESSIF_VID:
            continue
        pid = _read_sysfs(os.path.join(usb_dir, "idProduct")).lower()
        bcd = _read_sysfs(os.path.join(usb_dir, "bcdDevice")).lower()
        product = _read_sysfs(os.path.join(usb_dir, "product"))
        manufacturer = _read_sysfs(os.path.join(usb_dir, "manufacturer"))
        serial_str = _read_sysfs(os.path.join(usb_dir, "serial"))
        speed_mbps = _read_sysfs(os.path.join(usb_dir, "speed"))
        chip = _infer_chip(pid, bcd, product)
        if pid == ROM_DOWNLOAD_PID:
            role = "rom_download"
        elif pid == JTAG_PID:
            role = "debug_jtag"
        elif pid in TINYUSB_PIDS:
            role = "tinyusb_gadget"
        else:
            role = "unknown"

        devices.append({
            "sysfs_path": usb_dir,
            "bus_id": os.path.basename(usb_dir),
            "vid": vid,
            "pid": pid,
            "bcd": bcd,
            "chip": chip,
            "role": role,
            "manufacturer": manufacturer,
            "product": product,
            "serial": serial_str,
            "speed_mbps": speed_mbps,
            "tty_ports": _find_usb_ttys(usb_dir),
            "block_devices": _find_usb_blocks(usb_dir),
        })
    return devices


def get_usb_ids(port):
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        vid = _read_sysfs(os.path.join(usb_dir, "idVendor")).lower()
        pid = _read_sysfs(os.path.join(usb_dir, "idProduct")).lower()
        if vid and pid:
            return vid, pid
    except Exception:
        pass
    return None, None


def get_port_chip(port, default_chip=None):
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        pid = _read_sysfs(os.path.join(usb_dir, "idProduct")).lower()
        bcd = _read_sysfs(os.path.join(usb_dir, "bcdDevice")).lower()
        product = _read_sysfs(os.path.join(usb_dir, "product"))
        chip = _infer_chip(pid, bcd, product)
        if chip:
            return chip
        if pid in TINYUSB_PIDS:
            return default_chip or get_target_chip()
    except Exception:
        pass
    return None


def find_console_port(target_chip=None):
    """Find best console /dev/ttyACM* for target_chip, preferring JTAG (1001) on dual-USB."""
    chip = target_chip or get_target_chip()
    devs = [d for d in enumerate_espressif_usb() if d["tty_ports"] and (d["chip"] == chip or d["chip"] is None)]
    for preferred_role in ("debug_jtag", "tinyusb_gadget"):
        for d in devs:
            if d["role"] == preferred_role and d["chip"] == chip:
                return d["tty_ports"][0]
    for d in devs:
        return d["tty_ports"][0]
    return None


def find_gadget_device(target_chip=None):
    """Find the TinyUSB Composite Gadget USB device dict for target_chip, if connected."""
    chip = target_chip or get_target_chip()
    devs = [d for d in enumerate_espressif_usb() if d["role"] == "tinyusb_gadget"]
    for d in devs:
        if d["chip"] == chip:
            return d
    for d in devs:
        if d["chip"] is None:
            return d
    return None


def trigger_1200_bootloader(port):
    if serial is None:
        return subprocess.run(["stty", "-F", port, "1200"], check=False).returncode == 0
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
        return True
    except OSError:
        # Device may immediately disconnect/reboot on 1200-baud line-coding change
        return True
    except Exception:
        return False


def reset_target(port, target_chip=None):
    """Reset target cleanly via 1200-baud + watchdog-reset (TinyUSB) or OpenOCD JTAG."""
    chip = get_port_chip(port) or target_chip or get_target_chip()
    _, pid = get_usb_ids(port)
    usb_serial = None
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        usb_serial = _read_sysfs(os.path.join(usb_dir, "serial")) or None
    except Exception:
        pass

    if pid in TINYUSB_PIDS:
        trigger_1200_bootloader(port)
        deadline = time.time() + 4.0
        rom_port = None
        while time.time() < deadline and not rom_port:
            time.sleep(0.2)
            for d in enumerate_espressif_usb():
                if d["pid"] == ROM_DOWNLOAD_PID and d["tty_ports"]:
                    rom_port = d["tty_ports"][0]
                    break
        if rom_port:
            subprocess.run(
                [sys.executable, "-m", "esptool", "--chip", chip, "-p", rom_port,
                 "--before", "no-reset", "--after", "watchdog-reset", "chip-id"],
                check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8,
            )
            time.sleep(3.5)
            return True
        return False

    openocd_bin = find_openocd()
    if not openocd_bin:
        return False
    cfg = "board/esp32s3-builtin.cfg" if chip == "esp32s3" else "board/esp32p4-builtin.cfg"
    cmd = [openocd_bin]
    if chip == "esp32s3":
        cmd.extend(["-c", "set ESP_ONLYCPU 1"])
    cmd.extend(["-f", cfg])
    if usb_serial:
        cmd.extend(["-c", f"adapter serial {usb_serial}"])
    cmd.extend(["-c", "init; reset run; shutdown"])
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8)
        time.sleep(3.0)
        return True
    except Exception:
        return False


def main():
    parser = argparse.ArgumentParser(description="Detect connected Espressif/EOS boards and USB gadgets")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("--chip", help="Filter by target chip (esp32s3 / esp32p4)")
    args = parser.parse_args()

    devs = enumerate_espressif_usb()
    if args.chip:
        devs = [d for d in devs if d["chip"] == args.chip]

    info = {
        "active_board": get_active_board(),
        "target_chip": get_target_chip(),
        "openocd": find_openocd(),
        "devices": devs,
    }
    if args.json:
        print(json.dumps(info, indent=2))
        return 0

    print(f"Active Board : {info['active_board'] or 'unknown'} (chip={info['target_chip']})")
    print(f"OpenOCD      : {info['openocd'] or 'not found'}")
    if not devs:
        print("No Espressif (303a) USB devices detected.")
        return 1
    for d in devs:
        ttys = ", ".join(d["tty_ports"]) or "-"
        blks = ", ".join(f"{b['dev']} ({b['size_bytes'] // (1024*1024)} MiB)" for b in d["block_devices"]) or "-"
        print(f"  [{d['bus_id']}] {d['vid']}:{d['pid']} chip={d['chip'] or '?'} role={d['role']} "
              f"speed={d['speed_mbps']}M product='{d['product']}' tty=[{ttys}] blk=[{blks}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
