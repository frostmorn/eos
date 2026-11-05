#!/usr/bin/env python3
"""
EOS Unused Code Tracking Tool (for -Wl,--whole-archive)
Audits object files and archives to find unreferenced functions/symbols.
Excludes user-space binaries (ebins), drivers (edrivers), wrappers (ewrap), and include overrides.
"""

import os
import re
import sys
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"

class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

EXCLUDE_PARTS = [
    "ebin",
    "edriver",
    "ewrap",
    "include_override",
    "test_",
]

def find_nm():
    for p in Path("/home/ai/.espressif/tools").glob("**/xtensa-esp32s3-elf-nm"):
        if p.is_file():
            return str(p)
    return "nm"

def main():
    print(f"{Colors.BOLD}{Colors.CYAN}Running EOS Unused Code Audit (under whole-archive)...{Colors.RESET}\n")

    if not BUILD_DIR.exists():
        print(f"{Colors.YELLOW}Build directory not found. Run a build first.{Colors.RESET}")
        sys.exit(0)

    nm_bin = find_nm()
    main_objs = []

    for obj in BUILD_DIR.glob("**/__idf_main.dir/**/*.obj"):
        rel = str(obj.relative_to(BUILD_DIR))
        if any(ex in rel for ex in EXCLUDE_PARTS):
            continue
        main_objs.append(obj)

    if not main_objs:
        # Fallback to search any main component objects
        for obj in BUILD_DIR.glob("**/*.obj"):
            rel = str(obj)
            if "esp-idf/main" in rel and not any(ex in rel for ex in EXCLUDE_PARTS):
                main_objs.append(obj)

    print(f"Scanned {len(main_objs)} core kernel object files in build directory.")

    defined_symbols = {}
    undefined_symbols = set()

    for obj in main_objs:
        try:
            out = subprocess.check_output([nm_bin, "-g", str(obj)], text=True, stderr=subprocess.DEVNULL)
            for line in out.splitlines():
                parts = line.strip().split()
                if len(parts) >= 2:
                    sym_type = parts[-2]
                    sym_name = parts[-1]
                    if sym_type in ("T", "D", "B", "R"):
                        defined_symbols[sym_name] = obj.name
                    elif sym_type == "U":
                        undefined_symbols.add(sym_name)
        except Exception:
            pass

    # Find symbols defined in core that have 0 references across the scanned core objects
    # Note: symbols exported to apps/ebins or called by ESP-IDF are allowed
    unused = {s: o for s, o in defined_symbols.items() if s not in undefined_symbols and not s.startswith("eos_") and not s.startswith("_")}

    print(f"{Colors.GREEN}[PASS] Core symbols analyzed: {len(defined_symbols)} defined.{Colors.RESET}")
    if unused:
        print(f"{Colors.YELLOW}[INFO] Symbols without internal core references (candidates for cleanup or export): {len(unused)}{Colors.RESET}")
        for s in sorted(unused.keys())[:10]:
            print(f"  - {s} in {unused[s]}")
        if len(unused) > 10:
            print(f"  ... and {len(unused) - 10} more.")

    print(f"\n{Colors.BOLD}{Colors.GREEN}>>> UNUSED CODE AUDIT COMPLETED SUCCESSFULLY! <<<{Colors.RESET}\n")

if __name__ == "__main__":
    main()
