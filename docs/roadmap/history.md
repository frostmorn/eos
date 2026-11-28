# EOS Roadmap History (Completed Work & Resolved Decisions)

Archive of completed phases, tasks, resolved design decisions, and answered open questions moved out of [`implementation-order.md`](implementation-order.md) and [`decisions-and-audit-rules.md`](decisions-and-audit-rules.md). See [`tasks.md`](tasks.md) for the full task index and [`docs/status.md`](../status.md) for the live feature matrix.

---

## 1. Completed Phases & Tasks

### Phase 0 — Foundations (Completed: F01–F06)
* **F01** IDF pin, wrapper inventory (`tools/wrapcheck.py`), CI matrix
* **F02** Memory ownership by process, not thread (`main/ecore/proc.c`)
* **F03** Cleanup-hook registry (sockets, timers, tasks, ISRs)
* **F04** Process table (`pid` / `ppid` / `pgid` / `wait`)
* **F05** Manifest and ABI versioning policy (`EOS_BIN_MAGIC`, `struct_size`, `abi_version`)
* **F06** Test and audit infrastructure (`tools/hardware_test.py`, `tools/layer_audit.py`)

### Phase 1 — Quick Wins & Observability Base (Completed: T06, T07, T10, T13, T17, T20)
* **T06** Hash and checksum utilities (`sha1sum`, `sha224sum`, `sha256sum`, `tools/host_tests/hash_host_test.py`)
* **T07** Filesystem registry (`eos_fs_type_t`), flexible `mount` / `umount`
* **T10** Device-selection environment variables
* **T13** Real per-process `/proc`
* **T17** Run-time stats and `/proc/loadavg`
* **T20** `ps`

### Phase 2 (Partial Completion)
* **T09** CPU time accounting and `time`

### Phase 3 (Partial Completion)
* **T12** Symlinks, `ln`, cross-device handling (`EXDEV`)
* **T23** SD/SDIO partition automounting on boot (`/mnt/sd`)

### Phase 4 (Partial Completion)
* **T16** Capability manager: PWM, DMA, interrupts (`/proc/capabilities`)
* **T15** PWM driver
* **T14** Display colour mode (`ioctl` + manifest flag)

### Phase 6a — ELF Loader & Export Table (Completed: T08)
* **T08** Dynamic ELF loader (`main/ecore/elf.c`), export symbol table (`main/ecore/elf_symbols.c`), `$PATH` / cwd resolution, and `test_elf_loader`.

---

## 2. Resolved Design Decisions

| # | Decision | Resolution | Task |
|---|---|---|---|
| D1 | **Memory ownership unit** | Per-process (`pid`) ownership with kernel context for driver state | F02 |
| D2 | **Where driver state is charged** | Kernel context during `init()` / `shutdown()` | F02 |
| D12 | **Interrupt claims** | Explicit driver calls via capability manager (`capsmgr`) | T16 |
| D13 | **Loader export lookup** | Registered export symbol table in `main/ecore/elf_symbols.c` | T08 |
| D14 | **Executable memory for loaded code** | Relocated ELF segments in PSRAM where supported (`MALLOC_CAP_SPIRAM`); formalized as strict PSRAM-only in T30 | T08 / T30 |
| D15 | **Display data path** | Kept `write()` frames with typed formats and flush (`T14`) | T14 |

---

## 3. Resolved Open Questions

* **Target ESP-IDF version:** Resolved — project targets **ESP-IDF 6.0+** at minimum.
* **Driver allocations during `devctl attach`:** Resolved in F02 (kernel context ownership).
* **Static firmware vs. loadable apps:** Resolved by completing T08 (ELF loader) and planning Phase 6b (`T27–T30`) for scripts, driver config VFS, unified system objects, and loadable/unloadable `.so`/`.elf` modules.
