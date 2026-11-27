#!/usr/bin/env python3
"""
EOS Layer Separation & Complexity Audit Tool
Automated static analysis tool to check adherence to EOS architectural,
complexity, code-duplication, and named-constant rules.
Rules are declaratively defined in tools/layer_audit_rules.json (or specified via --rules).
"""

import argparse
import collections
import glob
import json
import os
import re
import subprocess
import sys


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


COMMENT_AND_STRING_RE = re.compile(
    r'//.*?$|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL | re.MULTILINE,
)

BRANCH_TOKEN_RE = re.compile(
    r"\b(?:if|else\s+if|for|while|case|catch)\b|\&\&|\|\||\?"
)

FUNC_START_RE = re.compile(
    r"^(?:static\s+|inline\s+|const\s+|unsigned\s+|signed\s+|struct\s+|enum\s+|__attribute__\s*\(\([^)]*\)\)\s*)*"
    r"[a-zA-Z_][\w\s\*]*?\b([a-zA-Z_]\w*)\s*\([^{;]*\)\s*\{?\s*$"
)

DEFINE_MACRO_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\b(.*)$")

ARRAY_DECL_MAGIC_RE = re.compile(
    r"\b(?:char|uint8_t|int8_t|uint16_t|int16_t|uint32_t|int32_t|int|unsigned|size_t)\s+"
    r"[a-zA-Z_]\w*\s*\[\s*(\d+|0x[0-9a-fA-F]+)\s*\]"
)

NUMERIC_LITERAL_RE = re.compile(
    r"(?<![.\w])(0x[0-9a-fA-F]+|0[0-7]{3,}|\d+)(?:[uUlL]*)\b(?![.\w])"
)

NON_FUNC_PREFIXES = (
    "if",
    "for",
    "while",
    "switch",
    "#",
    "//",
    "/*",
    "typedef",
    "return",
)

TRIVIAL_NORMALIZED_LINES = {
    "{",
    "}",
    "};",
    "break;",
    "continue;",
    "return;",
    "return 0;",
    "return -1;",
    "return 1;",
    "return true;",
    "return false;",
    "return NULL;",
    "else {",
    "} else {",
    "#endif",
    "#else",
}


def strip_comments_and_strings_keep_lines(content):
    """Replace comments and string literals with spaces while preserving line breaks and indentation."""
    def _repl(match):
        text = match.group(0)
        if text.startswith(('"', "'")):
            return '""'
        return "".join("\n" if ch == "\n" else " " for ch in text)

    return COMMENT_AND_STRING_RE.sub(_repl, content)


def parse_int_literal(tok):
    try:
        t = tok.rstrip("uUlL")
        if t.lower().startswith("0x"):
            return int(t, 16)
        if t.startswith("0") and len(t) > 1 and all(c in "01234567" for c in t):
            return int(t, 8)
        return int(t, 10)
    except ValueError:
        return None


def iter_target_files(paths, file_exts, exclude_dirs=None, exclude_files=None):
    exclude_dirs_set = set(exclude_dirs or [".git", "build", "components", "managed_components", ".board_cache"])
    exclude_files_set = set(exclude_files or [])
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
        if os.path.isfile(dir_path):
            if dir_path.endswith(file_exts) and dir_path not in exclude_files_set:
                yield dir_path
            continue
        for root, dirs, files in os.walk(dir_path):
            dirs[:] = sorted(d for d in dirs if d not in exclude_dirs_set)
            for file in sorted(files):
                if not file.endswith(file_exts):
                    continue
                filepath = os.path.join(root, file)
                norm_path = filepath.replace("\\", "/")
                if norm_path in exclude_files_set or file in exclude_files_set:
                    continue
                yield filepath


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
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
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


def run_rule_max_pattern_occurrences_per_file(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c", ".h"]))
    pattern_str = rule.get("pattern", "")
    max_count = int(rule.get("max_count", 5))
    pat = re.compile(pattern_str)

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                raw_content = f.read()
            clean_lines = strip_comments_and_strings_keep_lines(raw_content).splitlines()
            raw_lines = raw_content.splitlines()

            matches = []
            for idx, clean_line in enumerate(clean_lines, 1):
                if pat.search(clean_line):
                    matches.append((idx, raw_lines[idx - 1].strip()))

            if len(matches) > max_count:
                first_line = matches[0][0]
                print(
                    f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{first_line} "
                    f"({len(matches)} occurrences > max {max_count})"
                )
                for m_idx, m_text in matches[:4]:
                    print(f"    Line {m_idx}: {m_text}")
                if len(matches) > 4:
                    print(f"    ... (+{len(matches) - 4} more occurrences)")
                violations += 1
        except Exception as e:
            print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} file(s) exceeding pattern threshold for {name}.")
    return violations


def run_rule_function_metrics(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c", ".h"]))
    max_lines = rule.get("max_lines")
    max_branches = rule.get("max_branches")
    max_indent_levels = rule.get("max_indent_levels")
    max_case_labels = rule.get("max_case_labels")

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                raw_content = f.read()
            clean_lines = strip_comments_and_strings_keep_lines(raw_content).splitlines()

            i = 0
            n_lines = len(clean_lines)
            while i < n_lines:
                line = clean_lines[i]
                stripped = line.strip()
                # Check candidate function header starting at column 0
                if (
                    line
                    and not line[0].isspace()
                    and not stripped.startswith(NON_FUNC_PREFIXES)
                    and "=" not in line
                ):
                    sig_text = line.strip()
                    sig_end = i
                    # Allow multi-line function signature up to 4 lines
                    while (
                        "{" not in clean_lines[sig_end]
                        and ";" not in clean_lines[sig_end]
                        and sig_end + 1 < n_lines
                        and (sig_end - i) < 4
                    ):
                        sig_end += 1
                        sig_text += " " + clean_lines[sig_end].strip()

                    m = FUNC_START_RE.match(sig_text)
                    if m and "{" in clean_lines[sig_end]:
                        func_name = m.group(1)
                        if func_name not in ("if", "for", "while", "switch", "sizeof"):
                            start_idx = i
                            brace_depth = 0
                            found_open = False
                            branches = 0
                            case_count = 0
                            max_nesting = 0
                            j = i
                            while j < n_lines:
                                cl = clean_lines[j]
                                opens = cl.count("{")
                                closes = cl.count("}")
                                if opens > 0:
                                    found_open = True
                                # Nesting depth inside function body (excluding outer function braces)
                                current_nesting = max(0, brace_depth - 1) if found_open else 0
                                if cl.strip():
                                    max_nesting = max(max_nesting, current_nesting)
                                brace_depth += opens - closes
                                branches += len(BRANCH_TOKEN_RE.findall(cl))
                                case_count += len(re.findall(r"\bcase\b", cl))
                                j += 1
                                if found_open and brace_depth <= 0:
                                    break

                            if found_open and brace_depth == 0:
                                func_len = j - start_idx
                                reasons = []
                                if max_lines is not None and func_len > max_lines:
                                    reasons.append(f"lines={func_len} > {max_lines}")
                                if max_branches is not None and branches > max_branches:
                                    reasons.append(f"branches={branches} > {max_branches}")
                                if max_indent_levels is not None and max_nesting > max_indent_levels:
                                    reasons.append(f"nesting={max_nesting} > {max_indent_levels}")
                                if max_case_labels is not None and case_count > max_case_labels:
                                    reasons.append(f"cases={case_count} > {max_case_labels}")

                                if reasons:
                                    print(
                                        f"  [{Colors.RED}VIOLATION{Colors.RESET}] "
                                        f"{filepath}:{start_idx + 1} in {func_name}() "
                                        f"[{', '.join(reasons)}]"
                                    )
                                    violations += 1
                                i = j
                                continue
                i += 1
        except Exception as e:
            print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All functions meet complexity and size limits for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} function complexity violation(s) for {name}.")
    return violations


def run_rule_duplicate_blocks(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c", ".h"]))
    window_size = int(rule.get("min_normalized_lines", 12))
    cross_file_only = bool(rule.get("cross_file_only", True))

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    chunks = collections.defaultdict(list)

    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                raw_content = f.read()
            clean_lines = strip_comments_and_strings_keep_lines(raw_content).splitlines()
            raw_lines = raw_content.splitlines()

            norm = []
            for idx, (cl, rl) in enumerate(zip(clean_lines, raw_lines), 1):
                s = re.sub(r"\s+", " ", cl).strip()
                raw_s = re.sub(r"\s+", " ", rl.split("//")[0]).strip()
                if not s or s in TRIVIAL_NORMALIZED_LINES or s.startswith("#"):
                    continue
                # Skip pure data/table rows (e.g. font bitmaps or struct designated initializers)
                if raw_s.startswith(("{0x", ".")):
                    continue
                norm.append((idx, raw_s))

            for i in range(len(norm) - window_size + 1):
                block = tuple(x[1] for x in norm[i : i + window_size])
                chunks[block].append((filepath, norm[i][0], norm[i + window_size - 1][0]))
        except Exception as e:
            print(f"Error reading {filepath}: {e}")

    covered_lines = collections.defaultdict(set)
    for block, locs in sorted(chunks.items(), key=lambda item: (len(set(x[0] for x in item[1])), len(item[1])), reverse=True):
        unique_files = {x[0] for x in locs}
        if len(locs) < 2:
            continue
        if cross_file_only and len(unique_files) < 2:
            continue

        # Skip if all locations in this window are already covered by a previously reported clone
        if all(any(line in covered_lines[f] for line in range(start, end + 1)) for f, start, end in locs):
            continue

        for f, start, end in locs:
            for line in range(start - 5, end + 6):
                covered_lines[f].add(line)

        loc_summary = ", ".join(f"{f}:{start}-{end}" for f, start, end in locs[:5])
        if len(locs) > 5:
            loc_summary += f" (+{len(locs) - 5} more)"
        print(
            f"  [{Colors.RED}VIOLATION{Colors.RESET}] Duplicate {window_size}-line block "
            f"({len(locs)}x across {len(unique_files)} file(s)): {loc_summary}"
        )
        for sample in block[:3]:
            print(f"    | {sample[:95]}")
        violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No duplicate code blocks found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} duplicate code block group(s) for {name}.")
    return violations


def run_rule_macros_in_headers_only(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c"]))
    allowed_macros = set(rule.get("allowed_macros", []))
    allowed_patterns = [re.compile(p) for p in rule.get("allowed_patterns", [])]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                raw_content = f.read()
            clean_lines = strip_comments_and_strings_keep_lines(raw_content).splitlines()
            raw_lines = raw_content.splitlines()

            for idx, (cl, rl) in enumerate(zip(clean_lines, raw_lines), 1):
                m = DEFINE_MACRO_RE.match(cl)
                if not m:
                    continue
                macro_name = m.group(1)
                if macro_name in allowed_macros:
                    continue
                if any(pat.search(macro_name) for pat in allowed_patterns):
                    continue
                print(
                    f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx} "
                    f"defines '#define {macro_name}' inside a .c file (move to a .h header)"
                )
                print(f"    Match: {rl.strip()}")
                violations += 1
        except Exception as e:
            print(f"Error reading {filepath}: {e}")

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No unauthorized #define macros found in .c files.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} #define macro(s) in .c files that belong in .h headers.")
    return violations


def run_rule_magic_numbers(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c"]))
    ignore_const_tables = bool(rule.get("ignore_const_tables", True))
    flag_array_declarators = bool(rule.get("flag_array_declarators", True))
    allowed_array_sizes = set(rule.get("allowed_array_sizes", [1, 2, 3, 4, 6]))
    allowed_literals = set(rule.get("allowed_literals", [-1, 0, 1, 2, 3, 4, 8, 10, 16, 100]))
    flagged_literals = rule.get("flagged_literals", {})
    max_cross_file = rule.get("max_cross_file_occurrences", 4)
    min_cross_file_value = int(rule.get("min_cross_file_value", 36))
    ui_dimension_paths = tuple(rule.get("ui_dimension_paths", []))
    ui_dimension_funcs = rule.get("ui_dimension_funcs", [])
    ui_dim_re = (
        re.compile(r"\b(" + "|".join(re.escape(fn) for fn in ui_dimension_funcs) + r")\s*\(([^;]+)\)")
        if ui_dimension_funcs
        else None
    )

    # Match hex/octal keys by exact normalized token, and decimal keys by integer value
    flagged_by_str = {str(k).lower(): v for k, v in flagged_literals.items()}
    flagged_decimal_by_val = {}
    for k, v in flagged_literals.items():
        ks = str(k).strip()
        if not ks.lower().startswith("0"):
            parsed = parse_int_literal(ks)
            if parsed is not None:
                flagged_decimal_by_val[parsed] = (ks, v)

    cross_file_tracker = collections.defaultdict(list)

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        is_ui_dim_file = bool(ui_dim_re) and bool(ui_dimension_paths) and filepath.startswith(ui_dimension_paths)
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                raw_content = f.read()
            clean_lines = strip_comments_and_strings_keep_lines(raw_content).splitlines()
            raw_lines = raw_content.splitlines()

            in_const_table_depth = 0
            in_enum_depth = 0

            for idx, (cl, rl) in enumerate(zip(clean_lines, raw_lines), 1):
                s = cl.strip()
                if not s or s.startswith("#"):
                    continue

                # Track enum blocks so enum constant definitions aren't flagged as magic numbers
                if re.search(r"\benum\b", s) and "{" in s:
                    in_enum_depth += s.count("{") - s.count("}")
                    continue
                if in_enum_depth > 0:
                    in_enum_depth += s.count("{") - s.count("}")
                    continue

                # Optionally skip static const lookup tables and compound literal byte arrays
                if ignore_const_tables:
                    if in_const_table_depth == 0 and (
                        re.search(r"\bconst\b.*=\s*\{", s)
                        or re.search(r"\(\s*const\s+\w+\s*\[\s*\d+\s*\]\s*\)\s*\{", s)
                    ):
                        in_const_table_depth = s.count("{") - s.count("}")
                        continue
                    elif in_const_table_depth > 0:
                        in_const_table_depth += s.count("{") - s.count("}")
                        continue

                # 1. Check hardcoded numeric array bounds in declarations (e.g. char buf[64])
                flagged_spans = []
                if flag_array_declarators:
                    for m_arr in ARRAY_DECL_MAGIC_RE.finditer(s):
                        arr_tok = m_arr.group(1)
                        arr_val = parse_int_literal(arr_tok)
                        if arr_val is not None and arr_val not in allowed_array_sizes:
                            flagged_spans.append(m_arr.span(1))
                            print(
                                f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx} "
                                f"hardcoded array size [{arr_tok}] (use EOS_*_STR_LEN or a header macro)"
                            )
                            print(f"    Match: {rl.strip()}")
                            violations += 1

                # 2. Scan numeric literals on the line
                if is_ui_dim_file:
                    for m_ui in ui_dim_re.finditer(s):
                        for m_num in re.finditer(r"(?<![A-Za-z0-9_])(-?\d+)(?![A-Za-z0-9_])", m_ui.group(2)):
                            val = int(m_num.group(1))
                            if val not in allowed_literals:
                                print(
                                    f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx} "
                                    f"magic UI dimension '{val}' in {m_ui.group(1)}() (move to a header macro)"
                                )
                                print(f"    Match: {rl.strip()}")
                                violations += 1

                for m_num in NUMERIC_LITERAL_RE.finditer(s):
                    if any( start <= m_num.start(1) and m_num.end(1) <= end for start, end in flagged_spans ):
                        continue
                    tok = m_num.group(1)
                    val = parse_int_literal(tok)
                    if val is None:
                        continue

                    tok_lower = tok.lower()
                    is_hex_or_oct = tok_lower.startswith("0x") or (tok_lower.startswith("0") and len(tok_lower) > 1)
                    reason = None
                    if tok_lower in flagged_by_str:
                        reason = flagged_by_str[tok_lower]
                    elif not is_hex_or_oct and val in flagged_decimal_by_val:
                        reason = flagged_decimal_by_val[val][1]

                    if reason:
                        print(
                            f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx} "
                            f"magic constant '{tok}': {reason}"
                        )
                        print(f"    Match: {rl.strip()}")
                        violations += 1
                    elif val not in allowed_literals and (is_hex_or_oct or abs(val) >= min_cross_file_value):
                        cross_file_tracker[tok].append((filepath, idx, rl.strip()))
        except Exception as e:
            print(f"Error reading {filepath}: {e}")

    # 3. Report unlisted numeric literals repeated across >= max_cross_file distinct files
    if max_cross_file is not None:
        for tok, locs in sorted(
            cross_file_tracker.items(),
            key=lambda item: (len(set(x[0] for x in item[1])), len(item[1])),
            reverse=True,
        ):
            unique_files = sorted(set(x[0] for x in locs))
            if len(unique_files) < int(max_cross_file):
                continue
            loc_preview = ", ".join(f"{f}:{l}" for f, l, _ in locs[:5])
            if len(locs) > 5:
                loc_preview += f" (+{len(locs) - 5} more)"
            print(
                f"  [{Colors.RED}VIOLATION{Colors.RESET}] Repeated magic literal '{tok}' "
                f"({len(locs)}x across {len(unique_files)} files — move to a shared .h macro): {loc_preview}"
            )
            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No magic number violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} magic number / array bound violation(s) for {name}.")
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
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            lock_stack = []
            for idx, line in enumerate(lines, 1):
                stripped = line.strip()
                if not stripped or stripped.startswith(("//", "/*", "*")):
                    continue
                if line.startswith("}"):
                    lock_stack = []
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

    for filepath in iter_target_files(paths, (".c", ".h"), rule.get("exclude_dirs"), rule.get("exclude_files")):
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

    for filepath in iter_target_files(paths, (".h",), rule.get("exclude_dirs"), rule.get("exclude_files")):
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
    for filepath in iter_target_files(paths, (".c",), rule.get("exclude_dirs"), rule.get("exclude_files")):
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


def run_rule_filesystem_structure(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/efs"])
    fs_struct_pat = re.compile(rule.get("pattern", r"\bEOS_FS_ATTR\s+eos_fs_type_t\b"))

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue

        # 1. Check for loose files directly under dir_path
        for item in sorted(os.listdir(dir_path)):
            item_path = os.path.join(dir_path, item)
            if os.path.isfile(item_path):
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Loose file outside filesystem folder: {item_path}")
                violations += 1

        # 2. Check each filesystem directory for EOS_FS_ATTR eos_fs_type_t definition
        for fs_name in sorted(os.listdir(dir_path)):
            fs_path = os.path.join(dir_path, fs_name)
            if not os.path.isdir(fs_path):
                continue

            has_fs_struct = False
            for root, _, files in os.walk(fs_path):
                for file in files:
                    if file.endswith(".c"):
                        filepath = os.path.join(root, file)
                        try:
                            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                                content = f.read()
                            if fs_struct_pat.search(content):
                                has_fs_struct = True
                                break
                        except Exception as e:
                            print(f"Error reading {filepath}: {e}")
                if has_fs_struct:
                    break

            if not has_fs_struct:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Filesystem directory missing eos_fs_type_t structure: {fs_path}")
                violations += 1

        # 3. Check that VFS operations registration occurs strictly inside mount callbacks
        #    and no loose imperative filesystem init functions exist
        loose_fs_init_pat = re.compile(r"\beos_(?!fs_init\b|vfs_init\b)\w*fs_init\b")
        vfs_reg_pat = re.compile(r"\b(?:eos|esp)_vfs_register\w*\b")
        for root, _, files in os.walk(dir_path):
            for file in sorted(files):
                if not file.endswith((".c", ".h")):
                    continue
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                except Exception as e:
                    print(f"Error reading {filepath}: {e}")
                    continue

                curr_func = None
                brace_depth = 0
                for line_idx, line in enumerate(lines, 1):
                    # Check for forbidden loose fs init function
                    if loose_fs_init_pat.search(line):
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] Loose filesystem init function forbidden at {filepath}:{line_idx}: {line.strip()}")
                        violations += 1

                    # Track simple C function signature / scope
                    clean_line = line.split("//")[0].strip()
                    if brace_depth == 0:
                        m = FUNC_START_RE.match(clean_line)
                        if m:
                            curr_func = m.group(1)

                    brace_depth += line.count("{") - line.count("}")
                    if brace_depth <= 0:
                        curr_func = None
                        brace_depth = 0

                    if vfs_reg_pat.search(line):
                        if not curr_func or "mount" not in curr_func.lower():
                            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] VFS registration call outside mount callback at {filepath}:{line_idx} (in function '{curr_func}'): {line.strip()}")
                            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All filesystem folders contain an eos_fs_type_t structure and no loose files exist.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} filesystem structure violation(s).")
    return violations


def run_rule_gui_logic_separation(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/ebin"])
    prefix = rule.get("app_prefix", "lvgl_")

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for dir_path in paths:
        if not os.path.exists(dir_path):
            continue
        for app_name in sorted(os.listdir(dir_path)):
            if not app_name.startswith(prefix):
                continue
            app_dir = os.path.join(dir_path, app_name)
            if not os.path.isdir(app_dir):
                continue

            files = os.listdir(app_dir)
            has_logic_c = any(f.endswith("_logic.c") for f in files)
            has_logic_h = any(f.endswith("_logic.h") for f in files)

            if not has_logic_c:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {app_dir}: GUI application missing business logic source (*_logic.c)")
                violations += 1
            if not has_logic_h:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {app_dir}: GUI application missing business logic header (*_logic.h)")
                violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All GUI ({prefix}*) applications strictly decouple presentation and business logic into *_logic.c / *_logic.h.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} GUI application architecture violation(s).")
    return violations


def run_rule_required_patterns(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    raw_patterns = rule.get("patterns", [])
    file_exts = tuple(rule.get("file_extensions", [".c"]))
    exclude_dirs = rule.get("exclude_dirs")
    exclude_files = rule.get("exclude_files")

    compiled_patterns = [re.compile(p) for p in raw_patterns]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, exclude_dirs, exclude_files):
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            continue

        for pat in compiled_patterns:
            if not pat.search(content):
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}")
                print(f"    Required pattern '{pat.pattern}' not found in file")
                violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_host_syntax_check(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", [])
    file_exts = tuple(rule.get("file_extensions", [".c"]))
    include_dirs = rule.get("include_dirs", [])
    inc_args = [f"-I{d}" for d in include_dirs]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        cmd = ["gcc", "-fsyntax-only"] + inc_args + [filepath]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            err_line = proc.stderr.strip().splitlines()[0] if proc.stderr else "syntax check failed"
            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}")
            print(f"    Host compilation failed: {err_line}")
            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_files_referenced_by(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    sources = rule.get("sources", [])
    referenced_in = rule.get("referenced_in", [])

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    ref_files = []
    for pat in referenced_in:
        ref_files.extend(glob.glob(pat, recursive=True))

    combined_text = ""
    for rf in ref_files:
        try:
            with open(rf, "r", encoding="utf-8", errors="ignore") as f:
                combined_text += f.read() + "\n"
        except Exception:
            pass

    src_files = []
    for pat in sources:
        src_files.extend(glob.glob(pat, recursive=True))

    for sf in src_files:
        basename = os.path.basename(sf)
        if basename not in combined_text and sf not in combined_text:
            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {sf}")
            print(f"    File not referenced in any configured destination ({referenced_in})")
            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name}.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} violation(s) for {name}.")
    return violations


def run_rule_symbols_documented(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    headers = rule.get("headers", [])
    symbol_pattern = re.compile(rule.get("symbol_pattern", ""))
    docs = rule.get("docs", [])
    max_undoc = rule.get("max_undocumented", 0)

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    doc_files = []
    for pat in docs:
        doc_files.extend(glob.glob(pat, recursive=True))

    all_docs = ""
    for df in doc_files:
        try:
            with open(df, "r", encoding="utf-8", errors="ignore") as f:
                all_docs += f.read() + "\n"
        except Exception:
            pass

    symbols = []
    for h in headers:
        try:
            with open(h, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    m = symbol_pattern.search(line)
                    if m:
                        symbols.append((h, m.group(1)))
        except Exception as e:
            print(f"Error reading {h}: {e}")

    undocumented = [(h, s) for h, s in symbols if s not in all_docs]
    if len(undocumented) > max_undoc:
        excess = len(undocumented) - max_undoc
        for h, s in undocumented[:excess]:
            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {h}")
            print(f"    Symbol '{s}' is not documented in any {docs}")
            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No violations found for {name} ({len(undocumented)} undocumented <= {max_undoc} allowed).")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} undocumented symbol violation(s) for {name}.")
    return violations


def run_rule_init_sh_lifecycle(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/eboard"])
    exclude_dirs = rule.get("exclude_dirs")
    exclude_files = rule.get("exclude_files")

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, (".sh",), exclude_dirs, exclude_files):
        if not filepath.endswith("init.sh"):
            continue
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            continue

        mount_lines = []
        bus_lines = []
        storage_lines = []
        msc_lines = []

        for idx, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if stripped.startswith("mount"):
                mount_lines.append((idx, stripped))
            elif stripped.startswith("devctl attach bus"):
                bus_lines.append((idx, stripped))
            elif stripped.startswith("devctl attach storage"):
                storage_lines.append((idx, stripped))
            elif stripped.startswith("devctl attach usb msc"):
                msc_lines.append((idx, stripped))

        if not mount_lines:
            print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}")
            print(f"    Missing 'mount -a' command in board startup script")
            violations += 1
            continue

        first_mount_line, first_mount_cmd = mount_lines[0]

        # 1. Hardware bus attachments must precede mount
        for b_line, b_cmd in bus_lines:
            if b_line > first_mount_line:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{b_line}")
                print(f"    Hardware bus attached after mount at line {first_mount_line}: '{b_cmd}'")
                violations += 1

        # 2. Primary storage attachments must precede mount -a
        for s_line, s_cmd in storage_lines:
            if s_line > first_mount_line:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{s_line}")
                print(f"    Storage attached after mount at line {first_mount_line}: '{s_cmd}'")
                violations += 1

        # 3. USB MSC attachment must follow mount -a
        for m_line, m_cmd in msc_lines:
            if m_line < first_mount_line:
                print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{m_line}")
                print(f"    USB mass storage attached before mount at line {first_mount_line}: '{m_cmd}'")
                violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All board init.sh scripts adhere to bus -> storage -> mount -> usb_msc lifecycle order.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} init script lifecycle violation(s).")
    return violations


def run_rule_gui_theme_unification(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main/ebin"])
    prefix = rule.get("app_prefix", "lvgl_")
    exclude_dirs = rule.get("exclude_dirs")
    exclude_files = rule.get("exclude_files")

    ad_hoc_color_re = re.compile(
        r"^\s*#\s*define\s+([A-Za-z0-9_]*(?:COLOR|COL)[A-Za-z0-9_]*)\s+(0x[0-9a-fA-F]+|\d+)\b"
    )
    dark_text_color_re = re.compile(
        r"lv_obj_set_style_text_color\([^,]+,\s*(?:lv_color_hex\((0x[0-9a-fA-F]+)\)|lv_color_black\(\))"
    )

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")
    for filepath in iter_target_files(paths, (".c", ".h"), exclude_dirs, exclude_files):
        norm = filepath.replace("\\", "/")
        if not any(f"/{prefix}" in norm or norm.startswith(f"main/ebin/{prefix}") for _ in [1]):
            continue

        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            continue

        for idx, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith(("//", "/*", "*")):
                continue

            m_col = ad_hoc_color_re.match(stripped)
            if m_col:
                macro_name = m_col.group(1)
                macro_val = m_col.group(2)
                if not macro_name.endswith("_COLS") and not macro_name.endswith("_COUNT"):
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                    print(f"    Ad-hoc color macro '#define {macro_name} {macro_val}' violates theme unification; use EOS_THEME_* or EOS_COLOR_* from ecore/lvgl.h")
                    violations += 1

            m_dark = dark_text_color_re.search(stripped)
            if m_dark:
                hex_str = m_dark.group(1)
                if hex_str:
                    val = int(hex_str, 16)
                    r = (val >> 16) & 0xFF
                    g = (val >> 8) & 0xFF
                    b = val & 0xFF
                    brightness = 0.299 * r + 0.587 * g + 0.114 * b
                    if brightness < 60:
                        print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                        print(f"    Low contrast / dark text color ({hex_str}, brightness {brightness:.1f}) produces unreadable black text on dark surfaces")
                        violations += 1
                else:
                    print(f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx}")
                    print(f"    lv_color_black() text color produces unreadable black text on dark surfaces")
                    violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] All GUI ({prefix}*) applications follow centralized theme and high-contrast text rules.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} GUI theme unification violation(s).")
    return violations


def run_rule_unused_defines(rule):
    violations = 0
    name = rule.get("name", rule.get("id"))
    paths = rule.get("paths", ["main"])
    search_paths = rule.get("search_paths", ["main"])
    file_exts = tuple(rule.get("file_extensions", [".h"]))
    define_prefix = rule.get("define_prefix", "EOS_")
    allowlist = set(rule.get("allowlist", []))
    exclude_patterns = [re.compile(p) for p in rule.get("exclude_patterns", [])]

    print(f"\n{Colors.BOLD}=== Running check: {name} ==={Colors.RESET}")

    # Load all searchable source/header texts once
    all_texts = {}
    for spath in iter_target_files(search_paths, (".c", ".h"), None, None):
        try:
            with open(spath, "r", encoding="utf-8", errors="ignore") as f:
                all_texts[spath] = f.read()
        except Exception:
            pass

    def_re = re.compile(r"^\s*#\s*define\s+(" + re.escape(define_prefix) + r"[A-Za-z0-9_]+)\b")
    defs = []
    def_counts = collections.Counter()
    for filepath in iter_target_files(paths, file_exts, rule.get("exclude_dirs"), rule.get("exclude_files")):
        text = all_texts.get(filepath)
        if text is None:
            continue
        for idx, line in enumerate(text.splitlines(), 1):
            m = def_re.match(line)
            if m:
                macro = m.group(1)
                defs.append((filepath, idx, macro))
                def_counts[macro] += 1

    seen_macros = set()
    for filepath, idx, macro in defs:
        if macro in seen_macros or macro in allowlist:
            continue
        if any(p.search(macro) for p in exclude_patterns):
            continue
        seen_macros.add(macro)

        pat = re.compile(r"\b" + re.escape(macro) + r"\b")
        total_hits = sum(len(pat.findall(txt)) for txt in all_texts.values())
        if total_hits <= def_counts[macro]:
            print(
                f"  [{Colors.RED}VIOLATION{Colors.RESET}] {filepath}:{idx} "
                f"unused #define '{macro}' (remove or add to allowlist/exclude_patterns if reserved for future use)"
            )
            violations += 1

    if violations == 0:
        print(f"  [{Colors.GREEN}PASS{Colors.RESET}] No unused {define_prefix}* definitions found.")
    else:
        print(f"  [{Colors.RED}FAIL{Colors.RESET}] Found {violations} unused definition(s) for {name}.")
    return violations


DISPATCHER = {
    "forbidden_patterns": run_rule_forbidden_patterns,
    "max_pattern_occurrences_per_file": run_rule_max_pattern_occurrences_per_file,
    "function_metrics": run_rule_function_metrics,
    "duplicate_blocks": run_rule_duplicate_blocks,
    "macros_in_headers_only": run_rule_macros_in_headers_only,
    "magic_numbers": run_rule_magic_numbers,
    "no_calls_between_patterns": run_rule_no_calls_between_patterns,
    "no_globals": run_rule_no_globals,
    "mandatory_include_guards": run_rule_include_guards,
    "allowed_directory": run_rule_allowed_directory,
    "wrapper_definitions": run_rule_wrapper_definitions,
    "driver_enable_guards": run_rule_driver_enable_guards,
    "driver_structure": run_rule_driver_structure,
    "host_syntax_check": run_rule_host_syntax_check,
    "files_referenced_by": run_rule_files_referenced_by,
    "symbols_documented": run_rule_symbols_documented,
    "filesystem_structure": run_rule_filesystem_structure,
    "required_patterns": run_rule_required_patterns,
    "gui_logic_separation": run_rule_gui_logic_separation,
    "init_sh_lifecycle": run_rule_init_sh_lifecycle,
    "gui_theme_unification": run_rule_gui_theme_unification,
    "unused_defines": run_rule_unused_defines,
}


def load_rules(rules_file):
    if not os.path.exists(rules_file):
        print(f"{Colors.RED}Rules definition file not found: {rules_file}{Colors.RESET}")
        sys.exit(1)
    with open(rules_file, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("rules", [])


def main():
    parser = argparse.ArgumentParser(description="EOS Declarative Layer Separation & Complexity Audit Tool")
    parser.add_argument(
        "-r", "--rules",
        default="tools/layer_audit_rules.json",
        help="Path to declarative JSON rules definition (default: tools/layer_audit_rules.json)"
    )
    parser.add_argument(
        "--rule-id",
        action="append",
        dest="rule_ids",
        help="Run only specific rule ID(s) (can be specified multiple times)"
    )
    parser.add_argument(
        "-x", "--failfast",
        action="store_true",
        help="Exit immediately upon first architectural violation to speed up testing"
    )
    args = parser.parse_args()

    print(f"{Colors.CYAN}{Colors.BOLD}Running Declarative EOS Layer Separation Audit ({args.rules})...{Colors.RESET}")
    rules = load_rules(args.rules)

    total_violations = 0
    executed_rules = 0

    for rule in rules:
        if args.rule_ids and rule.get("id") not in args.rule_ids:
            continue
        rule_type = rule.get("type")
        handler = DISPATCHER.get(rule_type)
        if not handler:
            print(f"{Colors.YELLOW}[WARN] Unknown rule type '{rule_type}' for rule '{rule.get('id')}'; skipping.{Colors.RESET}")
            continue
        v = handler(rule)
        total_violations += v
        executed_rules += 1
        if v > 0 and args.failfast:
            print(f"{Colors.RED}{Colors.BOLD}>>> Halting audit immediately due to --failfast flag. <<<{Colors.RESET}")
            break

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