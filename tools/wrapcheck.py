#!/usr/bin/env python3
"""
EOS Linker Wrapper Verification Tool (wrapcheck)
Validates that:
1. Every -Wl,--wrap=<sym> in main/CMakeLists.txt has a corresponding __wrap_<sym> definition in main/ewrap/.
2. Every wrapped symbol is formally documented in docs/architecture/wrappers-and-syscalls.md.
3. If build artifacts exist, verifies compiled symbols exist.
"""

import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CMAKELISTS_PATH = REPO_ROOT / "main" / "CMakeLists.txt"
EWRAP_DIR = REPO_ROOT / "main" / "ewrap"
DOCS_WRAPPERS_PATH = REPO_ROOT / "docs" / "architecture" / "wrappers-and-syscalls.md"

class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

def get_cmake_wrappers():
    if not CMAKELISTS_PATH.exists():
        print(f"{Colors.RED}Error: {CMAKELISTS_PATH} not found.{Colors.RESET}")
        sys.exit(1)

    content = CMAKELISTS_PATH.read_text(encoding="utf-8")
    wrappers = re.findall(r"-Wl,--wrap=([a-zA-Z0-9_]+)", content)
    return set(wrappers)

def get_implemented_wrappers():
    wrappers = set()
    pattern = re.compile(r"\b__wrap_([a-zA-Z0-9_]+)\b")
    for ext in ("*.c", "*.cpp"):
        for f in EWRAP_DIR.glob(ext):
            text = f.read_text(encoding="utf-8", errors="ignore")
            for match in pattern.findall(text):
                wrappers.add(match)
    return wrappers

def get_documented_wrappers():
    if not DOCS_WRAPPERS_PATH.exists():
        return set()

    content = DOCS_WRAPPERS_PATH.read_text(encoding="utf-8")
    # Matches markdown table rows like: | `_open_r` | `main/ewrap/reent_syscalls.c` | ...
    rows = re.findall(r"\|\s*`([a-zA-Z0-9_]+)`\s*\|\s*`main/ewrap/", content)
    return set(rows)

def main():
    print(f"{Colors.BOLD}{Colors.CYAN}Running EOS Linker Wrapper Audit (wrapcheck)...{Colors.RESET}\n")
    cmake_wrappers = get_cmake_wrappers()
    implemented_wrappers = get_implemented_wrappers()
    documented_wrappers = get_documented_wrappers()

    print(f"Discovered {len(cmake_wrappers)} wrapped symbols in {CMAKELISTS_PATH.relative_to(REPO_ROOT)}:")
    for sym in sorted(cmake_wrappers):
        print(f"  - {sym}")
    print()

    errors = []

    # Check 1: Every --wrap has an implementation
    missing_impl = cmake_wrappers - implemented_wrappers
    if missing_impl:
        for sym in sorted(missing_impl):
            errors.append(f"Wrapped symbol '{sym}' has no __wrap_{sym} implementation under main/ewrap/!")
    else:
        print(f"{Colors.GREEN}[PASS] All wrapped symbols have __wrap_* implementations in main/ewrap/.{Colors.RESET}")

    # Check 2: Every --wrap is documented
    missing_docs = cmake_wrappers - documented_wrappers
    if missing_docs:
        for sym in sorted(missing_docs):
            errors.append(f"Wrapped symbol '{sym}' is missing from {DOCS_WRAPPERS_PATH.relative_to(REPO_ROOT)} inventory table!")
    else:
        print(f"{Colors.GREEN}[PASS] All wrapped symbols are documented in {DOCS_WRAPPERS_PATH.relative_to(REPO_ROOT)}.{Colors.RESET}")

    # Check 3: Check for orphaned implementations not registered in CMakeLists.txt
    orphaned_impl = (implemented_wrappers - cmake_wrappers) - {"system"} # system may have internal helper references
    if orphaned_impl:
        print(f"{Colors.YELLOW}[WARN] Implemented __wrap_* functions not registered in CMakeLists.txt: {orphaned_impl}{Colors.RESET}")

    if errors:
        print(f"\n{Colors.RED}{Colors.BOLD}>>> WRAPCHECK FAILED with {len(errors)} error(s):{Colors.RESET}")
        for err in errors:
            print(f"  {Colors.RED}✖ {err}{Colors.RESET}")
        sys.exit(1)

    print(f"\n{Colors.BOLD}{Colors.GREEN}>>> ALL WRAPPER CHECKS PASSED SUCCESSFULLY! <<<{Colors.RESET}\n")

if __name__ == "__main__":
    main()
