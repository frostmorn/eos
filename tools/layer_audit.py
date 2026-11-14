#!/usr/bin/env python3
"""
EOS Layer Separation Audit Tool
Automated static analysis tool to check adherence to EOS architectural rules.
Rules are declaratively defined in tools/layer_audit_rules.json (or specified via --rules).
"""

import argparse
import json
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


def is_file_scope_global_variable(line):
    stripped = line.strip()
    if not stripped or stripped.startswith(("//", "/*", "*", "#")):
        return False
    if "};" in stripped or stripped == "};" or stripped == "}":
        return False
    if stripped.startswith(".") or ". " in stripped or (" =" in stripped and stripped.startswith(".")):
        return False
    if any(kw in stripped for kw in ("typedef ", "struct ", "union ", "enum ", "const ", "extern ", "return ", "if ", "for ", "while ", "switch ", "case ")):
        return False
    if "(" in stripped and ")" in stripped:
        return False
    
    var_decl_pattern = re.compile(r"^(static\s+)?([a-zA-Z_][a-zA-Z0-9_*]+\s+)+[a-zA-Z_][a-zA-Z0-9_*]*(\s*\[.*\])?\s*(=.*;|;)$")
    return bool(var_decl_pattern.match(stripped))


def run_rule_forbidden_patterns(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c", ".h"]))
    patterns = rule.get("patterns", [])
    compiled = [re.compile(p) for p in patterns]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(file_exts):
                    continue
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                    for idx, line in enumerate(lines, 1):
                        stripped = line.strip()
                        if not stripped or stripped.startswith(("//", "/*", "*")):
                            continue
                        code_part = line.split("//")[0]
                        for pat in compiled:
                            if pat.search(code_part):
                                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                                print(f"    Match: {stripped}")
                                violations += 1
                except Exception as e:
                    print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_no_calls_between_patterns(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c", ".h"]))
    lock_patterns = [re.compile(p) for p in rule.get("lock_patterns", [])]
    unlock_patterns = [re.compile(p) for p in rule.get("unlock_patterns", [])]
    forbidden_calls = [re.compile(p) for p in rule.get("forbidden_calls", [])]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(file_exts):
                    continue
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                    lock_stack = []
                    for idx, line in enumerate(lines, 1):
                        stripped = line.strip()
                        if not stripped or stripped.startswith(("//", "/*", "*")):
                            continue
                        code_part = line.split("//")[0]
                        for lpat in lock_patterns:
                            if lpat.search(code_part):
                                lock_stack.append(idx)
                        if lock_stack:
                            for fpat in forbidden_calls:
                                if fpat.search(code_part):
                                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                                    print(f"    Forbidden I/O call while lock held (acquired at line {lock_stack[-1]}): {stripped}")
                                    violations += 1
                        for upat in unlock_patterns:
                            if upat.search(code_part):
                                if lock_stack:
                                    lock_stack.pop()
                except Exception as e:
                    print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_no_globals(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")

    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
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
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_include_guards(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")

    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
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
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_allowed_directory(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    base_dir = rule.get("base_dir", ".")
    file_exts = tuple(rule.get("file_extensions", []))
    allowed_prefix = os.path.abspath(rule.get("allowed_prefix", ""))
    exclude_dirs = rule.get("exclude_dirs", [".git", "build"])

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for root, dirs, files in os.walk(base_dir):
        # Exclude directories
        dirs[:] = [d for d in dirs if d not in exclude_dirs]
        for file in files:
            if file.endswith(file_exts):
                filepath = os.path.abspath(os.path.join(root, file))
                if not filepath.startswith(allowed_prefix):
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] File found outside {rule.get('allowed_prefix')}: {filepath}")
                    violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} misplaced file(s).")
    return violations


def run_rule_wrapper_definitions(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    base_dir = rule.get("base_dir", "main")
    allowed_prefix = os.path.abspath(rule.get("allowed_prefix", "main/ewrap"))
    exclude_dirs = rule.get("exclude_dirs", [".git", "build"])
    wrap_def_pat = re.compile(
        r"^\s*(int|void|char\*|ssize_t|off_t|DIR\*|pthread_t|BaseType_t|TaskHandle_t)\s+__wrap_\w+\s*\(",
        re.MULTILINE
    )

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for root, dirs, files in os.walk(base_dir):
        dirs[:] = [d for d in dirs if d not in exclude_dirs]
        for file in files:
            if not file.endswith((".c", ".h")):
                continue
            filepath = os.path.abspath(os.path.join(root, file))
            try:
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                for _ in wrap_def_pat.finditer(content):
                    if not filepath.startswith(allowed_prefix):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] __wrap_ definition outside {rule.get('allowed_prefix')}: {filepath}")
                        violations += 1
            except Exception as e:
                print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All __wrap_* wrapper definitions are correctly located.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} misplaced __wrap_* definition(s).")
    return violations


def run_rule_driver_enable_guards(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/edriver"])
    pattern = re.compile(rule.get("pattern", r"#\s*if(def)?\s+EOS_DRV_\w+_ENABLED"))

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(".c"):
                    continue
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        content = f.read()
                    if not pattern.search(content):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Driver source missing EOS_DRV_*_ENABLED guard: {filepath}")
                        violations += 1
                except Exception as e:
                    print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All driver source files are correctly guarded.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} unguarded driver source file(s).")
    return violations


def run_rule_driver_structure(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/edriver"])
    drv_struct_pat = re.compile(rule.get("pattern", r"\b(eos_drv_t|eos_driver_t)\b\s+\w+\s*="))

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue

        # 1. Check for loose files directly under dir_path or dir_path/<scope>
        for item in sorted(os.listdir(dir_path)):
            item_path = os.path.join(dir_path, item)
            if os.path.isfile(item_path):
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Loose file outside driver folder: {item_path}")
                violations += 1
            elif os.path.isdir(item_path):
                for sub_item in sorted(os.listdir(item_path)):
                    sub_path = os.path.join(item_path, sub_item)
                    if os.path.isfile(sub_path):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Loose file outside driver folder: {sub_path}")
                        violations += 1

        # 2. Check each driver directory for eos_drv_t / eos_driver_t definition
        for scope in sorted(os.listdir(dir_path)):
            scope_path = os.path.join(dir_path, scope)
            if not os.path.isdir(scope_path):
                continue
            for drv_name in sorted(os.listdir(scope_path)):
                drv_path = os.path.join(scope_path, drv_name)
                if not os.path.isdir(drv_path):
                    continue

                has_drv_struct = False
                for root, _, files in os.walk(drv_path):
                    for file in files:
                        if file.endswith(".c"):
                            filepath = os.path.join(root, file)
                            try:
                                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                                    content = f.read()
                                if drv_struct_pat.search(content):
                                    has_drv_struct = True
                                    break
                            except Exception as e:
                                print(f"Error reading {filepath}: {e}")
                    if has_drv_struct:
                        break

                if not has_drv_struct:
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Driver directory missing eos_drv_t structure: {drv_path}")
                    violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All driver folders contain an eos_drv_t structure and no loose files exist.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} driver structure violation(s).")
    return violations


DISPATCHER = {
    "forbidden_patterns": run_rule_forbidden_patterns,
    "no_calls_between_patterns": run_rule_no_calls_between_patterns,
    "no_globals": run_rule_no_globals,
    "mandatory_include_guards": run_rule_include_guards,
    "allowed_directory": run_rule_allowed_directory,
    "wrapper_definitions": run_rule_wrapper_definitions,
    "driver_enable_guards": run_rule_driver_enable_guards,
    "driver_structure": run_rule_driver_structure,
}


def load_rules(rules_file):
    if not os.path.exists(rules_file):
        print(f"{Colors.RED}Rules definition file not found: {rules_file}{Colors.RESET}")
        sys.exit(1)
    with open(rules_file, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("rules", [])


def main():
    parser = argparse.ArgumentParser(description="EOS Declarative Layer Separation Audit Tool")
    parser.add_argument(
        "-r", "--rules",
        default="tools/layer_audit_rules.json",
        help="Path to declarative JSON rules definition (default: tools/layer_audit_rules.json)"
    )
    args = parser.parse_args()

    print(f"{Colors.CYAN}{Colors.BOLD}Running Declarative EOS Layer Separation Audit ({args.rules})...{Colors.RESET}")
    rules = load_rules(args.rules)

    total_violations = 0
    executed_rules = 0

    for rule in rules:
        rule_type = rule.get("type")
        handler = DISPATCHER.get(rule_type)
        if not handler:
            print(f"{Colors.YELLOW}[WARN] Unknown rule type '{rule_type}' for rule '{rule.get('id')}'; skipping.{Colors.RESET}")
            continue
        total_violations += handler(rule)
        executed_rules += 1

    print(f"\n{Colors.BOLD}========================================{Colors.RESET}")
    print(f"Executed {executed_rules} architectural rules.")
    if total_violations == 0:
        print(f"{Colors.GREEN}{Colors.BOLD}>>> ALL LAYER AUDIT CHECKS PASSED! <<<{Colors.RESET}\n")
        sys.exit(0)
    else:
        print(f"{Colors.RED}{Colors.BOLD}>>> AUDIT FAILED: {total_violations} architectural violation(s) found! <<<{Colors.RESET}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
