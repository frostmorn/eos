# EOS: decisions to take, audit rules, test strategy

Companion to `eos-tasks.md` and `eos-implementation-order.md`. Items marked *(verify)* come from the docs, not from source code.

---

## 1. Decisions to make before the dependent task starts

Each entry gives options, a recommendation, and the latest point to decide.

| # | Decision | Options | Recommendation | Decide before |
|---|---|---|---|---|
| D1 | **Memory ownership unit** | Per thread (today) / per process / explicit hand-over only | Per process (pid) with a kernel context for driver state; `eos_mem_adopt` for explicit transfers | F02 |
| D2 | **Where driver state is charged** | Attaching thread / kernel context | Kernel context during `init()`/`shutdown()` | F02 |
| D3 | **Signal model** | Cooperative only / cooperative plus forced `vTaskDelete` for `SIGKILL` | Cooperative with documented, warned escalation | T18 |
| D4 | **Curses implementation** | Full ncurses / smaller BSD curses | Try the smaller one first if it covers `top`/`less`; measure flash and heap for both | T01 |
| D5 | **Filesystem layer** | Keep IDF VFS plus T07/T12a–c / write own VFS (T12d) | Keep IDF VFS; run the limits probe first; reopen only on evidence | T12d |
| D6 | **NVS file mapping** | Raw bytes plus type ioctl / text values / typed binary header | Raw bytes plus type ioctl and optional `.type` view | T22 |
| D7 | **libcurl approach** | Shim over `esp_http_client` / port real libcurl | Check the IDF component registry first; otherwise shim, `curl_multi` later | T05 |
| D8 | **SDL approach** | Extend shim / port SDL2 with EOS backends | Decide after the coverage spike. If the reference programs need under about 150 distinct symbols, extend the shim (SDL2 first, SDL1.2 on top) | T04 |
| D9 | **USB network function** | CDC-NCM / ECM / PPP over CDC-ACM | NCM first; PPP optional | T03b |
| D10 | **MSC exclusivity rule** | Unmount first / read-only export / dedicated partition | Unmount first, read-only by default | T03b |
| D11 | **Trust key for T21** | Content hash only / hash plus volume serial | Content hash only; volume serial as later hardening | T21 |
| D12 | **Interrupt claims** | Explicit driver calls / wrap `esp_intr_alloc*` | Explicit calls; wrap only if leaks appear in practice (each wrap costs maintenance, see F01) | T16 |
| D13 | **Loader export lookup** | Append-only index table / hash table in flash | Index table in flash plus build-time post-processing of apps; hash table as fallback | T08 |
| D14 | **Executable memory for loaded code** | Internal `MALLOC_CAP_EXEC` / PSRAM where supported | Measure on S3 and P4; assume internal only until proven | T08 |
| D15 | **Display data path** | Keep `write()` frames / add native zero-copy | Keep for now; add typed formats and flush (T14); revisit with SDL performance numbers | T04 |

---

## 2. `layer_audit` rules to add

Put each rule in the declarative rule file proposed in F06.

| Rule | Applies to | Forbids | Why / task |
|---|---|---|---|
| `no-direct-http-client` | `main/ebin/**` | `esp_http_client.h`, `esp_tls.h`, `esp-tls` APIs | Force the curl layer (T05) |
| `no-direct-http-server` | `main/ebin/**` | `esp_http_server.h` | Force the libmicrohttpd layer (T11) |
| `no-hardcoded-device-paths` | `main/ebin/**` | string literals `/dev/fb`, `/dev/sound`, `/dev/keyboard`, `/dev/input` outside tests | Use `eos_dev_open_default` (T10) |
| `no-direct-nvs` | everything except the `nvs` driver | `nvs_*` IDF calls | NVS access goes through the driver (T22) |
| `no-direct-idf-vfs` | `main/ebin/**`, `main/edriver/**` | `esp_vfs_*` | Keep VFS knowledge in `ecore`/`ewrap` (T07, T12) |
| `no-raw-sockets-without-hook` | `main/ebin/**` | `socket(` without going through a tracked wrapper | Cleanup coverage (F03) |
| `ioctl-has-type-and-doc` | all headers | ioctl macros without an argument struct or a doc row | F08 |
| `wrapped-symbol-documented` | `main/CMakeLists.txt` | `--wrap=` entries missing from the wrapper table | F01 |
| `manifest-v2` | `main/ebin/**` | manifests without `abi_version`/`struct_size` | F05 |
| `no-direct-led-pwm` | `main/edriver/**` except `pwm` | direct `ledc_*` | PWM goes through the pwm driver and capsmgr (T15, T16) |
| `no-direct-bus-apis-ecore` | `main/ecore/**` | direct vendor bus APIs (`spi_bus_*`, `spi_device_*`, `i2c_master_*`, `sdmmc_*`, `sdspi_*`, `adc_*`, `i2s_*`, `scan_i2c_bus`) | Kernel core communicates through driver abstractions and `eos_dev_*`, never direct vendor bus APIs |
| `no-direct-bus-apis-ebin` | `main/ebin/**` | direct vendor bus APIs (`spi_bus_*`, `spi_device_*`, `i2c_master_*`, `sdmmc_*`, `sdspi_*`, `adc_*`, `i2s_*`, `scan_i2c_bus`) | User apps communicate with peripherals strictly via `/dev` and standard VFS syscalls |

---

## 3. Test strategy

Tag each suite `qemu` or `hardware-only` (F06). CI runs every `qemu` suite.

| Task | Suite | Runs on |
|---|---|---|
| F02 | cross-thread free, attach-from-short-lived-ebin, per-process exit | QEMU |
| F03 | `test_leaks`: one leak of each resource type | QEMU (sockets need a loopback) |
| F04 | 1000 spawn/exit cycles, orphan re-parenting, pipelines share a pgid | QEMU |
| F07 | echo on/off, raw mode, line editing, winsize | QEMU (UART), hardware (`lvgl_term`) |
| T18/T19 | Ctrl-C, Ctrl-Z, fg/bg, `kill`, handlers | QEMU |
| T01 | `tput`, ncurses `top` on UART | QEMU, hardware |
| T06 | NIST/RFC vectors, 1 MB file | QEMU, hardware (SD throughput) |
| T07 | `/proc/filesystems`, mount/umount by path and device, busy umount | QEMU, hardware (SD) |
| T09/T13/T17/T20 | counters match expected load patterns; `/proc` entries appear and vanish | QEMU |
| T12a–c | symlinks, loops (`ELOOP`), `mv` across filesystems | QEMU |
| T22 | persistence across reboot, Wi-Fi init after NVS move | QEMU (partition), hardware (Wi-Fi) |
| T21 | prompt, trust store, modified file, headless skip | QEMU with scripted input |
| T16 | `/proc/capabilities`, conflicts, release on detach | QEMU (partly), hardware |
| T15 | dimming, shared timers | hardware-only |
| T14 | format switching and restore | QEMU (`GET_FORMATS`), hardware |
| T02 | play, record, control node | hardware-only |
| T03 | host MSC/HID, device composite, endpoint budget refusal | hardware-only |
| T05/T11 | HTTP/HTTPS requests, daemon cleanup | hardware, QEMU if networking is available |
| T04 | reference programs | QEMU graphics, hardware |
| T08 | load, run, no leaks, ABI index test, untrusted refusal | QEMU (if exec memory allows), hardware |

---

## 4. Documentation rules that go with every task

- Update the matching file under `docs/` (the layout is already topic-based) and the binaries catalog.
- One row per new feature in the generated status matrix (F06).
- New wrapper → row in the wrapper table (F01).
- New ioctls → typed macro and doc row (F08).
- New binary → manifest v2 and an entry in the catalog.
- Anything marked *(verify)* above gets resolved and the marker removed from the docs.

---

## 5. Open questions for the repo owner

1. Which board carries the ES8311 (and any other codec), and is capture wired?
2. Does QEMU provide networking and a second UART? It decides which tests can run in CI.
3. How does `ush` currently implement line editing and echo? It decides how much of F07 is new.
4. Are driver allocations made during `devctl attach` charged to the calling ebin today? This decides the urgency of F02.
5. Is the project targeting IDF 5.x, 6.0, or both?
6. Is "OS" meant to stay a statically linked firmware framework, or do loadable apps (T08) define the long-term direction? Phase 6 depends on the answer.
