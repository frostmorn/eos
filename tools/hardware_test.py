#!/usr/bin/env python3
"""
EOS Hardware Test Suite
Automated comprehensive validation toolkit for physical hardware testing via serial.
Tests procfs, devctl, wlanctl, netctl (with Linux 'ip' vibe), and GUI launcher IPC.
"""

import argparse
import hashlib
import glob
import os
import re
import subprocess
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


def get_port_usb_info(port):
    """Dynamically resolves USB bus topology and serial number from sysfs for any tty port."""
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        loc = os.path.basename(usb_dir)
        serial_path = os.path.join(usb_dir, "serial")
        serial_num = open(serial_path).read().strip() if os.path.exists(serial_path) else None
        return loc, serial_num
    except Exception:
        return None, None


def get_target_chip():
    if os.path.exists("sdkconfig"):
        try:
            with open("sdkconfig") as f:
                content = f.read()
                if 'CONFIG_IDF_TARGET="esp32s3"' in content:
                    return "esp32s3"
                if 'CONFIG_IDF_TARGET="esp32p4"' in content:
                    return "esp32p4"
        except Exception:
            pass
    return "esp32p4"


def get_port_chip(port):
    """Detects chip model ('esp32s3', 'esp32p4') from USB sysfs bcdDevice descriptor."""
    try:
        tty_name = os.path.basename(port)
        usb_dir = os.path.realpath(f"/sys/class/tty/{tty_name}/device/..")
        bcd_path = os.path.join(usb_dir, "bcdDevice")
        if os.path.exists(bcd_path):
            bcd = open(bcd_path).read().strip()
            if bcd == "0101":
                return "esp32s3"
            elif bcd == "0102":
                return "esp32p4"
    except Exception:
        pass
    return None


def get_target_cfg():
    target_chip = get_target_chip()
    if target_chip == "esp32s3":
        return "board/esp32s3-builtin.cfg"
    return "board/esp32p4-builtin.cfg"


def resolve_port(port, target_chip=None):
    if not port or port.startswith("socket://"):
        return port
    if not target_chip:
        target_chip = get_target_chip()
    candidates = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    for c in candidates:
        if get_port_chip(c) == target_chip:
            return c
    if os.path.exists(port):
        return port
    return candidates[0] if candidates else port


def jtag_reset(port):
    cfg = get_target_cfg()
    loc, serial_num = get_port_usb_info(port)
    cmd = [
        "/home/ai/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/bin/openocd",
        "-f", cfg
    ]
    if serial_num:
        cmd.extend(["-c", f"adapter serial {serial_num}"])
    if "esp32s3" in cfg:
        cmd.extend(["-c", "init; halt; mww 0x60008000 0x80000000; resume; shutdown"])
    else:
        cmd.extend(["-c", "init; reset run; shutdown"])
    print(f"{Colors.CYAN}Resetting target via OpenOCD JTAG ({serial_num or 'auto'})...{Colors.RESET}")
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8)
        time.sleep(1.5)
    except Exception as e:
        print(f"{Colors.YELLOW}Warning: OpenOCD reset failed: {e}{Colors.RESET}")


class EOSTestHarness:
    def __init__(self, port, baudrate=115200, timeout=0.2, reset=False):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.reset = reset
        self.ser = None
        self.passed = 0
        self.failed = 0
        self.suite_counts = {}
        self.target_chip = get_target_chip()
        self.loc, self.serial_num = get_port_usb_info(port)

    def _open_serial(self):
        resolved = resolve_port(self.port, getattr(self, "target_chip", None))
        if resolved:
            self.port = resolved
            loc, s = get_port_usb_info(resolved)
            if s:
                self.serial_num = s
        if hasattr(serial, "serial_for_url"):
            self.ser = serial.serial_for_url(self.port, baudrate=self.baudrate, timeout=self.timeout, write_timeout=2.0)
        else:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout, write_timeout=2.0)
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass

    def _read_available(self):
        try:
            if not self.ser or not self.ser.is_open:
                return b""
            if hasattr(self.ser, "_socket"):
                return self.ser._socket.recv(4096)
            n = self.ser.in_waiting
            if not n:
                return b""
            return self.ser.read(n)
        except Exception:
            return b""

    def _drain_serial(self, timeout=0.5):
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self._read_available()
            if not chunk:
                break
            time.sleep(0.02)

    def _wait_and_reopen(self, timeout=15.0):
        self.close()
        time.sleep(1.0)
        deadline = time.time() + timeout
        target_chip = getattr(self, "target_chip", None)
        while time.time() < deadline:
            target_port = resolve_port(self.port, target_chip)
            if target_port and os.path.exists(target_port):
                try:
                    self.port = target_port
                    self._open_serial()
                    if self.ser and self.ser.is_open:
                        _ = self.ser.in_waiting
                        return True
                except Exception:
                    self.close()
            time.sleep(0.5)
        return False

    def connect(self):
        if self.reset and not self.port.startswith("socket://"):
            jtag_reset(self.port)
            self._wait_and_reopen(12.0)
        else:
            self._open_serial()

        print(f"{Colors.CYAN}Connecting to EOS on {self.port} at {self.baudrate} baud...{Colors.RESET}")

        ready_marker = b"__EOS_READY__"
        deadline = time.time() + 15.0
        buf = b""
        last_ping = 0.0
        while time.time() < deadline:
            chunk = self._read_available()
            if chunk:
                buf += chunk
                if ready_marker in buf:
                    break
            now = time.time()
            if now - last_ping > 0.4:
                try:
                    self.ser.write(b"\r\necho __EOS_READY__\r\n")
                except Exception:
                    pass
                last_ping = now
            time.sleep(0.05)

        if ready_marker not in buf and not self.reset and not self.port.startswith("socket://"):
            print(f"{Colors.YELLOW}No prompt detected, recovering via OpenOCD JTAG reset...{Colors.RESET}")
            try:
                self.close()
            except Exception:
                pass
            time.sleep(0.3)
            jtag_reset(self.port)
            if not self._wait_and_reopen(12.0):
                return
            deadline = time.time() + 15.0
            buf = b""
            last_ping = 0.0
            while time.time() < deadline:
                chunk = self._read_available()
                if chunk:
                    buf += chunk
                    if ready_marker in buf:
                        break
                now = time.time()
                if now - last_ping > 0.4:
                    try:
                        self.ser.write(b"\r\necho __EOS_READY__\r\n")
                    except Exception:
                        pass
                    last_ping = now
                time.sleep(0.05)

        self._drain_serial(1.0)

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def run_cmd(self, cmd, wait=1.0):
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial connection not open")
        cmd_str = cmd.strip()
        self._seq = getattr(self, "_seq", 0) + 1
        marker = f"__EOS_{self._seq}__"
        if cmd_str.endswith("&"):
            full_cmd = f"{cmd_str} sleep 0.2; echo; echo {marker}\n"
        elif cmd_str:
            full_cmd = f"{cmd_str}; echo; echo {marker}\n"
        else:
            full_cmd = f"echo {marker}\n"

        while True:
            try:
                if self.ser and self.ser.in_waiting:
                    self._read_available()
                    time.sleep(0.005)
                else:
                    break
            except Exception:
                break
        # self.ser.reset_input_buffer()

        try:
            self.ser.write(full_cmd.encode("utf-8"))
        except Exception as e:
            return ""
        deadline = time.time() + max(wait, 2.0)
        buf = b""
        marker_bytes = f"\n{marker}".encode("utf-8")
        while time.time() < deadline:
            chunk = self._read_available()
            if chunk:
                    buf += chunk
                    pos = buf.find(marker_bytes)
                    if pos >= 0:
                        after = buf[pos + len(marker_bytes):]
                        if b"\n" in after or re.search(rb"/[ \t]*>\s*$", after):
                            break
            time.sleep(0.01)

        pos = buf.find(marker_bytes)
        if pos >= 0:
            extracted = buf[:pos].decode("utf-8", errors="replace")
        else:
            extracted = buf.decode("utf-8", errors="replace")

        lines = extracted.splitlines()
        if lines and (cmd_str in lines[0] or marker in lines[0] or ">" in lines[0]):
            lines = lines[1:]
        if lines and re.search(r"/[ \t]*>\s*$", lines[-1]):
            lines = lines[:-1]
        return "\n".join(lines).strip()

    def check(self, suite, name, cmd, pattern, wait=1.0, not_pattern=None):
        cmd_output = self.run_cmd(cmd, wait=wait)

        has_match = re.search(pattern, cmd_output, re.MULTILINE) is not None
        has_bad = (not_pattern and re.search(not_pattern, cmd_output, re.MULTILINE) is not None)

        if suite not in self.suite_counts:
            self.suite_counts[suite] = {"pass": 0, "fail": 0}

        if has_match and not has_bad:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] {name}")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
            return True, cmd_output
        else:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] {name}")
            print(f"    Command: {cmd}")
            print(f"    Expected regex: {pattern}")
            if not_pattern:
                print(f"    Forbidden regex: {not_pattern}")
            print(f"    Raw output repr: {repr(cmd_output)}")
            print(f"    Cmd output:\n{cmd_output}\n")
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1
            return False, cmd_output

    def test_procfs(self):
        suite = "procfs"
        print(f"\n{Colors.BOLD}=== 1. Virtual /proc Filesystem Tests ==={Colors.RESET}")
        self.check(suite, "Kernel version format", "cat /proc/version", r"EOS\s+\d+\.\d+\.\d+")
        self.check(suite, "IDF build version in /proc/version", "cat /proc/version", r"IDF\s+v[0-9\.]+")
        self.check(suite, "All driver scopes present", "cat /proc/scopes", r"bus[\s\S]+display[\s\S]+input[\s\S]+net[\s\S]+storage")
        self.check(suite, "Driver table headers", "cat /proc/drivers", r"SCOPE\s+NAME\s+DEVNAME\s+FLAGS")
        self.check(suite, "Net drivers in /proc/drivers", "cat /proc/drivers", r"net\s+ap[\s\S]+net\s+sta[\s\S]+net\s+wifi")
        drivers_out = self.run_cmd("cat /proc/drivers")
        has_sdio = "bus          sdio" in drivers_out or re.search(r"bus\s+sdio", drivers_out) is not None
        if has_sdio:
            self.check(suite, "SDIO driver in /proc/drivers", "cat /proc/drivers", r"bus\s+sdio\s+sdio")
        has_sd = "storage      sd" in drivers_out or re.search(r"storage\s+sd", drivers_out) is not None
        if has_sd:
            self.check(suite, "SD driver in /proc/drivers", "cat /proc/drivers", r"storage\s+sd\s+sd")
        else:
            self.check(suite, "Flash driver in /proc/drivers", "cat /proc/drivers", r"storage\s+flash\s+flash")
        self.check(suite, "Virtual bridge driver in /proc/drivers", "cat /proc/drivers", r"net\s+bridge\s+br")
        devices_out = self.run_cmd("cat /proc/devices")
        if "wifi0" in devices_out:
            self.check(suite, "Device tree /proc/devices has net interfaces", "cat /proc/devices", r"(wifi0[\s\S]+(sta0|ap0)|(sta0|ap0)[\s\S]+wifi0)")
        if has_sdio:
            self.check(suite, "Device tree /proc/devices has SDIO slots", "cat /proc/devices", r"sdio0[\s\S]+sdio1")
            if "sd0" in devices_out:
                self.check(suite, "Device tree has SD card attached", "cat /proc/devices", r"sd0\s+storage/sd")
        elif "spi0" in devices_out:
            self.check(suite, "Device tree /proc/devices has core interfaces", "cat /proc/devices", r"(flash0[\s\S]+spi0|spi0[\s\S]+flash0)")
        else:
            self.check(suite, "Device tree /proc/devices has core interfaces", "cat /proc/devices", r"flash0[\s\S]+fb0")
        self.check(suite, "Memory stats in /proc/meminfo", "cat /proc/meminfo", r"MemFree:\s+\d+\s+kB")
        self.check(suite, "SPIRAM stats in /proc/meminfo", "cat /proc/meminfo", r"(SPIRAMFree|MemFree):\s+\d+\s+kB")
        self.check(suite, "Filesystem types in /proc/filesystems", "cat /proc/filesystems", r"(nodev\s+tmpfs[\s\S]+fat|fat[\s\S]+nodev\s+tmpfs)")
        self.check(suite, "VFS mount points in /proc/mounts", "cat /proc/mounts", r"none\s+/\s+tmpfs\s+rw\s+0\s+0[\s\S]+none\s+/proc\s+procfs\s+rw\s+0\s+0")
        self.check(suite, "mount command dumps mounts", "mount", r"none\s+/\s+tmpfs\s+rw\s+0\s+0[\s\S]+none\s+/proc\s+procfs\s+rw\s+0\s+0")
        self.check(suite, "Create test mount directory", "mkdir -p /tmp/mnt_test", r"^$|")
        self.check(suite, "Mount tmpfs at /tmp/mnt_test", "mount -t tmpfs none /tmp/mnt_test", r"(Registered VFS mount|tmpfs mounted)")
        self.check(suite, "Mounted directory visible in mount command", "mount", r"none\s+/tmp/mnt_test\s+tmpfs")
        self.check(suite, "Write and read file in mounted tmpfs", "echo mnt_ok > /tmp/mnt_test/test.txt && cat /tmp/mnt_test/test.txt", r"mnt_ok")
        self.check(suite, "Busy unmount rejection on root", "umount /", r"Device or resource busy")
        self.check(suite, "Umount /tmp/mnt_test", "umount /tmp/mnt_test", r"^$|")
        self.check(suite, "Umounted directory removed from mount command", "mount", r"none\s+/\s+tmpfs", not_pattern=r"/tmp/mnt_test")
        self.check(suite, "Umount nonexistent mount failure", "umount /nonexistent", r"No such file or directory")
        self.check(suite, "Cleanup test mount directory", "rmdir /tmp/mnt_test", r"^$|")
        self.check(suite, "Interrupt allocation in /proc/interrupts", "cat /proc/interrupts", r"(CPU|intr|alloc|Level|Edge)")
        self.check(suite, "File descriptors in /proc/fds", "cat /proc/fds", r"(VFS|fd|File|Handle|Driver)")
        self.check(suite, "GPIO pin matrix in /proc/gpio", "cat /proc/gpio", r"(GPIO|IO|pin|input|output|Pullup|direction)", wait=5.0)
        self.check(suite, "Heap memory summary in /proc/heapinfo", "cat /proc/heapinfo", r"== HEAP SUMMARY ==[\s\S]+INTERNAL[\s\S]+DMA", wait=2.5)
        self.check(suite, "SoC and core info in /proc/cpuinfo", "cat /proc/cpuinfo", r"model name[\s\S]+cpu cores\s+:\s+\d+")
        self.check(suite, "System uptime in /proc/uptime", "cat /proc/uptime", r"uptime:\s+\d+s[\s\S]+reset reason:")
        self.check(suite, "/proc/stat cpu and irq lines", "cat /proc/stat", r"cpu\s+\d+\s+\d+\s+\d+\s+\d+[\s\S]+irq\s+\d+")
        self.check(suite, "time utility executes", "time -p echo eos_time_ok", r"eos_time_ok[\s\S]+real\s+[0-9\.]+")
        self.check(suite, "time execution accounting with sleep (O2)", "time sleep 0.2",
                   r"real\s+\d+m0\.2\d+s[\s\S]+wait\s+\d+m0\.20\ds", wait=3.0)
        self.check(suite, "time posix format with sleep (O2)", "time -p sleep 0.2",
                   r"real\s+0\.2\d[\s\S]+user\s+0\.0\d[\s\S]+sys\s+0\.00", wait=3.0)
        self.check(suite, "time help flag", "time --help", r"Usage:\s+time\s+\[-p\]")

        # /proc/stat idle tick progression check (O1)
        stat_out1 = self.run_cmd("cat /proc/stat")
        m1 = re.search(r"cpu\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", stat_out1)
        self.run_cmd("sleep 0.2")
        stat_out2 = self.run_cmd("cat /proc/stat")
        m2 = re.search(r"cpu\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", stat_out2)
        if m1 and m2 and int(m2.group(4)) > int(m1.group(4)):
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] /proc/stat idle tick advancement (O1)")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
        else:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] /proc/stat idle tick advancement (O1)")
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1

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
        self.check(suite, "Output redirection to /dev/null suppresses output",
                   "echo suppress_to_null > /dev/null", r"^$", not_pattern=r"suppress_to_null")
        self.check(suite, "Output append redirection to /dev/null succeeds",
                   "echo append_to_null >> /dev/null ; echo redir_null_ok", r"redir_null_ok", not_pattern=r"append_to_null")
        self.check(suite, "Cat /dev/null produces empty output",
                   "cat /dev/null ; echo eof_null_ok", r"eof_null_ok")

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

        # 10. Failure Resilience & Boundary Recovery (B1)
        self.check(suite, "Unknown command error and shell recovery (B1)",
                   "nonexistent_cmd_xyz ; echo recovered_after_notfound",
                   r"(not found|No such file)[\s\S]+recovered_after_notfound")
        self.check(suite, "Consecutive semicolons handling (B1)",
                   ";; echo semis_ok", r"semis_ok")
        self.check(suite, "Leading semicolon handling (B1)",
                   "; echo leading_semi_ok", r"leading_semi_ok")

    def test_devctl(self):
        suite = "devctl"
        print(f"\n{Colors.BOLD}=== 3. Device Control (devctl) Tests ==={Colors.RESET}")
        self.check(suite, "devctl usage / help", "devctl", r"Usage:\s+devctl\s+<command>")
        self.check(suite, "devctl scopes", "devctl scopes", r"Driver Scopes:[\s\S]+net")
        self.check(suite, "devctl drivers (all)", "devctl drivers", r"net\s+bridge")
        drivers_out = self.run_cmd("cat /proc/drivers")
        has_sdio = "bus          sdio" in drivers_out or re.search(r"bus\s+sdio", drivers_out) is not None
        if has_sdio:
            self.check(suite, "devctl drivers bus filter has sdio", "devctl drivers bus", r"bus\s+sdio")
        else:
            self.check(suite, "devctl drivers bus filter has gpio/spi", "devctl drivers bus", r"bus\s+gpio[\s\S]+bus\s+spi")
        has_sd = "storage      sd" in drivers_out or re.search(r"storage\s+sd", drivers_out) is not None
        if has_sd:
            self.check(suite, "devctl drivers storage filter has sd", "devctl drivers storage", r"storage\s+sd")
        else:
            self.check(suite, "devctl drivers storage filter has flash", "devctl drivers storage", r"storage\s+flash")
        self.check(suite, "devctl drivers net (scoped filter)", "devctl drivers net", r"net\s+sta", not_pattern=r"storage\s+flash")
        self.check(suite, "devctl devices listing", "devctl devices", r"(root\s+bus/root|flash0\s+storage/flash|wifi0\s+net/wifi)")
        devctl_devs = self.run_cmd("devctl devices")
        if has_sdio:
            if "sd0" in devctl_devs:
                self.check(suite, "devctl devices lists sdio0 and sd0", "devctl devices", r"sdio0\s+bus/sdio[\s\S]+sd0\s+storage/sd")
            else:
                self.check(suite, "devctl devices lists sdio0", "devctl devices", r"sdio0\s+bus/sdio")
            self.check(suite, "devctl devices lists sdio1 and wifi0", "devctl devices", r"sdio1\s+bus/sdio[\s\S]+wifi0\s+net/wifi")
        elif "spi0" in devctl_devs:
            self.check(suite, "devctl devices lists spi0 and fb0", "devctl devices", r"spi0\s+bus/spi[\s\S]+fb0\s+display/st7789")
        else:
            self.check(suite, "devctl devices lists fb0", "devctl devices", r"fb0\s+display/qemu_rgb")
        self.check(suite, "devctl attach net bridge", "devctl attach net bridge", r"Attached net/bridge as /dev/br0")
        self.check(suite, "Verify br0 appears in /proc/devices", "cat /proc/devices", r"br0\s+net/bridge")
        self.check(suite, "devctl info br0 basic", "devctl info br0", r"Device:\s+/dev/br0[\s\S]+Driver:\s+net/bridge")

        # Duplicate attachment validation: reject attaching same device at same place
        self.check(suite, "Reject duplicate attachment of same device at same place",
                   "devctl attach net bridge",
                   r"(failed to attach|File exists|already attached)")
        self.check(suite, "Verify br0 remains unique in /proc/devices", "cat /proc/devices",
                   r"br0\s+net/bridge", not_pattern=r"br1")

        self.check(suite, "devctl detach br0", "devctl detach br0", r"Detached /dev/br0")
        self.check(suite, "Verify br0 is removed from /proc/devices", "cat /proc/devices", r"bus/root", not_pattern=r"br0\s+net/bridge")

        # Option A syntax: attach with --cfg and verify info
        self.check(suite, "devctl attach with --cfg flags",
                   "devctl attach net bridge root --cfg mtu=1500,debug=1",
                   r"Attached net/bridge as /dev/br0")
        self.check(suite, "devctl info br0 verifies config output", "devctl info br0",
                   r"Config:.*mtu=1500.*debug=1")
        self.check(suite, "Reject duplicate attachment of configured device at same place",
                   "devctl attach net bridge root --cfg mtu=1500,debug=1",
                   r"(failed to attach|File exists|already attached)")
        self.check(suite, "devctl detach br0 configured device", "devctl detach br0", r"Detached /dev/br0")

        # Duplicate attachment validation on pre-existing devtree peripherals
        if "gpio" in devctl_devs:
            self.check(suite, "Reject duplicate attachment of existing bus/gpio under root",
                       "devctl attach bus gpio root",
                       r"(failed to attach|File exists|already attached)")
        if "flash0" in devctl_devs:
            self.check(suite, "Reject duplicate attachment of existing storage/flash under root",
                       "devctl attach storage flash root",
                       r"(failed to attach|File exists|already attached)")
        else:
            self.check(suite, "Attach storage/flash under root", "devctl attach storage flash root", r"Attached storage/flash as /dev/flash0")
            self.check(suite, "Reject duplicate attachment of storage/flash under root", "devctl attach storage flash root", r"(failed to attach|File exists|already attached)")
            self.check(suite, "Detach flash0", "devctl detach flash0", r"Detached /dev/flash0")
        if "sdio0" in devctl_devs:
            self.check(suite, "Reject duplicate attachment of existing bus/sdio slot 0 under root",
                       "devctl attach bus sdio root --cfg slot=0",
                       r"(failed to attach|File exists|already attached)")
        if "wifi0" in devctl_devs:
            self.check(suite, "Reject duplicate attachment of existing net/sta under wifi0",
                       "devctl attach net sta wifi0",
                       r"(failed to attach|File exists|already attached)")

        if "keyboard0" in devctl_devs:
            self.check(suite, "devctl devices lists keyboard0", "devctl devices", r"keyboard0\s+input/gpio_keyboard", wait=2.0)
            self.check(suite, "devctl info keyboard0", "devctl info keyboard0", r"Driver:\s+input/gpio_keyboard")
            self.check(suite, "Automated input event test via test_input", "test_input", r"KEYBOARD,\s+code=(103\s+\(KEY_UP\)|1\s+\(KEY_ESC\))", wait=2.0)

        if "sound0" in devctl_devs:
            self.check(suite, "devctl devices lists sound0", "devctl devices", r"sound0\s+sound/max98357")
            self.check(suite, "devctl info sound0", "devctl info sound0", r"Driver:\s+sound/max98357")
            self.check(suite, "Play Imperial March theme via test_sound", "test_sound march -t 5000 30",
                       r"(Imperial March|playback complete)", wait=6.5)

        if "adc0" in devctl_devs:
            self.check(suite, "devctl devices lists adc0", "devctl devices", r"adc0\s+bus/adc")
            self.check(suite, "devctl info adc0", "devctl info adc0", r"Driver:\s+bus/adc")

        if "battery0" in devctl_devs:
            self.check(suite, "devctl devices lists battery0", "devctl devices", r"battery0\s+power/battery")
            self.check(suite, "devctl info battery0", "devctl info battery0", r"Driver:\s+power/battery")
            self.check(suite, "Query battery status via battery CLI", "battery", r"Battery:")

        self.check(suite, "lsi2c usage / help", "lsi2c --help", r"Usage:\s+lsi2c")
        if "i2c" in devctl_devs:
            self.check(suite, "devctl devices lists i2c bus", "devctl devices", r"i2c\s+bus/i2c")
            self.check(suite, "devctl info i2c", "devctl info i2c", r"Driver:\s+bus/i2c")
            self.check(suite, "lsi2c without probe lists device nodes", "lsi2c --no-probe", r"Device nodes in /dev/i2c")
            self.check(suite, "lsi2c wire probe scan executes cleanly", "lsi2c", r"Scanning I2C bus: /dev/i2c", wait=4.0)
        # Comprehensive lifecycle test: detach all present devices and reattach them back
        self.test_devtree_detach_reattach_cycle()

    def test_devtree_detach_reattach_cycle(self):
        suite = "devctl"
        print(f"\n{Colors.BOLD}--- DevTree Detach & Reattach Lifecycle Validation ---{Colors.RESET}")

        # Step 1: Discover currently present devices from /proc/devices
        out = self.run_cmd("cat /proc/devices")
        lines = [line.strip() for line in out.splitlines() if line.strip()]

        devices = []
        for line in lines:
            parts = line.split()
            if not parts or parts[0] == "ID":
                continue
            if len(parts) >= 4 and parts[0].isdigit():
                dev_id, dev_name, driver, parent = parts[0], parts[1], parts[2], parts[3]
                if "/" in driver and driver != "bus/root" and not dev_name.startswith("("):
                    devices.append({
                        "name": dev_name,
                        "driver": driver,
                        "parent": parent,
                    })

        if not devices:
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] No non-root devices discovered to cycle.")
            return

        print(f"  Discovered {len(devices)} active non-root devices: {[d['name'] for d in devices]}")

        # Step 2: Query devctl info for each device to capture pins, config, and children
        for d in devices:
            info_out = self.run_cmd(f"devctl info {d['name']}")
            d["pins"] = None
            d["cfg"] = None
            d["children"] = []

            for iline in info_out.splitlines():
                iline = iline.strip()
                if iline.startswith("Driver:"):
                    d["driver"] = iline.split("Driver:")[1].strip()
                elif iline.startswith("Parent:"):
                    d["parent"] = iline.split("Parent:")[1].strip()
                elif iline.startswith("Pins:"):
                    raw_pins = iline.split("Pins:")[1].strip()
                    if raw_pins and raw_pins != "(none)":
                        d["pins"] = raw_pins.replace(" ", "")
                elif iline.startswith("Config:"):
                    raw_cfg = iline.split("Config:")[1].strip()
                    if raw_cfg and raw_cfg != "(none)":
                        cfg_items = [item.strip() for item in raw_cfg.split(",")]
                        filtered_cfg = [item for item in cfg_items if "=0x" not in item and "=(nil)" not in item]
                        if filtered_cfg:
                            d["cfg"] = ",".join(filtered_cfg).replace(" ", "")
                elif iline.startswith("Children:"):
                    raw_ch = iline.split("Children:")[1].strip()
                    if raw_ch and raw_ch != "(none)":
                        d["children"] = [c.strip() for c in raw_ch.split(",") if c.strip()]

        # Step 3: Topologically sort for attachment and detachment
        # Attach order: parents before children, preserving original order from devices list.
        attach_order = []
        attached = set(["root", "none", "-"])
        remaining = list(devices)
        while remaining:
            candidates = [d for d in remaining if d["parent"] in attached]
            if not candidates:
                candidates = remaining
            d = candidates[0]
            remaining.remove(d)
            attached.add(d["name"])
            attach_order.append(d)

        # Detach order is the exact reverse of attach order (children before parents)
        detach_order = list(reversed(attach_order))

        for d in detach_order:
            name = d["name"]
            self.check(suite, f"Detach leaf device {name} ({d['driver']})",
                       f"devctl detach {name}", rf"Detached /dev/{name}")

        # Step 4: Verify tree is bare (only bus/root remains)
        self.check(suite, "Verify all peripheral devices detached (/proc/devices bare)",
                   "cat /proc/devices", r"bus/root", not_pattern=r"display/|storage/|net/|bus/spi|bus/gpio")

        # Step 5: Re-attach in attach_order (parents before children, preserving deterministic naming)
        for d in attach_order:
            # If device was already auto-attached by parent (e.g. storage/partition by sd0)
            curr_devs = self.run_cmd("cat /proc/devices")
            if d["name"] in curr_devs:
                print(f"  [{Colors.GREEN}PASS{Colors.RESET}] Device {d['name']} ({d['driver']}) already attached by parent")
                self.passed += 1
                self.suite_counts[suite]["pass"] += 1
                continue

            scope, drv = d["driver"].split("/", 1)
            parent = d["parent"] if d["parent"] != "none" else "root"
            cmd = f"devctl attach {scope} {drv} {parent}"
            if d.get("pins"):
                cmd += f" --pin {d['pins']}"
            if d.get("cfg"):
                cmd += f" --cfg {d['cfg']}"

            self.check(suite, f"Reattach {d['name']} ({d['driver']}) under {parent}",
                       cmd, rf"Attached {scope}/{drv} as /dev/{d['name']}")

        # Step 6: Verify all original devices are back in /proc/devices
        proc_devs = self.run_cmd("cat /proc/devices")
        all_back = True
        for d in devices:
            if d["name"] not in proc_devs:
                all_back = False
                print(f"  [{Colors.RED}FAIL{Colors.RESET}] Device {d['name']} missing from /proc/devices after reattach")
        if all_back:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All {len(devices)} devices verified active in /proc/devices after lifecycle cycle")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
        else:
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1

    def test_fs(self):
        suite = "fs"
        print(f"\n{Colors.BOLD}=== Filesystem Registry & Mount Lifecycle Tests ==={Colors.RESET}")

        # 1. Syntax & Help
        self.check(suite, "mount usage / help", "mount --help", r"Usage:\s+mount")
        self.check(suite, "umount usage / help", "umount --help", r"Usage:\s+umount")
        self.check(suite, "umount without arguments shows usage", "umount", r"Usage:\s+umount")

        # 2. Filesystem Registry & Seed Mount Verification
        self.check(suite, "Filesystem types registered in /proc/filesystems", "cat /proc/filesystems",
                   r"(nodev\s+tmpfs[\s\S]+fat|fat[\s\S]+nodev\s+tmpfs)")
        self.check(suite, "Pseudo filesystem tmpfs registered", "cat /proc/filesystems", r"nodev\s+tmpfs")
        self.check(suite, "Pseudo filesystem devfs registered", "cat /proc/filesystems", r"nodev\s+devfs")
        self.check(suite, "Pseudo filesystem binfs registered", "cat /proc/filesystems", r"nodev\s+binfs")
        self.check(suite, "Pseudo filesystem procfs registered", "cat /proc/filesystems", r"nodev\s+procfs")
        self.check(suite, "Pseudo filesystem pipefs registered", "cat /proc/filesystems", r"nodev\s+pipefs")
        self.check(suite, "Block filesystem fat registered", "cat /proc/filesystems", r"(\s+fat|\s+vfat)")
        self.check(suite, "Default seeded mounts in /proc/mounts", "cat /proc/mounts",
                   r"none\s+/\s+tmpfs\s+rw[\s\S]+none\s+/dev\s+devfs[\s\S]+none\s+/bin\s+binfs\s+ro[\s\S]+none\s+/proc\s+procfs")
        self.check(suite, "mount command dumps active mount table", "mount",
                   r"none\s+/\s+tmpfs\s+rw[\s\S]+none\s+/proc\s+procfs")

        # 3. Mount & Umount Parameter Validation and Error Handling
        self.check(suite, "Reject mount with non-absolute target path", "mount -t tmpfs none mnt_relative_bad",
                   r"(Invalid argument|mount:)")
        self.check(suite, "Reject mount with unknown filesystem type", "mount -t nonexistent_fs_xyz none /tmp/mnt_bad",
                   r"(No such device|mount:)")
        self.check(suite, "Reject mount of block fs without block device", "mount -t fat none /tmp/mnt_bad_fat",
                   r"(Invalid argument|mount:)")
        self.check(suite, "Reject unmounting root filesystem", "umount /",
                   r"Device or resource busy")
        self.check(suite, "Reject unmounting non-existent mount or device", "umount /nonexistent_path_xyz",
                   r"No such file or directory")

        # 4. Basic Pseudo-Filesystem Lifecycle Cycle
        self.check(suite, "Create basic test mount directory", "mkdir -p /tmp/cycle_basic", r"^$|")
        self.check(suite, "Mount tmpfs at /tmp/cycle_basic", "mount -t tmpfs none /tmp/cycle_basic",
                   r"^$|(Registered VFS mount|tmpfs mounted)")
        self.check(suite, "Verify /tmp/cycle_basic in mount table", "mount",
                   r"none\s+/tmp/cycle_basic\s+tmpfs\s+rw")
        self.check(suite, "Write and read file in mounted tmpfs",
                   "echo 'cycle_basic_token_42' > /tmp/cycle_basic/verify.txt && cat /tmp/cycle_basic/verify.txt",
                   r"cycle_basic_token_42")
        self.check(suite, "Reject duplicate mount at same mountpoint", "mount -t tmpfs none /tmp/cycle_basic",
                   r"Device or resource busy")
        self.check(suite, "Unmount /tmp/cycle_basic", "umount /tmp/cycle_basic", r"^$|")
        self.check(suite, "Verify /tmp/cycle_basic removed from mount table", "mount",
                   r"none\s+/\s+tmpfs", not_pattern=r"/tmp/cycle_basic")
        self.check(suite, "Clean up basic test mount directory", "rm -rf /tmp/cycle_basic", r"^$|")

        # 5. Read-Only Mount Option Cycle
        self.check(suite, "Create ro mount directory", "mkdir -p /tmp/cycle_ro", r"^$|")
        self.check(suite, "Mount tmpfs read-only (-o ro)", "mount -t tmpfs -o ro none /tmp/cycle_ro",
                   r"^$|(Registered VFS mount|tmpfs mounted)")
        self.check(suite, "Verify ro flag in mount table", "mount",
                   r"none\s+/tmp/cycle_ro\s+tmpfs\s+ro")
        self.check(suite, "Unmount read-only tmpfs", "umount /tmp/cycle_ro", r"^$|")
        self.check(suite, "Verify /tmp/cycle_ro removed from mount table", "mount",
                   r"none\s+/\s+tmpfs", not_pattern=r"/tmp/cycle_ro")
        self.check(suite, "Clean up ro mount directory", "rmdir /tmp/cycle_ro", r"^$|")

        # 6. Hierarchical / Nested Mount Lifecycle Cycle
        self.check(suite, "Create nested mount directories", "mkdir -p /tmp/cycle_tree/child", r"^$|")
        self.check(suite, "Mount parent tmpfs at /tmp/cycle_tree", "mount -t tmpfs none /tmp/cycle_tree",
                   r"^$|(Registered VFS mount|tmpfs mounted)")
        self.check(suite, "Mount child tmpfs at /tmp/cycle_tree/child", "mount -t tmpfs none /tmp/cycle_tree/child",
                   r"^$|(Registered VFS mount|tmpfs mounted)")
        self.check(suite, "Verify parent and child in mount table", "mount",
                   r"none\s+/tmp/cycle_tree\s+tmpfs[\s\S]+none\s+/tmp/cycle_tree/child\s+tmpfs")
        self.check(suite, "Write data into parent and child mounts",
                   "echo parent_secret > /tmp/cycle_tree/parent.txt && echo child_secret > /tmp/cycle_tree/child/child.txt",
                   r"^$|")
        self.check(suite, "Verify parent data readable", "cat /tmp/cycle_tree/parent.txt", r"parent_secret")
        self.check(suite, "Verify child data readable", "cat /tmp/cycle_tree/child/child.txt", r"child_secret")
        self.check(suite, "Topological reverse unmount: child first", "umount /tmp/cycle_tree/child", r"^$|")
        self.check(suite, "Verify child removed from mount table", "mount",
                   r"none\s+/tmp/cycle_tree\s+tmpfs", not_pattern=r"/tmp/cycle_tree/child")
        self.check(suite, "Verify parent data intact after child unmount", "cat /tmp/cycle_tree/parent.txt", r"parent_secret")
        self.check(suite, "Topological reverse unmount: parent second", "umount /tmp/cycle_tree", r"^$|")
        self.check(suite, "Verify parent removed from mount table", "mount",
                   r"none\s+/\s+tmpfs", not_pattern=r"/tmp/cycle_tree")
        self.check(suite, "Clean up nested directory tree", "rm -rf /tmp/cycle_tree", r"^$|")

        # 7. Storage Partition Block Device Mount Lifecycle Cycle (if storage partition exists)
        devices_out = self.run_cmd("cat /proc/devices")
        has_part = "part0" in devices_out or "storage/partition" in devices_out
        if has_part:
            mounts_out = self.run_cmd("cat /proc/mounts")
            part_already_mounted = "/dev/part0" in mounts_out or "/sdcard" in mounts_out

            if part_already_mounted:
                self.check(suite, "Unmount existing partition by device (/dev/part0)", "umount /dev/part0",
                           r"^$|(unmounted part0|fat: unmounted)")
                self.check(suite, "Verify partition removed from mounts", "cat /proc/mounts",
                           r"none\s+/\s+tmpfs", not_pattern=r"/dev/part0")
                self.check(suite, "Remount partition to /sdcard", "mount /dev/part0 /sdcard",
                           r"^$|(mounted part0 at /sdcard|fat: mounted)")
                self.check(suite, "Verify partition active in mounts", "cat /proc/mounts",
                           r"/dev/part0\s+/sdcard\s+(vfat|fat)")
                self.check(suite, "Reject duplicate mount of already mounted partition device", "mount /dev/part0 /sdcard",
                           r"Device or resource busy")
                self.check(suite, "Unmount partition by target path (/sdcard)", "umount /sdcard",
                           r"^$|(unmounted part0|fat: unmounted)")
                self.check(suite, "Verify partition removed after target unmount", "cat /proc/mounts",
                           r"none\s+/\s+tmpfs", not_pattern=r"/dev/part0")
                # Restore original mounted state
                self.check(suite, "Restore partition mount at /sdcard", "mount /dev/part0 /sdcard",
                           r"^$|(mounted part0 at /sdcard|fat: mounted)")
            else:
                self.check(suite, "Mount partition /dev/part0 at /sdcard", "mount /dev/part0 /sdcard",
                           r"^$|(mounted part0 at /sdcard|fat: mounted)")
                self.check(suite, "Verify partition active in mounts", "cat /proc/mounts",
                           r"/dev/part0\s+/sdcard\s+(vfat|fat)")
                self.check(suite, "Reject duplicate mount of partition device", "mount /dev/part0 /sdcard",
                           r"Device or resource busy")
                self.check(suite, "Unmount partition by device path (/dev/part0)", "umount /dev/part0",
                           r"^$|(unmounted part0|fat: unmounted)")
                self.check(suite, "Verify partition unmounted by device path", "cat /proc/mounts",
                           r"none\s+/\s+tmpfs", not_pattern=r"/dev/part0")
                self.check(suite, "Remount partition /dev/part0 at /sdcard", "mount /dev/part0 /sdcard",
                           r"^$|(mounted part0 at /sdcard|fat: mounted)")
                self.check(suite, "Unmount partition by target path (/sdcard)", "umount /sdcard",
                           r"^$|(unmounted part0|fat: unmounted)")
                self.check(suite, "Verify partition unmounted by target path", "cat /proc/mounts",
                           r"none\s+/\s+tmpfs", not_pattern=r"/dev/part0")

        # 8. Dynamic Multi-Mount Hierarchy Lifecycle Validation
        self.test_mount_lifecycle_cycle()

    def test_mount_lifecycle_cycle(self):
        suite = "fs"
        print(f"\n{Colors.BOLD}--- Mount Table Detach & Reattach Lifecycle Validation ---{Colors.RESET}")

        # Step 1: Create a dynamic hierarchy of 3 mounts
        paths = [
            "/tmp/dyn_cycle/lvl1",
            "/tmp/dyn_cycle/lvl1/lvl2",
            "/tmp/dyn_cycle/lvl1/lvl2/lvl3"
        ]
        self.run_cmd("mkdir -p /tmp/dyn_cycle/lvl1/lvl2/lvl3")

        for p in paths:
            self.run_cmd(f"mount -t tmpfs none {p}")

        # Step 2: Discover active user mounts from /proc/mounts
        mounts_out = self.run_cmd("cat /proc/mounts")
        lines = [line.strip() for line in mounts_out.splitlines() if line.strip()]

        discovered = []
        for line in lines:
            parts = line.split()
            if len(parts) >= 3:
                src, tgt, fstype = parts[0], parts[1], parts[2]
                if tgt.startswith("/tmp/dyn_cycle"):
                    discovered.append({"src": src, "target": tgt, "fstype": fstype})

        if len(discovered) != 3:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] Expected 3 dynamic mounts, found {len(discovered)}")
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1
            return

        print(f"  Discovered {len(discovered)} active dynamic mounts: {[m['target'] for m in discovered]}")

        # Step 3: Topologically sort for detachment (longest prefix / leaf first)
        detach_order = sorted(discovered, key=lambda m: len(m["target"]), reverse=True)
        attach_order = sorted(discovered, key=lambda m: len(m["target"]))

        for m in detach_order:
            tgt = m["target"]
            self.check(suite, f"Detach mount {tgt} ({m['fstype']})",
                       f"umount {tgt}", r"^$|")

        # Step 4: Verify all dynamic mounts detached
        curr_mounts = self.run_cmd("cat /proc/mounts")
        self.check(suite, "Verify all dynamic mounts detached (/tmp/dyn_cycle clear)",
                   "cat /proc/mounts", r"none\s+/\s+tmpfs", not_pattern=r"/tmp/dyn_cycle")

        # Step 5: Re-attach in topological order (shortest prefix / root first)
        for m in attach_order:
            tgt = m["target"]
            fstype = m["fstype"]
            self.check(suite, f"Re-mount {tgt} ({fstype})",
                       f"mount -t {fstype} none {tgt}", r"^$|(Registered VFS mount|tmpfs mounted)")

        # Step 6: Verify all original mounts are back in /proc/mounts
        final_mounts = self.run_cmd("cat /proc/mounts")
        all_back = True
        for m in discovered:
            if m["target"] not in final_mounts:
                all_back = False
                print(f"  [{Colors.RED}FAIL{Colors.RESET}] Mount {m['target']} missing from /proc/mounts after re-mount")
        if all_back:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All {len(discovered)} mounts verified active in /proc/mounts after lifecycle cycle")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
        else:
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1

        # Step 7: Final teardown & cleanup
        for m in detach_order:
            self.run_cmd(f"umount {m['target']}")
        self.run_cmd("rm -rf /tmp/dyn_cycle")

    def test_wlanctl(self):
        suite = "wlanctl"
        print(f"\n{Colors.BOLD}=== 3. WLAN Radio & Sniffer (wlanctl) Tests ==={Colors.RESET}")
        devs = self.run_cmd("cat /proc/devices")
        if "wifi0" not in devs:
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] WiFi radio device (wifi0) not present on this board architecture")
            return
        self.check(suite, "wlanctl usage / help", "wlanctl", r"Commands:[\s\S]+radio[\s\S]+getmac[\s\S]+sniffer")
        self.check(suite, "wlanctl radio on", "wlanctl radio on", r"radio turned ON", wait=6.0)
        self.check(suite, "wlanctl radio status is ON", "wlanctl radio status", r"radio turned ON")
        self.check(suite, "wlanctl getmac returns valid non-zero MAC", "wlanctl getmac",
                   r"([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", not_pattern=r"00:00:00:00:00:00")
        self.check(suite, "wlanctl channel query", "wlanctl channel", r"channel \d+")
        self.check(suite, "wlanctl channel set 6", "wlanctl channel 6", r"channel set to 6")
        self.check(suite, "wlanctl verify channel 6", "wlanctl channel", r"channel 6")
        self.check(suite, "wlanctl channel set 11", "wlanctl channel 11", r"channel set to 11")
        self.check(suite, "wlanctl verify channel 11", "wlanctl channel", r"channel 11")
        self.check(suite, "wlanctl scan executes without error", "wlanctl scan", r"(SCAN_DONE|Found \d+ access point)",
                   wait=3.0, not_pattern=r"(JC-C6-Gateway|EOS-Testbed)")
        self.check(suite, "wlanctl sniffer on", "wlanctl sniffer on", r"sniffer mode enabled")
        self.check(suite, "wlanctl dump 3 captures frames or handles timeout", "wlanctl dump 3",
                   r"(Capturing 3 raw 802.11 frames|Timeout waiting for 802.11 packet|MGMT|DATA)", wait=2.5)
        self.check(suite, "wlanctl dump with type filter (--type mgmt)", "wlanctl dump 2 --type mgmt",
                   r"(Capturing.*type=mgmt|Timeout waiting for 802.11 packet|MGMT)", wait=2.5)
        self.check(suite, "wlanctl dump with verbose output (--verbose)", "wlanctl dump 1 --verbose",
                   r"(Payload hex|Timeout waiting for 802.11 packet)", wait=2.0)
        self.check(suite, "wlanctl dump with channel hopping (--hop)", "wlanctl dump 2 --hop 100",
                   r"(Capturing|Timeout waiting for 802.11 packet)", wait=2.5)
        self.check(suite, "wlanctl dump with PCAP export (--pcap)", "wlanctl dump 2 --pcap /tmp/test_wifi.pcap",
                   r"(Saving captured frames to PCAP file|PCAP capture saved successfully)", wait=2.5)
        self.check(suite, "wlanctl sniffer off", "wlanctl sniffer off", r"sniffer mode disabled")

    def test_netctl(self):
        suite = "netctl"
        print(f"\n{Colors.BOLD}=== 4. Network Interface Management (netctl - ip vibe) Tests ==={Colors.RESET}")
        devs = self.run_cmd("cat /proc/devices")
        if "sta0" not in devs and "ap0" not in devs:
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] Network interfaces (sta0/ap0) not present on this board architecture")
            return
        # Default display and interface discovery
        self.check(suite, "netctl default displays interfaces and IPs", "netctl", r"\d+:\s+sta0:\s+<")
        self.check(suite, "netctl link show lists interfaces", "netctl link",
                   r"(\d+:\s+sta0:\s+<[\s\S]+\d+:\s+ap0:\s+<|\d+:\s+ap0:\s+<[\s\S]+\d+:\s+sta0:\s+<)")
        self.check(suite, "netctl link show ap0 specifies DOWN state", "netctl link show ap0", r"state DOWN")

        # Link state control (ip link set)
        self.check(suite, "netctl link set sta0 up", "netctl link set sta0 up", r"(brought UP|interface set to UP)", wait=3.5)
        self.check(suite, "Verify sta0 state is UP in netctl link", "netctl link show sta0", r"state (UP|UNKNOWN)")
        self.check(suite, "netctl link set sta0 down", "netctl link set sta0 down", r"(brought DOWN|interface set to DOWN)")
        self.check(suite, "Verify sta0 state is DOWN in netctl link", "netctl link show sta0", r"state DOWN")

        # Address configuration (netctl addr / a)
        self.check(suite, "netctl a displays inet & MAC addresses", "netctl a", r"link/ether\s+([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}")
        self.check(suite, "netctl addr add static IP", "netctl addr add 192.168.10.50/24 dev sta0", r"(configured static IP|^$)")
        self.check(suite, "Verify static IP appears in netctl addr show", "netctl addr show sta0", r"inet 192\.168\.10\.50/24")

        # Shorthand up/down syntax
        self.check(suite, "Shorthand: netctl sta0 up", "netctl sta0 up", r"(brought UP|interface set to UP)")
        self.check(suite, "netctl sta0 status reports UP", "netctl sta0 status", r"STATUS:if=sta0,up=1", wait=2.5)
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
        self.check(suite, "netctl br0 up", "netctl br0 up", r"(brought UP|br0 set to UP)", wait=1.0)
        self.check(suite, "netctl br0 add sta0", "netctl br0 add sta0", r"attached port \"sta0\"")
        self.check(suite, "netctl br0 status shows sta0 port", "netctl br0 status", r"ports: \[sta0\]")
        self.check(suite, "netctl link set sta0 nomaster", "netctl link set sta0 nomaster", r"^$|removed port")
        self.check(suite, "netctl br0 status shows no ports after nomaster", "netctl br0 status", r"ports: \[none\]")
        self.check(suite, "netctl link set sta0 master br0", "netctl link set sta0 master br0", r"^$|attached port")
        self.check(suite, "netctl br0 status shows sta0 port after master", "netctl br0 status", r"ports: \[sta0\]")
        self.check(suite, "netctl br0 del sta0", "netctl br0 del sta0", r"removed port \"sta0\"")
        self.check(suite, "netctl br0 status shows no ports after del", "netctl br0 status", r"ports: \[none\]")
        self.check(suite, "devctl detach br0 cleanup", "devctl detach br0", r"Detached /dev/br0")

    def test_sdio(self):
        suite = "sdio"
        print(f"\n{Colors.BOLD}=== 5. Generic SDIO Driver & Hardware Attachment Tests ==={Colors.RESET}")
        drivers_out = self.run_cmd("cat /proc/drivers")
        if "bus          sdio" not in drivers_out and not re.search(r"bus\s+sdio", drivers_out):
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] SDIO driver not enabled on this board architecture")
            return
        self.check(suite, "SDIO bus driver registered", "cat /proc/drivers", r"bus\s+sdio\s+sdio")
        self.check(suite, "SD storage driver registered", "cat /proc/drivers", r"storage\s+sd\s+sd")
        self.check(suite, "SDIO Slot 0 device node in /dev", "ls /dev", r"sdio0")
        self.check(suite, "SDIO Slot 1 device node in /dev", "ls /dev", r"sdio1")
        devices_out = self.run_cmd("cat /proc/devices")
        if "sd0" in devices_out:
            self.check(suite, "SD storage device node in /dev", "ls /dev", r"sd0")
            self.check(suite, "SD card attached to sdio0 parent", "cat /proc/devices", r"sd0\s+storage/sd\s+sdio0")
        else:
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] SD card not installed in slot 0 (hardware optional)")
        self.check(suite, "SDIO Slot 0 in /proc/devices", "cat /proc/devices", r"sdio0\s+bus/sdio")
        self.check(suite, "SDIO Slot 1 in /proc/devices", "cat /proc/devices", r"sdio1\s+bus/sdio")
        self.check(suite, "WiFi radio attached to sdio1 parent", "cat /proc/devices", r"wifi0\s+net/wifi\s+sdio1")
        self.check(suite, "devctl reports sdio0", "devctl devices", r"sdio0\s+bus/sdio")
        self.check(suite, "devctl reports sdio1", "devctl devices", r"sdio1\s+bus/sdio")

    def test_gui_launcher(self):
        suite = "gui"
        print(f"\n{Colors.BOLD}=== 6. GUI Launcher & lvgl_net IPC Tests ==={Colors.RESET}")
        if "launcher.active" not in self.run_cmd("ls /tmp"):
            self.run_cmd("launcher &")
            for _ in range(20):
                time.sleep(0.1)
                if "launcher.active" in self.run_cmd("ls /tmp"):
                    break
            time.sleep(0.5)
        self.check(suite, "Launch lvgl_net through launcher IPC", "launcher lvgl_net &",
                   r"(requesting launch of 'lvgl_net'|Starting 'lvgl_net')", wait=2.5)
        time.sleep(1.0)
        self.check(suite, "Launch lvgl_term through launcher IPC", "launcher lvgl_term &",
                   r"(requesting launch of 'lvgl_term'|Starting 'lvgl_term')", wait=2.5)
        time.sleep(1.0)
        self.check(suite, "Launch test_lvgl automated test through launcher IPC",
                   "launcher test_lvgl -a -t 500 &",
                   r"(requesting launch of 'test_lvgl'|Starting 'test_lvgl')", wait=2.5)
        time.sleep(1.0)
        self.check(suite, "lvgl_term help banner", "lvgl_term -h",
                   r"Usage:\s+<cmd> \| lvgl_term", wait=2.0)
        self.check(suite, "lvgl_term execution (-c and -t)",
                   "lvgl_term -c \"echo __TERM_STREAM_OK__\" -t 400",
                   r"(__TERM_STREAM_OK__|Terminal Viewer|exit 0)", wait=2.5)
        self.check(suite, "lvgl_term piped input streaming",
                   "echo __PIPED_TERM_OK__ | lvgl_term -t 400",
                   r"(__PIPED_TERM_OK__|Piped Stream Output|done)", wait=2.5)

    def test_emisc(self):
        suite = "emisc"
        print(f"\n{Colors.BOLD}=== 7. emisc Math, Hierarchy & Utility Tests ==={Colors.RESET}")
        self.check(suite, "emisc unit test suite passes with 0 failures", "test_emisc",
                   r"emisc Tests:\s+\d+\s+passed,\s+0\s+failed", not_pattern=r"\[FAIL\]", wait=3.5)

    def test_coreutils(self):
        suite = "coreutils"
        print(f"\n{Colors.BOLD}=== 8. Coreutils (rm, cp, mv, sleep) Tests ==={Colors.RESET}")
        # 1. Native C Unit Test Suite (covers ~40 comprehensive unit assertions)
        self.check(suite, "Coreutils unit test binary passes with 0 failures", "test_coreutils",
                   r"Coreutils Tests:\s+\d+\s+passed,\s+0\s+failed", not_pattern=r"\[FAIL\]", wait=5.0)

        # 2. Shell Integration: sleep tests
        self.check(suite, "sleep help flag", "sleep --help", r"Usage:\s+sleep")
        self.check(suite, "sleep invalid argument error", "sleep abc", r"sleep:\s+invalid time interval")
        self.check(suite, "sleep negative interval error", "sleep -1", r"sleep:\s+invalid time interval")

        # Test sleep execution delay (~0.2s)
        t0 = time.time()
        self.run_cmd("sleep 0.2")
        elapsed = time.time() - t0
        if elapsed >= 0.15:
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] sleep 0.2 execution timing ({elapsed:.2f}s)")
            self.passed += 1
            self.suite_counts[suite]["pass"] += 1
        else:
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] sleep 0.2 execution timing too short ({elapsed:.2f}s)")
            self.failed += 1
            self.suite_counts[suite]["fail"] += 1

        # 3. Shell Integration: rm tests
        self.run_cmd("echo h_rm_data > /tmp/h_rm.txt")
        self.check(suite, "rm unlinks file", "rm -v /tmp/h_rm.txt", r"removed '/tmp/h_rm\.txt'", not_pattern=r"cannot")
        self.check(suite, "rm file is gone", "ls /tmp", r"^((?!h_rm\.txt).)*$")
        self.check(suite, "rm non-existent file fails", "rm /tmp/h_rm_ghost.txt", r"cannot remove")
        self.check(suite, "rm -f non-existent file succeeds silently", "rm -f /tmp/h_rm_ghost.txt", r".*", not_pattern=r"(cannot|error)")

        # rm directory without -r fails
        self.run_cmd("mkdir /tmp/h_rm_dir")
        self.check(suite, "rm directory without -r fails", "rm /tmp/h_rm_dir", r"Is a directory")
        self.run_cmd("echo sub > /tmp/h_rm_dir/sub.txt")
        self.check(suite, "rm -r removes directory tree", "rm -r -v /tmp/h_rm_dir", r"removed directory '/tmp/h_rm_dir'", not_pattern=r"cannot")
        self.check(suite, "rm -r directory is gone", "ls /tmp", r"^((?!h_rm_dir).)*$")

        # rmdir on non-empty directory fails (F4)
        self.run_cmd("mkdir /tmp/h_ne_dir")
        self.run_cmd("echo sub > /tmp/h_ne_dir/sub.txt")
        self.check(suite, "rmdir non-empty directory fails (F4)", "rmdir /tmp/h_ne_dir", r"Directory not empty")
        self.check(suite, "rm -rf removes non-empty directory tree (F4)", "rm -rf /tmp/h_ne_dir", r".*", not_pattern=r"(cannot|error)")
        self.check(suite, "rmdir target directory is gone (F4)", "ls /tmp", r"^((?!h_ne_dir).)*$")

        # 4. Shell Integration: cp tests
        self.run_cmd("echo cp_payload_888 > /tmp/h_cp_src.txt")
        self.check(suite, "cp file copy", "cp -v /tmp/h_cp_src.txt /tmp/h_cp_dst.txt", r"'/tmp/h_cp_src\.txt' -> '/tmp/h_cp_dst\.txt'", not_pattern=r"cannot")
        self.check(suite, "cp destination has matching content", "cat /tmp/h_cp_dst.txt", r"cp_payload_888")
        self.check(suite, "cp source file still exists", "cat /tmp/h_cp_src.txt", r"cp_payload_888")
        self.check(suite, "cp same file fails", "cp /tmp/h_cp_src.txt /tmp/h_cp_src.txt", r"same file")

        # cp to directory
        self.run_cmd("mkdir /tmp/h_cp_dir")
        self.check(suite, "cp file into directory", "cp -v /tmp/h_cp_src.txt /tmp/h_cp_dir", r"'/tmp/h_cp_src\.txt' -> '/tmp/h_cp_dir/h_cp_src\.txt'", not_pattern=r"cannot")
        self.check(suite, "cp target inside directory has content", "cat /tmp/h_cp_dir/h_cp_src.txt", r"cp_payload_888")

        # cp -r recursive directory
        self.run_cmd("mkdir /tmp/h_tree_src")
        self.run_cmd("echo leaf > /tmp/h_tree_src/leaf.txt")
        self.check(suite, "cp without -r on directory fails", "cp /tmp/h_tree_src /tmp/h_tree_dst", r"omitting directory")
        self.check(suite, "cp -r recursive directory copy", "cp -r -v /tmp/h_tree_src /tmp/h_tree_dst", r"'/tmp/h_tree_src/leaf\.txt' -> '/tmp/h_tree_dst/leaf\.txt'", not_pattern=r"cannot")
        self.check(suite, "cp -r nested file exists", "cat /tmp/h_tree_dst/leaf.txt", r"leaf")

        # 5. Shell Integration: mv tests
        self.run_cmd("echo mv_payload_999 > /tmp/h_mv_src.txt")
        self.check(suite, "mv file rename", "mv -v /tmp/h_mv_src.txt /tmp/h_mv_dst.txt", r"renamed '/tmp/h_mv_src\.txt' -> '/tmp/h_mv_dst\.txt'", not_pattern=r"cannot")
        self.check(suite, "mv destination has matching content", "cat /tmp/h_mv_dst.txt", r"mv_payload_999")
        self.check(suite, "mv source is removed", "ls /tmp", r"^((?!h_mv_src\.txt).)*$")

        # mv into directory
        self.run_cmd("mkdir /tmp/h_mv_dir")
        self.check(suite, "mv file into directory", "mv -v /tmp/h_mv_dst.txt /tmp/h_mv_dir", r"renamed '/tmp/h_mv_dst\.txt' -> '/tmp/h_mv_dir/h_mv_dst\.txt'", not_pattern=r"cannot", wait=3.0)
        self.check(suite, "mv file inside directory exists", "cat /tmp/h_mv_dir/h_mv_dst.txt", r"mv_payload_999")

        # mv directory
        self.run_cmd("mkdir /tmp/h_mv_d")
        self.run_cmd("echo d_item > /tmp/h_mv_d/d.txt")
        self.check(suite, "mv directory move", "mv -v /tmp/h_mv_d /tmp/h_mv_d2", r"renamed '/tmp/h_mv_d' -> '/tmp/h_mv_d2'", not_pattern=r"cannot")
        self.check(suite, "mv destination directory content exists", "cat /tmp/h_mv_d2/d.txt", r"d_item")
        self.check(suite, "mv source directory is removed", "ls /tmp", r"^((?!h_mv_d(\s|$)).)*$")

        # 6. Shell Integration: tree tests
        self.check(suite, "tree /proc terminates cleanly without infinite recursion", "tree /proc",
                   r"(cpuinfo|devices|meminfo)", not_pattern=r"├── \.", wait=3.0)
        self.check(suite, "tree /bin lists binary executables", "tree /bin",
                   r"(tree|cat|ls|rm)", not_pattern=r"├── \.", wait=3.0)
        self.check(suite, "tree /dev lists device nodes", "tree /dev",
                   r"(gpio|keyboard0|flash0|fb0)", not_pattern=r"├── \.", wait=5.0)
        self.check(suite, "tree / traverses root cleanly without recursion into .", "tree /",
                   r"(proc|bin|dev|tmp)", not_pattern=r"├── \.", wait=3.0)

        # 7. Shell Integration: ln & readlink tests
        self.run_cmd("echo hl_val_555 > /tmp/h_hl_src.txt")
        self.check(suite, "ln creates hard link", "ln -v /tmp/h_hl_src.txt /tmp/h_hl_dst.txt", r"'/tmp/h_hl_dst\.txt' -> '/tmp/h_hl_src\.txt'", not_pattern=r"cannot")
        self.check(suite, "ln destination shares content", "cat /tmp/h_hl_dst.txt", r"hl_val_555")
        self.run_cmd("rm /tmp/h_hl_src.txt")
        self.check(suite, "hard link persists after original removed", "cat /tmp/h_hl_dst.txt", r"hl_val_555")

        self.run_cmd("echo sl_val_666 > /tmp/h_sl_src.txt")
        self.check(suite, "ln -s creates symlink", "ln -s -v /tmp/h_sl_src.txt /tmp/h_sl_dst.txt", r"'/tmp/h_sl_dst\.txt' -> '/tmp/h_sl_src\.txt'", not_pattern=r"cannot")
        self.check(suite, "readlink prints symlink target", "readlink /tmp/h_sl_dst.txt", r"/tmp/h_sl_src\.txt")
        self.check(suite, "cat reads through symlink", "cat /tmp/h_sl_dst.txt", r"sl_val_666")

        # Symlink loop detection
        self.run_cmd("ln -s /tmp/h_loop2 /tmp/h_loop1")
        self.run_cmd("ln -s /tmp/h_loop1 /tmp/h_loop2")
        self.check(suite, "cat detects symlink loop without hang", "cat /tmp/h_loop1", r"(Too many|levels|failed|error|cannot|Can't)", wait=3.0)

        # Multi-hop symlink resolution and loop/depth limits (V6)
        self.run_cmd("echo symlink_chain_data > /tmp/h_target.txt")
        self.run_cmd("ln -s /tmp/h_target.txt /tmp/h_s1")
        self.run_cmd("ln -s /tmp/h_s1 /tmp/h_s2")
        self.run_cmd("ln -s /tmp/h_s2 /tmp/h_s3")
        self.run_cmd("ln -s /tmp/h_s3 /tmp/h_s4")
        self.run_cmd("ln -s /tmp/h_s4 /tmp/h_s5")
        self.run_cmd("ln -s /tmp/h_s5 /tmp/h_s6")
        self.run_cmd("ln -s /tmp/h_s6 /tmp/h_s7")
        self.check(suite, "cat resolves 7-hop symlink chain (V6)", "cat /tmp/h_s7", r"symlink_chain_data")
        self.run_cmd("ln -s /tmp/h_s7 /tmp/h_s8")
        self.check(suite, "cat detects symlink depth cutoff at 8 hops (V6)", "cat /tmp/h_s8",
                   r"Too many symbolic links")
        self.run_cmd("rm -f /tmp/h_s* /tmp/h_target.txt")

        # Relative symlink resolution (V6)
        self.run_cmd("echo rel_payload_777 > /tmp/h_rel_tgt.txt")
        self.run_cmd("ln -s h_rel_tgt.txt /tmp/h_rel_sym")
        self.check(suite, "readlink prints relative symlink target (V6)", "readlink /tmp/h_rel_sym",
                   r"h_rel_tgt\.txt")
        self.run_cmd("rm -f /tmp/h_rel_sym /tmp/h_rel_tgt.txt")

        # Cleanup all test artifacts
        self.run_cmd("rm -rf /tmp/h_cp_* /tmp/h_tree_* /tmp/h_mv_* /tmp/h_hl_* /tmp/h_sl_* /tmp/h_loop* /tmp/h_ne_* /tmp/h_s*")

    def test_hash(self):
        suite = "hash"
        print(f"\n{Colors.BOLD}=== 9. Hash, Checksum & Base64 Tests ==={Colors.RESET}")
        # 1. Native C unit tests (known-answer vectors, padding boundaries, streaming, base64, check mode)
        self.check(suite, "Hash unit test binary passes with 0 failures", "test_hash",
                   r"Hash Tests:\s+\d+\s+passed,\s+0\s+failed", not_pattern=r"\[FAIL\]", wait=6.0)

        # 2. Shell integration. echo may or may not append a newline, so accept either digest.
        payload = b"eos_hash_payload"
        variants = [payload, payload + b"\n"]

        def digests(name):
            return "(" + "|".join(hashlib.new(name, v).hexdigest() for v in variants) + ")"

        self.check(suite, "new tools are installed in /bin", "ls /bin", r"sha256sum[\s\S]*base64|base64[\s\S]*sha256sum", wait=1.5)
        self.run_cmd("echo eos_hash_payload > /tmp/h_hash.txt")
        for tool, algo in (("md5sum", "md5"), ("sha1sum", "sha1"), ("sha224sum", "sha224"),
                           ("sha256sum", "sha256"), ("sha384sum", "sha384"), ("sha512sum", "sha512")):
            self.check(suite, f"{tool} prints digest and name", f"{tool} /tmp/h_hash.txt",
                       r"^" + digests(algo) + r"  /tmp/h_hash\.txt\s*$")
        self.check(suite, "sha256sum reads standard input from a pipe", "cat /tmp/h_hash.txt | sha256sum",
                   r"^" + digests("sha256") + r"  -\s*$")
        self.check(suite, "sha256sum -b marks binary mode", "sha256sum -b /tmp/h_hash.txt",
                   r"^" + digests("sha256") + r" \*/tmp/h_hash\.txt\s*$")
        self.check(suite, "sha256sum missing file reports an error", "sha256sum /tmp/h_hash_ghost.txt",
                   r"No such file", not_pattern=r"^[0-9a-f]{64}")
        self.check(suite, "sha256sum --help", "sha256sum --help", r"Usage:\s+sha256sum")

        # 3. Check mode round trip, then tamper with the file
        self.run_cmd("sha256sum /tmp/h_hash.txt > /tmp/h_hash.lst")
        self.check(suite, "sha256sum -c verifies a correct list", "sha256sum -c /tmp/h_hash.lst",
                   r"/tmp/h_hash\.txt: OK", not_pattern=r"FAILED")
        self.check(suite, "sha256sum -c --quiet prints nothing on success", "sha256sum -c --quiet /tmp/h_hash.lst",
                   r".*", not_pattern=r"(OK|FAILED|WARNING)")
        self.run_cmd("echo tampered > /tmp/h_hash.txt")
        self.check(suite, "sha256sum -c detects a modified file", "sha256sum -c /tmp/h_hash.lst",
                   r"/tmp/h_hash\.txt: FAILED[\s\S]*did NOT match")
        self.run_cmd("rm /tmp/h_hash.txt")
        self.check(suite, "sha256sum -c detects a missing file", "sha256sum -c /tmp/h_hash.lst",
                   r"FAILED open or read")

        # 4. cksum (POSIX CRC): value for a fixed payload
        self.run_cmd("echo eos_hash_payload > /tmp/h_hash.txt")
        ck = "|".join(f"{zlib_cksum(v)} {len(v)}" for v in variants)
        self.check(suite, "cksum prints POSIX CRC, size and name", "cksum /tmp/h_hash.txt",
                   r"^(" + ck + r") /tmp/h_hash\.txt\s*$")

        # 5. base64 encode, decode, wrap, error handling
        self.check(suite, "base64 decodes a known value", "echo Zm9v | base64 -d", r"^foo")
        self.check(suite, "base64 encodes a known value", "echo abc | base64", r"^YWJj(Cg==)?\s*$")
        self.check(suite, "base64 rejects invalid input", "echo Zm9v! | base64 -d", r"invalid input")
        self.check(suite, "base64 rejects truncated input", "echo Zg= | base64 -d", r"invalid input")
        self.run_cmd("base64 /tmp/h_hash.txt > /tmp/h_hash.b64")
        self.check(suite, "base64 round trip restores the file", "base64 -d /tmp/h_hash.b64", r"eos_hash_payload")
        self.check(suite, "base64 --help", "base64 --help", r"Usage:\s+base64")

        # 6. NIST empty file hash vectors (HS1)
        self.run_cmd("touch /tmp/h_empty.txt")
        self.check(suite, "sha256sum empty file matches NIST vector (HS1)", "sha256sum /tmp/h_empty.txt",
                   r"^e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  /tmp/h_empty\.txt\s*$")
        self.check(suite, "md5sum empty file matches known vector (HS1)", "md5sum /tmp/h_empty.txt",
                   r"^d41d8cd98f00b204e9800998ecf8427e  /tmp/h_empty\.txt\s*$")
        self.check(suite, "sha1sum empty file matches known vector (HS1)", "sha1sum /tmp/h_empty.txt",
                   r"^da39a3ee5e6b4b0d3255bfef95601890afd80709  /tmp/h_empty\.txt\s*$")

        # Cleanup
        self.run_cmd("rm -f /tmp/h_hash.txt /tmp/h_hash.lst /tmp/h_hash.b64 /tmp/h_empty.txt")

    def test_proc(self):
        suite = "proc"
        print(f"\n{Colors.BOLD}=== 11. Process Table, Syscalls & Lifecycle Tests ==={Colors.RESET}")
        self.check(suite, "Process table test binary passes with 0 failures", "test_proc",
                   r"Process Table Tests:\s+\d+\s+passed,\s+0\s+failed", not_pattern=r"\[FAIL\]", wait=25.0)

    def test_leaks(self):
        suite = "leaks"
        print(f"\n{Colors.BOLD}=== 12. Cleanup-Hook Registry & Leak Reaping Tests (F03) ==={Colors.RESET}")
        self.check(suite, "Cleanup hooks and leak reaping test binary passes with 0 failures", "test_leaks",
                   r"Leak Registry Tests:\s+\d+\s+passed,\s+0\s+failed", not_pattern=r"\[FAIL\]", wait=15.0)

    def test_boot_reset_survival(self, count=3):
        suite = "bootreset"
        print(f"\n{Colors.BOLD}=== 13. Board Boot & Reset Survival Tests ({count} Consecutive Reset Cycles) ==={Colors.RESET}")
        if self.port.startswith("socket://"):
            print(f"  [{Colors.YELLOW}SKIP{Colors.RESET}] JTAG hardware reset not applicable to socket / QEMU connection")
            return

        if suite not in self.suite_counts:
            self.suite_counts[suite] = {"pass": 0, "fail": 0}

        for cycle in range(1, count + 1):
            print(f"\n{Colors.CYAN}--- Reset & Boot Cycle {cycle} of {count} ---{Colors.RESET}")
            try:
                self.close()
            except Exception:
                pass
            time.sleep(1.0)

            jtag_reset(self.port)

            if not self._wait_and_reopen(15.0):
                print(f"  [{Colors.RED}FAIL{Colors.RESET}] Cycle {cycle}: Reopening serial port {self.port} timed out after reset")
                self.failed += 1
                self.suite_counts[suite]["fail"] += 1
                continue

            time.sleep(0.5)

            ready_marker = b"__EOS_READY__"
            deadline = time.time() + 25.0
            buf = b""
            last_ping = 0.0
            prompt_found = False
            reopened_stale = False

            while time.time() < deadline:
                chunk = self._read_available()
                if chunk:
                    buf += chunk
                    if ready_marker in buf:
                        prompt_found = True
                        break
                now = time.time()
                # Stale CDC-ACM endpoint recovery if no data received after 5s
                if len(buf) == 0 and (now - (deadline - 25.0) > 5.0) and not reopened_stale:
                    reopened_stale = True
                    try:
                        self.close()
                        time.sleep(0.5)
                        self._open_serial()
                    except Exception:
                        pass
                # Wait until boot completes (~2.5s) before sending ping so we do not flood init.sh
                if len(buf) > 3000 or (now - (deadline - 25.0) > 2.5):
                    if now - last_ping > 0.5:
                        try:
                            self.ser.write(b"\r\necho __EOS_READY__\r\n")
                        except Exception:
                            pass
                        last_ping = now
                time.sleep(0.05)

            if not prompt_found:
                print(f"  [{Colors.RED}FAIL{Colors.RESET}] Cycle {cycle}: Prompt ready canary timed out after reset (buf len={len(buf)})")
                print(buf.decode("utf-8", errors="replace")[-1500:])
                self.failed += 1
                self.suite_counts[suite]["fail"] += 1
                continue

            self._drain_serial(0.5)

            self.check(suite, f"Cycle {cycle}: Shell prompt alive and responsive",
                       f"echo boot_cycle_{cycle}_alive", rf"boot_cycle_{cycle}_alive")
            self.check(suite, f"Cycle {cycle}: /proc/uptime reports clean system restart",
                       "cat /proc/uptime", r"uptime:\s+\d+s")
            self.check(suite, f"Cycle {cycle}: /proc/devices verifies active device tree",
                       "cat /proc/devices", r"bus/root")

    def test_disp_format(self):
        suite = "disp_format"
        print(f"\n{Colors.BOLD}=== Display Color Format & Manifest v2 Tests (T14/F05) ==={Colors.RESET}")
        self.check(suite, "binfs exposes Manifest v2 for test_disp_format", "ls -l /bin/test_disp_format", r"test_disp_format", wait=2.0)
        self.check(suite, "Display format self-test passes", "test_disp_format", r"Display Format Self-Test:\s+\d+\s+passed,\s+0\s+failed", wait=4.0)
        self.check(suite, "Format query on /dev/fb0", "test_disp_format /dev/fb0", r"Supported formats mask:\s+0x[0-9a-fA-F]+", wait=3.0)
        self.check(suite, "Default format on /dev/fb0 is RGB565", "test_disp_format /dev/fb0", r"Current format:\s+1", wait=3.0)

        devices_out = self.run_cmd("cat /proc/devices", wait=2.0)
        if "vfb0" in devices_out:
            self.check(suite, "Compositor /dev/vfb0 forwards format query", "test_disp_format /dev/vfb0", r"Display Format Self-Test:\s+\d+\s+passed,\s+0\s+failed", wait=4.0)

        has_rgb888 = "st7701s" in devices_out
        if has_rgb888:
            self.check(suite, "Manifest RGB888 app runs with active format 3", "test_disp_rgb888", r"running with active format 3", wait=3.0)
            self.check(suite, "Display restored to RGB565 after test_disp_rgb888 exit", "test_disp_format /dev/fb0", r"Current format:\s+1", wait=3.0)
        else:
            self.check(suite, "Unsupported RGB888 rejected gracefully", "test_disp_rgb888", r"(not supported|failed|running with active format 1|^$)", wait=3.0)
            self.check(suite, "Display remains RGB565", "test_disp_format /dev/fb0", r"Current format:\s+1", wait=3.0)

    def test_caps(self):
        suite = "caps"
        print(f"\n{Colors.BOLD}=== Capability Manager Tests (T16) ==={Colors.RESET}")
        self.check(suite, "binfs exposes test_caps", "ls -l /bin/test_caps", r"test_caps", wait=2.0)
        self.check(suite, "/proc/capabilities exists and has headers", "cat /proc/capabilities", r"TYPE\s+ID\s+OWNER", wait=2.0)
        self.check(suite, "/proc/capabilities contains system:flash", "cat /proc/capabilities", r"system:flash", wait=2.0)
        self.check(suite, "Capability manager self-test passes", "test_caps", r"Capability Manager Tests:\s+\d+\s+passed,\s+0\s+failed", wait=4.0)

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



def zlib_cksum(data):
    """POSIX cksum (CRC with polynomial 0x04C11DB7, length appended), used to build expected values."""
    crc = 0
    def feed(crc, b):
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
        return crc
    for b in data:
        crc = feed(crc, b)
    n = len(data)
    while n:
        crc = feed(crc, n & 0xFF)
        n >>= 8
    return (~crc) & 0xFFFFFFFF


SUITE_REGISTRY = {
    "proc": {"name": "Process Table & POSIX Lifecycle Tests", "tag": "qemu", "func": lambda h: h.test_proc()},
    "procfs": {"name": "Virtual /proc Filesystem Tests", "tag": "qemu", "func": lambda h: h.test_procfs()},
    "pratt": {"name": "Shell & Pratt Parser Tests", "tag": "qemu", "func": lambda h: h.test_pratt_parser()},
    "devctl": {"name": "Device Control (devctl) Tests", "tag": "qemu", "func": lambda h: h.test_devctl()},
    "fs": {"name": "Filesystem Registry & Mount Lifecycle Tests", "tag": "qemu", "func": lambda h: h.test_fs()},
    "disp_format": {"name": "Display Color Format & Manifest v2 Tests", "tag": "qemu", "func": lambda h: h.test_disp_format()},
    "caps": {"name": "Capability Manager Tests", "tag": "qemu", "func": lambda h: h.test_caps()},
    "gui": {"name": "GUI Launcher & lvgl_net IPC Tests", "tag": "qemu", "func": lambda h: h.test_gui_launcher()},
    "emisc": {"name": "emisc Math, Hierarchy & Utility Tests", "tag": "qemu", "func": lambda h: h.test_emisc()},
    "coreutils": {"name": "Coreutils (rm, cp, mv, sleep) Tests", "tag": "qemu", "func": lambda h: h.test_coreutils()},
    "hash": {"name": "Hash, Checksum & Base64 Tests", "tag": "qemu", "func": lambda h: h.test_hash()},
    "leaks": {"name": "Cleanup-Hook Registry & Resource Leak Tests", "tag": "qemu", "func": lambda h: h.test_leaks()},
    "bootreset": {"name": "Board Boot & Reset Survival Tests", "tag": "hardware-only", "func": lambda h: h.test_boot_reset_survival()},
    "wlanctl": {"name": "WLAN Radio & Sniffer (wlanctl) Tests", "tag": "hardware-only", "func": lambda h: h.test_wlanctl()},
    "netctl": {"name": "Network Interface Management (netctl) Tests", "tag": "hardware-only", "func": lambda h: h.test_netctl()},
    "sdio": {"name": "Generic SDIO Driver Tests", "tag": "hardware-only", "func": lambda h: h.test_sdio()},
}


def auto_detect_port():
    target_chip = get_target_chip()
    candidates = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    for c in candidates:
        if get_port_chip(c) == target_chip:
            return c
    return candidates[0] if candidates else "/dev/ttyACM0"


def main():
    parser = argparse.ArgumentParser(description="EOS Physical Hardware & QEMU Test Suite")
    parser.add_argument("-p", "--port", default=auto_detect_port(), help="Serial port or socket URL (default: auto-detected)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-t", "--timeout", type=float, default=0.2, help="Read timeout in seconds")
    parser.add_argument("--suite", choices=["all"] + list(SUITE_REGISTRY.keys()),
                        default="all", help="Specific test suite to execute (default: all)")
    parser.add_argument("--tag", choices=["all", "qemu", "hardware-only"],
                        default="all", help="Filter test suites by execution environment tag (default: all)")
    parser.add_argument("--reset", action="store_true", help="Explicitly trigger OpenOCD JTAG reset before testing")
    parser.add_argument("--no-reset", action="store_true", help="Deprecated: reset is now off by default")
    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    harness = EOSTestHarness(args.port, args.baud, args.timeout, reset=args.reset)
    try:
        harness.connect()

        # Determine candidate suites
        for suite_name, meta in SUITE_REGISTRY.items():
            if args.suite != "all" and args.suite != suite_name:
                continue
            if args.tag != "all" and meta["tag"] != args.tag:
                continue
            meta["func"](harness)
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
