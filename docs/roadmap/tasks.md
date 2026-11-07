# EOS task list (revised)

Revision of `Eos_tasks` (22 items), based on the repo docs (`docs/`, 333-commit snapshot) and the earlier architecture review. I have **not read the source code**. Statements marked *(verify)* are inferred from the docs and must be checked in code before the task starts.

- **IDs:** `T01…T22` keep the original numbering. `F01…F08` are new foundation tasks that the original list silently depends on.
- **Sizes** (one developer, rough guesses): **S** 1–2 days · **M** 3–7 days · **L** 2–3 weeks · **XL** 4+ weeks.
- **Order of work** is in `eos-implementation-order.md`. Open design decisions and new `layer_audit` rules are in `eos-decisions-and-audit-rules.md`.

---

## 0. Summary

| Status | ID | Task | Epic | Size | Depends on | Phase |
|---|---|---|---|---|---|---|
| Done | F01 | IDF pin, wrapper inventory, CI matrix | Foundation | S | – | 0 |
| Pending | F02 | Memory ownership by process, not thread | Foundation | M | F04 | 0 |
| Pending | F03 | Cleanup-hook registry (sockets, timers, tasks, ISRs) | Foundation | M | F04 | 0 |
| Done | F04 | Process table (pid / ppid / pgid / wait) | Foundation | M | – | 0 |
| Pending | F05 | Manifest v2 and ABI versioning policy | Foundation | S | – | 0 |
| Done | F06 | Test and audit infrastructure | Foundation | S–M | – | 0 |
| Pending | F07 | TTY layer (termios, echo, line discipline) | Terminal | L | F04 | 2 |
| Pending | F08 | Typed ioctl convention and lint | Foundation | S | F06 | 0 |
| Done | T06 | Hash and checksum utilities | Userland | S | F06 | 1 |
| Pending | T10 | Device-selection environment variables | Userland | S | – | 1 |
| Done | T17 | Run-time stats and `/proc/loadavg` | Observability | S | – | 1 |
| Pending | T07 | Filesystem registry, flexible `mount`/`umount` | Filesystems | M | F06 | 1 |
| Done | T13 | Real per-process `/proc` | Observability | M | F04 | 1 |
| Done | T20 | `ps` | Observability | S | T13 | 1 |
| Pending | T18 | Signals (cooperative) and `kill` | Terminal | L | F04, F07 | 2 |
| Pending | T19 | Job control (fg / bg / Ctrl-Z) | Terminal | M | T18, F07 | 2 |
| Pending | T01 | terminfo and ncurses port | Terminal | L | F07, T18, T10 | 2 |
| Pending | T09 | CPU time accounting and `time` | Observability | M | T13, T17 | 2 |
| Pending | T22 | NVS filesystem and Preferences API | Filesystems | M | T07 | 3 |
| Pending | T21 | Autoload `.eos/init.sh` with trust store | Security | M | T07, T22, T06, F07 | 3 |
| Pending | T12 | Symlinks, `ln`, cross-device handling (a–c); own VFS (d) | Filesystems | M + decision | T07 | 3 |
| Pending | T16 | Capability manager: PWM, DMA, interrupts | Drivers | M | – | 4 |
| Pending | T15 | PWM driver | Drivers | M | T16 | 4 |
| Pending | T14 | Display colour mode (ioctl + manifest flag) | Drivers | M | F05, F03, T10 | 4 |
| Pending | T02 | ES8311 codec and sound-driver split | Drivers | L | T16, F08 | 4 |
| Pending | T03 | USB: host (a) and composite device (b) | Drivers | L + XL | T16, T07, F07 | 4 |
| Pending | T05 | libcurl compatibility layer | Network libs | L | F03 | 5 |
| Pending | T11 | libmicrohttpd compatibility layer | Network libs | L | F03 | 5 |
| Pending | T04 | SDL / SDL2 completion | Apps | XL | T02, T10, T14 | 5 |
| Pending | T08 | ELF loader and export table | Loader | XL | F02, F03, F05, T06, T21 | 6 |

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

### F02 — Memory ownership by process, not thread
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

### F03 — Cleanup-hook registry
`Size: M · Depends: F04`

**Why:** The tracker covers fds, dirs and heap. The docs do not mention sockets, timers, spawned tasks, ISR registrations or device claims *(verify)*. T05, T11, T14 and T16 each add such a resource.

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

### F05 — Manifest v2 and ABI versioning policy
`Size: S (design) · Depends: –`

**Why:** `eos_bin_t` will be extended by T14 (display format) and reused by T08 (manifest inside an ELF). Changing it twice breaks 45+ binaries twice.

**Scope**
- Add `struct_size`, `abi_version`, `min_api`, `display_format` (T14), `caps_required` (reserved for later permission checks), reserved words.
- `EOS_BIN_INITIALIZER` fills the new fields so existing binaries keep compiling.
- Write an ABI policy: what counts as a breaking change, how the version number moves, how the export table (T08) is versioned.

**Acceptance:** all binaries build unchanged. `binfs` still returns the manifest on `read()`; `launcher` handles v1 and v2 structures.

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

### F08 — Typed ioctl convention and lint
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

#### T09 — CPU time accounting and `time`
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

#### T07 — Filesystem registry and flexible `mount`/`umount`
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

#### T12 — Symlinks, `ln`, cross-device handling, own VFS
`Size: M (a–c) + decision (d) · Depends: T07 · Orig #12`

**Reframing:** the original mixes a utility (`ln`) with a rewrite (own VFS). Most of the value comes from the first part.

- **12a — Symlinks:** symlink nodes in `tmpfs` (and optionally as special files on FAT, since FAT has no native links); `symlink`, `readlink`, `lstat`; resolution in `fapi` with loop limit (≤ 8). Ebins: `ln -s`, `readlink`.
- **12b — Hard links:** `link()` inside one filesystem that supports it (`tmpfs`); `ln` without `-s`.
- **12c — `EXDEV`:** `rename` across filesystems returns `EXDEV` from the wrapper; `mv` falls back to copy + unlink; `cp -a` preserves symlinks.
- **12d — Own VFS (decision gate):** only if a limit is hit that 12a–c and T07 cannot solve. Typical IDF VFS limits to test first: maximum registered filesystems, path prefix length, fd table size, no symlinks, select/poll integration. Write the findings into the decisions file before starting.

**Acceptance (a–c):** `ln -s /sdcard/x /tmp/x` works, `cat /tmp/x` reads through, `mv` across `/tmp` and `/sdcard` works, symlink loops fail with `ELOOP`.

#### T06 — Hash and checksum utilities [DONE]
`Size: S · Depends: F06 · Orig #6`

**Scope**
- Shared streaming helper `emisc/hash.h` (`init/update/final`), built on mbedTLS (hardware acceleration for SHA where the SoC has it); constant-time compare helper.
- Ebins: `md5sum`, `sha1sum`, `sha256sum`, `sha512sum` with `-c` check mode and stdin (`-`), `cksum` (CRC32), `base64`, `xxd`/`hexdump`.
- 4–8 KB read chunks from a PSRAM buffer; works on files of any size.

**Acceptance:** NIST/RFC test vectors; hashing a 1 MB file from SD; `sha256sum -c` round trip; throughput numbers recorded in the docs.

**Why early:** T21 and T08 verify hashes.

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

#### T16 — Capability manager: PWM, DMA, interrupts
`Size: M · Depends: – · Orig #16`

**Reframing "load already claimed ones by default":** build it as three layers.
1. **Static** per-SoC resource counts and reserved items from `soc_caps.h` (extends the existing reserved-pin handling). `kernel-core.md` already lists LEDC among governed resources *(verify what `capsmgr.h` actually has)*.
2. **Boot snapshot** *(best effort)*: after IDF initialization and before `init.sh`, record resources IDF components already use where an API exists (for example `gpio_dump_io_configuration`, `esp_intr_dump`); owner shown as `system:<source>`.
3. **Driver claims** on top, with the owner name in the conflict message.

**New resource types:** LEDC timers and channels, GDMA channels, peripheral controllers (SPI/I2S/UART/RMT/MCPWM/GPTimer instances), interrupt sources (peripheral source level, not CPU line).

**Interrupts:** claims are by explicit driver call. Wrapping `esp_intr_alloc*` would give automatic tracking and leak cleanup, at the cost of another wrapper (F01 rule applies). Decide in the decisions file.

**Scope also:** `/proc/capabilities` (type, id, owner); release on detach; conflict log shows both owners.

**Acceptance:** attach two drivers wanting the same LEDC channel → second fails with a message naming the first; `cat /proc/capabilities` matches reality on S3 and P4.

#### T15 — PWM driver
`Size: M · Depends: T16 · Orig #15`

**Scope**
- `pwm` driver, `/dev/pwmN`: ioctls `SET_FREQ`, `SET_DUTY` (16-bit, scaled to hardware resolution), `SET_POLARITY`, `ENABLE`, `FADE(target, ms)`, `GET_CAPS`.
- Channels that share a frequency share an LEDC timer (reference-counted); a channel requesting a different frequency on an occupied timer is rejected with a clear error. Resolution depends on frequency, so `GET_CAPS` reports the achievable bits.
- First consumer: **display backlight** (`bl` pin is currently a plain pin argument *(verify)*). Fallback to on/off when no PWM is attached.
- Other consumers: buzzer/tone, servo helper (50 Hz). MCPWM is out of scope.
- `devctl attach` syntax documented with examples.

**Acceptance:** `echo`/ioctl dims the backlight on Lilka and the P4 board; two channels at different frequencies conflict cleanly; detach releases the timer.

#### T14 — Display colour mode
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
- ES8311 first (most likely on the JC4880P4 board *(verify)*); `max98357` migrated to the new structure without behaviour change.
- Hearing-protection volume cap stays at the control node.

**Acceptance:** play and record on the board; changing volume through the control node during playback; `test_sound` passes unchanged for max98357; docs updated with the new structure.

#### T03 — USB: host (a) and composite device (b)
`Size: L (a) + XL (b) · Depends: T16, T07, F07 · Orig #3`

**Facts that shape the design**
- On ESP32-S3 the built-in USB-Serial-JTAG and the OTG controller **share one PHY**. Enabling device mode via TinyUSB takes over the pins, so the built-in console and JTAG go away. Document this and give a CDC function as the replacement console. P4 has separate PHYs *(verify)*.
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

#### T10 — Device-selection environment variables
`Size: S · Depends: – · Orig #10`

**Scope**
- Standard names: `EOS_DISPLAY`, `EOS_INPUT`, `EOS_AUDIO_OUT`, `EOS_AUDIO_IN`, `TERM`. Precedence: command-line option > environment > default.
- Defaults set per board in `init.sh` with `export`.
- Helper `eos_dev_open_default(EOS_KIND_DISPLAY, flags)` used by SDL layer, LVGL apps, tests; no hard-coded `/dev/fb0`-style paths in ebins (audit rule).
- `env` / `printenv` ebin if missing; verify per-process inheritance and thread-safety of `getenv/setenv` *(verify)*.

**Acceptance:** point `EOS_AUDIO_OUT` at a second sound device and an app follows; unset variable falls back to the board default.

#### T04 — SDL / SDL2 completion
`Size: XL · Depends: T02, T10, T14 · Orig #4`

**Approach**
1. **Coverage spike (S):** choose 3–5 reference programs (one SDL1.2 game, one SDL2 game, one software-rendered demo). `nm -u` the objects for undefined `SDL_*` symbols; generate `docs/.../sdl-coverage.md` with implemented / stubbed / missing.
2. **Decision:** extend the shim by frequency, or port real SDL2 with EOS video/audio/input backends (see decisions file). If keeping the shim, implement SDL2 first and build the SDL 1.2 layer on top of it instead of maintaining two independent surfaces.
3. **Implementation order:** timers and threads/mutex/semaphores (map to pthreads) → events (keyboard, touch as mouse) → video (surfaces, blits, palettes, pixel formats via T14) → audio (callback model on a worker thread over the T02 playback node) → RWops.
4. Out of scope at first: SDL_image/mixer/ttf, OpenGL.

**Acceptance:** all reference programs build unmodified and run on QEMU graphics and one real board; coverage doc is generated by script and CI fails if coverage regresses.

#### T08 — ELF loader and export table
`Size: XL · Depends: F02, F03, F05, T06, T21 · Orig #8`

**This changes the app model** from "static only" (the docs say so explicitly) to loadable code, so it comes last. Do the design part early (F05) so nothing else conflicts.

**Scope**
- **Export table:** generate from a manifest (`exports.def`) at build time. Entries get a **stable append-only index**; the table lives in flash (`.rodata`) as 4-byte addresses indexed by number. Names are not stored in firmware; an app is linked or post-processed against the same `exports.def`, or uses a 32-bit name hash table in flash. CI fails if an existing index changes.
- **Do not use ROM libc addresses for functions EOS wraps** (`malloc`, `open`, `system`, …): calling the ROM function directly would bypass tracking and path resolution. Static ROM offsets are fine only for pure functions (`memcpy`, `strlen`, …).
- **Loader:** parse ELF, apply relocations for each SoC (Xtensa and RISC-V differ), embed the existing manifest in an `.eos_bin` section. Evaluate Espressif's `elf_loader` component as a base before writing one.
- **Executable memory:** check where loadable code may live per SoC. Internal memory allocated with `MALLOC_CAP_EXEC` is the safe assumption on S3; PSRAM execution support differs by SoC and IDF version. This limits loadable program size, so measure early.
- **Tracking:** loaded segments are owned by the process (F02) and freed on exit; the loader runs through `exec` so F04 pids, F03 hooks and `/proc` work for loaded apps.
- **Trust:** loading code from SD is stricter than T21. Require a hash allow-list at minimum; signing is a later step. `eos_bin_t.caps_required` (F05) prepares per-app device permissions.
- ABI version check on load (F05 policy).

**Acceptance:** `hello.eos` built outside the firmware loads from `/sdcard`, runs, leaks nothing, exits; a program built against an older ABI index still runs after new exports are added; an unlisted hash is refused.
