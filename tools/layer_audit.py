#!/usr/bin/env python3
"""
EOS Layer Separation Audit Tool
Automated static analysis tool to check adherence to EOS architectural rules:
1. No RTOS primitives (FreeRTOS tasks, delays, semaphores) in user-space (ebin).
2. No global/static state variables in user-space (ebin) or drivers (edriver).
3. No direct ESP-IDF vendor hardware/Wi-Fi/Bus APIs (SPI, I2C, GPIO, UART) in core kernel (ecore) or user-space (ebin).
4. No raw printf/puts in drivers (edriver) - must use EOS_LOG macros.
5. Mandatory #pragma once in all header files (.h) across main/.
6. Linker scripts (.ld, .lf) must reside exclusively within main/eld/.
"""

import os
import re
import sys

class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

RTOS_IN_EBIN_PATTERNS = [
    r"\bvTaskDelay\b",
    r"\bxTaskCreate\b",
    r"\bSemaphoreHandle_t\b",
    r"\bQueueHandle_t\b",
    r"\bTaskHandle_t\b",
    r"\bpdMS_TO_TICKS\b",
    r"\bvSemaphoreCreateBinary\b",
    r"\bxSemaphoreTake\b",
    r"\bxSemaphoreGive\b",
]

ESP_IDF_HARDWARE_PATTERNS = [
    r"\besp_wifi_\w+\b",
    r"\bgpio_set_level\b",
    r"\bgpio_reset_pin\b",
    r"\bgpio_config\b",
    r"\bgpio_set_direction\b",
    r"\bspi_bus_initialize\b",
    r"\bspi_bus_add_device\b",
    r"\bspi_device_transmit\b",
    r"\bi2c_param_config\b",
    r"\bi2c_driver_install\b",
    r"\buart_driver_install\b",
]

def is_file_scope_global_variable(line):
    stripped = line.strip()
    if not stripped or stripped.startswith(("//", "/*", "*", "#")):
        return False
    if "};" in stripped or stripped == "};" or stripped == "}":
        return False
    if stripped.startswith(".") or ". " in stripped or " =" in stripped and stripped.startswith("."):
        return False
    if any(kw in stripped for kw in ("typedef ", "struct ", "union ", "enum ", "const ", "extern ", "return ", "if ", "for ", "while ", "switch ", "case ")):
        return False
    if "(" in stripped and ")" in stripped:
        return False
    
    var_decl_pattern = re.compile(r"^(static\s+)?([a-zA-Z_][a-zA-Z0-9_*]+\s+)+[a-zA-Z_][a-zA-Z0-9_*]*(\s*\[.*\])?\s*(=.*;|;)$")
    return bool(var_decl_pattern.match(stripped))

def scan_globals(dir_path):
    violations = 0
    desc = f"Unauthorized global/static variables in {dir_path}"
    print(f"\n{Colors.BOLD}=== Scanning {dir_path} for {desc} ==={Colors.RESET}")
    
    if not os.path.exists(dir_path):
        print(f"Directory {dir_path} not found.")
        return 0

    for root, _, files in os.walk(dir_path):
        for file in files:
            if not file.endswith((".c", ".h")):
                continue
            filepath = os.path.join(root, file)
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    lines = f.readlines()
                
                brace_depth = 0
                for idx, line in enumerate(lines, 1):
                    stripped = line.strip()
                    if not stripped or stripped.startswith(("//", "/*", "*", "#")):
                        continue
                    brace_depth += stripped.count("{") - stripped.count("}")
                    if brace_depth == 0 and is_file_scope_global_variable(line):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                        print(f"    Match: {stripped}")
                        violations += 1
            except Exception as e:
                print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {desc}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {desc}.")
    return violations

def scan_directory(dir_path, patterns, desc):
    violations = 0
    print(f"\n{Colors.BOLD}=== Scanning {dir_path} for {desc} ==={Colors.RESET}")
    
    if not os.path.exists(dir_path):
        print(f"Directory {dir_path} not found.")
        return 0

    compiled_patterns = [re.compile(p) for p in patterns]

    for root, _, files in os.walk(dir_path):
        for file in files:
            if not file.endswith((".c", ".h")):
                continue
            filepath = os.path.join(root, file)
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    lines = f.readlines()
                for idx, line in enumerate(lines, 1):
                    for pat in compiled_patterns:
                        if pat.search(line):
                            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                            print(f"    Match: {line.strip()}")
                            violations += 1
            except Exception as e:
                print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {desc}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {desc}.")
    return violations

def scan_printf_in_drivers(dir_path):
    violations = 0
    desc = "Raw printf/puts in drivers (edriver)"
    print(f"\n{Colors.BOLD}=== Scanning {dir_path} for {desc} ==={Colors.RESET}")
    
    if not os.path.exists(dir_path):
        print(f"Directory {dir_path} not found.")
        return 0

    printf_pat = re.compile(r"\b(printf|puts)\s*\(")

    for root, _, files in os.walk(dir_path):
        for file in files:
            if not file.endswith((".c", ".h")):
                continue
            filepath = os.path.join(root, file)
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    lines = f.readlines()
                for idx, line in enumerate(lines, 1):
                    stripped = line.strip()
                    if not stripped or stripped.startswith(("//", "/*", "*", "#")):
                        continue
                    if printf_pat.search(line):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                        print(f"    Match: {stripped}")
                        violations += 1
            except Exception as e:
                print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {desc}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {desc}.")
    return violations

def scan_pragma_once(dir_path):
    violations = 0
    desc = "Missing #pragma once in header files"
    print(f"\n{Colors.BOLD}=== Scanning {dir_path} for {desc} ==={Colors.RESET}")
    
    if not os.path.exists(dir_path):
        print(f"Directory {dir_path} not found.")
        return 0

    for root, _, files in os.walk(dir_path):
        for file in files:
            if not file.endswith(".h"):
                continue
            filepath = os.path.join(root, file)
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                if "#pragma once" not in content and "#ifndef" not in content:
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}: missing #pragma once / include guard")
                    violations += 1
            except Exception as e:
                print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {desc}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {desc}.")
    return violations

def scan_linker_scripts(base_dir):
    violations = 0
    desc = "Linker scripts (.ld, .lf) outside main/eld"
    print(f"\n{Colors.BOLD}=== Scanning for {desc} ==={Colors.RESET}")
    
    allowed_eld_dir = os.path.abspath(os.path.join("main", "eld"))

    for root, _, files in os.walk(base_dir):
        # Ignore .git and build directories
        if ".git" in root or "build" in root or "components" in root:
            continue
        for file in files:
            if file.endswith((".ld", ".lf")):
                filepath = os.path.abspath(os.path.join(root, file))
                if not filepath.startswith(allowed_eld_dir):
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Linker script found outside main/eld: {filepath}")
                    violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All linker scripts are correctly located in main/eld.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} misplaced linker script(s).")
    return violations

def main():
    print(f"{Colors.CYAN}{Colors.BOLD}Running Fully Comprehensive EOS Layer Separation Audit...{Colors.RESET}")
    
    total_violations = 0
    total_violations += scan_directory("main/ebin", RTOS_IN_EBIN_PATTERNS, "RTOS primitives in user-space (ebin)")
    total_violations += scan_globals("main/ebin")
    total_violations += scan_globals("main/edriver")
    total_violations += scan_printf_in_drivers("main/edriver")
    total_violations += scan_pragma_once("main")
    total_violations += scan_linker_scripts(".")
    total_violations += scan_directory("main/ecore", ESP_IDF_HARDWARE_PATTERNS, "ESP-IDF vendor hardware/bus APIs in core kernel (ecore)")
    total_violations += scan_directory("main/ebin", ESP_IDF_HARDWARE_PATTERNS, "ESP-IDF vendor hardware/bus APIs in user-space (ebin)")

    print(f"\n{Colors.BOLD}========================================{Colors.RESET}")
    if total_violations == 0:
        print(f"{Colors.GREEN}{Colors.BOLD}>>> ALL LAYER AUDIT CHECKS PASSED! <<<{Colors.RESET}\n")
        sys.exit(0)
    else:
        print(f"{Colors.RED}{Colors.BOLD}>>> AUDIT FAILED: {total_violations} architectural violation(s) found! <<<{Colors.RESET}\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
