#!/usr/bin/env python3
"""
EOS Hardware Test Suite
Automated validation toolkit for testing EOS on physical hardware via serial.
Tests procfs, devctl, wlanctl, netctl, and GUI launcher IPC.
"""

import argparse
import glob
import re
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install via: pip install pyserial")
    sys.exit(1)


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


class EOSTestHarness:
    def __init__(self, port, baudrate=115200, timeout=0.2):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.passed = 0
        self.failed = 0

    def connect(self):
        print(f"{Colors.CYAN}Connecting to EOS on {self.port} at {self.baudrate} baud...{Colors.RESET}")
        self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def run_cmd(self, cmd, wait=1.0):
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial connection not open")
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode("utf-8"))
        time.sleep(wait)
        out = b""
        while self.ser.in_waiting:
            out += self.ser.read(self.ser.in_waiting)
            time.sleep(0.05)
        return out.decode("utf-8", errors="replace").strip()

    def check(self, name, cmd, pattern, wait=1.0, not_pattern=None):
        out = self.run_cmd(cmd, wait=wait)
        has_match = re.search(pattern, out, re.MULTILINE) is not None
        has_bad = (not_pattern and re.search(not_pattern, out, re.MULTILINE) is not None)

        if has_match and not has_bad:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] {name}")
            self.passed += 1
            return True, out
        else:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] {name}")
            print(f"    Command: {cmd}")
            print(f"    Expected regex: {pattern}")
            if not_pattern:
                print(f"    Forbidden regex: {not_pattern}")
            print(f"    Actual output:\n{out}\n")
            self.failed += 1
            return False, out

    def test_procfs(self):
        print(f"\n{Colors.BOLD}--- 1. Testing Virtual /proc Filesystem ---{Colors.RESET}")
        self.check("Kernel version exposed", "cat /proc/version", r"EOS\s+\d+\.\d+\.\d+")
        self.check("Driver scopes exposed", "cat /proc/scopes", r"net")
        self.check("Registered drivers table", "cat /proc/drivers", r"net\s+sta")
        self.check("Active device tree", "cat /proc/devices", r"sta0")
        self.check("System memory info", "cat /proc/meminfo", r"MemFree:\s+\d+\s+kB")

    def test_devctl(self):
        print(f"\n{Colors.BOLD}--- 2. Testing devctl & Dynamic Attach/Detach ---{Colors.RESET}")
        self.check("devctl scopes", "devctl scopes", r"net")
        self.check("devctl drivers", "devctl drivers", r"net\s+bridge")
        self.check("devctl devices", "devctl devices", r"wifi0|sta0")
        self.check("devctl attach net bridge", "devctl attach net bridge", r"Attached net/bridge as /dev/br0")
        self.check("Verify br0 in /proc/devices", "cat /proc/devices", r"br0\s+net/bridge")
        self.check("devctl detach br0", "devctl detach br0", r"Detached /dev/br0")
        self.check("Verify br0 removed", "cat /proc/devices", r"sta0", not_pattern=r"br0\s+net/bridge")

    def test_wlanctl(self):
        print(f"\n{Colors.BOLD}--- 3. Testing wlanctl Radio & Management ---{Colors.RESET}")
        self.check("wlanctl usage / help", "wlanctl", r"Commands:")
        self.check("wlanctl getmac (non-zero MAC)", "wlanctl getmac",
                   r"(MAC address ([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})",
                   not_pattern=r"00:00:00:00:00:00")
        self.check("wlanctl channel query", "wlanctl channel", r"channel \d+")
        self.check("wlanctl channel set", "wlanctl channel 6", r"channel set to 6")
        self.check("wlanctl verify channel", "wlanctl channel", r"channel 6")

    def test_netctl(self):
        print(f"\n{Colors.BOLD}--- 4. Testing netctl Interface Configuration ---{Colors.RESET}")
        self.check("netctl interface list", "netctl", r"sta0\s+:")
        self.check("netctl sta0 status", "netctl sta0 status", r"STATUS:if=sta0")
        self.check("netctl sta0 up", "netctl sta0 up", r"brought UP")
        self.check("netctl sta0 status is UP", "netctl sta0 status", r"STATUS:if=sta0,up=1")
        self.check("netctl ap0 status", "netctl ap0 status", r"STATUS:if=ap0")

    def test_gui_launcher(self):
        print(f"\n{Colors.BOLD}--- 5. Testing GUI Launcher & lvgl_net IPC ---{Colors.RESET}")
        self.check("Launch lvgl_net through launcher IPC", "launcher lvgl_net",
                   r"Starting 'lvgl_net'", wait=2.0)

    def print_summary(self):
        total = self.passed + self.failed
        print(f"\n{Colors.BOLD}========================================{Colors.RESET}")
        print(f"{Colors.BOLD}          TEST SUITE SUMMARY            {Colors.RESET}")
        print(f"{Colors.BOLD}========================================{Colors.RESET}")
        print(f"Total Tests Run: {total}")
        print(f"Passed:         {Colors.GREEN}{self.passed}{Colors.RESET}")
        print(f"Failed:         {Colors.RED if self.failed else Colors.GREEN}{self.failed}{Colors.RESET}")
        if self.failed == 0:
            print(f"\n{Colors.GREEN}{Colors.BOLD}ALL TESTS PASSED!{Colors.RESET}\n")
            return 0
        else:
            print(f"\n{Colors.RED}{Colors.BOLD}SOME TESTS FAILED!{Colors.RESET}\n")
            return 1


def auto_detect_port():
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    return candidates[0] if candidates else "/dev/ttyACM0"


def main():
    parser = argparse.ArgumentParser(description="EOS Physical Hardware Test Suite")
    parser.add_argument("-p", "--port", default=auto_detect_port(), help="Serial port (default: auto-detected)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-t", "--timeout", type=float, default=0.2, help="Read timeout in seconds")
    parser.add_argument("--suite", choices=["all", "procfs", "devctl", "wlanctl", "netctl", "gui"],
                        default="all", help="Test suite to execute (default: all)")
    args = parser.parse_args()

    harness = EOSTestHarness(args.port, args.baud, args.timeout)
    try:
        harness.connect()
        if args.suite in ("all", "procfs"):
            harness.test_procfs()
        if args.suite in ("all", "devctl"):
            harness.test_devctl()
        if args.suite in ("all", "wlanctl"):
            harness.test_wlanctl()
        if args.suite in ("all", "netctl"):
            harness.test_netctl()
        if args.suite in ("all", "gui"):
            harness.test_gui_launcher()
        return harness.print_summary()
    except KeyboardInterrupt:
        print("\nTest cancelled by user.")
        return 1
    except Exception as e:
        print(f"\n{Colors.RED}Error running test suite: {e}{Colors.RESET}")
        return 1
    finally:
        harness.close()


if __name__ == "__main__":
    sys.exit(main())
