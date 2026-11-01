# Ebin Manifest Flags & Stack Allocation Architecture

## 1. Overview & Problem Context

In embedded operating systems running on Espressif SoCs (ESP32-S3, ESP32-P4), **internal SRAM (DRAM)** is an exceptionally scarce and valuable resource. Out of 320 KB to 512 KB of physical SRAM, a large portion is permanently reserved for WiFi PHY buffers, Bluetooth stacks, LWIP networking pools, DMA descriptors, and FreeRTOS kernel structures. Remaining SRAM must support all dynamic heap allocations and task execution stacks.

Prior to these architectural fixes:
1. **Pratt Parser Memory Footprint:** The MicroShell Pratt parser AST and token structures allocated large fixed internal SRAM tables, causing substantial DRAM pressure at boot.
2. **GUI Task Stack Starvation:** Modern graphical applications (such as `lvgl_net` and desktop launchers) require large call stacks (16 KB to 32 KB) due to complex LVGL widget hierarchies, string rendering, network status queries, and event handlers. Allocating these stacks from internal SRAM severely depleted DRAM headroom.
3. **Architecture-Specific Stack Pressure:** On RISC-V architectures (ESP32-P4), the standard calling convention mandates saving 12 callee-saved registers (`s0`–`s11`, plus `ra`) across deep stack frames. When GUI applications ran synchronously on the caller's thread stack inside `system()`, stack overflow exceptions or silent memory corruption occurred.

To resolve these challenges holistically across Xtensa (ESP32-S3) and RISC-V (ESP32-P4), EOS introduced:
- Dynamic PSRAM vector migration for the Pratt parser via `kvec`.
- Direct stack allocation preference flags in binary manifests (`eos_bin_t`).
- Automatic routing of GUI applications (`EOS_BIN_FLAG_GUI`) to 16 KB external PSRAM stacks with **zero internal SRAM cost**.
- Intelligent single-stage command dispatch in `syscalls.c` that spawns isolated PSRAM threads for GUI binaries instead of executing on the caller's stack.
- Graceful SRAM fallback in `ewrap/pthread.c` if external PSRAM is exhausted or disabled.

---

## 2. Ebin Manifest Flags (`main/ecore/bin.h`)

Applications in `main/ebin/` declare metadata and execution properties using the `eos_bin_t` manifest structure:

```c
typedef struct {
  uint32_t magic;         // EOS_BIN_MAGIC (0x0C0A00AC)
  uint32_t flags;         // Manifest behavior flags
  uint32_t stack_size;    // Desired thread stack size in bytes (0 = default)
  char filename[EOS_SMALL_STR_LEN];
  char name[EOS_SMALL_STR_LEN];
  char group[EOS_XSMALL_STR_LEN];
  char description[EOS_MID_STR_LEN];
  int (*entry_point)(int argc, char **argv);
} eos_bin_t;
```

### Supported Flags

| Flag | Value | Purpose | Stack Placement |
| :--- | :--- | :--- | :--- |
| `EOS_BIN_FLAG_NONE` | `0U` | Default command behavior | Internal SRAM (8 KB) |
| `EOS_BIN_FLAG_HIDDEN` | `1U << 0` | Hides binary from GUI launcher / desktop menus | Manifest default |
| `EOS_BIN_FLAG_GUI` | `1U << 1` | Identifies application as interactive graphical UI | **External PSRAM (16 KB)** |
| `EOS_BIN_FLAG_STACK_SRAM` | `1U << 2` | Explicitly forces internal SRAM stack allocation | **Internal SRAM** (override) |
| `EOS_BIN_FLAG_STACK_PSRAM` | `1U << 3` | Explicitly requests external PSRAM stack allocation | **External PSRAM** |

### Stack Size Constants

```c
#define EOS_BIN_DEFAULT_STACK_SIZE 8192U   // Standard CLI commands
#define EOS_BIN_MIN_STACK_SIZE     4096U   // Lower clamp limit
#define EOS_BIN_GUI_STACK_SIZE     16384U  // GUI / LVGL applications
```

---

## 3. Decision Rules & Resolution Logic

### PSRAM Stack Preference (`eos_bin_prefers_psram_stack`)

Stack placement preference is evaluated dynamically via `eos_bin_prefers_psram_stack()`:

```c
static inline bool eos_bin_prefers_psram_stack(const eos_bin_t *bin) {
  if (!bin) return false;
  if (bin->flags & EOS_BIN_FLAG_STACK_SRAM) return false;
  return ((bin->flags & EOS_BIN_FLAG_STACK_PSRAM) || (bin->flags & EOS_BIN_FLAG_GUI));
}
```

#### Rule Precedence:
1. **Explicit SRAM Veto (`EOS_BIN_FLAG_STACK_SRAM`):** Takes absolute precedence. If set, the task will **never** use PSRAM for its stack, even if `EOS_BIN_FLAG_GUI` or `EOS_BIN_FLAG_STACK_PSRAM` is present. This is vital for time-critical, high-frequency interrupt, or DMA-adjacent worker threads.
2. **GUI Task Default (`EOS_BIN_FLAG_GUI`):** Automatically implies PSRAM stack preference unless overridden by `EOS_BIN_FLAG_STACK_SRAM`.
3. **Explicit PSRAM Request (`EOS_BIN_FLAG_STACK_PSRAM`):** Explicitly routes CLI or daemon tools with large memory or deep recursion needs to PSRAM.

### Effective Stack Size Resolution (`eos_bin_effective_stack_size`)

Stack dimensions are resolved dynamically via `eos_bin_effective_stack_size()`:

```c
static inline uint32_t eos_bin_effective_stack_size(const eos_bin_t *bin) {
  if (!bin) {
    return EOS_BIN_DEFAULT_STACK_SIZE;
  }
  if (bin->stack_size == 0) {
    return (bin->flags & EOS_BIN_FLAG_GUI) ? EOS_BIN_GUI_STACK_SIZE
                                           : EOS_BIN_DEFAULT_STACK_SIZE;
  }
  return (bin->stack_size < EOS_BIN_MIN_STACK_SIZE) ? EOS_BIN_MIN_STACK_SIZE
                                                    : bin->stack_size;
}
```

- When `stack_size == 0`:
  - GUI tasks automatically receive `EOS_BIN_GUI_STACK_SIZE` (**16 KB**).
  - CLI tasks receive `EOS_BIN_DEFAULT_STACK_SIZE` (**8 KB**).
- When a custom `stack_size > 0` is specified:
  - Clamped to a minimum of `EOS_BIN_MIN_STACK_SIZE` (**4 KB**) to prevent stack starvation.

---

## 4. ESP-IDF pthread Stack Configuration & Dynamic Fallback

In ESP-IDF, thread stacks for POSIX pthreads are configured via `esp_pthread_set_cfg()` before calling `pthread_create()`.

### Configuration Injection (`main/ewrap/pthread.c`)

EOS bridges binary manifest configuration directly into the pthread subsystem:

```c
void eos_pthread_set_manifest_cfg(const eos_bin_t *bin) {
  esp_pthread_cfg_t cfg = esp_pthread_get_default_configuration();
  cfg.stack_size = eos_bin_effective_stack_size(bin);
  cfg.stack_alloc_caps = eos_bin_prefers_psram_stack(bin)
                             ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                             : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  cfg.inherit_cfg = false;
  esp_pthread_set_cfg(&cfg);
}
```

### Graceful Internal SRAM Fallback

If external PSRAM is exhausted, uninitialized, or disabled on the target chip, `pthread_create()` fails if `MALLOC_CAP_SPIRAM` cannot be satisfied. EOS intercepts this in `__wrap_pthread_create`:

```c
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {
  int rc = __real_pthread_create(thread, attr, start_routine, arg);
  if (rc != 0) {
    esp_pthread_cfg_t cur_cfg;
    if (esp_pthread_get_cfg(&cur_cfg) == ESP_OK) {
      if (cur_cfg.stack_alloc_caps & MALLOC_CAP_SPIRAM) {
        EOS_LOGW("pthread", "PSRAM stack allocation failed (rc=%d), falling back to internal SRAM", rc);
        cur_cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        if (cur_cfg.stack_size > EOS_BIN_DEFAULT_STACK_SIZE) {
          cur_cfg.stack_size = EOS_BIN_DEFAULT_STACK_SIZE;
        }
        esp_pthread_set_cfg(&cur_cfg);
        rc = __real_pthread_create(thread, attr, start_routine, arg);
      }
    }
  }
  return rc;
}
```

---

## 5. Execution Routing & Stack Isolation in `syscalls.c`

When executing commands via `system(cmd)` or `ush`:
- Multi-stage commands (e.g. `cmd1 | cmd2` or `cmd1 && cmd2`) always spawn dedicated POSIX pthreads via `exec.c`.
- Historically, single-stage commands (`stage_count == 1`, e.g. `system("launcher lvgl_net")`) executed synchronously on the caller's stack to avoid thread creation overhead.
- However, when a GUI application like `launcher` or `lvgl_net` was executed from a shell or init script, its 16 KB stack requirements exceeded the caller's stack, triggering catastrophic stack overflows on RISC-V architectures.

### Resolution in `__wrap_system()`:
`__wrap_system()` inspects the target binary:
```c
const eos_bin_t *bin = eos_bin_find_by_filename(cmd_name);
if (bin && eos_bin_prefers_psram_stack(bin)) {
  // Spawn dedicated joinable worker thread on 16 KB PSRAM stack
  eos_pthread_set_manifest_cfg(bin);
  pthread_t tid;
  pthread_create(&tid, NULL, worker_trampoline, task_args);
  pthread_join(tid, &exit_code);
} else {
  // Execute safely on caller's stack
  bin->entry_point(argc, argv);
}
```

This guarantees stack isolation for all GUI and memory-intensive applications without compromising the lightweight execution of basic utilities.
