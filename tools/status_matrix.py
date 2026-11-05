#!/usr/bin/env python3
"""
EOS Architecture & Driver Status Matrix Generator
Generates docs/status.md tracking drivers, core subsystems, test suites, and board support.
"""

import argparse
import datetime
import os
import re
import sys

DRIVER_TEST_MAP = {
    ("bus", "gpio"): "devctl, test_input",
    ("bus", "i2c"): "devctl, lsi2c",
    ("bus", "spi"): "devctl",
    ("bus", "adc"): "devctl, battery",
    ("bus", "sdio"): "sdio (hardware_test.py)",
    ("bus", "root"): "devctl DevTree Lifecycle",
    ("bus", "onewire"): "devctl",
    ("bus", "usb"): "devctl",
    ("display", "st7789"): "devctl, test_disp_*, launcher",
    ("display", "st7701s"): "devctl, test_disp_*, launcher",
    ("display", "qemu_rgb"): "devctl, launcher",
    ("input", "gpio_keyboard"): "devctl, test_input",
    ("input", "gt911"): "devctl, test_input, lvgl",
    ("net", "wifi"): "wlanctl (hardware_test.py)",
    ("net", "sta"): "netctl (hardware_test.py)",
    ("net", "ap"): "netctl, wlanctl",
    ("net", "bridge"): "devctl, netctl",
    ("power", "battery"): "devctl, battery CLI",
    ("sound", "max98357"): "devctl, test_sound",
    ("storage", "flash"): "devctl, coreutils",
    ("storage", "partition"): "devctl",
    ("storage", "sd"): "sdio (hardware_test.py)",
}

FEATURE_MATRIX = [
    {
        "category": "Kernel & Filesystem",
        "feature": "Virtual procfs (/proc)",
        "state": "Implemented",
        "test_suite": "procfs (19/19 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "/proc/{version,meminfo,drivers,devices,mounts,interrupts,fds,gpio,heapinfo,cpuinfo,uptime}"
    },
    {
        "category": "Kernel & Filesystem",
        "feature": "Shell & Pratt Parser",
        "state": "Implemented",
        "test_suite": "pratt (27/27 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "Precedence, ;, &&, ||, >, >>, quotes, escaping, globbing, &"
    },
    {
        "category": "Kernel & Filesystem",
        "feature": "Device Control (devctl)",
        "state": "Implemented",
        "test_suite": "devctl (51-59 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "Topological DevTree attach, detach, info, and dynamic lifecycle re-attachment"
    },
    {
        "category": "Kernel & Filesystem",
        "feature": "Virtual RAM tmpfs (/tmp)",
        "state": "Implemented",
        "test_suite": "coreutils, hash",
        "env": "QEMU, Lilka, JC",
        "notes": "Fast in-memory filesystem for temporary buffers, pipes, and IPC commands"
    },
    {
        "category": "Kernel & Filesystem",
        "feature": "Dynamic kvec_psram_t vector",
        "state": "Implemented",
        "test_suite": "emisc, membench",
        "env": "QEMU, Lilka, JC",
        "notes": "SPIRAM-backed dynamically resizing vectors with DRAM fallback"
    },
    {
        "category": "Binary & Loader",
        "feature": "Dynamic Relocatable ELF Loader",
        "state": "Implemented",
        "test_suite": "test_elf_loader (13/13 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "Loads standalone relocatable ELF binaries with manifest symbols and arguments"
    },
    {
        "category": "Binary & Loader",
        "feature": "Embedded binfs (/bin)",
        "state": "Implemented",
        "test_suite": "coreutils",
        "env": "QEMU, Lilka, JC",
        "notes": "Read-only filesystem exposing all builtin firmware utilities"
    },
    {
        "category": "Userland & Utilities",
        "feature": "POSIX Core Utilities",
        "state": "Implemented",
        "test_suite": "coreutils (33/33 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "rm, cp, mv, sleep, cat, echo, ls, mkdir, rmdir, tree, write"
    },
    {
        "category": "Userland & Utilities",
        "feature": "Cryptographic Hashes & Base64",
        "state": "Implemented",
        "test_suite": "hash (23/23 tests)",
        "env": "QEMU, Lilka, JC",
        "notes": "md5sum, sha1sum, sha224sum, sha256sum, sha384sum, sha512sum, cksum, base64"
    },
    {
        "category": "Userland & Utilities",
        "feature": "Memory Benchmark (membench)",
        "state": "Implemented",
        "test_suite": "membench",
        "env": "QEMU, Lilka, JC",
        "notes": "Multi-block DRAM, DRAM DMA, and PSRAM throughput profiling with OOM guard"
    },
    {
        "category": "Networking",
        "feature": "WLAN Radio & Sniffer (wlanctl)",
        "state": "Implemented",
        "test_suite": "wlanctl (17/17 tests)",
        "env": "Lilka, JC (Hardware)",
        "notes": "Scan, channel config, 802.11 promiscuous packet capture, hopping, PCAP dump"
    },
    {
        "category": "Networking",
        "feature": "Network Interface Management (netctl)",
        "state": "Implemented",
        "test_suite": "netctl (30/30 tests)",
        "env": "Lilka, JC (Hardware)",
        "notes": "Linux 'ip' style link up/down, static IP configuration, virtual bridge port add/del"
    },
    {
        "category": "GUI & Display",
        "feature": "Graphical Launcher & IPC",
        "state": "Implemented",
        "test_suite": "gui (1/1 test)",
        "env": "QEMU, Lilka, JC",
        "notes": "Categorized GUI app menu with LVGL on framebuffer devices and IPC launching"
    }
]

TEST_SUITE_SPECS = [
    {"suite": "procfs", "name": "Virtual /proc Filesystem", "tag": "qemu", "asserts": 19, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "pratt", "name": "Shell & Pratt Parser", "tag": "qemu", "asserts": 27, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "devctl", "name": "Device Control & DevTree", "tag": "qemu", "asserts": 51, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "gui", "name": "GUI Launcher & IPC", "tag": "qemu", "asserts": 1, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "emisc", "name": "Hierarchy & Math Utils", "tag": "qemu", "asserts": 1, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "coreutils", "name": "POSIX Core Utilities", "tag": "qemu", "asserts": 33, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "hash", "name": "Cryptographic Hashes & Base64", "tag": "qemu", "asserts": 23, "qemu": "PASS", "lilka": "PASS", "jc": "PASS"},
    {"suite": "wlanctl", "name": "WLAN Radio & Sniffer", "tag": "hardware-only", "asserts": 17, "qemu": "SKIP", "lilka": "PASS", "jc": "PASS"},
    {"suite": "netctl", "name": "Network Management (ip vibe)", "tag": "hardware-only", "asserts": 30, "qemu": "SKIP", "lilka": "PASS", "jc": "PASS"},
    {"suite": "sdio", "name": "Generic SDIO Bus & Slot Attachment", "tag": "hardware-only", "asserts": 9, "qemu": "SKIP", "lilka": "SKIP", "jc": "PASS"},
]


def discover_board_driver_usage():
    """Scans main/eboard/*/init.sh to find which drivers are attached on which boards."""
    board_usage = {}
    eboard_dir = "main/eboard"
    if not os.path.exists(eboard_dir):
        return board_usage

    for b in os.listdir(eboard_dir):
        bpath = os.path.join(eboard_dir, b)
        init_file = os.path.join(bpath, "init.sh")
        if not os.path.isfile(init_file):
            continue
        try:
            with open(init_file, "r", encoding="utf-8") as f:
                content = f.read()
            for line in content.splitlines():
                line = line.strip()
                if line.startswith("devctl attach"):
                    parts = line.split()
                    if len(parts) >= 4:
                        scope = parts[2]
                        drv = parts[3]
                        key = (scope, drv)
                        board_usage.setdefault(key, set()).add(b)
        except Exception:
            pass
    return board_usage


def inspect_drivers():
    drivers = []
    base_dir = "main/edriver"
    if not os.path.exists(base_dir):
        return drivers

    board_usage = discover_board_driver_usage()

    for scope in sorted(os.listdir(base_dir)):
        scope_path = os.path.join(base_dir, scope)
        if not os.path.isdir(scope_path):
            continue
        for drv_name in sorted(os.listdir(scope_path)):
            drv_path = os.path.join(scope_path, drv_name)
            if not os.path.isdir(drv_path):
                continue

            # Analyze files
            c_files = []
            has_init = False
            has_attach = False
            has_io = False

            for root, _, files in os.walk(drv_path):
                for f in files:
                    if f.endswith(".c"):
                        c_files.append(f)
                        try:
                            with open(os.path.join(root, f), "r", encoding="utf-8", errors="ignore") as src:
                                text = src.read()
                            if ".init =" in text or "init(" in text:
                                has_init = True
                            if "attach_req" in text:
                                has_attach = True
                            if ".read =" in text or ".write =" in text or ".ioctl =" in text:
                                has_io = True
                        except Exception:
                            pass

            if has_init and (has_attach or has_io):
                state = "Implemented"
            elif c_files:
                state = "Partial"
            else:
                state = "Stub"

            key = (scope, drv_name)
            test = DRIVER_TEST_MAP.get(key, "devctl attach/detach")
            boards = board_usage.get(key, set())
            board_str = ", ".join(sorted(boards)) if boards else "Manual / Optional"

            drivers.append({
                "scope": scope,
                "name": drv_name,
                "state": state,
                "test": test,
                "boards": board_str,
                "files_count": len(c_files)
            })

    return drivers


def generate_markdown():
    drivers = inspect_drivers()
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    md = []
    md.append("# EOS Architecture, Driver & Verification Status Matrix\n")
    md.append(f"> Auto-generated by `tools/status_matrix.py` on {timestamp}.\n")
    md.append("This document tracks the implementation, test coverage, and hardware verification status across all EOS peripheral drivers, core subsystems, and supported board targets.\n")

    md.append("## 1. Supported Hardware & Emulation Targets\n")
    md.append("| Target Board | Architecture | Display | Storage | Connectivity | Status |")
    md.append("| :--- | :--- | :--- | :--- | :--- | :---: |")
    md.append("| **`esp32s3_qemu`** | ESP32-S3 (Xtensa LX7, 32MB Octal PSRAM) | Virtual RGB (280x240) | Virtual Flash | OpenETH socket | **Supported** |")
    md.append("| **`lilka_v23`** | ESP32-S3 (Xtensa LX7, 8MB Octal PSRAM) | ST7789 SPI (280x240) | SPI Flash | Native Wi-Fi, MAX98357 sound | **Verified** |")
    md.append("| **`jc4880p443c_i_w_y`** | ESP32-P4 (Dual-Core RISC-V 400MHz, 16MB) | ST7701S MIPI/DPI (480x800) | SDIO Slot 0/1 | SDIO Wi-Fi (`esp_hosted`), GT911 touch | **Verified** |\n")

    md.append("## 2. Test Suite & Board Verification Matrix\n")
    md.append("| Test Suite | Category Tag | Assertions | QEMU (`esp32s3_qemu`) | Lilka (`lilka_v23`) | JC (`jc4880p443c_i_w_y`) |")
    md.append("| :--- | :---: | :---: | :---: | :---: | :---: |")
    for s in TEST_SUITE_SPECS:
        q_badge = "**PASS**" if s["qemu"] == "PASS" else "SKIP"
        l_badge = "**PASS**" if s["lilka"] == "PASS" else "SKIP"
        j_badge = "**PASS**" if s["jc"] == "PASS" else "SKIP"
        md.append(f"| **`{s['suite']}`** ({s['name']}) | `{s['tag']}` | {s['asserts']} | {q_badge} | {l_badge} | {j_badge} |")
    md.append("\n*(Note: `wlanctl`, `netctl`, and `sdio` require physical radio/SDIO hardware and are intentionally skipped on QEMU).*\n")

    md.append("## 3. Peripheral Driver Status Matrix\n")
    md.append("| Scope | Driver Name | Implementation State | Test Coverage | Active Board Attachments |")
    md.append("| :--- | :--- | :---: | :--- | :--- |")
    for d in drivers:
        st_badge = f"**{d['state']}**"
        md.append(f"| `{d['scope']}` | `{d['name']}` | {st_badge} | {d['test']} | {d['boards']} |")
    md.append("\n")

    md.append("## 4. Core Kernel Subsystems & Features\n")
    md.append("| Category | Subsystem / Feature | Implementation State | Test Suite | Verified Environments | Notes |")
    md.append("| :--- | :--- | :---: | :--- | :--- | :--- |")
    for f in FEATURE_MATRIX:
        md.append(f"| {f['category']} | **{f['feature']}** | **{f['state']}** | `{f['test_suite']}` | {f['env']} | {f['notes']} |")
    md.append("\n")

    md.append("## 5. Architectural Quality Audits\n")
    md.append("- **Declarative Layer Audit (`tools/layer_audit.py`):** 15/15 rules passed (`tools/layer_audit_rules.json`).")
    md.append("- **Linker Symbol Wrappers (`tools/wrapcheck.py`):** 23/23 wrapped symbols documented and verified in `main/ewrap/`.")
    md.append("- **Unused Code Tracking (`tools/unused_code_audit.py`):** Clean analysis of whole-archive core objects.\n")

    return "\n".join(md)


def main():
    parser = argparse.ArgumentParser(description="EOS Architecture & Driver Status Matrix Generator")
    parser.add_argument("-o", "--output", default="docs/status.md", help="Output path (default: docs/status.md)")
    parser.add_argument("--check", action="store_true", help="Check if existing file is up to date instead of writing")
    args = parser.parse_args()

    content = generate_markdown()

    if args.check:
        if not os.path.exists(args.output):
            print(f"Error: {args.output} does not exist.")
            sys.exit(1)
        with open(args.output, "r", encoding="utf-8") as f:
            existing = f.read()
        # Compare ignoring timestamp line
        c_lines = [l for l in content.splitlines() if "Auto-generated by" not in l]
        e_lines = [l for l in existing.splitlines() if "Auto-generated by" not in l]
        if c_lines == e_lines:
            print(f"Status matrix {args.output} is up to date.")
            sys.exit(0)
        else:
            print(f"Status matrix {args.output} is out of date. Run 'make docs' to regenerate.")
            sys.exit(1)

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"Generated EOS Status Matrix: {args.output}")


if __name__ == "__main__":
    main()
