# EOS Executable Binaries (`ebin`) Reference Catalog

This catalog is an everyday operational reference for all **55 native executable binaries** implemented in **EOS (ESP Operating System)**. Every command includes its command-line syntax, options, functional description, hardware/subsystem capabilities, stack placement, and practical examples.

All binaries reside in `main/ebin/` and are exposed as executable nodes under `/bin/` via `binfs`.

---

## Quick Navigation Index

- [Core POSIX Utilities](#1-core-posix-utilities): `cat`, `cd`, `cp`, `echo`, `ls`, `mkdir`, `mv`, `ps`, `pwd`, `rm`, `rmdir`, `sleep`, `touch`, `tree`, `uptime`, `write`, plus the checksum and encoding tools `base64`, `cksum`, `md5sum`, `sha1sum`, `sha224sum`, `sha256sum`, `sha384sum`, `sha512sum`
- [System & Shell](#2-system--shell): `ush`, `launcher`, `mount`, `umount`
- [Hardware & Bus Control](#3-hardware--bus-control): `battery`, `devctl`, `lsi2c`, `soldercheck`
- [Network Management](#4-network-management): `netctl`, `wlanctl`
- [GUI Applications (LVGL)](#5-gui-applications-lvgl): `lvgl_calc`, `lvgl_keypad`, `lvgl_net`, `lvgl_snake`, `lvgl_sysmon`, `lvgl_term`
- [Diagnostics & Test Suites](#6-diagnostics--test-suites): `argvexpose`, `fdsetcheck`, `membench`, `test_coreutils`, `test_disp_bars`, `test_disp_colors`, `test_disp_doom_fire`, `test_disp_gradient`, `test_elf_loader`, `test_emisc`, `test_hash`, `test_input`, `test_lvgl`, `test_sdl`, `test_sdl2`, `test_sdl_input`, `test_sound`

---

## 1. Core POSIX Utilities

### `cat` — Concatenate and Print Files
* **Syntax:** `cat [file...]` or `... | cat`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Reads specified files or standard input (`-`) and writes content to standard output in 512-byte streaming chunks.
* **Capabilities:** Filesystem read (`O_RDONLY`).
* **Examples:**
  ```sh
  cat /proc/devices
  cat /proc/meminfo
  echo "hello" | cat
  ```

### `cd` — Change Working Directory
* **Syntax:** `cd [path]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Updates the calling process's Current Working Directory (`cwd`) in `eos_proc_t`. If no argument is provided, changes to `/`.
* **Capabilities:** Process context manipulation, directory traversal (`stat`, `chdir`).
* **Examples:**
  ```sh
  cd /dev
  cd ..
  cd /proc
  ```

### `cp` — Copy Files and Directories
* **Syntax:** `cp [-r|-R] [-f] [-v] SOURCE... DEST`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB SRAM)
* **Options:**
  * `-r`, `-R`, `--recursive`: Copy directories recursively.
  * `-f`, `--force`: Overwrite existing destination files without error.
  * `-v`, `--verbose`: Print names of copied files.
  * `-h`, `--help`: Display usage information.
* **Capabilities:** Recursive directory reading (`opendir`, `readdir`), file creation/truncation (`O_CREAT | O_WRONLY | O_TRUNC`), stream copying (4 KB chunks).
* **Examples:**
  ```sh
  cp /bin/init.sh /init.bak
  cp -r /bin /bin_backup
  cp -v file1.txt file2.txt /tmp/
  ```

### `echo` — Print Arguments to Standard Output
* **Syntax:** `echo [-n] [string...]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Options:**
  * `-n`: Do not output trailing newline.
* **Capabilities:** Standard I/O output stream.
* **Examples:**
  ```sh
  echo "Hello EOS"
  echo -n "counter: "
  echo "data" > /tmp/log.txt
  ```

### `ls` — List Directory Contents
* **Syntax:** `ls [path...]`
* **Flags:** `0` (Visible in launcher) | **Stack:** Default (8 KB SRAM)
* **Function:** Lists files and directories at the given path (defaults to current working directory). Formats output in tabular or multi-column format.
* **Capabilities:** Directory enumeration (`opendir`, `readdir`, `closedir`).
* **Examples:**
  ```sh
  ls
  ls /dev
  ls /proc
  ```

### `mkdir` — Create Directories
* **Syntax:** `mkdir [path...]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Creates one or more directories with default permissions (0777). Automatically handles relative and absolute paths.
* **Capabilities:** Filesystem mutation (`mkdir`).
* **Examples:**
  ```sh
  mkdir /tmp/test
  mkdir dir1 dir2 dir3
  ```

### `mv` — Move and Rename Files or Directories
* **Syntax:** `mv [-f] [-v] SOURCE... DEST`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB SRAM)
* **Options:**
  * `-f`, `--force`: Overwrite existing destination target.
  * `-v`, `--verbose`: Print source -> destination moves.
  * `-h`, `--help`: Display usage information.
* **Capabilities:** POSIX `rename()`, cross-device fallback (copy + remove), directory tree relocation.
* **Examples:**
  ```sh
  mv config.old config.new
  mv -v script.sh /tmp/
  ```

### `ps` — Report Process Status
* **Syntax:** `ps [-e|-A] [-f|-ef] [-l] [-h|--help]`
* **Flags:** `0` (Visible in launcher) | **Stack:** Default (8 KB SRAM)
* **Function:** Queries `/proc` to report a snapshot of active processes, their PIDs, PPIDs, state, controlling TTY, CPU run-time (`TIME`), start time (`STIME`), and command line (`CMD`).
* **Options:**
  * `-e`, `-A`: Select all processes (default).
  * `-f`, `-ef`: Full-format listing (`UID`, `PID`, `PPID`, `C`, `STIME`, `TTY`, `TIME`, `CMD`).
  * `-l`: Long-format listing (`S`, `UID`, `PID`, `PPID`, `C`, `TTY`, `TIME`, `CMD`).
* **Capabilities:** Enumerates `/proc`, parses `/proc/<pid>/status` and `/proc/<pid>/cmdline`.
* **Examples:**
  ```sh
  ps
  ps -ef
  ps -l
  ```

### `pwd` — Print Working Directory
* **Syntax:** `pwd`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Prints the absolute pathname of the calling thread's Current Working Directory via `getcwd()`.
* **Capabilities:** Thread context CWD query.
* **Examples:**
  ```sh
  pwd
  ```

### `rm` — Remove Files or Directories
* **Syntax:** `rm [-r|-R] [-f] [-v] FILE...`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB SRAM)
* **Options:**
  * `-r`, `-R`, `--recursive`: Recursively remove directories and their contents.
  * `-f`, `--force`: Ignore nonexistent files, never prompt.
  * `-v`, `--verbose`: Print each file/directory removed.
  * `-h`, `--help`: Display usage information.
* **Capabilities:** POSIX `unlink()`, recursive `rmdir()`, directory traversal.
* **Examples:**
  ```sh
  rm file.txt
  rm -rf /tmp/scratch_dir
  rm -v a.txt b.txt
  ```

### `rmdir` — Remove Empty Directories
* **Syntax:** `rmdir [dir...]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Removes specified empty directories via POSIX `rmdir()`. Fails with `ENOTEMPTY` if directory contains files (use `rm -r` for recursive deletion).
* **Capabilities:** Directory removal (`rmdir`).
* **Examples:**
  ```sh
  rmdir /tmp/empty_dir
  ```

### `sleep` — Suspend Execution for an Interval
* **Syntax:** `sleep NUMBER[s|ms|m|h]...`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** `EOS_BIN_MIN_STACK_SIZE` (4 KB SRAM)
* **Options:**
  * Time units: `s` (seconds, default), `ms` (milliseconds), `m` (minutes), `h` (hours).
  * Accepts multiple time arguments and sums them.
  * `-h`, `--help`: Display usage information.
* **Capabilities:** Thread suspension (`usleep`).
* **Examples:**
  ```sh
  sleep 100ms
  sleep 2s
  sleep 1s 500ms
  ```

### `touch` — Create Empty File or Update Timestamp
* **Syntax:** `touch [file...]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Creates an empty regular file if it does not exist, or updates its modification time.
* **Capabilities:** File creation (`open(O_CREAT | O_WRONLY, 0666)`).
* **Examples:**
  ```sh
  touch /tmp/flag.lock
  ```

### `tree` — Display Directory Tree Hierarchy
* **Syntax:** `tree [path]`
* **Flags:** `0` | **Stack:** `12288U` (12 KB SRAM)
* **Function:** Recursively walks filesystem directories and displays a formatted visual ASCII tree showing subdirectories and files.
* **Capabilities:** Recursive directory reading (`opendir`, `readdir`).
* **Examples:**
  ```sh
  tree
  tree /proc
  tree /dev
  ```

### `uptime` — Tell How Long System Has Been Running
* **Syntax:** `uptime [-h|--help]`
* **Flags:** `0` (Visible in launcher) | **Stack:** Default (8 KB SRAM)
* **Function:** Queries `/proc/uptime` and `/proc/loadavg` to display how long the system has been up and the 1, 5, and 15-minute exponentially damped load averages.
* **Capabilities:** Reads `/proc/uptime` and `/proc/loadavg`.
* **Examples:**
  ```sh
  uptime
  ```

### `write` — Write Text Directly to File
* **Syntax:** `write <string> <destination_path>`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Creates or overwrites destination file with the provided string argument.
* **Capabilities:** File creation and write (`open`, `write`, `close`).
* **Examples:**
  ```sh
  write "configured=true" /tmp/config.ini
  ```

### Checksums & Encoding

Eight small tools share one implementation (`emisc/eos_hash.h`, `emisc/eos_base64.h`). They read files or standard input in 4 KB chunks, so inputs of any size work with a few hundred bytes of stack. See [Hash, Checksum & Base64 Tools](./hash-and-checksum-tools.md) for design notes and differences from GNU coreutils.

### `sha256sum` — SHA-256 Message Digests
* **Syntax:** `sha256sum [-b|-t] [FILE]...` · `sha256sum -c [--status] [--quiet] [-w] LIST...`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Options:**
  * `-b`, `--binary`: print `*` before the file name; `-t`, `--text`: default.
  * `-c`, `--check`: read `<hex>  <name>` lines from LIST (or standard input) and verify each file. Blank lines and lines starting with `#` are ignored.
  * `--status`: print nothing, report only through the exit code. `--quiet`: do not print `OK` lines. `-w`, `--warn`: report malformed list lines.
  * `-h`, `--help`: Display usage information.
  * With no FILE, or when FILE is `-`, standard input is read.
* **Exit status:** `0` on success; `1` if a file cannot be read, a checksum does not match, or a check list contains no valid line.
* **Capabilities:** Streaming digest (`eos_hash_*`), constant-time comparison in check mode, periodic CPU yield on large files.
* **Examples:**
  ```sh
  sha256sum /sdcard/firmware.bin
  sha256sum /sdcard/*.bin > /tmp/sums.txt
  sha256sum -c /tmp/sums.txt
  cat /tmp/data | sha256sum
  ```

### `md5sum`, `sha1sum`, `sha224sum`, `sha384sum`, `sha512sum` — Other Message Digests
* **Syntax and options:** identical to `sha256sum`.
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Notes:** `md5sum` and `sha1sum` exist for compatibility with published checksums only. They are not collision resistant and must not be used to decide whether a file is trusted.
* **Examples:**
  ```sh
  md5sum /sdcard/archive.zip
  sha512sum -c /sdcard/SHA512SUMS
  ```

### `cksum` — POSIX CRC Checksum
* **Syntax:** `cksum [FILE]...`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Output:** `<crc> <bytes> <name>` (no name for standard input), identical to GNU `cksum`.
* **Examples:**
  ```sh
  cksum /tmp/log.txt
  cat /tmp/log.txt | cksum
  ```

### `base64` — Base64 Encode or Decode
* **Syntax:** `base64 [-d] [-i] [-w COLS] [FILE]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Options:**
  * `-d`, `--decode`: decode instead of encode.
  * `-i`, `--ignore-garbage`: when decoding, skip characters outside the Base64 alphabet.
  * `-w COLS`, `--wrap=COLS`: wrap encoded lines after COLS characters (default 76; `0` disables wrapping).
  * Short options can be combined (`-dw0`). With no FILE, or when FILE is `-`, standard input is read.
  * Decoding skips LF and CR; any other invalid character is an error unless `-i` is given.
* **Exit status:** `0` on success, `1` for unreadable input or invalid Base64.
* **Examples:**
  ```sh
  echo hello | base64
  echo aGVsbG8K | base64 -d
  base64 -w 0 /sdcard/key.bin > /tmp/key.b64
  ```

---

## 2. System & Shell

### `ush` — EOS MicroShell
* **Syntax:** `ush [script_path]`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** `USH_STACK_SIZE` (16 KB)
* **Function:** Interactive command-line shell interpreter. Supports:
  * Standard I/O redirection: `<`, `>`, `>>`, `2>`, `2>&1`, `&>`
  * Pipelines: `cmd1 | cmd2 | cmd3`
  * Chaining operators: `&&`, `||`, `;`
  * Background jobs: `cmd &`
  * Pratt expression parser with PSRAM-migrated `kvec` AST nodes.
* **Capabilities:** Process spawning (`execve`), pipe creation (`pipe`), environment variables, script interpretation.
* **Examples:**
  ```sh
  ush
  ush /bin/init.sh
  ```

### `launcher` — Graphical Application Launcher
* **Syntax:** `launcher`
* **Flags:** `EOS_BIN_FLAG_HIDDEN | EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Renders an LVGL-based graphical desktop launcher with categorized tabs (`Apps`, `Games`, `Tools`, `Tests`, `System`, `Utils`). Filters out binaries with `EOS_BIN_FLAG_HIDDEN`. Executes selected apps via `system()`.
* **Capabilities:** Framebuffer (`/dev/fb0`), input events (`/dev/keyboard0` or touchscreen), `.eos_bins` traversal.

### `mount` — Mount Block Device Filesystem
* **Syntax:** `mount <device_path> <mount_target>`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Issues `EOS_STORAGE_IOCTL_MOUNT` to block devices (e.g. `/dev/sd0`) to attach FATFS or other filesystem to target path.
* **Capabilities:** Storage ioctl (`EOS_STORAGE_IOCTL_MOUNT`), VFS mount registration.
* **Examples:**
  ```sh
  mount /dev/sd0 /sdcard
  ```

### `umount` — Unmount Storage Filesystem
* **Syntax:** `umount <device_path>`
* **Flags:** `EOS_BIN_FLAG_HIDDEN` | **Stack:** Default (8 KB SRAM)
* **Function:** Issues `EOS_STORAGE_IOCTL_UMOUNT` to block devices to cleanly unmount filesystem volumes and flush cached sectors.
* **Capabilities:** Storage ioctl (`EOS_STORAGE_IOCTL_UMOUNT`).
* **Examples:**
  ```sh
  umount /dev/sd0
  ```

---

## 3. Hardware & Bus Control

### `devctl` — Device Tree & Driver Control
* **Syntax:** `devctl <command> [args...]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Subcommands:**
  * `devctl scopes`: Lists registered driver scopes from `/proc/scopes`.
  * `devctl drivers [scope]`: Lists registered drivers from `/proc/drivers`.
  * `devctl devices`: Lists active device nodes and parents from `/proc/devices`.
  * `devctl attach <scope> <driver> [parent] [--pin key=val,...] [--cfg key=val,...]`: Attaches a device dynamically.
  * `devctl detach <dev_name>`: Recursively detaches device and all children from device tree.
  * `devctl info <dev_name>`: Displays device status, attached parent, child nodes, pins, and config.
* **Capabilities:** Dynamic devtree mutation (`eos_dev_attach`, `eos_dev_detach`), procfs inspection.
* **Examples:**
  ```sh
  devctl devices
  devctl attach bus i2c root --pin sda=10,scl=11 --cfg port=0
  devctl attach sound max98357 root --pin bclk=42,ws=1,dout=2 --cfg rate=22050
  devctl detach sound0
  devctl info fb0
  ```

### `battery` — Battery State of Charge & Voltage Monitor
* **Syntax:** `battery [-r|--raw] [path]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Options:**
  * `-r`, `--raw`: Dump raw `/dev/battery0` VFS properties (`present`, `status`, `percentage`, `voltage_mv`, `cell_mv`).
  * `path`: Optional custom battery device path (default: `/dev/battery0`).
* **Capabilities:** Battery driver VFS read / ioctl (`EOS_BATTERY_IOCTL_GET_INFO`).
* **Examples:**
  ```sh
  battery
  battery -r
  ```

### `lsi2c` — I2C Bus Scanner & Device Identification
* **Syntax:** `lsi2c [device_path]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Probes I2C 7-bit addresses (0x03 to 0x77) on specified bus (default: auto-detects `/dev/i2c*` via `opendir`), prints hex detection grid using `EOS_I2C_IOCTL_PROBE`, enumerates attached child devices via `opendir`/`readdir`, and matches ACKs against built-in database of 80+ known sensors/codecs/controllers.
* **Capabilities:** Standard VFS ioctl (`EOS_I2C_IOCTL_GET_PINS`, `EOS_I2C_IOCTL_PROBE`), directory traversal via `opendir`/`readdir`.
* **Examples:**
  ```sh
  lsi2c
  lsi2c /dev/i2c0
  ```

### `soldercheck` — Hardware Short & Solder Bridge Diagnostic
* **Syntax:** `soldercheck`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Scans `/dev/gpio/*`, drives test patterns across available user GPIO pins with pull-ups and pull-downs, detecting shorts to GND, VCC, and pin-to-pin solder bridges during PCB manufacturing.
* **Capabilities:** Direct GPIO VFS manipulation (`/dev/gpio/*`), GPIO ioctls (`SET_PULLUP`, `SET_PULLDOWN`, `SET_DIRECTION`).

---

## 4. Network Management

### `netctl` — Network Link & IP Address Configuration
* **Syntax:** `netctl [ OBJECT | COMMAND ] [ args... ]`
* **Flags:** `0` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB SRAM)
* **Options & Subcommands (Linux `ip` vibe):**
  * `netctl link [show [dev]]`: Displays network interfaces, MAC addresses, and operational flags.
  * `netctl link set <dev> up|down`: Brings network interface up or down.
  * `netctl link set <dev> master <bridge>`: Slaves interface to network bridge.
  * `netctl link set <dev> nomaster`: Detaches interface from bridge.
  * `netctl addr [show [dev]]`: Displays IP addresses, netmasks, gateways, and DHCP status.
  * `netctl addr add <ip/prefix> dev <dev> [gw <gateway>]`: Sets static IPv4 address.
  * `netctl <dev> connect <ssid> [password]`: Connects STA interface to Wi-Fi AP.
  * `netctl <dev> disconnect`: Disconnects STA interface from AP.
  * `netctl <dev> ap <ssid> [password] [channel]`: Configures SoftAP settings.
  * `netctl <dev> clients`: Lists connected stations on SoftAP.
  * `netctl <bridge> add <port> | del <port>`: Adds or removes bridge port.
  * `netctl <bridge> napt on|off`: Enables/disables IP network address translation.
* **Capabilities:** lwIP TCP/IP stack (`esp_netif`), interface management, STA/AP/bridge ioctls.
* **Examples:**
  ```sh
  netctl link
  netctl addr
  netctl sta0 connect MyWiFi secretPass
  netctl link set sta0 up
  netctl addr add 192.168.1.50/24 dev sta0 gw 192.168.1.1
  ```

### `wlanctl` — Physical WLAN Radio & Packet Sniffer
* **Syntax:** `wlanctl <command> [args...] [interface]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Subcommands:**
  * `wlanctl radio on|off|status [dev]`: Turns physical RF radio on/off or queries state.
  * `wlanctl getmac [dev]`: Displays hardware MAC address.
  * `wlanctl setmac <mac> [dev]`: Assigns custom MAC address (`XX:XX:XX:XX:XX:XX`).
  * `wlanctl channel <1-13> [dev]`: Reconfigures physical RF channel.
  * `wlanctl sniffer on|off [dev]`: Toggles promiscuous 802.11 sniffer mode.
  * `wlanctl dump [count] [dev] [opts]`: Captures raw 802.11 frames. Options:
    * `--type <mgt|data|ctrl>`: Filter by frame type.
    * `--bssid <mac>`: Filter by BSSID.
    * `--src <mac>`, `--dst <mac>`: Filter by source/destination MAC.
    * `--hop <ms>`: Enable periodic background channel hopping.
    * `--verbose`: Hex dump full packet payloads.
    * `--pcap <file>`: Stream captured packets into standard PCAP file on VFS.
  * `wlanctl send <hex-bytes> [dev]`: Injects raw 802.11 frame directly into RF medium.
  * `wlanctl scan [-p|--passive] [ch]`: Scans RF spectrum for wireless access points.
* **Capabilities:** Wi-Fi physical radio (`EOS_WIFI_IOCTL_*`), raw packet queue (PSRAM), PCAP generation.
* **Examples:**
  ```sh
  wlanctl scan
  wlanctl radio status
  wlanctl channel 6
  wlanctl sniffer on
  wlanctl dump 50 --hop 250 --pcap /sdcard/capture.pcap
  ```

---

## 5. GUI Applications (LVGL)

All GUI binaries automatically declare `EOS_BIN_FLAG_GUI` and execute on dedicated **16 KB external PSRAM stacks**, requiring **0 internal DRAM bytes**.

### `lvgl_calc` — Graphical Calculator
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Interactive calculator supporting decimal and hexadecimal arithmetic, styled with LVGL buttons and keypad/touch navigation.

### `lvgl_keypad` — Keypad Matrix Visualizer
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Visual matrix of handheld buttons displaying real-time press/release highlights, keycodes, and debounce latencies.

### `lvgl_net` — Graphical Network Manager
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Visual Wi-Fi scanner, SSID selection, password entry modal, signal strength RSSI bars, STA/AP status panel, and IP assignment interface.

### `lvgl_snake` — Snake Arcade Game
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Classic Snake game with scoring, food generation, speed ramping, high-score tracking, and keypad input controls.

### `lvgl_sysmon` — System Resource Monitor
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Real-time graphical dashboard showing CPU load gauges, DRAM/PSRAM/DMA memory consumption bars, system uptime, and display framerate (FPS).

### `lvgl_term` — Graphical Terminal Viewer
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Embedded graphical terminal emulator allowing interactive shell command execution directly on the handheld screen.

---

## 6. Diagnostics & Test Suites

### `argvexpose` (`argvdump`) — Command-Line Argument Verifier
* **Syntax:** `argvdump [args...]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Dumps `argc` count and enumerates every `argv[i]` index and string for shell argument parsing and escaping verification.

### `fdsetcheck` — File Descriptor Capacity Test
* **Syntax:** `fdsetcheck`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Validates the maximum concurrent open file descriptors against POSIX `FD_SETSIZE` limits, checking table allocation bounds in `ecore/vfs.c`.

### `membench` — Memory Throughput Benchmark
* **Syntax:** `membench`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Measures sequential read, write, and `memcpy` throughput (in MB/s) across block sizes (64 B to 64 KB) in Internal DRAM, External PSRAM, and DMA SRAM.

### `test_coreutils` — Automated POSIX Core Utilities Test Suite
* **Syntax:** `test_coreutils`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Executes 33 automated tests validating `rm`, `cp`, `mv`, `sleep`, and `tree` commands, file boundaries, recursive directories, error codes, and edge cases.

### `test_disp_bars` — SMPTE Color Bars Display Test
* **Syntax:** `test_disp_bars`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB PSRAM)
* **Function:** Renders RGB565 SMPTE color bars, gradient grayscales, and perimeter alignment borders to `/dev/fb0`.

### `test_disp_colors` — Primary Solid Color Display Cycle
* **Syntax:** `test_disp_colors`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB PSRAM)
* **Function:** Cycles full-screen solid Red, Green, Blue, White, and Black frames to detect dead pixels and color reproduction.

### `test_disp_doom_fire` — Classic Doom Fire Particle Animation
* **Syntax:** `test_disp_doom_fire`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB PSRAM)
* **Function:** Real-time procedural Doom fire particle simulation benchmarking direct framebuffer rendering throughput and FPS.

### `test_disp_gradient` — Animated Color Gradient Benchmark
* **Syntax:** `test_disp_gradient`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_DEFAULT_STACK_SIZE` (8 KB PSRAM)
* **Function:** Computes dynamic sine-wave animated color gradients, measuring peak sustained display refresh framerates.

### `test_elf_loader` — Dynamic ELF Loader & Standalone App Test Suite
* **Syntax:** `test_elf_loader`
* **Flags:** `0` | **Stack:** `16384U` (16 KB SRAM/PSRAM)
* **Function:** Executes 13 automated tests verifying dynamic relocatable ELF file detection, staging to `/tmp/hello.elf`, PSRAM execution via absolute path, standard I/O redirection (`>`), command-line argument passing (`argc`, `argv`), relative `./` execution from current directory, and `$PATH` directory resolution.

### `test_emisc` — emisc Library Unit Tests
* **Syntax:** `test_emisc`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Comprehensive unit tests for `emisc` components: dynamic vectors (`kvec`), hierarchical trees (`fancytree`), FNV-1a hashing, and network utilities (`netmisc`).

### `test_hash` — Hash, Checksum & Base64 Test Suite
* **Syntax:** `test_hash`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Executes 62 automated checks: published digest vectors for MD5 and the SHA family, block-boundary padding lengths, chunked streaming, CRC-32 and `cksum` values, hex helpers, RFC 4648 Base64 vectors and error cases, and the exit codes of the command-line tools (check mode, tampered and missing files, invalid options).

### `test_input` — Input Event Monitor
* **Syntax:** `test_input [max_events] [/dev/input_path]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Function:** Listens to `/dev/keyboard0` (or specified input device) and prints formatted key event codes, button names, and press/release transitions.

### `test_lvgl` — LVGL Widget & Driver Demo
* **Syntax:** `test_lvgl`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `EOS_BIN_GUI_STACK_SIZE` (16 KB PSRAM)
* **Function:** Demonstrates LVGL widget capabilities: interactive sliders, button counters, animated progress bars, and keypad navigation.

### `test_sdl` — SDL 1.2 Compatibility Layer Test
* **Syntax:** `test_sdl`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `12288U` (12 KB PSRAM)
* **Function:** Validates the SDL 1.2 compatibility layer (`SDL_SetVideoMode`, `SDL_CreateRGBSurface`, `SDL_BlitSurface`, `SDL_Flip`) over `/dev/fb0`.

### `test_sdl2` — SDL 2.0 Compatibility Layer Test
* **Syntax:** `test_sdl2`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `12288U` (12 KB PSRAM)
* **Function:** Validates SDL 2.0 APIs (`SDL_CreateWindow`, `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_RenderCopy`, `SDL_RenderPresent`).

### `test_sdl_input` — SDL Input Translation Test
* **Syntax:** `test_sdl_input`
* **Flags:** `EOS_BIN_FLAG_GUI` | **Stack:** `12288U` (12 KB PSRAM)
* **Function:** Validates SDL event translation and keypad keycode mapping (`SDL_PollEvent`, `SDL_KEYDOWN`, `SDL_KEYUP`).

### `test_sound` — Audio Waveform Synthesis & Playback
* **Syntax:**
  * `test_sound [freq_hz] [duration_ms] [volume] [multiplier] [flags] [device_path]`
  * `test_sound march [-t duration_ms] [volume] [flags]`
* **Flags:** `0` | **Stack:** Default (8 KB SRAM)
* **Options:**
  * `march`: Play Star Wars Imperial March theme.
  * `-t`, `--time <ms>`: Override playback duration.
  * `--verbose`: Print detailed per-chunk time shift logs, DMA latency, and driver metrics.
* **Capabilities:** PCM audio streaming (`/dev/sound0`), I2S DMA playback, sample rate configuration (`EOS_SOUND_IOCTL_*`).
* **Examples:**
  ```sh
  test_sound 440 500 30
  test_sound march -t 3200 30
  test_sound 440 1000 30 --verbose
  ```
