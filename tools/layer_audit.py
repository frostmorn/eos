#!/usr/bin/env python3
"""
EOS Layer Separation Audit Tool
Automated static analysis tool to check adherence to EOS architectural rules:
1. No RTOS primitives (FreeRTOS tasks, delays, semaphores) in user-space (ebin).
2. No global/static state variables in user-space (ebin) or drivers (edriver).
3. No direct ESP-IDF vendor hardware/Wi-Fi/Bus APIs (SPI, I2C, GPIO, UART) in core kernel (ecore) or user-space (ebin).
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
    # Ignore closing braces, struct initializers, typedefs, const, extern, functions
    if "};" in stripped or stripped == "};" or stripped == "}":
        return False
    if stripped.startswith(".") or ". " in stripped or " =" in stripped and stripped.startswith("."):
        return False
    if any(kw in stripped for kw in ("typedef ", "struct ", "union ", "enum ", "const ", "extern ", "return ", "if ", "for ", "while ", "switch ", "case ")):
        return False
    if "(" in stripped and ")" in stripped: # function definition or macro call
        return False
    
    # Check if it looks like a variable declaration: type name [= value];
    # e.g., static net_app_t *s_app = NULL; or static SemaphoreHandle_t mutex;
    var_decl_pattern = re.compile(r"^(static\s+)?([a-zA-Z_][a-zA-Z0-9_*]+\s+)+[a-zA-Z_][a-zA-Z0-9_*]*(\s*\[.*\])?\s*(=.*;|;)$")
    if var_decl_pattern.match(stripped):
        return True
    
    return False

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
                    
                    # File-scope is when brace_depth == 0
                    if brace_depth == 0:
                        if is_file_scope_global_variable(line):
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

def main():
    print(f"{Colors.CYAN}{Colors.BOLD}Running Refined Strict EOS Layer Separation & Global State Audit...{Colors.RESET}")
    
    total_violations = 0
    total_violations += scan_directory("main/ebin", RTOS_IN_EBIN_PATTERNS, "RTOS primitives in user-space (ebin)")
    total_violations += scan_globals("main/ebin")
    total_violations += scan_globals("main/edriver")
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
