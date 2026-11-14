# Embedded Memory Architecture & Stack Allocation

This document outlines the memory hierarchy, allocation capabilities, hardware DMA constraints, and stack placement architecture in **EOS (ESP Operating System)** across Xtensa (ESP32-S3) and RISC-V (ESP32-P4) SoCs.

---

## 1. Physical Memory Hierarchy

In Espressif microcontrollers, system memory is divided into two distinct domains:

```text
+-----------------------------------------------------------------------------------+
|                                 EOS MEMORY DOMAINS                                |
+-------------------------------------------------+---------------------------------+
|              INTERNAL SRAM (DRAM)               |      EXTERNAL PSRAM (SPIRAM)    |
|               384 KB – 768 KB                   |            8 MB – 32 MB         |
+-------------------------------------------------+---------------------------------+
| • Ultra-fast (1–2 CPU cycles)                   | • High capacity (SPI/Octal bus) |
| • Hard requirement for ESP32-S3 SPI DMA         | • Cache-miss penalty (30-50 cyc)|
| • Wi-Fi PHY & Bluetooth hardware ring-buffers   | • Ideal for bulk buffers & GUI  |
| • Time-critical ISRs & FreeRTOS kernel data     | • Large task execution stacks   |
+-------------------------------------------------+---------------------------------+
```

---

## 2. Allocation Capabilities & Best Practices

| Memory Capability Flag | Typical Target | Usage Guidelines |
| :--- | :--- | :--- |
| `MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT` | Internal DRAM | Hot-path data structures, frequent mutation tables (e.g. `proc->memblocks`), ISR buffers. |
| `MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT` | External PSRAM | Bulk data, dynamic vectors (`kvec_psram_t`), large strings, GUI draw buffers, task stacks, process table (`s_proc_table`). |
| `MALLOC_CAP_INTERNAL \| MALLOC_CAP_DMA` | DMA-capable SRAM | Required on ESP32-S3 for SPI/LCD line strip descriptors and Wi-Fi packet descriptors. |

### Golden Rules for Everyday Developers:
1. **Never allocate bulk data in internal SRAM:** Any buffer exceeding 1 KB that is not touched on high-frequency hardware interrupt loops should prefer PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
2. **Never place `proc->memblocks` in PSRAM:** The process memory block tracker is accessed on every single `malloc()` and `free()`; placing it in PSRAM causes cache contention and latency penalties. Keep it in internal SRAM (`kvec_t(void *)`).
3. **Use Capability-Aware `kvec`:** Use `kvec_psram_t(T)` (`caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`) for dynamic array growth in user utilities.

---

## 3. Hardware DMA Constraints Matrix

Before selecting buffer placement, hardware DMA bus routing must be evaluated:

| SoC Architecture | Hardware Controller | Direct PSRAM DMA Capable? | Constraint Rule |
| :--- | :--- | :---: | :--- |
| **ESP32-S3** | General SPI (SPI2 / SPI3) | ❌ **No** | Buffers passed to `spi_device_transmit` **must** allocate with `MALLOC_CAP_DMA \| MALLOC_CAP_INTERNAL`. |
| **ESP32-S3** | I2S Audio DMA | ❌ **No** | Audio DMA descriptors and bounce rings allocate from internal DMA SRAM. |
| **ESP32-P4** | High-Speed AXI 2D-DMA / LCD | ✅ **Yes** | Displays and peripherals can source frames and sector buffers directly from PSRAM. |
| **ESP32-P4** | SDMMC Host (SDIO) | ✅ **Yes** | SD card and co-processor transfers support direct PSRAM DMA buffers. |

---

## 4. Stack Allocation & GUI Routing

Task call stacks represent one of the largest consumers of RAM in embedded systems:
* **Standard CLI Commands:** Default to **8 KB stacks** in internal SRAM (`EOS_BIN_DEFAULT_STACK_SIZE`).
* **GUI / LVGL Applications:** Automatically routed to **16 KB stacks in external PSRAM** (`EOS_BIN_FLAG_GUI` or `EOS_BIN_FLAG_STACK_PSRAM`), resulting in **0 internal SRAM bytes consumed**.
* **Stack Isolation in `system()`:** Single-stage GUI commands run in dedicated worker pthreads on PSRAM stacks, preventing stack overflows on RISC-V architectures with 12 callee-saved registers (`s0`–`s11`).
* **Graceful Fallback:** If PSRAM allocation fails, `ewrap/pthread.c` automatically clamps stack size to 8 KB and falls back to internal SRAM.

---

## 5. Pratt Parser PSRAM Optimization (+56.3 KB SRAM Reclaimed)

The EOS MicroShell (`ush`) utilizes a Pratt precedence parser for script interpretation and command pipelines.
* Previously, AST nodes and token arrays allocated static internal SRAM structures, consuming over 56 KB of DRAM at boot.
* Migrating AST nodes to dynamic `kvec` allocations from `MALLOC_CAP_SPIRAM` permanently freed **56.3 KB of internal SRAM** back to the system DRAM pool with zero performance degradation.

---

## 6. Global SRAM Shrinkification & PSRAM Migration

To maximize internal SRAM availability for DMA and hardware peripherals, all non-hotpath global structures have been eliminated from static `.bss`/`.data` and migrated to dynamic allocations steered by access frequency:

### A. Static Buffer & Structure Relocation
1. **Device Tree Registry (`s_devices`):** Removed hardcoded static array `eos_devices[EOS_MAX_DEVICES]`. Devices are now tracked dynamically in PSRAM via `kvec_psram_t(eos_dev_t *) s_devices` in [dev.c](../../main/ecore/dev.c).
2. **Kernel Process Context (`s_kernel_proc`):** Removed static 1,744 B `.bss` allocation; allocated on-demand in PSRAM heap during `eos_proc_init()` in [proc.c](../../main/ecore/proc.c).
3. **FAPI Scratch Buffer (`eos_fapi_buffer`):** Removed static 4,096 B `.bss` path normalization buffer; dynamically allocated in PSRAM with heap tracking disabled in [fapi.c](../../main/ecore/fapi.c).
4. **File Descriptor Map (`proc->fd_map`):** Migrated from fixed array `int fd_map[FD_SETSIZE]` (256 B per process) to dynamic `kvec_psram_t(int) fd_map` with inline helpers `eos_proc_fd_map_get` and `eos_proc_fd_map_set`.
5. **Procfs Enumeration Buffers:** Replaced static `pid_t pids[EOS_MAX_PROCS]` and `int fds[EOS_MAX_PROCS]` in `procfs_dir_t` with `kvec_psram_t(pid_t)` and `kvec_psram_t(int)` in [procfs.c](../../main/ecore/procfs.c).
6. **Task Flags Tracking (`s_task_flags`):** Dynamic internal SRAM allocation via `heap_caps_calloc(..., MALLOC_CAP_INTERNAL)` for spinlock-guarded fast-path access.

### B. Network & TCB Shrinkification
* **LwIP PSRAM Steering:** Intercepted LwIP network thread (`"tiT"` / `"tcpip"`) allocations in [heap_caps.c](../../main/ewrap/heap_caps.c) to prefer `MALLOC_CAP_SPIRAM` for packet buffers and PCB lists while preserving WLAN hardware buffers strictly in internal SRAM.
* **ESP-Hosted SDIO Mempool Steering (`CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`):** On ESP32-P4 SoCs where GDMA routes across cache lines, SDIO transport mempools (over 300 KB) are steered to external PSRAM, preserving scarce internal DRAM.
* **FreeRTOS TCB Reduction:** Reduced FreeRTOS Thread Local Storage pointers to 1 (`CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS=1`), shrinking every Task Control Block throughout the system.
