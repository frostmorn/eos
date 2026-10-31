# EOS ebin Analysis & Standard Utility Implementation Roadmap

This document provides a comprehensive audit of all native executable binaries (`ebin`) currently implemented in **EOS (ESP Operating System)**, identifies critical functional and operational gaps, and outlines a prioritized roadmap of standard POSIX / UNIX / embedded utilities to implement to dramatically enhance EOS usability, developer productivity, and system administration.

---

## 1. Executive Summary & Architecture Context

In EOS, executable binaries reside in [`main/ebin/`](file:///home/ai/eos/main/ebin/) and are registered statically at compile time into the `.eos_bins` ELF linker section via `eos_bin_t` manifests ([`ecore/bin.h`](file:///home/ai/eos/main/ecore/bin.h)). 

The kernel VFS subsystem mounts [`binfs`](file:///home/ai/eos/main/ecore/binfs.c) at `/bin`, presenting every registered binary as an executable node (e.g., `/bin/ls`, `/bin/cat`). Execution is managed via [`exec.c`](file:///home/ai/eos/main/ecore/exec.c) and [`syscalls.c`](file:///home/ai/eos/main/ewrap/syscalls.c), which spawn joinable POSIX pthreads isolated with per-thread context (`eos_tctx_t`).

The EOS MicroShell ([`ush`](file:///home/ai/eos/main/ebin/ush/ush.c)) and system execution layer support:
*   Standard I/O redirection (`<`, `>`, `>>`, `2>`, `2>&1`, `&>`).
*   Command pipelining (`|`).
*   Chaining operators (`&&`, `||`, `;`).
*   Asynchronous background tasks (`&`).

### Current Status
*   **Total directories in `main/ebin/`:** 41
*   **Active functional commands:** 40
*   **Empty / Stub directories:** 1 (`flashprot`)

While EOS features rich graphical demos, games, test harnesses, and hardware-control commands (`devctl`, `wlanctl`, `netctl`), **basic POSIX file management and system observability are severely lacking**. Most critically:
1.  **There is no `rm` command** (regular files cannot be deleted; only empty folders with `rmdir`).
2.  **There is no `cp` or `mv` command** (files cannot be copied, backed up, or renamed).
3.  **There is no `grep` command** (piping exists in the shell, but stream filtering is impossible).
4.  **There are no inspection tools** like `head`, `tail`, `wc`, `hexdump` / `xxd`, `stat`, or `df`.
5.  **There is no process / task management** (`ps` or `kill`).
6.  **There is no `sleep` command** (crucial for declarative shell scripting like `init.sh`).
7.  **There is no `ping` command** (network connectivity cannot be verified from the CLI).

---

## 2. Complete Inventory of Existing ebins

Below is the complete inventory of all 41 existing `ebin` entries across EOS:

| Directory | Command (`filename`) | Name (`.name`) | Group (`.group`) | Flags | Purpose / Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `argvexpose/` | `argvdump` | Argv Dump | `Diag` | `0` | Diagnostic tool to dump `argc` and `argv` parameters for verifying argument parsing. |
| `battery/` | `battery` | Battery Monitor | `System` | `0` | Inspects `/dev/battery0` via ADC driver for state-of-charge (SoC), millivolts, and charging status. |
| `cat/` | `cat` | Concatenate File | `Utils` | `HIDDEN` | Reads files or stdin and streams contents to stdout in 512-byte chunks. |
| `cd/` | `cd` | Change Directory | `Utils` | `HIDDEN` | Updates the current working directory in the calling thread context (`chdir()`). |
| `devctl/` | `devctl` | Device Control Utility | `System` | `0` | Inspects `/proc` nodes, lists scopes/drivers/devices, attaches/detaches devtree nodes dynamically. |
| `echo/` | `echo` | Echo | `Utils` | `HIDDEN` | Prints command line arguments to stdout separated by spaces (supports `-n`). |
| `fdsetcheck/` | `fdsetcheck` | FD Set Check | `Diag` | `0` | Diagnostic tool validating maximum concurrent open file descriptors against `FD_SETSIZE`. |
| `flashprot/` | *(None)* | *(Empty Stub)* | *(None)* | *(None)* | Empty directory; no manifest or implementation currently present. |
| `launcher/` | `launcher` | Launcher | `System` | `HIDDEN \| GUI` | Graphical application launcher menu rendered with LVGL featuring category tabs and command execution. |
| `ls/` | `ls` | List Directory | `Utils` | `0` | Lists directory entry names (basic enumeration without long listing or flags). |
| `lsi2c/` | `lsi2c` | List I2C Devices | `Utils` | `0` | Probes system I2C bus addresses (0x08..0x77) and reports active peripheral ACKs. |
| `lvgl_calc/` | `lvgl_calc` | Calculator | `Apps` | `GUI` | Keypad-driven graphical calculator supporting decimal and hexadecimal arithmetic. |
| `lvgl_keypad/` | `lvgl_keypad` | Keypad Tester | `Tests` | `GUI` | Graphical utility displaying visual matrix of keypad buttons with live press feedback. |
| `lvgl_net/` | `lvgl_net` | Network Manager | `Tools` | `GUI` | Graphical Wi-Fi scanner, STA/AP configuration panel, and network status monitor. |
| `lvgl_snake/` | `lvgl_snake` | Snake | `Games` | `GUI` | Classic Snake arcade mini-game running on LVGL with score and speed levels. |
| `lvgl_sysmon/` | `lvgl_sysmon` | System Monitor | `Tools` | `GUI` | Real-time graphical monitor displaying uptime, DRAM, PSRAM, DMA bars, and LVGL FPS. |
| `lvgl_term/` | `lvgl_term` | Terminal Viewer | `Tools` | `GUI` | Interactive graphical terminal emulator and command runner. |
| `membench/` | `membench` | Memory Benchmark | `Tools` | `0` | Benchmarks sequential read, write, and copy throughput across DRAM, PSRAM, and DMA memory. |
| `mkdir/` | `mkdir` | Make Directory | `Utils` | `HIDDEN` | Creates directories using POSIX `mkdir()` with default 0777 mode. |
| `mount/` | `mount` | Mount Filesystem | `System` | `HIDDEN` | Issues `EOS_STORAGE_IOCTL_MOUNT` to block devices to mount FAT/storage filesystems. |
| `netctl/` | `netctl` | Netctl | `Network` | `0` | Configures network interfaces, IP addresses, netmasks, gateways, and interface link flags (ip link/addr equivalent). |
| `pwd/` | `pwd` | Print Working Directory | `Utils` | `0` | Prints the absolute path of the current working directory via `getcwd()`. |
| `rmdir/` | `rmdir` | Remove Directory | `Utils` | `HIDDEN` | Removes empty directories from the filesystem via `rmdir()`. |
| `soldercheck/` | `soldercheck` | Solder Check | `Diag` | `0` | Hardware manufacturing diagnostic checking GPIO pin states for solder shorts. |
| `test_disp_bars/` | `test_disp_bars` | Display Bars | `Tests` | `GUI` | Renders SMPTE-style RGB565 color bars and alignment borders to the active display driver. |
| `test_disp_colors/`| `test_disp_colors` | Display Colors | `Tests` | `GUI` | Cycles primary solid colors (Red, Green, Blue, White, Black) across the display. |
| `test_disp_doom_fire/` | `test_disp_doom_fire` | Doom Fire | `Tests` | `GUI` | Procedural Doom fire particle animation benchmark rendering directly to framebuffer. |
| `test_disp_gradient/` | `test_disp_gradient` | Display Gradient | `Tests` | `GUI` | Real-time animated color gradient measuring peak display FPS. |
| `test_emisc/` | `test_emisc` | emisc Unit Tests | `Tests` | `0` | Unit tests for `emisc` utilities (vector, tree, fnv-1a, netmisc). |
| `test_input/` | `test_input` | Input Test | `Tests` | `0` | Listens to `/dev/keyboard0` and prints key event codes and press/release states. |
| `test_lvgl/` | `test_lvgl` | LVGL Test | `Tests` | `GUI` | Demonstrates LVGL widgets, button counters, and slider controls. |
| `test_sdl/` | `test_sdl` | SDL 1.2 Test | `Tests` | `GUI` | Validates the SDL 1.2 compatibility layer surface allocation and blitting. |
| `test_sdl2/` | `test_sdl2` | SDL2 Test | `Tests` | `GUI` | Validates SDL2 compatibility layer window, renderer, and texture pipelines. |
| `test_sdl_input/`| `test_sdl_input` | SDL Input Test | `Tests` | `GUI` | Validates SDL event translation and keypad keycode mapping. |
| `test_sound/` | `test_sound` | Sound Test | `Tests` | `0` | Plays audio tones or the Imperial March theme through `/dev/sound0` with configurable sample rate & duration. |
| `touch/` | `touch` | Touch File | `Utils` | `HIDDEN` | Creates an empty file or opens in append mode to simulate touch. |
| `tree/` | `tree` | Directory Tree | `Utils` | `0` | Recursively walks directories and prints a visual ASCII/Unicode hierarchy tree. |
| `umount/` | `umount` | Unmount Filesystem | `System` | `HIDDEN` | Issues `EOS_STORAGE_IOCTL_UMOUNT` to block devices to unmount storage volumes. |
| `ush/` | `ush` | MicroShell | `System` | `HIDDEN` | EOS interactive shell interpreter supporting script execution, pipes, and jobs. |
| `wlanctl/` | `wlanctl` | WLAN Radio Control | `Network` | `0` | Controls Wi-Fi radio state, scans APs, manages STA/AP modes, sniffer, and raw frame injection. |
| `write/` | `write` | Write File | `Utils` | `HIDDEN` | Writes string argument directly to a target file (`write <string> <file>`). |

---

## 3. Detailed Gaps & Operational Pain Points

Analyzing the current commands reveals several key obstacles to everyday use and debugging:

### 1. File Deletion is Impossible for Files
*   [`rmdir`](file:///home/ai/eos/main/ebin/rmdir/rmdir.c) only calls POSIX `rmdir()`. It will fail with `ENOTEMPTY` on directories with content and `ENOTDIR` on regular files.
*   **Result:** Once a file is created on LittleFS, FAT, or SDCard, there is no shell command to delete it! An `rm` utility is urgently needed.

### 2. No File Copying or Renaming
*   Neither `cp` nor `mv` is implemented.
*   **Result:** Users cannot duplicate configuration files, back up scripts, or move files between directories or mount points.

### 3. Pipeline Inutility (No Text Stream Filters)
*   EOS provides a sophisticated shell parser supporting pipelines (`cmd1 | cmd2 | cmd3`), but **no stream processing tools exist**.
*   **Missing:** `grep` (filter lines), `head`/`tail` (slice lines), `wc` (count lines/bytes), and `tee` (split streams).
*   **Result:** Commands like `cat /proc/devices` or `tree` output large amounts of text that cannot be filtered or inspected conveniently on small screens or serial consoles.

### 4. Binary & Hardware Inspection Blindspot
*   No `hexdump` or `xxd` command exists.
*   **Result:** Developers debugging raw block storage partitions (`/dev/sdio0p1`), SPI flash dumps, network packet payloads, or binary assets cannot view hexadecimal/ASCII memory representations.

### 5. Memory & Uptime Require Raw Procfs Manipulation
*   To check memory, users must execute `cat /proc/meminfo` or `cat /proc/heapinfo`.
*   To check uptime, users must execute `cat /proc/uptime`.
*   **Result:** Clunky and non-standard. Simple `free` and `uptime` commands should provide human-friendly formatting.

### 6. Process & Background Task Invisibility
*   EOS supports running tasks in the background with `&` (e.g. `launcher &`).
*   However, there is no `ps` command to inspect running FreeRTOS tasks (task name, stack high-water mark, priority, core ID) and no `kill` command to terminate rogue or background tasks.

### 7. Scripting Gaps in `init.sh`
*   EOS executes declarative startup scripts at boot (`init.sh`).
*   Without a standard `sleep` command, scripts cannot introduce delays (e.g. waiting for SD card stabilization or peripheral power rails before mounting).

### 8. Network Connectivity Cannot Be Tested
*   EOS has drivers for Wi-Fi (`wifi0`, `sta0`, `ap0`, `bridge0`) and utilities (`wlanctl`, `netctl`), but **no `ping` command**.
*   **Result:** After connecting to a Wi-Fi network, there is no way to verify gateway reachability, DNS, or Internet access from the CLI.

### 9. Non-Standard / Redundant Utilities
*   `write <string> <file>` was created before shell redirection was fully functional. Now that `echo "text" > file` is supported, `write` is redundant and non-standard.
*   `ls` is extremely basic: it lacks `-l` (permissions, file sizes, modification times), `-a` (show dotfiles), and `-h` (human-readable units).

---

## 4. Prioritized Implementation Roadmap

To make EOS significantly more practical, standard utilities should be implemented across three prioritized tiers.

### Tier 1: Core Essentials ("Must Have")
*These utilities resolve critical operational roadblocks and provide standard POSIX system expectations.*

| Utility | Command Syntax | Description & Expected Flags | Implementation Strategy in EOS |
| :--- | :--- | :--- | :--- |
| **`rm`** | `rm [-rf] <path...>` | Remove files and directories. Supports `-r`/`-R` (recursive) and `-f` (force/ignore missing). | Uses POSIX `unlink()` for files and recursive directory traversal for `-r`. Respects read-only filesystems. |
| **`cp`** | `cp [-r] <src> <dst>` | Copy file or directory hierarchy. Supports `-r`/`-R` (recursive) and `-f` (overwrite). | Streaming 1 KB buffer copy loop via `open()` / `read()` / `write()`. For directories, iterates with `opendir()` and creates destination folders with `mkdir()`. |
| **`mv`** | `mv [-f] <src> <dst>` | Move / rename files and directories. | Tries POSIX `rename(src, dst)`. If `rename()` fails with `EXDEV` (cross-device/mount move), falls back to `cp` + `rm`. |
| **`grep`** | `grep [-ivnc] <pattern> [file...]` | Search for lines matching a pattern in files or stdin. Supports `-i` (case-insensitive), `-v` (invert), `-n` (line numbers), `-c` (count). | Reads line-by-line using `getline()` or `fgets()`. Uses `strstr()` or `strcasestr()` for substring matching (or lightweight regex from `regex.h`). Streams to stdout. |
| **`sleep`** | `sleep <seconds>` | Pause execution for a specified duration in seconds or fractional seconds (e.g., `sleep 0.5`). | Converts input to microseconds and invokes `usleep()` or `vTaskDelay()`. Essential for `init.sh` startup scripts. |
| **`free`** | `free [-h\|-k\|-m]` | Display free, used, and minimum free memory in system. | Queries ESP-IDF heap APIs (`heap_caps_get_free_size`, `heap_caps_get_total_size`) for Internal DRAM, SPIRAM/PSRAM, and DMA. Formats in clean tabular layout with human-readable units. |
| **`uptime`** | `uptime` | Display system uptime, current tick count, and reset reason. | Reads tick counter via `xTaskGetTickCount()`, computes days/hours/minutes/seconds, and queries `esp_reset_reason()`. |
| **`ps`** | `ps [-a]` | List active FreeRTOS tasks / EOS threads, state, priority, stack watermark, and CPU core affinity. | Calls `uxTaskGetSystemState()` or reads FreeRTOS task status. Formats into a clean tabular output. *(Requires `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` in `sdkconfig`)*. |
| **`reboot`** | `reboot [-f]` / `reset` | Perform clean hardware / software restart of the MCU. | Calls `esp_restart()`. Flushes stdout and unmounts active filesystems if possible. |
| **`clear`** | `clear` | Clear terminal screen and reset cursor position. | Emits standard ANSI escape sequence: `\033[2J\033[H`. |
| **`ping`** | `ping [-c count] [-i interval] <host>` | Send ICMP ECHO_REQUEST packets to network hosts. | Uses ESP-IDF's lwIP ping socket API (`ping/ping_sock.h`). Reports transmitted/received packets, packet loss, and min/avg/max RTT. |

---

### Tier 2: System & Diagnostic Enhancements ("Should Have")
*These utilities expand developer productivity, scripting robustness, and diagnostic depth.*

| Utility | Command Syntax | Description & Expected Flags | Implementation Strategy in EOS |
| :--- | :--- | :--- | :--- |
| **`hexdump` / `xxd`** | `hexdump [-C] [-n len] [-s offset] <file>` | Display file or stdin in canonical hex + ASCII format. | Reads 16-byte chunks and formats into traditional 3-column view: offset, hex bytes, ASCII characters. Essential for analyzing partitions and assets. |
| **`head`** | `head [-n lines] [file...]` | Print the first N lines of files or stdin (default 10). | Reads line-by-line until line limit is reached; supports stdin piping. |
| **`tail`** | `tail [-n lines] [file...]` | Print the last N lines of files or stdin (default 10). | Circular line buffer in memory (bounded stack or heap) storing the last N lines, printed on EOF. |
| **`wc`** | `wc [-lwmcz] [file...]` | Count lines (`-l`), words (`-w`), characters/bytes (`-c`). | Streaming character classifier tracking newline transitions and whitespace boundaries. |
| **`df`** | `df [-h]` | Report filesystem disk space usage (total, used, free, percentage, mount point). | Inspects registered VFS mount paths via `statvfs()` or custom storage driver ioctl (`EOS_STORAGE_IOCTL_GET_SECTOR_COUNT`). |
| **`uname`** | `uname [-a\|-s\|-r\|-m]` | Print system information, OS name, release version, and target chip architecture. | Reports `EOS 1.0.0`, ESP-IDF version (`esp_get_idf_version()`), and chip model (`ESP32-S3` / `ESP32-P4`). |
| **`dmesg`** | `dmesg [-c]` | Print the kernel / system log buffer. Supports `-c` to clear buffer after reading. | Reads directly from the `ecore/log.c` ring buffer to display boot logs and recent driver events. |
| **`date`** | `date [-s "YYYY-MM-DD HH:MM:SS"]` | Display or configure system wall-clock date and time. | Uses POSIX `time()`, `gettimeofday()`, and `localtime_r()`. |
| **`find`** | `find <path> [-name glob] [-type f\|d]` | Recursively search filesystem directory tree matching criteria. | Recursive `opendir()`/`readdir()` with `fnmatch()` pattern filtering. |
| **`tee`** | `tee [-a] <file...>` | Read from stdin and write simultaneously to stdout and specified files. | Streaming loop writing chunks to `stdout` and all open file descriptors. |
| **`kill`** | `kill <task_id\|task_name>` | Request cancellation / termination of a background task or thread. | Matches FreeRTOS task handle and signals thread cancellation or `vTaskDelete()`. |

---

### Tier 3: Advanced Network, Hardware & Media Tools ("Nice to Have")
*High-value specialized additions for portable handhelds and IoT applications.*

| Utility | Command Syntax | Description & Expected Flags | Implementation Strategy in EOS |
| :--- | :--- | :--- | :--- |
| **`wget` / `curl`** | `wget [-O file] <url>` | Download a file or fetch content over HTTP/HTTPS. | Uses ESP-IDF `esp_http_client`. Supports writing payload to stdout (for pipes) or to a file. |
| **`sntp`** | `sntp [server]` | Query an NTP server and synchronize system RTC clock. | Wraps `esp_sntp` component to obtain UTC network time. |
| **`i2cget` / `i2cset`** | `i2cget <bus> <chip> <reg>`<br>`i2cset <bus> <chip> <reg> <val>` | Read or write specific registers of an I2C slave device. | Opens `/dev/i2c0`, executes direct register read/write transactions via I2C ioctls. |
| **`gpio`** | `gpio read <pin>`<br>`gpio write <pin> <0\|1>`<br>`gpio mode <pin> <in\|out\|pullup>` | Direct CLI manipulation and monitoring of individual GPIO pins. | Uses `/dev/gpio/<pin>` or `gpio_*` driver interface for rapid pin hardware debugging. |
| **`diff` / `cmp`** | `cmp <file1> <file2>` | Compare two files byte-by-byte and report differences. | Dual-file streaming read loop reporting first mismatched byte offset. |
| **`sort` / `uniq`** | `sort [file]`<br>`uniq [file]` | Sort text lines alphabetically and filter duplicate lines. | Memory-bounded line collection and quicksort; filters adjacent duplicate lines. |
| **`play` / `wavplay`** | `play <file.wav>` | Play standard WAV/PCM audio files directly to `/dev/sound0`. | Parses RIFF/WAV header (rate, channels, bit-depth), configures sound driver via ioctl, and streams PCM samples to `/dev/sound0`. |
| **`edit` / `nano`** | `edit <file>` | Minimal full-screen console text editor. | Lightweight Kilo-based text editor using ANSI terminal escape sequences to edit scripts and config files directly on-device. |

---

## 5. Enhancement Proposals for Existing ebins

In addition to introducing new commands, several existing commands should be upgraded to match POSIX expectations:

1.  **`ls` Enhancement:**
    *   Add `-l`: Long format displaying permissions, file size (in bytes), and entry name.
    *   Add `-h`: Human-readable size format (e.g. `4.2K`, `1.5M`).
    *   Add `-a`: Show hidden entries (files beginning with `.`).
    *   Add `-F`: Append file type indicator (`/` for directories, `*` for binaries).
2.  **`cat` Enhancement:**
    *   Add `-n`: Prepend line numbers to each output line.
3.  **`echo` Enhancement:**
    *   Add `-e`: Enable interpretation of backslash escapes (`\n`, `\t`, `\r`, `\033`).
4.  **`flashprot` Cleanup:**
    *   Directory [`main/ebin/flashprot`](file:///home/ai/eos/main/ebin/flashprot) is currently empty. It should either be populated with a flash block-protection utility or removed to maintain codebase hygiene.
5.  **`write` Deprecation / Standard Compliance:**
    *   `write <value> <file>` should remain for backward compatibility, but documentation and scripts should transition to standard POSIX redirection: `echo <value> > <file>`.

---

## 6. Implementation Architecture & Standards

When implementing new `ebin` utilities, developers must strictly adhere to EOS architectural rules:

### 1. Linker Registration (`.eos_bins`)
Every utility must export an `eos_bin_t` manifest in the `.eos_bins` section:
```c
#include "ecore/bin.h"

int rm_main(int argc, char **argv) {
  // Implementation...
  return 0;
}

EOS_BIN_ATTR eos_bin_t rm_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN, // CLI utility (hidden from GUI launcher)
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE, // 8192 bytes
    .filename = "rm",
    .name = "Remove File",
    .group = "Utils",
    .description = "Remove files or directories",
    .entry_point = rm_main,
};
```

### 2. Stack Size Allocation Guidelines
*   **Minimal utilities (`clear`, `sleep`, `uptime`, `echo`):** `EOS_BIN_MIN_STACK_SIZE` (4,096 bytes).
*   **Standard file & text utilities (`rm`, `cp`, `mv`, `grep`, `hexdump`):** `EOS_BIN_DEFAULT_STACK_SIZE` (8,192 bytes).
*   **Networking & Recursive search (`ping`, `find`, `wget`):** 12,288 to 16,384 bytes.
*   **Direct Framebuffer / Graphical Apps:** `EOS_BIN_GUI_STACK_SIZE` (24,576 bytes).

### 3. Layer Separation Compliance
*   **`ebin` utilities must NEVER bypass the VFS or Unified Device Model.**
*   Do not invoke low-level ESP-IDF HAL or hardware registers directly inside `ebin` code.
*   Interact with hardware solely through standard libc functions (`open`, `read`, `write`, `ioctl`, `stat`) directed at `/dev/`, `/proc/`, or storage paths.

### 4. Reentrant Argument Parsing
*   Standard libc `getopt()` maintains global static variables (`optind`, `optarg`, `opterr`). In a multi-threaded embedded OS like EOS where multiple `ebin` commands may execute concurrently or sequentially in the same process address space, standard `getopt()` can cause subtle state-pollution bugs.
*   **Best Practice:** Use simple positional parameter loops (`for (int i = 1; i < argc; i++)`) or thread-safe local argument scanners.

### 5. Memory Efficiency & Microcontroller Constraints
*   Avoid allocating large buffers on the BSS (global static arrays) or deep on the task stack.
*   Prefer streaming I/O with small, cache-friendly working buffers (512 bytes to 1024 bytes) that match flash and SD card sector boundaries.
*   Always free dynamically allocated buffers before exiting `main` to prevent thread-context memory leaks.

---

## 7. Next Steps & Recommended Action Plan

1.  **Immediate Sprint (Tier 1 Foundational Coreutils):**
    *   Implement `rm` (file unlinking and recursive directory removal).
    *   Implement `cp` (file and directory copying).
    *   Implement `mv` (file moving and renaming).
    *   Implement `sleep` (delay for shell scripts).
    *   Upgrade `ls` with `-l`, `-a`, `-h`.
2.  **Second Sprint (Text Stream & Observability):**
    *   Implement `grep` (pattern search for shell pipes).
    *   Implement `free` and `uptime` (formatted memory and uptime reports).
    *   Implement `clear` (ANSI escape screen reset).
    *   Implement `reboot` (clean software reset).
3.  **Third Sprint (Networking & Deep Diagnostics):**
    *   Implement `ping` (ICMP network validation via `ping_sock.h`).
    *   Implement `hexdump` / `xxd` (binary inspection).
    *   Implement `ps` (FreeRTOS task status table).
