# EOS: decisions to take, audit rules, test strategy

Companion to [`tasks.md`](tasks.md) and [`implementation-order.md`](implementation-order.md). Resolved decisions (`D1, D2, D12–D15`) and completed-task history are archived in [`history.md`](history.md).

---

## 1. Decisions (Active & Architectural)

Each entry gives options, a recommendation/rule, and the dependent task.

| # | Decision | Options | Recommendation / Rule | Decide before |
|---|---|---|---|---|
| D3 | **Signal model** | Cooperative only / cooperative plus forced `vTaskDelete` for `SIGKILL` | Cooperative with documented, warned escalation | T18 |
| D4 | **Curses implementation** | Full ncurses / smaller BSD curses | Try the smaller one first if it covers `top`/`less`; measure flash and heap for both | T01 |
| D5 | **Filesystem layer** | Keep IDF VFS plus T07/T12a–c / write own VFS (T12d) | Keep IDF VFS; run the limits probe first; reopen only on evidence | T12d |
| D6 | **NVS file mapping** | Raw bytes plus type ioctl / text values / typed binary header | Raw bytes plus type ioctl and optional `.type` view | T22 |
| D7 | **libcurl approach** | Shim over `esp_http_client` / port real libcurl | Check the IDF component registry first; otherwise shim, `curl_multi` later | T05 |
| D8 | **SDL approach** | Extend shim / port SDL2 with EOS backends | Decide after the coverage spike. If the reference programs need under about 150 distinct symbols, extend the shim (SDL2 first, SDL1.2 on top) | T04 |
| D9 | **USB network function** | CDC-NCM / ECM / PPP over CDC-ACM | NCM first; PPP optional | T03b |
| D10 | **MSC exclusivity rule** | Unmount first / read-only export / dedicated partition | Unmount first, read-only by default | T03b |
| D11 | **Trust key for T21** | Content hash only / hash plus volume serial | Content hash only; volume serial as later hardening | T21 |
| D16 | **ELF / `.so` memory placement** | PSRAM with SRAM fallback / strict PSRAM-only | **PSRAM-first (`MALLOC_CAP_SPIRAM`)**; fail with `-ENOMEM` on PSRAM exhaustion on PSRAM-equipped boards unless explicitly opted in, with SRAM fallback allowed when `!CONFIG_SPIRAM` | T30 |
| D17 | **ABI evolution policy** | Ad-hoc `LEGACY` shims / sequential per-scope step-upgraders | **No `EOS_*_ABI_LEGACY` shims**; core code is single-version, and ABI bumps add contiguous `1_to_2`..`(N-1)_to_N` upgraders run once at load time | T29 |

---

## 2. `layer_audit` rules (Active & Planned)

Put each rule in `tools/layer_audit_rules.json` / `tools/layer_audit.py`.

| Rule | Applies to | Forbids / Enforces | Why / task |
|---|---|---|---|
| `no-direct-http-client` | `main/ebin/**` | Forbids `esp_http_client.h`, `esp_tls.h`, `esp-tls` APIs | Force the curl layer (T05) |
| `no-direct-http-server` | `main/ebin/**` | Forbids `esp_http_server.h` | Force the libmicrohttpd layer (T11) |
| `no-hardcoded-device-paths` | `main/ebin/**` | Forbids string literals `/dev/fb`, `/dev/sound`, `/dev/keyboard`, `/dev/input` outside tests | Use `eos_dev_open_default` (T10) |
| `no-direct-nvs` | everything except the `nvs` driver | Forbids `nvs_*` IDF calls | NVS access goes through the driver (T22) |
| `no-direct-idf-vfs` | `main/ebin/**`, `main/edriver/**` | Forbids `esp_vfs_*` | Keep VFS knowledge in `ecore`/`ewrap` (T07, T12) |
| `no-raw-sockets-without-hook` | `main/ebin/**` | Forbids `socket(` without going through a tracked wrapper | Cleanup coverage (F03) |
| `ioctl-has-type-and-doc` | all headers | Forbids ioctl macros without an argument struct or a doc row | F08 |
| `wrapped-symbol-documented` | `main/CMakeLists.txt` | Forbids `--wrap=` entries missing from the wrapper table | F01 |
| `manifest-abi` | `main/ebin/**` | Forbids manifests without `abi_version`/`struct_size` | F05 |
| `no-direct-led-pwm` | `main/edriver/**` except `pwm` | Forbids direct `ledc_*` | PWM goes through the pwm driver and capsmgr (T15, T16) |
| `no-direct-bus-apis-ecore` | `main/ecore/**` | Forbids direct vendor bus APIs (`spi_bus_*`, `spi_device_*`, `i2c_master_*`, `sdmmc_*`, `sdspi_*`, `adc_*`, `i2s_*`, `scan_i2c_bus`) | Kernel core communicates through driver abstractions and `eos_dev_*` |
| `no-direct-bus-apis-ebin` | `main/ebin/**` | Forbids direct vendor bus APIs (`spi_bus_*`, `spi_device_*`, `i2c_master_*`, `sdmmc_*`, `sdspi_*`, `adc_*`, `i2s_*`, `scan_i2c_bus`) | User apps communicate with peripherals strictly via `/dev` |
| `no-legacy-abi-shims` | `main/**` | Forbids `EOS_.*_LEGACY` symbols/macros and `abi_version` branching outside the dedicated ABI upgrader/loader module | Enforce single-version core paths (T29) |
| `abi-step-upgrader-chain` | `main/ecore/**` | Enforces contiguous `1_to_2` .. `(N-1)_to_N` step-upgrader functions whenever `EOS_*_ABI_VERSION > 1` | Safe deterministic ABI upgrades (T29, T30) |
| `prefer-psram-elf-load` | `main/ecore/elf.c` | Enforces `MALLOC_CAP_SPIRAM` first for `.elf`/`.so` segments when `CONFIG_SPIRAM` is enabled (preventing silent SRAM drain on PSRAM boards) | Protect internal DMA/ISR SRAM (T30) |

---

## 3. Test strategy (Pending & Active Tasks)

Tag each suite `qemu` or `hardware-only` (F06). CI runs every `qemu` suite.

| Task | Suite | Runs on |
|---|---|---|
| F07 | echo on/off, raw mode, line editing, winsize | QEMU (UART), hardware (`lvgl_term`) |
| T18/T19 | Ctrl-C, Ctrl-Z, fg/bg, `kill`, handlers | QEMU |
| T01 | `tput`, ncurses `top` on UART | QEMU, hardware |
| T22 | persistence across reboot, Wi-Fi init after NVS move | QEMU (partition), hardware (Wi-Fi) |
| T21 | prompt, trust store, modified file, headless skip | QEMU with scripted input |
| T24/T25/T26 | `lvgl_fm`, `eos_open` MIME/exec dispatch, `lvgl_edit` `--auto` | QEMU graphics, hardware |
| T02 | play, record, control node | hardware-only |
| T03 | host MSC/HID, device composite, endpoint budget refusal | hardware-only |
| T05/T11 | HTTP/HTTPS requests, daemon cleanup | hardware, QEMU if networking is available |
| T04 | reference programs | QEMU graphics, hardware |
| T27 | host `#$` parser unit tests; `/tmp` `#!/bin/ush` script exec, `eos_obj_get_path()`, launcher filtering | Host, QEMU, hardware |
| T28 | host `eos_drv_param_t` validator tests; `/dev/<drv>/{cfg,pins}/*` enum/bounds/`-EBUSY`/stale-FD checks | Host, QEMU, hardware |
| T29 | `layer_audit.py -x` ABI rules; `/proc/objects` enumeration | Host, QEMU, hardware |
| T30 | `/tmp` `.so` load/unload, `-EBUSY` refcount guard, PSRAM `-ENOMEM`, 10× `test_leaks` cycle, concurrent `/proc/objects` | QEMU, hardware |

---

## 4. Documentation rules that go with every task

- Update the matching file under `docs/` (the layout is already topic-based) and the binaries catalog.
- One row per new feature in the generated status matrix (`python3 tools/status_matrix.py`).
- New wrapper → row in the wrapper table (F01 / `tools/wrapcheck.py`).
- New ioctls → typed macro and doc row (F08).
- New binary → manifest and an entry in `docs/binaries/catalog.md`.

---

## 5. Open questions for the repo owner

1. Which board carries the ES8311 (and any other codec), and is capture wired?
2. Does QEMU provide networking and a second UART? It decides which tests can run in CI.
3. How does `ush` currently implement line editing and echo? It decides how much of F07 is new.
