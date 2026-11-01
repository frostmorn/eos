# EOS Executable Binaries (`ebin`) Audit & Evolution Roadmap

This engineering report documents the audit of native executable binaries (`ebin`) in **EOS (ESP Operating System)**, reviews completed POSIX core utility implementations, and outlines the evolution roadmap for future developer and system administration utilities.

---

## 1. Executive Summary & Audit Context

In EOS, user commands and native applications reside in `main/ebin/`, registered at compile-time via `eos_bin_t` manifests into the `.eos_bins` linker section. The VFS layer exposes them under `/bin/` via `binfs`, and execution is managed by `exec.c` and `ush.c` in isolated pthreads.

### Current Status:
* **Total Active Binaries:** 45 commands
* **Core POSIX Utilities:** 14 commands (`cat`, `cd`, `cp`, `echo`, `ls`, `mkdir`, `mv`, `pwd`, `rm`, `rmdir`, `sleep`, `touch`, `tree`, `write`)
* **Hardware & Bus Control:** 4 commands (`battery`, `devctl`, `lsi2c`, `soldercheck`)
* **Network Management:** 2 commands (`netctl`, `wlanctl`)
* **GUI Applications (LVGL):** 6 applications (`lvgl_calc`, `lvgl_keypad`, `lvgl_net`, `lvgl_snake`, `lvgl_sysmon`, `lvgl_term`, plus `launcher`)
* **Diagnostics & Test Suites:** 15 test suites (`test_coreutils`, `test_sound`, `test_input`, `test_emisc`, `membench`, etc.)

---

## 2. Completed Implementations (Phase 1 Milestones)

During recent development phases, critical operational gaps in file management, scripting delays, and automated verification were resolved:

| Command | Status | Description & Capabilities | Verification Suite |
| :--- | :---: | :--- | :--- |
| **`rm`** | ✅ **Complete** | Removes regular files via `unlink()`. Supports `-r`/`-R` for recursive directory deletion, `-f` for silent operation, and `-v` for verbosity. | `test_coreutils` (7 tests) |
| **`cp`** | ✅ **Complete** | Copies files in 4 KB streaming chunks. Supports recursive directory cloning (`-r`), force overwrite (`-f`), and verbose mode (`-v`). | `test_coreutils` (8 tests) |
| **`mv`** | ✅ **Complete** | Atomically moves/renames files and directories via `rename()`. Supports cross-device copy+remove fallback and `-v` reporting. | `test_coreutils` (6 tests) |
| **`sleep`** | ✅ **Complete** | Suspends calling thread via `usleep()`. Parses compound durations (`s`, `ms`, `m`, `h`, e.g. `sleep 100ms`). Crucial for `init.sh` timing. | `test_coreutils` (6 tests) |
| **`test_coreutils`** | ✅ **Complete** | Automated in-tree test runner validating 33 test cases covering boundary conditions, recursive deletion, and error codes. | Hardware test suite |

---

## 3. Prioritized Evolution Roadmap (Future Phases)

While core file manipulation is fully operational, the following POSIX and diagnostic utilities are prioritized for upcoming milestones:

### Priority Tier 1: Text Stream Filtering (Pipelines)
Although the EOS MicroShell (`ush`) features stream pipelining (`cmd1 | cmd2`), text filtering utilities will substantially improve console debugging:
1. **`grep`:** Regular expression / substring line filtering. Essential for filtering large `/proc/devices`, `/proc/drivers`, and network scan outputs.
2. **`head` & `tail`:** Slices first/last $N$ lines from a stream or file.
3. **`wc`:** Counts lines, words, and byte lengths of streams.
4. **`tee`:** Duplicates stdin to stdout and one or more files simultaneously.

### Priority Tier 2: System Observability & Diagnostics
1. **`ps`:** Formats thread status, task IDs, stack watermarks, and priorities by reading `/proc/tasks`.
2. **`kill`:** Sends termination or break signals to background worker pthreads.
3. **`df`:** Displays filesystem disk usage, total blocks, and free inodes from `/proc/mounts`.
4. **`hexdump` / `xxd`:** Hexadecimal and ASCII byte inspector for firmware assets, storage blocks, and PCAP dumps.

### Priority Tier 3: Network Diagnostics
1. **`ping`:** Sends ICMP echo requests to verify remote IPv4 host reachability and round-trip time (RTT).
2. **`traceroute`:** Probes network routing hops using TTL decrementing.
3. **`sntp` / `date`:** Queries Network Time Protocol servers to synchronize real-time clock (RTC).

---

## 4. Architectural Rules for New Binaries

1. **POSIX / Standard C Only:** User-space utilities must strictly consume standard POSIX APIs (`open`, `read`, `write`, `close`, `usleep`, `pthread`). FreeRTOS and vendor SDK functions (`vTaskDelay`, `xSemaphoreTake`) are prohibited.
2. **No Global State:** Binaries must be reentrant and stateless. All local data must allocate on the stack or transient heap.
3. **Manifest Stack Dimensioning:** High-recursion or GUI applications must set `EOS_BIN_FLAG_GUI` or `EOS_BIN_FLAG_STACK_PSRAM` to avoid depleting internal DRAM.
