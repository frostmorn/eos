# BinFS, Native Binaries & Execution Architecture

EOS features an in-memory execution environment (`binfs` and `ecore/exec.c`) for running statically linked native applications, system commands, and embedded shell scripts under `/bin/`.

---

## 1. Statically Linked Binaries (`bin.h`, `.eos_bins`)

Applications residing in `main/ebin/` are compiled and registered at compile time into a dedicated ELF linker section (`.eos_bins`) using manifest structures (`eos_bin_t`):
*(Tip: New binary applications can be quickly scaffolded using `make bin`, which invokes `tools/codegen.py` to generate templates under `main/ebin/`).*

```c
typedef struct {
  uint32_t magic;         // EOS_BIN_MAGIC (0x0C0A00AC)
  uint32_t flags;
  char filename[EOS_SMALL_STR_LEN];
  char name[EOS_SMALL_STR_LEN];
  char group[EOS_XSMALL_STR_LEN];
  char description[EOS_MID_STR_LEN];
  int (*entry_point)(int argc, char **argv);
} eos_bin_t;
```

*   **Linker Fragments (`eld/bin.lf`):** Instructs the linker to collect all `.eos_bins` sections across object files, placing them into read-only flash/PSRAM memory bracketed by `_eos_bins_start` and `_eos_bins_end`.

---

## 2. Embedded Script Assets (`asset.h`, `init.sh`)

In addition to compiled ELF binaries, `binfs` integrates embedded text assets into `/bin/`:
*   **Asset Embedding:** Scripts like `init.sh` are linked into the firmware binary via CMake:
    ```cmake
    target_add_binary_data(eos.elf "init.sh" TEXT)
    ```
*   **Asset Manifest (`asset.h`):** The embedded data is referenced via linker symbols `_binary_init_sh_start` and `_binary_init_sh_end` encapsulated in an `eos_asset_t` structure.
*   **Filesystem Visibility:** `binfs` surfaces these assets directly under `/bin/` (e.g. `/bin/init.sh`), allowing standard file read and shell script execution.

---

## 3. BinFS VFS Integration (`binfs.c`, `binfs.h`)

`binfs` mounts at `/bin` via ESP-IDF's VFS layer, presenting registered binaries and assets as executable files:
*   **Directory Enumeration:** `binfs_readdir` iterates through both `.eos_bins` manifests and registered assets, exposing each entry under `/bin/`.
*   **Read-Only Manifest Access:** Opening a native binary file (`binfs_open`, `binfs_read`) provides access to the binary's `eos_bin_t` manifest structure; opening an asset provides direct access to its text payload.

---

## 4. Process Execution & Shell (`exec.c`, `ush.c`, `syscalls.c`)

*   **POSIX Execution Wrappers (`exec.c`):** Implements POSIX process spawning functions (`execve`, `execv`, `execl`, etc.) by creating joinable pthreads that execute the binary's entry point function.
*   **Synchronous Wait (`eos_wait`):** Tracks child thread execution handles, allowing shell processes to wait for child completion and retrieve exit status codes.
*   **MicroShell (`ush.c`):** The default EOS command-line interpreter (`ush`) reads user input over serial (USB Serial/JTAG), parses command arguments, and executes binaries via `execv()` or `system()`. When executing shell scripts, it parses and executes commands sequentially line-by-line.
*   **System Call Interception (`syscalls.c`):** `__wrap_system()` inspects command targets for scripts or native binaries and handles dispatching them in-process or via thread spawning. At boot, `main/eos.c` calls `system("init.sh")` to process declarative board startup.

---
[Back to Documentation Index](./Readme.md) | [See Board Porting Scope](./board-porting-scope.md) | [See Kernel Core Scope](./kernel-core-scope.md)
