#!/usr/bin/env python3
"""
tools/catch_backtrace.py

Non-blocking Serial Monitor and Backtrace Catcher for EOS targets.
Captures panic dumps, Guru Meditation errors, and asserts from physical
hardware (ESP32-S3 / ESP32-P4), automatically resolving addresses to file:line
using xtensa or riscv addr2line and the local build/eos.elf.
"""

import argparse
import fcntl
import glob
import os
import re
import select
import shutil
import subprocess
import sys
import termios
import time

TIOCMBIS = 0x5416
TIOCMBIC = 0x5417
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004
USBDEVFS_RESET = ord('U') << 8 | 20

PANIC_PATTERNS = [
    re.compile(r"Guru Meditation Error", re.I),
    re.compile(r"Backtrace:(?:\s+0x[0-9a-fA-F]+:0x[0-9a-fA-F]+)+"),
    re.compile(r"abort\(\) was called", re.I),
    re.compile(r"assert failed:", re.I),
    re.compile(r"CORRUPT HEAP:", re.I),
    re.compile(r"Stack dump:", re.I),
    re.compile(r"Register dump:", re.I),
]

SHELL_PROMPT_PATTERN = re.compile(r"~/==\(.*?\)\s*/\s*>\s*")


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    MAGENTA = "\033[95m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def auto_detect_port():
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    return candidates[0] if candidates else "/dev/ttyACM0"


def find_usb_device_for_tty(tty_path):
    try:
        real_dev = os.path.realpath(tty_path)
        sys_dev = os.path.realpath(f"/sys/class/tty/{os.path.basename(real_dev)}/device")
        # Walk up until busnum and devnum are found
        cur = sys_dev
        while cur and cur != "/":
            bus_file = os.path.join(cur, "busnum")
            dev_file = os.path.join(cur, "devnum")
            if os.path.exists(bus_file) and os.path.exists(dev_file):
                busnum = int(open(bus_file).read().strip())
                devnum = int(open(dev_file).read().strip())
                return f"/dev/bus/usb/{busnum:03d}/{devnum:03d}"
            cur = os.path.dirname(cur)
    except Exception:
        pass
    return None


def usb_device_reset(dev_path):
    if not dev_path or not os.path.exists(dev_path):
        return False
    try:
        fd = os.open(dev_path, os.O_WRONLY)
        fcntl.ioctl(fd, USBDEVFS_RESET, 0)
        os.close(fd)
        return True
    except Exception as e:
        print(f"{Colors.YELLOW}[catch_backtrace] USB reset warning: {e}{Colors.RESET}", file=sys.stderr)
        return False


def find_addr2line_tool():
    for candidate in [
        "xtensa-esp32s3-elf-addr2line",
        "riscv32-esp-elf-addr2line",
        "addr2line",
    ]:
        path = shutil.which(candidate)
        if path:
            return path
    # Try finding in ~/.espressif or /opt/esp-idf
    extra_paths = glob.glob("/home/ai/.espressif/tools/**/bin/*addr2line", recursive=True)
    if extra_paths:
        return extra_paths[0]
    return None


def decode_backtrace(addrs, elf_path):
    tool = find_addr2line_tool()
    if not tool or not os.path.exists(elf_path):
        return []
    try:
        cmd = [tool, "-pfia", "-e", elf_path] + addrs
        out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, timeout=5).decode("utf-8", errors="replace")
        return [line.strip() for line in out.splitlines() if line.strip()]
    except Exception as e:
        return [f"(addr2line error: {e})"]


def try_write_nonblocking(fd, data, timeout=0.5):
    if isinstance(data, str):
        data = data.encode("utf-8")
    start = time.time()
    total_written = 0
    while total_written < len(data) and time.time() - start < timeout:
        _, w, _ = select.select([], [fd], [], 0.05)
        if w:
            try:
                n = os.write(fd, data[total_written:])
                if n > 0:
                    total_written += n
            except (BlockingIOError, InterruptedError):
                pass
            except Exception:
                break
        else:
            time.sleep(0.01)
    return total_written == len(data)


def run_monitor(port, baudrate=115200, timeout=5.0, reset_mode="rts", elf_path="build/eos.elf", cmd=None):
    print(f"{Colors.CYAN}[catch_backtrace] Opening {port} (non-blocking, baud {baudrate})...{Colors.RESET}")
    try:
        fd = os.open(port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
    except Exception as e:
        print(f"{Colors.RED}[catch_backtrace] Failed to open {port}: {e}{Colors.RESET}", file=sys.stderr)
        return 2

    # Set raw termios settings
    try:
        attrs = termios.tcgetattr(fd)
        # raw mode
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                      termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
        attrs[1] &= ~termios.OPOST
        attrs[2] &= ~(termios.CSIZE | termios.PARENB)
        attrs[2] |= termios.CS8
        attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        # baud rate
        baud_const = getattr(termios, f"B{baudrate}", termios.B115200)
        attrs[4] = baud_const
        attrs[5] = baud_const
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception as e:
        print(f"{Colors.YELLOW}[catch_backtrace] Termios warning: {e}{Colors.RESET}", file=sys.stderr)

    # Apply hardware reset if requested
    if reset_mode == "usb":
        usb_dev = find_usb_device_for_tty(port)
        if usb_dev:
            print(f"{Colors.CYAN}[catch_backtrace] Triggering USBDEVFS reset on {usb_dev}...{Colors.RESET}")
            os.close(fd)
            usb_device_reset(usb_dev)
            time.sleep(1.0)
            try:
                fd = os.open(port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
            except Exception as e:
                print(f"{Colors.RED}[catch_backtrace] Failed to re-open {port} after USB reset: {e}{Colors.RESET}")
                return 2
    elif reset_mode == "rts":
        print(f"{Colors.CYAN}[catch_backtrace] Pulsing RTS pin to trigger hardware reset...{Colors.RESET}")
        try:
            # Assert RTS (CHIP_PU low)
            fcntl.ioctl(fd, TIOCMBIS, (TIOCM_RTS).to_bytes(4, 'little'))
            time.sleep(0.1)
            # Deassert RTS (release reset) & ensure DTR deasserted
            fcntl.ioctl(fd, TIOCMBIC, (TIOCM_RTS | TIOCM_DTR).to_bytes(4, 'little'))
        except Exception as e:
            print(f"{Colors.YELLOW}[catch_backtrace] Reset ioctl warning: {e}{Colors.RESET}", file=sys.stderr)
    elif reset_mode == "dtr":
        print(f"{Colors.CYAN}[catch_backtrace] Pulsing DTR pin...{Colors.RESET}")
        try:
            fcntl.ioctl(fd, TIOCMBIS, (TIOCM_DTR).to_bytes(4, 'little'))
            time.sleep(0.1)
            fcntl.ioctl(fd, TIOCMBIC, (TIOCM_DTR).to_bytes(4, 'little'))
        except Exception:
            pass

    # Ensure RTS and DTR are NOT held asserted
    try:
        fcntl.ioctl(fd, TIOCMBIC, (TIOCM_RTS | TIOCM_DTR).to_bytes(4, 'little'))
    except Exception:
        pass

    start_time = time.time()
    collected = []
    panic_detected = False
    panic_lines = []
    backtrace_addrs = []

    print(f"{Colors.GREEN}[catch_backtrace] Listening for boot / panic logs (timeout {timeout:.1f}s)...{Colors.RESET}")

    cmd_sent = False
    if cmd is None:
        cmd_sent = True

    try:
        line_buffer = ""
        while time.time() - start_time < timeout:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(fd, 2048)
                    if chunk:
                        text = chunk.decode("utf-8", errors="replace")
                        sys.stdout.write(text)
                        sys.stdout.flush()
                        line_buffer += text
                        if not panic_detected and cmd_sent and SHELL_PROMPT_PATTERN.search(line_buffer):
                            print(f"\n{Colors.GREEN}{Colors.BOLD}[PASS] Shell prompt reached cleanly!{Colors.RESET}")
                            os.close(fd)
                            return 0
                except (BlockingIOError, InterruptedError):
                    pass
                except Exception as e:
                    print(f"{Colors.RED}[catch_backtrace] Read error: {e}{Colors.RESET}")
                    break

            # Send command once ready or after brief delay
            if not cmd_sent and (time.time() - start_time >= 0.5 or SHELL_PROMPT_PATTERN.search(line_buffer)):
                time.sleep(0.1)
                try_write_nonblocking(fd, f"{cmd}\n")
                cmd_sent = True
                line_buffer = ""

            # Check for newlines to inspect completed lines
            while "\n" in line_buffer:
                line, line_buffer = line_buffer.split("\n", 1)
                line = line.strip("\r")
                collected.append(line)

                # Check for panic indicators
                for pat in PANIC_PATTERNS:
                    if pat.search(line):
                        panic_detected = True
                        panic_lines.append(line)

                # Extract Backtrace addresses
                if "Backtrace:" in line:
                    matches = re.findall(r"0x[0-9a-fA-F]+:0x[0-9a-fA-F]+", line)
                    for m in matches:
                        pc, _ = m.split(":")
                        backtrace_addrs.append(pc)

                # Check for successful shell prompt after command was dispatched
                if not panic_detected and cmd_sent and (SHELL_PROMPT_PATTERN.search(line) or SHELL_PROMPT_PATTERN.search(line_buffer)):
                    print(f"\n{Colors.GREEN}{Colors.BOLD}[PASS] Shell prompt reached cleanly!{Colors.RESET}")
                    os.close(fd)
                    return 0

            # If panic was detected, give 0.8s for complete backtrace frames to stream
            if panic_detected and time.time() - start_time > 1.0:
                time.sleep(0.5)
                # Drain remainder
                while True:
                    r, _, _ = select.select([fd], [], [], 0.05)
                    if not r:
                        break
                    try:
                        c = os.read(fd, 2048)
                        if not c:
                            break
                        t = c.decode("utf-8", errors="replace")
                        sys.stdout.write(t)
                        sys.stdout.flush()
                        line_buffer += t
                    except Exception:
                        break
                break

    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}[catch_backtrace] Interrupted by user.{Colors.RESET}")
    finally:
        try:
            fcntl.ioctl(fd, TIOCMBIC, (TIOCM_RTS | TIOCM_DTR).to_bytes(4, 'little'))
            os.close(fd)
        except Exception:
            pass

    print()
    if panic_detected or backtrace_addrs:
        print(f"{Colors.RED}{Colors.BOLD}══════════════════════════════════════════════════════════════{Colors.RESET}")
        print(f"{Colors.RED}{Colors.BOLD}                  CRASH / BACKTRACE DETECTED                  {Colors.RESET}")
        print(f"{Colors.RED}{Colors.BOLD}══════════════════════════════════════════════════════════════{Colors.RESET}")
        for pl in panic_lines:
            print(f"{Colors.RED}  {pl}{Colors.RESET}")

        if backtrace_addrs:
            print(f"\n{Colors.MAGENTA}{Colors.BOLD}Decoded Stack Frames ({elf_path}):{Colors.RESET}")
            decoded = decode_backtrace(backtrace_addrs, elf_path)
            if decoded:
                for idx, frame in enumerate(decoded):
                    print(f"  [{idx:02d}] {frame}")
            else:
                print(f"  Raw PC addresses: {' '.join(backtrace_addrs)}")
        return 1

    if any(SHELL_PROMPT_PATTERN.search(l) for l in collected):
        print(f"{Colors.GREEN}{Colors.BOLD}[PASS] Target booted into EOS shell prompt.{Colors.RESET}")
        return 0

    print(f"{Colors.YELLOW}[catch_backtrace] Timeout reached with no panic and no prompt detected.{Colors.RESET}")
    return 3


def main():
    parser = argparse.ArgumentParser(description="EOS Serial Monitor and Backtrace Catcher")
    parser.add_argument("-p", "--port", default=auto_detect_port(), help="Target serial port (default: auto-detect)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-t", "--timeout", type=float, default=4.0, help="Listening timeout in seconds (default: 4.0)")
    parser.add_argument("--reset", choices=["rts", "usb", "dtr", "none"], default="rts",
                        help="Reset trigger method (default: rts)")
    parser.add_argument("-e", "--elf", default="build/eos.elf", help="Path to ELF binary for symbol decoding (default: build/eos.elf)")
    parser.add_argument("-c", "--cmd", default=None, help="Command to send to target after connect/reset")
    args = parser.parse_args()

    rc = run_monitor(port=args.port, baudrate=args.baud, timeout=args.timeout,
                     reset_mode=args.reset, elf_path=args.elf, cmd=args.cmd)
    sys.exit(rc)


if __name__ == "__main__":
    main()
