# BinFS, Native Binaries & Execution Architecture

This document details the in-memory execution environment, binary registration mechanism, filesystem visibility under `binfs`, and process spawning architecture in **EOS (ESP Operating System)**.

---

## 1. Statically Linked Native Binaries (`.eos_bins`)

Because microcontrollers lack MMU-based virtual memory paging and dynamic ELF loaders, all native user-space applications in EOS reside in `main/ebin/` and are registered statically at compile time.

### Manifest Structure (`eos_bin_t`, `main/ecore/bin.h`)
Every binary declares an `eos_bin_t` manifest annotated with the `EOS_BIN_ATTR` macro:

```c
typedef struct {
  uint32_t magic;         // EOS_BIN_MAGIC (0x0C0A00AC)
  uint32_t flags;         // Behavior flags (e.g. EOS_BIN_FLAG_GUI, EOS_BIN_FLAG_STACK_PSRAM)
  uint32_t stack_size;    // Desired thread stack size in bytes (0 = default 8192)
  char filename[EOS_SMALL_STR_LEN];     // Canonical command name (e.g. "ls", "netctl")
  char name[EOS_SMALL_STR_LEN];         // Human-readable title (e.g. "List Files")
  char group[EOS_XSMALL_STR_LEN];       // Category (Apps, Games, Network, System, Tools, Utils, Tests, Diag)
  char description[EOS_MID_STR_LEN];    // Brief functional summary
  int (*entry_point)(int argc, char **argv); // Standard C entry point
} eos_bin_t;
```

### Manifest Initialization Macro
Binaries initialize this structure using `EOS_BIN_INITIALIZER`:
```c
EOS_BIN_ATTR const eos_bin_t mycmd_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "mycmd",
    .name = "My Command",
    .group = "Tools",
    .flags = 0,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .description = "Demonstration tool",
    .entry_point = mycmd_main,
};
```

### Linker Placement (`main/eld/bin.lf`)
The linker placement file collects all manifest instances from `.eos_bins` across compiled translation units into contiguous read-only memory bounded by linker symbols:
* `_eos_bins_start`
* `_eos_bins_end`

Kernel code iterates between these boundaries to enumerate available binaries dynamically without needing a central header file or registry array.

---

## 2. Embedded Script Assets (`asset.h`, `init.sh`)

In addition to compiled ELF functions, `binfs` integrates embedded text assets into `/bin/`:

1. **Asset Embedding via CMake:** Scripts such as `init.sh` are linked into the firmware binary using CMake's binary data target:
   ```cmake
   target_add_binary_data(eos.elf "init.sh" TEXT)
   ```
2. **Asset Manifest (`asset.h`):** The embedded script is represented as an `eos_asset_t` referencing linker symbols:
   ```c
   extern const uint8_t _binary_init_sh_start[];
   extern const uint8_t _binary_init_sh_end[];
   ```
3. **Filesystem Representation:** `binfs` surfaces these assets directly under `/bin/init.sh`, enabling both file reading (`cat /bin/init.sh`) and sequential script interpretation (`ush /bin/init.sh`).

---

## 3. BinFS Virtual Filesystem Integration (`binfs.c`)

`binfs` mounts at `/bin` via ESP-IDF's VFS layer:

* **Directory Enumeration (`readdir`):** When enumerating `/bin`, `binfs_readdir` steps through `.eos_bins` manifests first, followed by registered static assets, exposing every tool as an executable directory node.
* **Manifest Access (`read`):** Opening a native binary file (`open("/bin/ls", O_RDONLY)`) and issuing `read()` returns the raw `eos_bin_t` structure. This enables tools like `launcher` to query application titles, categories, and icon metadata directly from the filesystem.
* **Asset Access (`read`):** Opening a script asset (`open("/bin/init.sh", O_RDONLY)`) yields its plaintext lines.

---

## 4. Process Spawning & Execution Pipeline

```mermaid
flowchart TD
    Shell["MicroShell (ush) / system()"] --> Resolve["Resolve /bin/<name> via binfs"]
    Resolve --> CheckType{"Binary or Script?"}
    
    CheckType -->|Script (*.sh)| ExecScript["Read lines sequentially -> Feed back to ush"]
    CheckType -->|Native Binary| Manifest["Evaluate eos_bin_t Manifest"]
    
    Manifest --> StackCheck{"eos_bin_prefers_psram_stack()?"}
    StackCheck -->|True| AllocPSRAM["Alloc 16 KB Stack in External PSRAM"]
    StackCheck -->|False| AllocSRAM["Alloc 8 KB Stack in Internal SRAM"]
    
    AllocPSRAM --> Pthread["Spawn POSIX pthread (exec.c)"]
    AllocSRAM --> Pthread
    
    Pthread --> SetupTLS["Initialize Thread Context (eos_tctx_t)"]
    SetupTLS --> Run["Invoke entry_point(argc, argv)"]
    Run --> Cleanup["Automatic Resource Cleanup (FDs, Dirs, Memory)"]
    Cleanup --> Join["Return Exit Status Code"]
```

### Execution Flow:
1. **Command Resolution:** When `ush` or `system()` receives a command name (e.g. `cat file.txt`), it checks `/bin/` or the current path for matching manifests or script assets.
2. **Thread Context Isolation (`ecore/exec.c`):** EOS executes commands in isolated POSIX pthreads. Before launching the thread:
   * Parent thread's current working directory (`cwd`) is inherited.
   * Argument array (`argv`) is copied into transient heap storage.
   * Thread stack memory attributes are configured based on manifest flags.
3. **Thread Local Storage (`ecore/threadctx.c`):** The new thread receives an `eos_tctx_t` stored in FreeRTOS TLS. This tracks open file descriptors, opened directories, and heap allocations.
4. **Automatic Resource Cleanup:** When the thread exits, the kernel automatically closes leaked file descriptors, closes leaked directories, and frees leaked heap memory blocks, guaranteeing system stability even when user commands crash or fail to clean up.
5. **Synchronous Waiting:** Shell processes call `eos_wait()` to join the worker thread, release its thread descriptor, and obtain its return code (`0` for success, non-zero for error).

---

## 5. MicroShell Pipeline & Job Architecture (`ush.c`)

The default interactive shell interpreter (`main/ebin/ush/ush.c`) provides advanced POSIX shell mechanics:

* **I/O Redirections:**
  * Standard output redirect (`>` and `>>`)
  * Standard input redirect (`<`)
  * Standard error redirect (`2>` and `2>&1`)
  * Combined stream redirect (`&>`)
* **Pipelines (`|`):**
  Spawns producer and consumer threads connected via kernel pipes (`pipefs`), streaming stdout of command $N$ into stdin of command $N+1$.
* **Chaining Operators:**
  * Conditional AND (`&&`): Executes right command only if left succeeds (`exit == 0`).
  * Conditional OR (`||`): Executes right command only if left fails (`exit != 0`).
  * Sequential separator (`;`): Executes commands sequentially.
* **Asynchronous Jobs (`&`):**
  Launches thread in detached mode without blocking the interactive prompt.
* **Pratt Expression Parser:**
  The shell syntax engine utilizes a Pratt precedence parser. Token vectors and AST nodes allocate dynamically from external PSRAM via `kvec_psram_t`, saving over **56 KB of internal SRAM**.

---

## 6. Scaffolding New Binaries (`make bin`)

New binary applications can be scaffolded interactively:

```bash
make bin
```
This invokes `tools/codegen.py`, creating a standard template under `main/ebin/<name>/`:
- `<name>.c`: Manifest declaration, argument parser, and entry point.
- Automatic registration in the build system.
