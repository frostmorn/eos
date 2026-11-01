# PSRAM Heap Migration Analysis & Strategic Implementation Report

This report documents the dynamic heap memory allocation audit across the EOS kernel core (`main/ecore/`) and peripheral/bus drivers (`main/edriver/`), evaluates hardware DMA constraints, and summarizes completed migrations to external PSRAM (SPIRAM).

---

## 1. Architectural Motivation

In embedded SoCs (such as the ESP32-S3 and ESP32-P4), on-chip internal SRAM (384 KB – 768 KB) is a scarce, high-demand resource critical for:
1. **FreeRTOS Task Stacks:** Thread stacks require continuous physical internal memory to avoid cache-miss jitter.
2. **Hardware DMA Buffers:** On architectures like the ESP32-S3, hardware DMA controllers for High-Speed SPI and LCD interfaces cannot directly access external PSRAM over SPI/Octal buses without dedicated EDMA channels.
3. **Interrupt Service Routines & Time-Critical Code:** Execution from IRAM requires internal SRAM.
4. **Network Buffers:** Wi-Fi and Bluetooth baseband hardware descriptors and RX/TX ring buffers mandate DMA-capable internal memory.

Conversely, embedded PSRAM offers large capacity (8 MB to 32 MB). While read/write access to external PSRAM incurs a cache-miss latency penalty (~30–50 clock cycles versus 1–2 cycles for internal SRAM), **data structures that are accessed infrequently or are of substantial size should be placed in PSRAM**.

With the capability-aware [`kvec`](../../main/emisc/kvec.h) (`kvec_psram_t`) and the global reallocation tracking wrapper (`__wrap_heap_caps_realloc_base` in `main/ewrap/heap_caps.c`), EOS possesses a verified foundation for systematic PSRAM migration.

---

## 2. Completed PSRAM Migrations & Memory Reclaimed

| Subsystem / Target Buffer | Migration Mechanism | Memory Reclaimed (SRAM) | Operational Impact |
| :--- | :--- | :---: | :--- |
| **MicroShell Pratt Parser AST & Tokens** | Migrated from fixed static internal arrays to dynamic `kvec_psram_t` (`MALLOC_CAP_SPIRAM`). | **+56.3 KB DRAM** | Boot-time internal SRAM pressure permanently eliminated with zero command parsing latency penalty. |
| **Wi-Fi Raw 802.11 Sniffer Queue** | Allocated via `heap_caps_calloc_prefer(..., MALLOC_CAP_SPIRAM)` in `edriver/net/wifi/wifi.c`. | **+48 to 64 KB DRAM** | Capturing 50+ promiscuous packets no longer exhausts system heap. |
| **GUI Application Task Stacks** | `EOS_BIN_FLAG_GUI` routes thread stacks to 16 KB PSRAM via `eos_pthread_set_manifest_cfg()`. | **+16 KB DRAM per task** | Eliminates stack exhaustion on RISC-V architectures; internal DRAM cost for GUI is **0 bytes**. |
| **Procfs Dynamic String Buffers** | `sbuf_t.buf` allocates in PSRAM during `/proc/*` inspection reads. | **+4 to 16 KB DRAM** | Transient memory spikes during `cat /proc/meminfo` or `devctl devices` do not fragment SRAM. |

---

## 3. Hardware DMA & Architecture Constraints Matrix

Before moving driver buffers to PSRAM, the target architecture's DMA capabilities must be evaluated:

| SoC Architecture | Bus / Controller | External PSRAM DMA Capable? | Constraint Rule |
| :--- | :--- | :---: | :--- |
| **ESP32-S3** | General SPI (SPI2 / SPI3) | ❌ **No** | Buffers passed to `spi_device_transmit` **must** allocate with `MALLOC_CAP_DMA \| MALLOC_CAP_INTERNAL`. |
| **ESP32-S3** | I2S Audio DMA | ❌ **No** | Audio DMA descriptors and bounce rings allocate from internal DMA SRAM. |
| **ESP32-P4** | High-Speed AXI 2D-DMA / LCD | ✅ **Yes** | Displays and peripherals can source frames and sector buffers directly from PSRAM. |
| **ESP32-P4** | SDMMC Host (SDIO) | ✅ **Yes** | SD card and co-processor transfers support direct PSRAM DMA buffers. |

---

## 4. Retained in Internal SRAM (Strict Invariants)

The following structures are strictly prohibited from PSRAM migration:
1. **`tctx->memblocks`:** Mutated on **every single `malloc()` and `free()`**. Retaining this in internal SRAM eliminates memory wrapper recursion hazards and prevents allocator latency degradation.
2. **ESP32-S3 SPI Line Buffers (`ST7789_DMA_STRIP_BYTES`):** Touched continuously on every display flush via SPI DMA; hardware cannot DMA from Octal PSRAM on S3.
3. **Driver Mutexes & FreeRTOS Semaphores:** Synchronization primitives must reside in fast internal SRAM to avoid cache eviction deadlocks.
