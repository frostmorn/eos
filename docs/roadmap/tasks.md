# EOS task list

- **IDs:** `T01…T30` cover functional tasks. `F01…F08` are foundation tasks.
- **Sizes** (one developer, rough guesses): **S** 1–2 days · **M** 3–7 days · **L** 2–3 weeks · **XL** 4+ weeks.
- **Order of work** is in [`implementation-order.md`](implementation-order.md). Architectural decisions and `layer_audit` rules are in [`decisions-and-audit-rules.md`](decisions-and-audit-rules.md). Completed work is archived in [`history.md`](history.md).

---

## 0. Summary

| Status | ID | Task | Epic | Size | Depends on | Phase |
|---|---|---|---|---|---|---|
| Done | F01 | IDF pin, wrapper inventory, CI matrix | Foundation | S | – | 0 |
| Done | F02 | Memory ownership by process, not thread | Foundation | M | F04 | 0 |
| Done | F03 | Cleanup-hook registry (sockets, timers, tasks, ISRs) | Foundation | M | F04 | 0 |
| Done | F04 | Process table (pid / ppid / pgid / wait) | Foundation | M | – | 0 |
| Done | F05 | Manifest and ABI versioning policy | Foundation | S | – | 0 |
| Done | F06 | Test and audit infrastructure | Foundation | S–M | – | 0 |
| Pending | F07 | TTY layer (termios, echo, line discipline) | Terminal | L | F04 | 2 |
| Partial | F08 | Typed ioctl convention and lint | Foundation | S | F06 | 0 |
| Done | T06 | Hash and checksum utilities | Userland | S | F06 | 1 |
| Done | T10 | Device-selection environment variables | Userland | S | – | 1 |
| Done | T17 | Run-time stats and `/proc/loadavg` | Observability | S | – | 1 |
| Done | T07 | Filesystem registry, flexible `mount`/`umount` | Filesystems | M | F06 | 1 |
| Done | T13 | Real per-process `/proc` | Observability | M | F04 | 1 |
| Done | T20 | `ps` | Observability | S | T13 | 1 |
| Pending | T18 | Signals (cooperative) and `kill` | Terminal | L | F04, F07 | 2 |
| Pending | T19 | Job control (fg / bg / Ctrl-Z) | Terminal | M | T18, F07 | 2 |
| Pending | T01 | terminfo and ncurses port | Terminal | L | F07, T18, T10 | 2 |
| Done | T09 | CPU time accounting and `time` | Observability | M | T13, T17 | 2 |
| Pending | T22 | NVS filesystem and Preferences API | Filesystems | M | T07 | 3 |
| Pending | T21 | Autoload `.eos/init.sh` with trust store | Security | M | T07, T22, T06, F07 | 3 |
| Done | T12 | Symlinks, `ln`, cross-device handling (a–c); own VFS (d) | Filesystems | M + decision | T07 | 3 |
| Done | T16 | Capability manager: PWM, DMA, interrupts | Drivers | M | – | 4 |
| Done | T15 | PWM driver | Drivers | M | T16 | 4 |
| Done | T14 | Display colour mode (ioctl + manifest flag) | Drivers | M | F05, F03, T10 | 4 |
| Pending | T02 | ES8311 codec and sound-driver split | Drivers | L | T16, F08 | 4 |
| Partial | T03 | USB: host (a) and composite device (b) | Drivers | L + XL | T16, T07, F07 | 4 |
| Pending | T05 | libcurl compatibility layer | Network libs | L | F03 | 5 |
| Pending | T11 | libmicrohttpd compatibility layer | Network libs | L | F03 | 5 |
| Pending | T04 | SDL / SDL2 completion | Apps | XL | T02, T10, T14 | 5 |
| Done | T08 | ELF loader and export table | Loader | XL | F02, F03, F05, T06, T21 | 6 |
| Done | T23 | SD/SDIO partition automounting on boot (/mnt/sd) | Filesystems | S | T07 | 3 |
| Partial | T24 | lvgl_fm — Graphical File Manager | Apps | M | T07, T10 | 3 |
| Pending | T25 | eos_open — File opener and MIME dispatcher | Userland | S | T08 | 3 |
| Pending | T26 | lvgl_edit — Graphical text editor and viewer | Apps | M | T10 | 3 |
| Pending | T27 | Script Metadata (`#!` + `#$ Key=Value`) & Shebang Dispatch | Loader | S | T08 | 6 |
| Pending | T28 | Driver Config & Pin Schema VFS (`/dev/<name>/{cfg,pins}/*`) | Drivers | M | F08, T16 | 6 |
| Pending | T29 | Unified System Object (`eos_handle_t`, ABI/Magic Header, `/proc/objects`) | Foundation | M | F05, T13 | 6 |
| Pending | T30 | Dynamic Loadable/Unloadable `.elf`/`.so` Modules (Strict PSRAM & ABI Upgraders) | Loader | L | T08, T29 | 6 |

---

## 1. What changed compared with the original list

1. **Eight foundation tasks added.** Most original items need a process table (T13, T18, T19, T20, T09), a terminal layer (T01, T18, T19, T21) or a generic cleanup mechanism (T05, T11, T14). Without them each task would invent its own version.
2. **Split where the item contained two different jobs:** T01 (TTY layer vs terminfo/ncurses), T03 (USB host vs USB device), T12 (symlinks/`ln` vs "own VFS"), T08 (export table vs loader), T04 (coverage audit vs implementation).
3. **Reworded where the original was ambiguous:** T05/T11 (what "compat layer" means), T12 (what "cross-device linkage" means), T16 ("load already claimed ones").
4. **Traps flagged inside tasks:** shared USB PHY on S3, MSC exclusivity, `esp_http_server` control-port clash, LEDC timer sharing, executable memory for loaded code, TOCTOU in the init-script trust check, driver state allocated under a short-lived thread.

---

## 2. Foundation tasks

### F01 — IDF pin, wrapper inventory, CI matrix [DONE]
`Size: S · Depends: –`

**Why:** The list adds more linker wraps (T12, T16, T18). `--wrap` on a symbol that no longer exists is silent, so an IDF upgrade can disable a wrapper without any build error. The P4 guide already mentions IDF 6.0, so the supported matrix should be explicit.

**Scope**
- Decide and document supported IDF versions and targets (ESP32-S3, ESP32-P4, QEMU).
- Add a table to `docs/architecture/wrappers-and-syscalls.md`: symbol | file | reason | IDF/newlib assumption | test that fails if it breaks.
- Add `make wrapcheck`: fails if a wrapped symbol is no longer referenced (`nm` on objects for `__real_<sym>`).
- CI: build all three targets, run `layer_audit.py`, run the QEMU suite.

**Acceptance:** CI is red when a wrapped symbol is renamed or removed. Every wrap in `main/CMakeLists.txt` has a row in the table.

---

### F02 — Memory ownership by process, not thread [DONE]
`Size: M · Depends: F04`

**Why:** Allocations are recorded in the allocating thread's `tctx` and freed when that thread exits. Two consequences *(verify both)*:
1. A buffer allocated in thread A and freed in thread B is not found in B's list, and A's exit frees memory B may still use.
2. Driver state allocated in `init()` during `devctl attach` is charged to the thread that ran the attach. If that is the short-lived `devctl` ebin, its exit may free live driver state.

**Scope**
- Ownership key becomes the **pid** (F04). Threads of one process share one allocation registry.
- Kernel/driver allocations are owned by a kernel context (`pid 0`) and are never auto-freed. Attach/detach switches the calling thread to the kernel context for the duration of `init()`/`shutdown()`.
- Replace the linear `memblocks` vector with a structure that finds a pointer in O(log n) or better (sorted vector or hash). Keep the registry in internal SRAM as the docs require.
- `eos_mem_adopt(ptr, pid)` for explicit hand-over (for example a message passed to a service process).

**Acceptance:** tests for (a) cross-thread free, (b) attach from a short-lived ebin followed by driver use, (c) exit frees only that process's blocks. A micro-benchmark with `membench` shows the registry overhead per malloc/free.

---

### F03 — Cleanup-hook registry [DONE]
`Size: M · Depends: F04`

**Why:** The tracker covers fds, dirs and heap; sockets, timers, spawned tasks, ISR registrations and device claims are also tracked here for T05, T11, T14 and T16.

**Scope**
- `eos_proc_on_exit(fn, arg)` / `eos_proc_cancel_exit(handle)`; hooks run in reverse order before memory is freed.
- First users: sockets (track `socket`/`accept`/`close`, either by wrapping the lwIP calls or at the fd layer), `esp_timer` handles created by the process, detached threads, display-format restore (T14), HTTP daemons (T11), curl handles (T05).
- Report each leaked resource with the process name, like the existing fd leak log line.

**Acceptance:** a `test_leaks` ebin leaks one resource of each type and exits; afterwards `/proc/meminfo`, the socket table and the timer list show no residue, and a warning per leak is logged.

---

### F04 — Process table [DONE]
`Size: M · Depends: –`

**Why:** An exec created a thread and `eos_wait()` joined it, without a pid, parent, process group or exit-status record that other components could query. T09, T13, T18, T19, T20, F02 and F07 all need a proper process table.

**Scope**
- `eos_proc_t`: `pid, ppid, pgid, sid, state (R/S/T/Z), exit_code, start_time, name, cmdline, tty, cwd, env, fds, dirs, fd_map, memblocks, mem_mutex, main_thread, threads, wait_cond, in_use`.
- Unified architecture: thread-context structures (`tctx`) are completely merged into `eos_proc_t`, binding threads to processes directly via FreeRTOS TLS.
- `getpid/getppid/getpgid/setpgid/setsid`, `waitpid(pid)`, zombie reaping, orphan re-parenting to pid 1 (the init shell).
- Centralized table allocated dynamically in external PSRAM (`s_proc_table`); low-latency `memblocks` retained in internal SRAM.

**Acceptance:** `eos_wait` becomes a thin wrapper over `waitpid`. Pipelines get one pgid. Table survives 1000 spawn/exit cycles with no growth (QEMU test).

---

### F05 — Manifest and ABI versioning policy [DONE]
`Size: S (design) · Depends: –`

**Why:** `eos_bin_t` will be extended by T14 (display format) and reused by T08 (manifest inside an ELF). Changing it twice breaks 45+ binaries twice.

**Scope**
- Add `struct_size`, `abi_version`, `min_api`, `display_format` (T14), `caps_required` (reserved for later permission checks), reserved words.
- `EOS_BIN_INITIALIZER` fills the new fields so existing binaries keep compiling.
- Write an ABI policy: what counts as a breaking change, how the version number moves, how the export table (T08) is versioned.

**Acceptance:** all binaries build unchanged. `binfs` still returns the manifest on `read()`; `launcher` handles manifest structures.

---

### F06 — Test and audit infrastructure [DONE]
`Size: S–M · Depends: –`

**Scope**
- Convention: every task ships a `test_<name>` ebin or a `hardware_test.py` suite, tagged `qemu` or `hardware-only`.
- CI runs the `qemu` tagged suites on every push.
- Move `layer_audit.py` rules into a declarative file so new rules (T05, T11, T10) are one entry each.
- A generated status matrix (`docs/status.md`): driver / feature, state (implemented / partial / stub), test coverage, last verified board.

**Acceptance:** adding a rule requires no Python change. The status matrix is regenerated by `make docs`.

---

### F08 — Typed ioctl convention and lint [PARTIAL]
`Size: S · Depends: F06`

**Why:** T02, T14, T15, T16 and T03 add dozens of ioctls. Today numbers come from per-scope ranges but arguments are untyped.

**Scope**
- Each ioctl is declared with its argument struct in one header (`EOS_IOCTL_DEF(scope, NAME, code, arg_type)`), plus a `size` check in the dispatcher.
- `layer_audit` rule: every ioctl macro has a doc row and an argument type.
- `EOS_IOCTL_GET_CAPS` per scope: returns a bitmask of supported optional ioctls so apps can probe instead of failing on `-ENOSYS`.

**Acceptance:** one existing scope (display) migrated as the reference. New ioctls in other tasks use the macro.

---

### F07 — TTY layer (split from T01)
`Size: L · Depends: F04`

**Why:** T01 asks for "external echo". That is a line-discipline question, not a terminfo question. ncurses, `less`, job control, Ctrl-C and the T21 confirmation prompt all need a real terminal device.

**Scope**
- New `tty` scope: `/dev/console`, `/dev/tty0..n`. Backends: UART, USB-CDC / USB-Serial-JTAG, and the `lvgl_term` display terminal.
- termios subset: `ECHO, ECHOE, ICANON, ISIG, ICRNL, ONLCR, VMIN, VTIME`; `tcgetattr/tcsetattr`; `isatty`; `TIOCGWINSZ/TIOCSWINSZ`.
- Canonical mode with line editing (backspace, `^U`, `^W`, `^D`), raw mode for ncurses, **local echo toggle** (the original "external echo").
- `^C`, `^Z`, `^\` generate signals for the foreground process group (hooks into T18/T19; stub until then).
- Each process gets stdin/stdout/stderr bound to its controlling tty through the process table, replacing the `_GLOBAL_REENT` override.
- Move `ush`'s line editor onto the tty layer so every program gets the same behaviour.
- `lvgl_term` must interpret the escape sequences that terminfo advertises *(verify what it handles today)*.

**Acceptance:** `stty -a`-like output from a small `stty` ebin; echo off for a password prompt; raw-mode `test_tty` reads single keys; same behaviour on UART and `lvgl_term`.

**Risks:** two writers to the same UART (log output versus tty) need a policy: logs go to a separate channel or are tagged.

---

## 3. Original tasks

### Epic: Terminal and process control

#### T01 — terminfo and ncurses port
`Size: L · Depends: F07, T18, T10 · Orig #1`

**Scope**
- Ship a minimal compiled terminfo set (`vt100`, `vt102`, `xterm`, `linux`, `ansi`, `dumb`; target under about 10 KB) as `binfs` assets, exposed under `/etc/terminfo` (or `/usr/share/terminfo`) at boot.
- `TERM` set by `init.sh` per board / tty backend (see T10).
- Order: terminfo lookup library plus `tput`, `clear`, `reset` first; then the ncurses port. Decision needed on full ncurses versus a smaller curses implementation (see decisions file).
- `SIGWINCH` support so full-screen programs reflow (needs T18).
- Proof programs: `top` and `less` (or `more`).

**Acceptance:** `tput cols` and `clear` work over UART; `top` runs full-screen on UART and `lvgl_term`; resize on the serial terminal triggers a redraw.

**Risks:** ncurses footprint and static RAM. Measure flash and heap before and after; allocate its buffers from PSRAM where possible.

#### T18 — Signals (cooperative) and `kill`
`Size: L · Depends: F04, F07 · Orig #18`

**Why this needs a design note:** there is no MMU, and ESP-IDF does not implement `pthread_cancel` *(verify for your IDF version)*. A thread cannot be interrupted at an arbitrary instruction, so signals must be **cooperative**.

**Scope**
- Per-process pending mask and handler table; `signal`, `sigaction`, `raise`, `kill(pid, sig)`, `sigprocmask` (subset), `pause`.
- Delivery points: syscall boundaries. Blocking calls in the wrappers (`read`, `write`, `select/poll`, `usleep/nanosleep`, pipe waits, `eos_wait`) wake early and return `-EINTR`; the handler runs on return to user code.
- `SIGINT`/`SIGTSTP` from the tty (F07); `SIGWINCH`; `SIGCHLD`; `SIGTERM`.
- `SIGKILL` policy: first set pending and wake; if the process is in a compute loop, escalate after a timeout to `vTaskDelete` of the process's threads **with a warning**, because a deleted task can leave a held mutex locked. Document this as unsafe, and run F03 cleanup afterwards.
- `kill` ebin: `kill [-sig] pid`, `kill -l`, `killall name`.

**Acceptance:** Ctrl-C ends a `sleep 100`, a blocked `cat` on a pipe, and a spinning test program (via escalation). Handler test with `sigaction`.

#### T19 — Job control (fg / bg / Ctrl-Z)
`Size: M · Depends: T18, F07 · Orig #19`

**Scope**
- `ush` builtins `jobs`, `fg`, `bg`; `&` jobs get a job id and pgid.
- Ctrl-Z → `SIGTSTP` → suspend the process's threads (`vTaskSuspend`); `bg`/`fg` resume.
- Terminal foreground pgrp: background read from the tty returns `SIGTTIN` / blocks.
- Reuse F04 states (R/S/T).

**Acceptance:** run a long job, Ctrl-Z, `jobs`, `bg`, `fg`, Ctrl-C works against the correct job.

**Risks:** suspending a thread that holds a lock stalls others. Document and keep stops to user processes, not kernel contexts.

---

### Epic: Observability

#### T17 — Run-time stats and `/proc/loadavg` [DONE]
`Size: S · Depends: – · Orig #17`

**Scope**
- Enable in `sdkconfig` (verify option names for your IDF): `FREERTOS_USE_TRACE_FACILITY`, `FREERTOS_GENERATE_RUN_TIME_STATS`, with the esp_timer-based counter for microsecond resolution; apply to all board configs.
- `/proc/stat`: per-core busy/idle ticks (idle task run-time).
- `/proc/loadavg`: Linux's load average is the exponentially damped **run-queue length**, not CPU percent. Sample the number of tasks in the ready/running state every 5 s and apply 1/5/15-minute decay. Also expose CPU percent in `/proc/stat`.
- `uptime` ebin.

**Acceptance:** `cat /proc/loadavg` format matches Linux (`0.10 0.05 0.01 1/12 34`-style, last field may be approximate). A busy-loop test raises the 1-minute value.

**Risks:** `uxTaskGetSystemState` briefly blocks scheduling and scales with task count; sample slowly (≥ 1 s) and measure.

#### T13 — Real per-process `/proc` [DONE]
`Size: M · Depends: F04 · Orig #13`

**Scope**
- `/proc/<pid>/{status,stat,cmdline,cwd,environ,fd/,stack}` and `/proc/self`.
- `status` fields: Name, State, Pid, PPid, Pgid, Threads, open fds, **tracked heap in SRAM and PSRAM** (from F02's registry), stack high-water mark (`uxTaskGetStackHighWaterMark`).
- Also `/proc/uptime`, `/proc/version`, `/proc/cpuinfo` (cheap and useful for scripts).
- `/proc/self` and `cwd` are symlinks once T12a exists; until then special-case them in the procfs resolver.
- Keep dynamic buffers in PSRAM (existing rule).

**Acceptance:** `ls /proc` lists live pids; `cat /proc/self/status` works inside a pipeline; entries vanish on exit.

#### T20 — `ps` [DONE]
`Size: S · Depends: T13 · Orig #20`

`ps` and `ps -ef` style output (pid, ppid, state, cpu time once T09 lands, memory, command) read purely from `/proc`. After T01, add `top`.

**Acceptance:** `ps` output matches `/proc/*/status` snapshots; hidden launcher apps are marked.

#### T09 — CPU time accounting and `time` [DONE]
`Size: M · Depends: T13, T17 · Orig #9`

**Why this needs a design note:** wall time inside a driver call includes blocking (waiting on a semaphore is not CPU). To split "system" from "user" CPU you need per-task CPU time sampled at the syscall boundary.

**Scope**
- Per-process counters `user_us`, `sys_us`, `wait_us` in the process table.
- Instrument the dispatch wrappers (`eos_drv_*` in `driver.c`) and the VFS/syscall wrappers: on entry read the task's run-time counter, on exit add the delta to `sys_us`. Blocking time shows up as sleep, not CPU, because the run-time counter does not advance.
- Interrupt time is not attributed to any process; account it under a separate `irq` line in `/proc/stat`.
- `time cmd` prints real/user/sys; `/proc/<pid>/stat` carries the counters; `ps` shows CPU time.

**Acceptance:** a CPU-bound test shows ~100 % user; a `write` loop to `/dev/fb0` shows a large sys share; a blocked `read` shows near-zero CPU. Overhead measured with `membench`; target < 2 % on a syscall-heavy loop.

---

### Epic: Filesystems and storage

#### T07 — Filesystem registry and flexible `mount`/`umount` [DONE]
`Size: M · Depends: F06 · Orig #7`

**Scope**
- `eos_fs_type_t` registry in a linker section (same style as drivers): name, `mount(source, target, flags, opts)`, `umount`, flags (`needs_block_device`, `pseudo`).
- Types: `fat`, `littlefs`, `tmpfs`, `devfs`, `binfs`, `pipefs`, `procfs`, later `nvs` (T22).
- `/proc/filesystems` in Linux format (`nodev` column for pseudo filesystems).
- `mount [-t type] [-o ro] source target`; `source` may be a **device path** (`/dev/sd0p1`), a pseudo source (`none`, `nvs0`), or empty for auto-detect using `diskpart` probing. `mount` with no args prints `/proc/mounts`.
- `umount` accepts **either the target path or the device path**; refuses when busy (open fds) with `EBUSY`.
- Post-mount / pre-umount **hooks** (used by T21).
- Design the registry so it works over the IDF VFS today and over an EOS mount table later (T12d).

**Acceptance:** `cat /proc/filesystems`; `mount /dev/sd0p1 /sdcard`; `umount /dev/sd0p1` and `umount /sdcard` both work; mounting a pseudo fs without a block device works; busy umount fails cleanly.

#### T22 — NVS filesystem and Preferences-style API
`Size: M · Depends: T07 · Orig #22`

**Scope**
- `nvs` driver/fs mounted at `/nvs`: namespaces are directories, keys are files. Names ≤ 15 characters (NVS limit); longer names return `ENAMETOOLONG`.
- Typed values: `i8…i64`, `u8…u64`, `float`, `double`, string, blob. `read`/`write` handle raw bytes; the **type** is available through an ioctl (`GET_TYPE/SET_TYPE`) and an optional `<key>.type` view. Writes are buffered and committed on `close()` to limit flash wear.
- C API `eos_prefs_*` mirroring Arduino `Preferences` (`putInt/getInt`, `putString`, `putBytes`, `isKey`, `remove`, `clear`).
- **Move `nvs_flash_init` out of `wifi0`**, which currently owns it. Wi-Fi then depends on the NVS driver (init order via `init.sh`).
- `nvsctl`/`prefs` ebin: `list`, `get`, `set <type>`, `rm`.
- Automount from `init.sh`: `mount -t nvs none /nvs`.

**Acceptance:** write via `echo`, read via Preferences API and back; reboot persistence; `ls /nvs/<ns>`; Wi-Fi still initializes after the move.

#### T12 — Symlinks, `ln`, cross-device handling, own VFS [DONE]
`Size: M (a–c) + decision (d) · Depends: T07 · Orig #12`

**Reframing:** the original mixes a utility (`ln`) with a rewrite (own VFS). Most of the value comes from the first part.

- **12a — Symlinks:** symlink nodes in `tmpfs` (and optionally as special files on FAT, since FAT has no native links); `symlink`, `readlink`, `lstat`; resolution in `fapi` with loop limit (≤ 8). Ebins: `ln -s`, `readlink`.
- **12b — Hard links:** `link()` inside one filesystem that supports it (`tmpfs`); `ln` without `-s`.
- **12c — `EXDEV`:** `rename` across filesystems returns `EXDEV` from the wrapper; `mv` falls back to copy + unlink; `cp -a` preserves symlinks.
- **12d — Own VFS upper-level descriptor layer [DONE]:** Centralized VFS descriptor synchronization (`main/ecore/vfs.c`, `main/ecore/vfs.h`) providing pre-allocated static descriptor tables (`eos_vfs_fd_desc_t` for 128 FDs, `eos_vfs_dir_desc_t` for 32 DIR streams) with dedicated recursive mutexes (`lock`). Pre-initialized upfront at boot (`eos_vfs_init()`) before filesystem mounts to eliminate lazy ESP-IDF POSIX heap allocation traps. High-level VFS syscalls (`read`, `write`, `lseek`, `fstat`, `readdir`, `close`, `closedir`) acquire the per-descriptor mutex before invoking filesystem driver vtables, guaranteeing thread-safe descriptor operations and preventing descriptor races across concurrent threads and pipelines. Internal driver table manipulations are strictly isolated from user process memory via `eos_proc_heap_tracking_disable()`. Complete removal of FreeRTOS thread-local storage (`pvTaskGetThreadLocalStoragePointer`/`vTaskSetThreadLocalStoragePointer`) across the kernel, replaced with a spinlock-guarded task table.

**Acceptance (a–c):** `ln -s /sdcard/x /tmp/x` works, `cat /tmp/x` reads through, `mv` across `/tmp` and `/sdcard` works, symlink loops fail with `ELOOP`.
**Acceptance (12d):** `tree /proc` and `tree /` traverse cleanly without watchdogs or recursion; pipelines (`echo ... | cat`) execute reliably across threads; 1000/1000 process spawn cycles pass without memory leaks or descriptor corruption.

#### T06 — Hash and checksum utilities [DONE]
`Size: S · Depends: F06 · Orig #6`

**Scope**
- Shared streaming helper `emisc/hash.h` (`init/update/final`), built on mbedTLS (hardware acceleration for SHA where the SoC has it); constant-time compare helper.
- Ebins: `md5sum`, `sha1sum`, `sha256sum`, `sha512sum` with `-c` check mode and stdin (`-`), `cksum` (CRC32), `base64`, `xxd`/`hexdump`.
- 4–8 KB read chunks from a PSRAM buffer; works on files of any size.

**Acceptance:** NIST/RFC test vectors; hashing a 1 MB file from SD; `sha256sum -c` round trip; throughput numbers recorded in the docs.

**Why early:** T21 and T08 verify hashes.

#### T23 — SD/SDIO partition automounting on boot (/mnt/sd) [DONE]
`Size: S · Depends: T07`

**Scope**
- Implement `mount -a` in `main/ebin/mount/mount.c` to iterate over detected partition block devices in `/dev/part*`.
- Auto-detect filesystem types via `eos_fs_detect_type()` and mount partitions to `/mnt/sd` (and `/mnt/sd1`, etc.).
- SD partition is mounted exclusively at `/mnt/sd` (no legacy `/sdcard`).
- Add `mount -a` to declarative board startup scripts (`main/eboard/*/init.sh`).
- Silent pass if no SD card or partitions exist at boot (exit code 0).

**Acceptance:** `mount -a` automounts partition 0 to `/mnt/sd` on boot; partition is accessible at `/mnt/sd`; boot succeeds without errors when no card is inserted; automated tests pass in `tools/hardware_test.py`.

---

### Epic: Security

#### T21 — Autoload `.eos/init.sh` with trust store
`Size: M · Depends: T07, T22, T06, F07 · Orig #21`

**Why careful:** a script on a removable card that runs on insertion is the classic way to attack a device. The confirmation requirement is right; the details below are what make it hold.

**Scope**
- Hook: T07 post-mount hook for non-pseudo filesystems, runs in an agent thread, never inline in `mount`.
- Read `{fsroot}/.eos/init.sh` **once into memory**, compute SHA-256 of that buffer, and execute **from the same buffer** (no second read, avoiding a swap between review and run).
- If the hash is in the trust store → run. Otherwise, if there is an interactive tty (F07) → show the script (paged) and ask; accept → store hash; refuse → remember the refusal for this mount only. **No tty → skip and log.**
- Trust store: NVS namespace `eos_trust` (T22), key = first 15 hex chars of the hash, value = full 32-byte hash plus label/date. Entry cap (for example 32) with a clear error.
- `trustctl list | add <file> | del <hash> | clear`.
- Limits: scripts run with a timeout and the normal process cleanup (F03). Only that one file is covered; sourcing further files from the card is out of scope and documented.
- Optional: key trust by hash only (portable across cards); mention volume serial as a later hardening option.

**Acceptance:** insert card → prompt → accept → script runs → remount runs silently; edit one byte → prompt again; headless boot → skipped with log line; `trustctl del` revokes.

---

### Epic: Drivers

#### T16 — Capability manager: PWM, DMA, interrupts [DONE]
`Size: M · Depends: – · Orig #16`

**Reframing "load already claimed ones by default":** build it as three layers.
1. **Static** per-SoC resource counts and reserved items from `soc_caps.h` (extends the existing reserved-pin handling). `kernel-core.md` already lists LEDC among governed resources *(verify what `capsmgr.h` actually has)*.
2. **Boot snapshot** *(best effort)*: after IDF initialization and before `init.sh`, record resources IDF components already use where an API exists (for example `gpio_dump_io_configuration`, `esp_intr_dump`); owner shown as `system:<source>`.
3. **Driver claims** on top, with the owner name in the conflict message.

**New resource types:** LEDC timers and channels, GDMA channels, peripheral controllers (SPI/I2S/UART/RMT/MCPWM/GPTimer instances), interrupt sources (peripheral source level, not CPU line).

**Interrupts:** claims are by explicit driver call. Wrapping `esp_intr_alloc*` would give automatic tracking and leak cleanup, at the cost of another wrapper (F01 rule applies). Decide in the decisions file.

**Scope also:** `/proc/capabilities` (type, id, owner); release on detach; conflict log shows both owners.

**Acceptance:** attach two drivers wanting the same LEDC channel → second fails with a message naming the first; `cat /proc/capabilities` matches reality on S3 and P4.

#### T15 — PWM driver [DONE]
`Size: M · Depends: T16 · Orig #15`

**Scope**
- `pwm` driver, `/dev/pwmN`: ioctls `SET_FREQ`, `SET_DUTY` (16-bit, scaled to hardware resolution), `SET_POLARITY`, `ENABLE`, `FADE(target, ms)`, `GET_CAPS`.
- Channels that share a frequency share an LEDC timer (reference-counted); a channel requesting a different frequency on an occupied timer is rejected with a clear error. Resolution depends on frequency, so `GET_CAPS` reports the achievable bits.
- First consumer: **display backlight** (`bl` pin). Fallback to on/off when no PWM is attached.
- Other consumers: buzzer/tone, servo helper (50 Hz). MCPWM is out of scope.
- `devctl attach` syntax documented with examples.

**Acceptance:** `echo`/ioctl dims the backlight on Lilka and the P4 board; two channels at different frequencies conflict cleanly; detach releases the timer.

#### T14 — Display colour mode [DONE]
`Size: M · Depends: F05, F03, T10 · Orig #14`

**Scope**
- Display ioctls: `GET_FORMATS` (bitmask of supported), `GET_FORMAT`, `SET_FORMAT` (a full reinit is acceptable). Formats: RGB565, RGB666, RGB888, GRAY8, MONO1 (per driver capability; verify what ST7789 / ST7701S / QEMU support).
- `write()` validates its length against the current format (`EINVAL` on mismatch).
- Manifest field `display_format` (from F05); `0` means keep the current format.
- `exec` applies the requested format to the display chosen by `EOS_DISPLAY` (T10) and registers a **restore hook** (F03) so the previous format returns on exit, even after a crash.
- Policy: one GUI owner at a time; a second GUI process requesting a different format gets `EBUSY`.
- LVGL: use runtime colour format (LVGL 9 supports it *(verify the version in use)*). SDL layer maps SDL pixel formats (T04).

**Acceptance:** an ebin with `display_format = RGB888` runs on the P4 board; on exit the shell returns to RGB565; the QEMU target reports unsupported formats via `GET_FORMATS`.

#### T02 — ES8311 codec and sound-driver split
`Size: L · Depends: T16, F08 · Orig #2`

**Why it is bigger than "add a driver":** `max98357` is a data-only I2S amplifier. A codec has a control path (I2C registers) and a data path (I2S). The sound scope needs two roles:
- **Stream controller** (I2S bus: format, rate, DMA buffers, play and capture), and
- **Codec** (control: volume, mute, input gain, sample-rate/format programming, power sequencing, MCLK requirements).

**Scope**
- Introduce an `i2s` bus (or I2S controller device) as parent for sound devices; codec drivers have two parents/links: I2S for data, I2C for control.
- Exposed files: `/dev/sound0` (playback), `/dev/sound0c` (capture), and a control node (for example `/dev/soundctl0` or `/dev/mixer0`) with typed ioctls: `GET/SET_VOLUME`, `MUTE`, `INPUT_GAIN`, `ROUTE`.
- `SET_CONFIG` stays atomic (it already is in the sound doc).
- Full-duplex support where the SoC allows it; capture path with the same PSRAM/DMA rules as the sound doc.
- MCLK pin claim via capsmgr.
- ES8311 first (JC4880P4 board); `max98357` migrated to the new structure without behaviour change.
- Hearing-protection volume cap stays at the control node.

**Acceptance:** play and record on the board; changing volume through the control node during playback; `test_sound` passes unchanged for max98357; docs updated with the new structure.

#### T03 — USB: host (a) and composite device (b) [PARTIAL]
`Size: L (a) + XL (b) · Depends: T16, T07, F07 · Orig #3`

**Facts that shape the design**
- On ESP32-S3 the built-in USB-Serial-JTAG and the OTG controller **share one PHY**. Enabling device mode takes over the pins, so the built-in console and JTAG go away and CDC provides the replacement console. ESP32-P4 has separate PHYs (USB-Serial-JTAG on port 0 + dedicated HS/FS OTG).
- The S3 OTG controller has a small fixed endpoint budget. Not every function fits at once (MSC + CDC + NCM + HID). Check the budget at attach time using the existing **attach veto**.
- An MSC function backed by an SD card that EOS also has mounted corrupts the filesystem. Rule: expose a block device only after unmounting it, read-only by default, or expose a dedicated partition.

**T03a — Host mode (the original "in")**
- `usb` bus in host mode; hub support; **dynamic child creation** on device events (this is new: today `devctl` attaches statically). Hot-plug event node (for example `/dev/hotplug` or `/proc/events`) so userland and T21 can react.
- Class drivers as children: MSC → `storage` scope device (`/dev/sd1`, partitions via `diskpart`), HID → `input` scope, CDC-ACM → `tty` backend (F07).
- VBUS switch control is board-specific: capsmgr claim plus `init.sh` option.

**T03b — Device mode (the original "out"), composite**
- `usb` bus config `mode=device`; functions attach as children: `msc`, `cdc` (serial/console), `ncm` (network), `hid_keyboard`. Descriptors are assembled when `USB_ENABLE` is called; changes require disable/enable (re-enumeration).
- **Network:** implement CDC-NCM (or ECM) first because Windows/Linux/macOS support it well. The requested "ppp-modem" (CDC-ACM + PPP) is optional and later.
- Console: CDC-ACM as a tty backend.

**Acceptance (a):** USB stick appears as `/dev/sd1`, mounts, unplug detaches cleanly; USB keyboard produces input events. **(b):** host sees MSC + serial + keyboard simultaneously where the endpoint budget allows; attach beyond the budget is refused with a message.

---

### Epic: Network libraries

#### T05 — libcurl compatibility layer
`Size: L · Depends: F03 · Orig #5`

**Reframing:** ESP-IDF has `esp_http_client`, not libcurl. "Compat layer" means a `curl/curl.h`-style API implemented on top of `esp_http_client`/`esp-tls`, so ebins use one portable API. Before writing it, check the IDF component registry for an existing libcurl port.

**Scope**
- First stage: `curl_global_init`, `curl_easy_init/setopt/perform/getinfo/cleanup`, `curl_slist_*`. Options: `URL`, `WRITEFUNCTION/WRITEDATA`, `HEADERFUNCTION`, `POST/POSTFIELDS/POSTFIELDSIZE`, `HTTPHEADER`, `CUSTOMREQUEST`, `TIMEOUT`, `FOLLOWLOCATION`, `USERAGENT`, `SSL_VERIFYPEER`, `CAINFO`/built-in bundle. Everything `esp_http_client` can do and nothing more.
- Return `CURLE_*` codes with `curl_easy_strerror`. `curl_multi_*` only in a later stage.
- A `curl` ebin built on the layer (dogfooding and tests).
- Handles register an F03 cleanup hook (sockets, TLS contexts).
- mbedTLS memory: move TLS buffers to PSRAM where configuration allows; record handshake heap and time.
- `layer_audit` rule: ebins may not include `esp_http_client.h` / `esp_tls.h` (see audit rules).

**Acceptance:** `curl http://…`, `curl https://…` (cert bundle), POST with headers, redirect following; leaked handle cleaned on exit; audit rule fails a deliberate violation.

**Risks:** TLS RAM and handshake time on S3; HTTPS tests need a network (hardware only unless QEMU networking is available).

#### T11 — libmicrohttpd compatibility layer
`Size: L · Depends: F03 · Orig #11`

**Scope**
- Subset on top of `esp_http_server`: `MHD_start_daemon` (with `MHD_USE_INTERNAL_POLLING_THREAD`), `MHD_stop_daemon`, `MHD_AccessHandlerCallback`, `MHD_create_response_from_buffer`, `MHD_queue_response`, `MHD_add_response_header`, `MHD_get_connection_values`, simple POST processing.
- One catch-all URI handler (wildcard matcher) dispatches to the MHD access callback.
- Each `esp_http_server` instance needs a **unique control port** (`ctrl_port`); allocate it per daemon or two daemons collide.
- Connection and handler-count limits follow `esp_http_server` configuration; document them.
- Streaming responses map to chunked sends.
- **Daemon lifetime is bound to the starting process** via F03 hooks; otherwise an exited ebin leaves a server calling freed code.
- `layer_audit` rule: ebins may not include `esp_http_server.h`.

**Acceptance:** a hello-world MHD program serves a page and a POST; two daemons run on different ports; killing the ebin stops the server.

---

### Epic: Apps and loader

#### T10 — Device-selection environment variables [DONE]
`Size: S · Depends: – · Orig #10`

**Scope**
- Standard names: `EOS_DISPLAY` / `EOS_FB`, `EOS_KEYBOARD`, `EOS_TOUCH`, `EOS_AUDIO_OUT`, `EOS_AUDIO_IN`, `EOS_LOG`, `EOS_LOG_FILE`, `TERM`, `PATH`. Precedence: command-line option > environment > default.
- Per-process environment variables (`eos_proc_getenv`, `getenv/setenv` POSIX wrapper override).
- Helper resolution in SDL (`ewrap/sdl.c`, `ewrap/sdl2.c`), LVGL (`ecore/lvgl.c`), and execution loader (`ecore/elf.c`).
- `env` / `printenv` ebins; per-process inheritance verified.

**Acceptance:** `EOS_FB`, `EOS_KEYBOARD`, `EOS_TOUCH`, `PATH` honored by display, input, and shell loaders; fallback defaults applied when variables are unset.

#### T04 — SDL / SDL2 completion
`Size: XL · Depends: T02, T10, T14 · Orig #4`

**Approach**
1. **Coverage spike (S):** choose 3–5 reference programs (one SDL1.2 game, one SDL2 game, one software-rendered demo). `nm -u` the objects for undefined `SDL_*` symbols; generate `docs/.../sdl-coverage.md` with implemented / stubbed / missing.
2. **Decision:** extend the shim by frequency, or port real SDL2 with EOS video/audio/input backends (see decisions file). If keeping the shim, implement SDL2 first and build the SDL 1.2 layer on top of it instead of maintaining two independent surfaces.
3. **Implementation order:** timers and threads/mutex/semaphores (map to pthreads) → events (keyboard, touch as mouse) → video (surfaces, blits, palettes, pixel formats via T14) → audio (callback model on a worker thread over the T02 playback node) → RWops.
4. Out of scope at first: SDL_image/mixer/ttf, OpenGL.

**Acceptance:** all reference programs build unmodified and run on QEMU graphics and one real board; coverage doc is generated by script and CI fails if coverage regresses.

#### T08 — ELF loader and export table [DONE]
`Size: XL · Depends: F02, F03, F05, T06, T21 · Orig #8`

**Scope**
- **Dynamic ELF Loader:** Implemented in `main/ecore/elf.c` and `main/ecore/elf.h`. Parses ELF headers, validates magic, reads segments, loads code and data, applies relocations (Xtensa LX7 and RISC-V), handles executable memory flags (`MALLOC_CAP_EXEC`), and flushes instruction caches (`esp_cache_msync`).
- **Export Table:** Registered symbol resolution in `main/ecore/elf_symbols.c` mapping standard C runtime, POSIX wrappers, FreeRTOS primitives, and EOS syscalls without exposing internal memory allocators.
- **Path and Shell Integration:** Resolves direct paths (`/path/to/binary`), relative cwd paths (`./prog`), and `$PATH` directory search from `system()` and `ush`.
- **Verification:** Tested via `test_elf_loader` ebin (`main/ebin/test_elf_loader/test_elf_loader.c`) with relocations, parameter passing, relative path execution, and $PATH search.

#### T24 — lvgl_fm — Graphical File Manager [PARTIAL]
`Size: M · Depends: T07, T10, T23`

**Scope**
- Graphical directory and file browser in `main/ebin/lvgl_fm/`.
- File list with type indicators (`[DIR]`, `[APP]`, `[FILE]`) and formatted file sizes.
- Full file management actions:
  - **Open**: Invokes `eos_open <path>`.
  - **Edit**: Launches `lvgl_edit <path>`.
  - **Delete / Remove**: Confirmation dialog, unlinks file or removes directory.
  - **New Folder (mkdir)**: Directory creation dialog.
- Dual controls: Touch interface and Keypad (`KEY_UP`, `KEY_DOWN`, `KEY_A`, `KEY_B`, `KEY_DEL`).
- Toolbar bindings in `/tmp/tbar`.
- Headless test mode with `--auto` and `-t <timeout_ms>` adhering to Rule A2 and `AGENTS.md §5`.

**Acceptance:** Launches from launcher; browses directories; creates folders; deletes files; opens files; passes `--auto` test headlessly.

#### T25 — eos_open — File opener and MIME dispatcher
`Size: S · Depends: T08`

**Scope**
- Standard file opening utility in `main/ebin/eos_open/` (analogous to `xdg-open`).
- Dispatching rules based on executable status and file extensions:
  - Executables / binaries in `/bin/` or `access(X_OK)`: `system(path)`.
  - `.elf` binaries: loaded and executed via ELF loader / runner.
  - `.sh` scripts: executed via `ush <path>`.
  - Text and config files (`.txt`, `.c`, `.h`, `.json`, `.md`, `.log`, `.conf`): launched in `lvgl_edit <path>`.
  - Fallback to text viewer / `cat`.

**Acceptance:** `eos_open <file>` launches correct viewer/interpreter; returns exit status of launched target.

#### T26 — lvgl_edit — Graphical text editor and viewer
`Size: M · Depends: T10`

**Scope**
- Standalone graphical text viewer and editor in `main/ebin/lvgl_edit/`.
- Scrollable text display with monospace font.
- View and edit modes with on-screen virtual keypad or hardware keyboard input.
- Save modifications back to file path via VFS.
- Headless smoke test mode (`--auto`, `-t <timeout_ms>`).

**Acceptance:** Opens and renders text files; supports editing and saving; exits cleanly without leaks.

---

### Phase 6b — Dynamic System Objects, Driver Config VFS & Script Dispatch

#### T27 — Script Metadata (`#!` + `#$ Key=Value`) & Shebang Dispatch
`Size: S · Depends: T08`

**Implementation Details**
- **No `eos_bin_t` ABI Break (`main/ecore/bin.h`):** Keep `EOS_BIN_ABI_VERSION` at `1`; add `EOS_BIN_FLAG_SCRIPT` to `EOS_BIN_FLAG_*`. Do **not** add `interpreter[]` or `origin_path[]` arrays to static flash `eos_bin_t` records (avoids ~10 KB flash growth across 77 static bins). Resolve interpreter paths transiently during script parse/exec, and compute paths dynamically via `eos_obj_get_path()`.
- **Direct `#!` Shebang Dispatch (`main/ecore/exec.c`, `main/ebin/ush/ush.c`):** When executing a non-ELF regular file, read line 1; if it begins with `#!<interpreter>` (e.g. `#!/bin/ush`, `#!/bin/lua`), invoke `[interpreter, script_path, ...argv]`.
- **Bounded `#$ Key=Value` Script Manifest Parser (`main/ecore/bin_script.c` or `main/ecore/exec.c`):**
  - Read at most the leading ~1 KB of the file and stop immediately on the first non-`#` line.
  - Parse contiguous `#$ Key=Value` lines (`Name=`, `Group=`, `Description=`, `Flags=GUI|HIDDEN`, `Hint=`, `MinW=`, `MinH=`) into a transient `eos_bin_t` with `EOS_BIN_FLAG_SCRIPT` set.
  - Scope GUI launcher script discovery to configured roots (`$PATH` / `/sdcard/apps`) rather than walking the entire SD card; do not eagerly cache full `eos_bin_t` structs for every file in RAM.

**Acceptance & Tests**
- Host unit tests (`tools/host_tests/`): valid `#!/bin/ush` & `#!/bin/lua` with `#$ Key=Value`, missing shebang, unknown interpreter, CRLF, non-`#$` comments, ~1 KB bounded scan, malformed `Key=Value`, integer overflow, and `EOS_*_STR_LEN` truncation.
- Target tests (`tools/hardware_test.py -x`): execute `#!/bin/ush` + `#$` script from `/tmp` with args/exit status; verify missing interpreter (`ENOENT`/`ENOEXEC`), `eos_obj_get_path()`, and launcher `GUI`/`HIDDEN` filtering.

#### T28 — Driver Config & Pin Schema VFS (`/dev/<name>/{cfg,pins}/*`)
`Size: M · Depends: F08, T16`

**Implementation Details**
- **Flash-Resident Parameter Schema (`main/ecore/driver.h`):**
  - Reuse existing `eos_cfg_type_t` (`main/emisc/types.h`: `EOS_CFG_INT`, `EOS_CFG_FLOAT`, `EOS_CFG_STR`, `EOS_CFG_PTR`) instead of defining a duplicate type enum.
  - Define `eos_drv_param_t` (`name`, `description`, `type`, `group` [`EOS_PARAM_GRP_CFG` / `EOS_PARAM_GRP_PIN`], `flags` [`EOS_PARAM_RO`, `EOS_PARAM_ATTACH_ONLY`, `EOS_PARAM_LIVE`], `int64_t min_val`, `int64_t max_val`, `default_val`, `enum_vals`).
  - Add `const eos_drv_param_t *params; size_t param_count;` to `eos_drv_t` (zero-initialized `NULL, 0` by default; `const` in `.rodata` = 0 B RAM).
- **Step 2a — Centralized Read-Only VFS & Attach Validation (`main/ecore/dev.c`, `main/efs/devfs/`):**
  - Intercept `/dev/<devname>/cfg`, `/dev/<devname>/cfg/<key>`, `/dev/<devname>/pins`, and `/dev/<devname>/pins/<key>` centrally in `devfs` / `eos_dev_*` (**0 extra `esp_vfs` slots**, zero per-driver VFS boilerplate).
  - `read()` exposes current `dev->cfg` / `dev->pins` value + schema bounds/type/flags.
  - `eos_dev_attach()` validates `cfg` and `pins` against `dev->driver->params` before calling `driver->init(dev)`.
  - Store minimal per-open FD `(dev, param_idx, offset, generation)` and `DIR *` `(dev, idx, generation)` state so detached devices reject stale FDs (`-ENODEV`).
- **Step 2b — Opt-In Live Writes:**
  - Allow `write()` only on `EOS_PARAM_LIVE` parameters for drivers opting in via `EOS_IOCTL_PARAM_SET`. Reject `EOS_PARAM_RO` (`-EACCES`), `EOS_PARAM_ATTACH_ONLY`/pins while attached (`-EBUSY`), and out-of-range values (`-EINVAL`).

**Acceptance & Tests**
- Host unit tests (`tools/host_tests/`): `eos_drv_param_t` type/range checks at boundaries (`min_val`, `max_val`, `min_val - 1`, `max_val + 1`), invalid enums, `EOS_PARAM_RO`, and `EOS_PARAM_ATTACH_ONLY`.
- Target tests (`tools/hardware_test.py -x`, extending `test_devctl` & `test_devtree_detach_reattach_cycle`): enumerate `/dev/<drv>/{cfg,pins}` without FD/`DIR*` leaks; verify `-EINVAL` / `-EBUSY` rejections, `devctl attach` validation, `EOS_PARAM_LIVE` writes, and stale-FD rejection after detach.

#### T29 — Unified System Object (`eos_handle_t`, ABI/Magic Header, `/proc/objects`)
`Size: M · Depends: F05, T13`

**Implementation Details**
- **Tagged `eos_handle_t` (`main/ecore/obj.h`, `main/ecore/obj.c`):**
  - Define `eos_obj_kind_t` (`EOS_OBJ_DRIVER = 1`, `EOS_OBJ_FS = 2`, `EOS_OBJ_BIN = 3`) and `eos_handle_t { eos_obj_kind_t kind; const void *ptr; }`. Keeps `eos_drv_t`, `eos_fs_type_t`, and `eos_bin_t` separate in flash without merging ~31 VFS pointers onto `bin`/`fs`.
  - Provide `eos_obj_name(eos_handle_t)`, `eos_obj_scope(eos_handle_t)`, and `eos_obj_get_path(eos_handle_t, char *out, size_t len)` (computed for static objects: `"/bin/<filename>"`, `"/dev/<devname>"`).
- **12-Byte ABI & Magic Header (`main/ecore/driver.h`, `main/ecore/fs.h`):**
  - Mirror the first 3 fields of `eos_bin_t` (`uint32_t magic; uint32_t struct_size; uint32_t abi_version;`) at the top of `eos_drv_t` (`EOS_DRV_MAGIC`, `EOS_DRV_ABI_VERSION`) and `eos_fs_type_t` (`EOS_FS_MAGIC`, `EOS_FS_ABI_VERSION`), populated via `EOS_DRV_INIT` and the 7 static `eos_fs_type_t` definitions.
- **No-Legacy-Shims & `layer_audit.py` Enforcement (`tools/layer_audit.py`, `tools/layer_audit_rules.json`):**
  - **No Ad-Hoc Legacy Shims:** Core subsystems operate strictly on current ABI (`abi_version == current`, `struct_size == sizeof(...)`); ban `EOS_.*_LEGACY` symbols/macros and `abi_version` branching outside the dedicated ABI upgrader/loader module.
  - **Sequential ABI Step-Upgraders:** Any future bump to `EOS_*_ABI_VERSION > 1` must provide a contiguous per-scope transposition chain (`1_to_2`, `2_to_3`, ..., `(N-1)_to_N`), enforced by `tools/layer_audit.py`.
- **`/proc/objects` (`main/efs/procfs/procfs.c`):**
  - Expose `/proc/objects` iterating `_eos_drivers_start..end`, `_eos_fs_start..end`, `_eos_bins_start..end`, and dynamically loaded objects.

**Acceptance & Tests**
- `python3 tools/layer_audit.py -x` passes with 0 violations including the no-legacy-shim and ABI step-upgrader chain rules.
- Target tests (`tools/hardware_test.py -x`, extending `test_procfs`): `/proc/objects` lists drivers, filesystems, and bins with valid `kind`, `scope`, and `name` with zero regressions.

#### T30 — Dynamic Loadable/Unloadable `.elf`/`.so` Modules (Strict PSRAM & ABI Upgraders)
`Size: L · Depends: T08, T29`

**Implementation Details**
- **PSRAM-First `.elf`/`.so` Allocation (`main/ecore/elf.c`, `components/elf_loader`):**
  - All `.elf` and `.so` loading—both the staging `payload` buffer and relocated `.text`/`.rodata`/`.data`/`.bss` sections—allocates in **PSRAM first** (`MALLOC_CAP_SPIRAM`). On PSRAM-equipped boards (`CONFIG_SPIRAM`), fail cleanly with `-ENOMEM` if PSRAM is exhausted unless explicitly opted in; allow internal SRAM fallback only when `!CONFIG_SPIRAM`.
  - Free the raw staging `payload` immediately after `esp_elf_relocate()`.
- **Build Target (`Makefile`, `tools/`):**
  - Add a standalone `.so` module build target for loadable drivers/filesystems and deterministic test fixtures.
- **Single Canonical Export (`eos_mod_objects`):**
  - Loadable `.so`/`.elf` modules export a single null-terminated `const eos_handle_t eos_mod_objects[]` symbol (mirroring `eos_app_manifest` in `main/ecore/elf.h`).
  - After relocation, verify `magic` matches `kind`, `min_api` passes, and if `abi_version < current` run the per-scope sequential ABI step-upgrader chain (`v -> v+1 -> ... -> current`) once before registering into a dynamic PSRAM `kvec` (mutex-protected; never hold registry mutex across blocking I/O, `driver->init()`, or `fs->mount()`).
  - Update `eos_drv_find()` and `eos_fs_find()` to search the dynamic registry after static flash sections.
- **Lifetime, Refcounting, CLI & ISR Rules:**
  - Track `open_fd_count`, `attached_dev_count`, `mounted_fs_count`, and `active_ktask_count` per loaded module. Unload (`modprobe -r` / `rmmod` / `eos_mod_unload()`) returns `-EBUSY` until refcount == 0.
  - Keep ISR-bearing drivers built-in or route interrupts through IRAM-resident core trampolines (PSRAM code cannot run in strict IRAM / cache-disabled ISR contexts).

**Acceptance & Tests**
- Target tests (`tools/hardware_test.py -x`, extending `test_elf_loader` with `/tmp` `.so` fixture): load `.so`, verify `/proc/objects` + `eos_drv_find()`/`eos_fs_find()`, attach/open/ioctl/close, verify `-EBUSY` refcount guard on `rmmod`, clean unload, and error paths (bad magic, ABI mismatch, unresolved symbol, PSRAM `-ENOMEM`).
- Leak & concurrency tests (`test_leaks`): 10× load/attach/use/detach/unload cycles assert both internal SRAM and PSRAM free bytes return to baseline; concurrent `/proc/objects` reads during load/unload do not deadlock.
- `python3 tools/layer_audit.py -x`, `python3 tools/wrapcheck.py`, and `make build` pass across `esp32s3_qemu`, `lilka_v23`, and `jc4880p443c_i_w_y`.
