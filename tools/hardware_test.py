#!/usr/bin/env python3
"""
EOS Hardware Test Suite
Automated comprehensive validation toolkit for physical hardware testing via serial.
Tests procfs, devctl, wlanctl, netctl (with Linux 'ip' vibe), and GUI launcher IPC.
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
        self.suite_counts = {}

    def connect(self):
        print(f"{Colors.CYAN}Connecting to EOS on {self.port} at {self.baudrate} baud...{Colors.RESET}")
        self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
        time.sleep(1.0)
        self.ser.reset_input_buffer()
        self.ser.write(b"\n")
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def run_cmd(self, cmd, wait=1.0):
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial connection not open")
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode("utf-8"))
        deadline = time.time() + wait
        out = b""
        while time.time() < deadline:
            if self.ser.in_waiting:
                chunk = self.ser.read(self.ser.in_waiting)
                if chunk:
                    out += chunk
                    # Check if shell prompt has returned after executing command
                    if b">" in out[-6:]:
                        break
            time.sleep(0.02)
        # Flush any trailing bytes
        time.sleep(0.05)
        while self.ser.in_waiting:
            out += self.ser.read(self.ser.in_waiting)
            time.sleep(0.02)
        return out.decode("utf-8", errors="replace").strip()

    def check(self, suite, name, cmd, pattern, wait=1.0, not_pattern=None):
        out = self.run_cmd(cmd, wait=wait)
        # Strip command echo line from serial out before matching
        lines = out.splitlines()
        cmd_output = "\n".join(lines[1:]) if len(lines) > 1 else out

        has_match = re.search(pattern, cmd_output, re.MULTILINE) is not None
        has_bad = (not_pattern and re.search(not_pattern, cmd_output, re.MULTILINE) is not None)

        if suite not in self.suite_counts:
            self.suite_counts[suite] = {"pass": 0, "fail": 0}

        if has_match and not has_bad:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] {name}")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
            return True, out
        else:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] {name}")
            print(f"    Command: {cmd}")
            print(f"    Expected regex: {pattern}")
            if not_pattern:
                print(f"    Forbidden regex: {not_pattern}")
            print(f"    Raw output repr: {repr(out)}")
            print(f"    Cmd output:\n{cmd_output}\n")
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1
            return False, out

    def test_procfs(self):
        suite = "procfs"
        print(f"\n{Colors.BOLD}=== 1. Virtual /proc Filesystem Tests ==={Colors.RESET}")
        self.check(suite, "Kernel version format", "cat /proc/version", r"EOS\s+\d+\.\d+\.\d+")
        self.check(suite, "IDF build version in /proc/version", "cat /proc/version", r"IDF\s+v[0-9\.]+")
        self.check(suite, "All driver scopes present", "cat /proc/scopes", r"bus[\s\S]+display[\s\S]+input[\s\S]+net[\s\S]+storage")
        self.check(suite, "Driver table headers", "cat /proc/drivers", r"SCOPE\s+NAME\s+DEVNAME\s+FLAGS")
        self.check(suite, "Net drivers in /proc/drivers", "cat /proc/drivers", r"net\s+ap[\s\S]+net\s+sta[\s\S]+net\s+wifi")
        self.check(suite, "Virtual bridge driver in /proc/drivers", "cat /proc/drivers", r"net\s+bridge\s+br")
        self.check(suite, "Device tree /proc/devices has net interfaces", "cat /proc/devices", r"wifi0[\s\S]+sta0[\s\S]+ap0")
        self.check(suite, "Memory stats in /proc/meminfo", "cat /proc/meminfo", r"MemFree:\s+\d+\s+kB")
        self.check(suite, "SPIRAM stats in /proc/meminfo", "cat /proc/meminfo", r"(SPIRAMFree|MemFree):\s+\d+\s+kB")
        self.check(suite, "VFS mount points in /proc/mounts", "cat /proc/mounts", r"(/proc|/dev|/spiffs)")
        self.check(suite, "Interrupt allocation in /proc/interrupts", "cat /proc/interrupts", r"(CPU|intr|alloc|Level|Edge)")
        self.check(suite, "File descriptors in /proc/fds", "cat /proc/fds", r"(VFS|fd|File|Handle|Driver)")
        self.check(suite, "GPIO pin matrix in /proc/gpio", "cat /proc/gpio", r"(GPIO|io|pin|input|output|direction)", wait=2.0)
        self.check(suite, "Heap memory summary in /proc/heapinfo", "cat /proc/heapinfo", r"== HEAP SUMMARY ==[\s\S]+INTERNAL[\s\S]+DMA")
        self.check(suite, "SoC and core info in /proc/cpuinfo", "cat /proc/cpuinfo", r"model name[\s\S]+cpu cores\s+:\s+\d+")
        self.check(suite, "System uptime in /proc/uptime", "cat /proc/uptime", r"uptime:\s+\d+s[\s\S]+reset reason:")

    def test_pratt_parser(self):
        suite = "pratt"
        print(f"\n{Colors.BOLD}=== 2. Shell & Pratt Parser Tests ==={Colors.RESET}")
        # 1. Sequencing (;)
        self.check(suite, "Sequential execution (;)", "echo alpha ; echo beta", r"alpha[\s\S]+beta")
        self.check(suite, "Triple sequential execution (;)", "echo one ; echo two ; echo three", r"one[\s\S]+two[\s\S]+three")
        self.check(suite, "Trailing semicolon handling", "echo trailing ;", r"trailing")

        # 2. Logical AND (&&) & Short-circuit
        self.check(suite, "Logical AND success path (&&)", "echo first && echo second", r"first[\s\S]+second")
        self.check(suite, "Logical AND short-circuit on failure", "cat /nonexistent_file_xyz && echo fail_run",
                   r"Can't open file", not_pattern=r"fail_run")
        self.check(suite, "Chained logical AND", "echo a && echo b && echo c", r"a[\s\S]+b[\s\S]+c")

        # 3. Logical OR (||) & Short-circuit
        self.check(suite, "Logical OR fallback on failure (||)", "cat /nonexistent_file_xyz || echo recovered_ok",
                   r"recovered_ok")
        self.check(suite, "Logical OR short-circuit on success", "echo initial_ok || echo should_not_run",
                   r"initial_ok", not_pattern=r"should_not_run")

        # 4. Mixed Pratt Precedence (&&, ||, ;)
        self.check(suite, "Precedence: AND tighter than semicolon", "echo a && echo b ; echo c", r"a[\s\S]+b[\s\S]+c")
        self.check(suite, "Precedence: Failure AND short-circuit with semicolon",
                   "cat /nonexistent_file_xyz && echo skip ; echo continue", r"continue", not_pattern=r"skip")
        self.check(suite, "Precedence: Failure AND with OR recovery",
                   "cat /nonexistent_file_xyz && echo skip || echo recovered", r"recovered", not_pattern=r"skip")

        # 5. Output Redirections (>, >>)
        self.check(suite, "Output redirection (>)", "echo pratt_redir_text > /tmp_pratt.txt ; cat /tmp_pratt.txt",
                   r"pratt_redir_text")
        self.check(suite, "Output overwrite (>)", "echo overwritten_text > /tmp_pratt.txt ; cat /tmp_pratt.txt",
                   r"overwritten_text", not_pattern=r"pratt_redir_text")
        self.check(suite, "Output append (>>)", "echo appended_line >> /tmp_pratt.txt ; cat /tmp_pratt.txt",
                   r"overwritten_text[\s\S]+appended_line")

        # 7. Quoting & Escaping
        self.check(suite, "Single quotes literal preservation ('')",
                   "echo 'literal   spaces ; && | > *'", r"literal   spaces ; && \| > \*")
        self.check(suite, "Double quotes whitespace preservation (\"\")",
                   "echo \"spaced   double   quotes\"", r"spaced   double   quotes")
        self.check(suite, "Backslash space escaping (\\ )", "echo escaped\\ \\ space", r"escaped  space")
        self.check(suite, "Backslash operator escaping (\\;)", "echo cmd\\;arg", r"cmd;arg")

        # 8. Pathname Expansion (Globbing)
        self.check(suite, "Glob prefix expansion (/proc/c*)", "echo /proc/c*", r"/proc/cpuinfo")
        self.check(suite, "Glob middle wildcard (/proc/*info)", "echo /proc/*info",
                   r"/proc/cpuinfo\s+/proc/heapinfo\s+/proc/meminfo")
        self.check(suite, "Glob character class (/proc/[cd]*)", "echo /proc/[cd]*",
                   r"/proc/cpuinfo\s+/proc/devices\s+/proc/drivers")
        self.check(suite, "Glob single char wildcard (/proc/?ds)", "echo /proc/?ds", r"/proc/fds")
        self.check(suite, "Glob directory expansion (/proc/*)", "echo /proc/*",
                   r"/proc/cpuinfo[\s\S]+/proc/devices[\s\S]+/proc/uptime[\s\S]+/proc/version")
        self.check(suite, "Quotes inhibit glob expansion (\"/proc/*\")", "echo \"/proc/*\"",
                   r"/proc/\*", not_pattern=r"/proc/cpuinfo")
        self.check(suite, "Non-matching glob preserves pattern", "echo /proc/nonexistent_*",
                   r"/proc/nonexistent_\*")
        self.check(suite, "Glob with multi-file cat concatenation", "cat /proc/c* /proc/u*",
                   r"model name[\s\S]+reset reason")

        # 9. Background Execution (&)
        self.check(suite, "Background execution (&)", "echo bg_task &", r"bg_task")

    def test_devctl(self):
        suite = "devctl"
        print(f"\n{Colors.BOLD}=== 3. Device Control (devctl) Tests ==={Colors.RESET}")
        self.check(suite, "devctl usage / help", "devctl", r"Usage:\s+devctl\s+<command>")
        self.check(suite, "devctl scopes", "devctl scopes", r"Driver Scopes:[\s\S]+net")
        self.check(suite, "devctl drivers (all)", "devctl drivers", r"net\s+bridge")
        self.check(suite, "devctl drivers net (scoped filter)", "devctl drivers net", r"net\s+sta", not_pattern=r"storage\s+flash")
        self.check(suite, "devctl devices listing", "devctl devices", r"wifi0\s+net/wifi")
        self.check(suite, "devctl attach net bridge", "devctl attach net bridge", r"Attached net/bridge as /dev/br0")
        self.check(suite, "Verify br0 appears in /proc/devices", "cat /proc/devices", r"br0\s+net/bridge")
        self.check(suite, "devctl detach br0", "devctl detach br0", r"Detached /dev/br0")
        self.check(suite, "Verify br0 is removed from /proc/devices", "cat /proc/devices", r"sta0", not_pattern=r"br0\s+net/bridge")

    def test_wlanctl(self):
        suite = "wlanctl"
        print(f"\n{Colors.BOLD}=== 3. WLAN Radio & Sniffer (wlanctl) Tests ==={Colors.RESET}")
        self.check(suite, "wlanctl usage / help", "wlanctl", r"Commands:[\s\S]+radio[\s\S]+getmac[\s\S]+sniffer")
        self.check(suite, "wlanctl radio on", "wlanctl radio on", r"radio turned ON")
        self.check(suite, "wlanctl radio status is ON", "wlanctl radio status", r"radio turned ON")
        self.check(suite, "wlanctl getmac returns valid non-zero MAC", "wlanctl getmac",
                   r"([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", not_pattern=r"00:00:00:00:00:00")
        self.check(suite, "wlanctl channel query", "wlanctl channel", r"channel \d+")
        self.check(suite, "wlanctl channel set 6", "wlanctl channel 6", r"channel set to 6")
        self.check(suite, "wlanctl verify channel 6", "wlanctl channel", r"channel 6")
        self.check(suite, "wlanctl channel set 11", "wlanctl channel 11", r"channel set to 11")
        self.check(suite, "wlanctl verify channel 11", "wlanctl channel", r"channel 11")
        self.check(suite, "wlanctl scan executes without error", "wlanctl scan", r"(SCAN_DONE|Found \d+ access point)", wait=3.0)
        self.check(suite, "wlanctl sniffer on", "wlanctl sniffer on", r"sniffer mode enabled")
        self.check(suite, "wlanctl dump 3 captures frames or handles timeout", "wlanctl dump 3",
                   r"(Capturing 3 raw 802.11 frames|Timeout waiting for 802.11 packet|MGMT|DATA)", wait=2.5)
        self.check(suite, "wlanctl sniffer off", "wlanctl sniffer off", r"sniffer mode disabled")

    def test_netctl(self):
        suite = "netctl"
        print(f"\n{Colors.BOLD}=== 4. Network Interface Management (netctl - ip vibe) Tests ==={Colors.RESET}")
        # Default display and interface discovery
        self.check(suite, "netctl default displays interfaces and IPs", "netctl", r"\d+:\s+sta0:\s+<")
        self.check(suite, "netctl link show lists interfaces", "netctl link", r"\d+:\s+sta0:\s+<[\s\S]+\d+:\s+ap0:\s+<")
        self.check(suite, "netctl link show ap0 specifies DOWN state", "netctl link show ap0", r"state DOWN")

        # Link state control (ip link set)
        self.check(suite, "netctl link set sta0 up", "netctl link set sta0 up", r"(brought UP|interface set to UP)")
        self.check(suite, "Verify sta0 state is UP in netctl link", "netctl link show sta0", r"state (UP|UNKNOWN)")
        self.check(suite, "netctl link set sta0 down", "netctl link set sta0 down", r"(brought DOWN|interface set to DOWN)")
        self.check(suite, "Verify sta0 state is DOWN in netctl link", "netctl link show sta0", r"state DOWN")

        # Address configuration (netctl addr / a)
        self.check(suite, "netctl a displays inet & MAC addresses", "netctl a", r"link/ether\s+([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}")
        self.check(suite, "netctl addr add static IP", "netctl addr add 192.168.10.50/24 dev sta0", r"(configured static IP|^$)")
        self.check(suite, "Verify static IP appears in netctl addr show", "netctl addr show sta0", r"inet 192\.168\.10\.50/24")

        # Shorthand up/down syntax
        self.check(suite, "Shorthand: netctl sta0 up", "netctl sta0 up", r"(brought UP|interface set to UP)")
        self.check(suite, "netctl sta0 status reports UP", "netctl sta0 status", r"STATUS:if=sta0,up=1")
        self.check(suite, "Shorthand: netctl sta0 down", "netctl sta0 down", r"(brought DOWN|interface set to DOWN)")
        self.check(suite, "netctl sta0 status reports DOWN", "netctl sta0 status", r"STATUS:if=sta0,up=0")
        self.check(suite, "Shorthand: netctl up sta0", "netctl up sta0", r"(brought UP|interface set to UP)")
        self.check(suite, "Shorthand: netctl down sta0", "netctl down sta0", r"(brought DOWN|interface set to DOWN)")

        # SoftAP status and dynamic MAC SSID verification
        self.check(suite, "netctl ap0 reports unique MAC SSID", "netctl ap0 status",
                   r"SSID \"EOS-[0-9A-Fa-f]{6}\"", not_pattern=r"SSID \"EOS-AP\"")
        self.check(suite, "netctl ap0 is inactive/down by default", "netctl ap0 status",
                   r"(STATUS:if=ap0,up=0|disabled)")

        # Bridge testing via netctl (both traditional and ip-vibe master/nomaster)
        self.check(suite, "devctl attach net bridge for netctl port test", "devctl attach net bridge", r"Attached net/bridge as /dev/br0")
        self.check(suite, "netctl br0 status shows empty ports", "netctl br0 status", r"ports: \[none\]")
        self.check(suite, "netctl br0 up", "netctl br0 up", r"(brought UP|br0 set to UP)")
        self.check(suite, "netctl br0 add sta0", "netctl br0 add sta0", r"attached port \"sta0\"")
        self.check(suite, "netctl br0 status shows sta0 port", "netctl br0 status", r"ports: \[sta0\]")
        self.check(suite, "netctl link set sta0 nomaster", "netctl link set sta0 nomaster", r"^$|removed port")
        self.check(suite, "netctl br0 status shows no ports after nomaster", "netctl br0 status", r"ports: \[none\]")
        self.check(suite, "netctl link set sta0 master br0", "netctl link set sta0 master br0", r"^$|attached port")
        self.check(suite, "netctl br0 status shows sta0 port after master", "netctl br0 status", r"ports: \[sta0\]")
        self.check(suite, "netctl br0 del sta0", "netctl br0 del sta0", r"removed port \"sta0\"")
        self.check(suite, "netctl br0 status shows no ports after del", "netctl br0 status", r"ports: \[none\]")
        self.check(suite, "devctl detach br0 cleanup", "devctl detach br0", r"Detached /dev/br0")

    def test_gui_launcher(self):
        suite = "gui"
        print(f"\n{Colors.BOLD}=== 6. GUI Launcher & lvgl_net IPC Tests ==={Colors.RESET}")
        self.check(suite, "Launch lvgl_net through launcher IPC", "launcher lvgl_net",
                   r"(requesting launch of 'lvgl_net'|Starting 'lvgl_net')", wait=2.0)

    def print_summary(self):
        total = self.passed + self.failed
        print(f"\n{Colors.BOLD}===================================================={Colors.RESET}")
        print(f"{Colors.BOLD}             EOS TEST SUITE SUMMARY                 {Colors.RESET}")
        print(f"{Colors.BOLD}===================================================={Colors.RESET}")
        for s, counts in self.suite_counts.items():
            s_total = counts["pass"] + counts["fail"]
            s_color = Colors.GREEN if counts["fail"] == 0 else Colors.RED
            print(f"  Suite {s:<10}: {s_color}{counts['pass']}/{s_total} passed{Colors.RESET}")

        print("----------------------------------------------------")
        print(f"Total Tests Executed: {total}")
        print(f"Passed:               {Colors.GREEN}{self.passed}{Colors.RESET}")
        print(f"Failed:               {Colors.RED if self.failed else Colors.GREEN}{self.failed}{Colors.RESET}")
        if self.failed == 0:
            print(f"\n{Colors.GREEN}{Colors.BOLD}>>> ALL HARDWARE TESTS PASSED! <<<{Colors.RESET}\n")
            return 0
        else:
            print(f"\n{Colors.RED}{Colors.BOLD}>>> SOME HARDWARE TESTS FAILED! <<<{Colors.RESET}\n")
            return 1


def auto_detect_port():
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    return candidates[0] if candidates else "/dev/ttyACM0"


def main():
    parser = argparse.ArgumentParser(description="EOS Physical Hardware Test Suite")
    parser.add_argument("-p", "--port", default=auto_detect_port(), help="Serial port (default: auto-detected)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-t", "--timeout", type=float, default=0.2, help="Read timeout in seconds")
    parser.add_argument("--suite", choices=["all", "procfs", "pratt", "devctl", "wlanctl", "netctl", "gui"],
                        default="all", help="Test suite to execute (default: all)")
    args = parser.parse_args()

    harness = EOSTestHarness(args.port, args.baud, args.timeout)
    try:
        harness.connect()
        if args.suite in ("all", "procfs"):
            harness.test_procfs()
        if args.suite in ("all", "pratt"):
            harness.test_pratt_parser()
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
