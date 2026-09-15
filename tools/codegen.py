#!/usr/bin/env python3
"""
gen_template.py - EOS template-based code generator.

Templates live under ./templates/<template_name>/ and can contain any
number of files, directories and filetypes. Any $TOKEN$ occurring in a
file name, directory name, or file content is a template variable.

Variables are split into two kinds:

  * Regular variables (e.g. $DRIVER_NAME$) are requested interactively
    from the user via input().
  * "Special" variables, whose name starts with an underscore
    (e.g. $_COMMIT$, $_DATE$), are filled in automatically by the
    generator itself - the user is never asked for these. New special
    variables can be added by extending the SPECIAL_VARS registry
    below.

A variable can optionally be followed by one or more ":operation"
suffixes that transform its value only at that specific occurrence,
e.g. $DRIVER_NAME:upper$ or $DRIVER_NAME:snake:upper$ (chained
left-to-right). The user is still only asked once for the raw value
of $DRIVER_NAME$; every decorated occurrence derives from it. New
operations can be added by extending the OPERATIONS registry below.

Usage:
    python3 tools/gen_template.py [template_name] [-o OUTPUT_DIR]

If template_name is omitted, the available templates are listed and
the user is prompted to pick one.

The output directory defaults to the current working directory (not
the repo root) -- a template's own internal folder layout (e.g.
"$DRIVER_SCOPE$/$DRIVER_NAME$.c") is generated relative to it, so run
the tool from wherever in the tree the template is meant to land, or
point it elsewhere explicitly with -o/--output.

Examples:
    # generate a new edriver from wherever you are, into ./out/
    python3 tools/gen_template.py edriver -o out/

    # cd to where drivers actually live, use the default cwd output
    cd main/edriver && python3 ../../tools/gen_template.py edriver
"""

import os
import re
import sys
import argparse
import subprocess
import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
TEMPLATES_DIR = os.path.join(REPO_ROOT, "templates")

TOKEN_RE = re.compile(r"\$([A-Za-z0-9_]+)((?::[A-Za-z0-9_]+)*)\$")


def _split_words(s):
    """Split an arbitrary user-entered string into words, regardless
    of whether it came in as snake_case, kebab-case, camelCase,
    PascalCase, "space separated", or any mix thereof."""
    s = re.sub(r"[_\-\s]+", " ", s)
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", s)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", s)
    return [w for w in s.split(" ") if w]


# Registry of :operation suffixes: name -> callable(str) -> str.
# Add new value transforms here.
OPERATIONS = {
    "upper": lambda s: s.upper(),
    "lower": lambda s: s.lower(),
    "capitalize": lambda s: s[:1].upper() + s[1:] if s else s,
    "title": lambda s: " ".join(w.capitalize() for w in _split_words(s)),
    "snake": lambda s: "_".join(w.lower() for w in _split_words(s)),
    "kebab": lambda s: "-".join(w.lower() for w in _split_words(s)),
    "camel": lambda s: (lambda ws: (ws[0].lower() + "".join(w.capitalize() for w in ws[1:])) if ws else s)(_split_words(s)),
    "pascal": lambda s: "".join(w.capitalize() for w in _split_words(s)),
}


def git(*args):
    """Run a git command in the repo root, return stripped stdout or
    None on any failure."""
    try:
        return subprocess.check_output(
            ["git", *args], cwd=REPO_ROOT, stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return None


# Registry of special ($_NAME$) variables: name (without the leading
# underscore, uppercase) -> zero-arg callable producing its value.
# Add new auto-filled variables here.
SPECIAL_VARS = {
    "COMMIT": lambda: git("rev-parse", "--short", "HEAD") or "unknown",
    "COMMIT_FULL": lambda: git("rev-parse", "HEAD") or "unknown",
    "DATE": lambda: datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    "YEAR": lambda: str(datetime.datetime.now().year),
    "BRANCH": lambda: git("rev-parse", "--abbrev-ref", "HEAD") or "unknown",
    "AUTHOR": lambda: git("config", "user.name") or os.environ.get("USER", "unknown"),
}


def list_templates():
    if not os.path.isdir(TEMPLATES_DIR):
        print(f"No templates directory found at {TEMPLATES_DIR}")
        return []
    return sorted(
        d for d in os.listdir(TEMPLATES_DIR)
        if os.path.isdir(os.path.join(TEMPLATES_DIR, d))
    )


def collect_tokens(template_root):
    """Return the set of every unique token name found across all file
    names, directory names, and file contents inside template_root."""
    tokens = set()

    def scan(text):
        for base, _ops in TOKEN_RE.findall(text):
            tokens.add(base)

    for dirpath, dirnames, filenames in os.walk(template_root):
        for name in list(dirnames) + list(filenames):
            scan(name)
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                with open(path, "r", encoding="utf-8") as f:
                    content = f.read()
            except (UnicodeDecodeError, OSError):
                continue  # binary or unreadable file: skip content scan
            scan(content)
    return tokens


def resolve_variables(tokens):
    """Build {token_name: value}. Special ($_X$) tokens are computed
    automatically; regular ones are requested from the user."""
    values = {}
    regular = sorted(t for t in tokens if not t.startswith("_"))
    special = sorted(t for t in tokens if t.startswith("_"))

    for tok in special:
        key = tok[1:]  # strip leading underscore
        if key in SPECIAL_VARS:
            values[tok] = SPECIAL_VARS[key]()
        else:
            print(f"Warning: unknown special variable ${tok}$, "
                  f"defaulting to empty string.")
            values[tok] = ""

    for tok in regular:
        values[tok] = input(f"Enter value for ${tok}$: ")

    return values


_warned_ops = set()


def apply_ops(value, ops_str):
    """Apply a ':op1:op2:...' chain to value, left to right."""
    for op in filter(None, ops_str.split(":")):
        if op in OPERATIONS:
            value = OPERATIONS[op](value)
        elif op not in _warned_ops:
            _warned_ops.add(op)
            print(f"Warning: unknown operation ':{op}', ignoring it.")
    return value


def substitute(text, values):
    def repl(match):
        base, ops = match.group(1), match.group(2)
        if base not in values:
            return match.group(0)
        return apply_ops(values[base], ops)
    return TOKEN_RE.sub(repl, text)


def generate(template_name, output_dir):
    template_root = os.path.join(TEMPLATES_DIR, template_name)
    if not os.path.isdir(template_root):
        print(f"Template '{template_name}' not found in {TEMPLATES_DIR}")
        available = list_templates()
        if available:
            print("Available templates:", ", ".join(available))
        sys.exit(1)

    output_dir = os.path.abspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)

    tokens = collect_tokens(template_root)
    values = resolve_variables(tokens)

    generated = []
    for dirpath, dirnames, filenames in os.walk(template_root):
        rel_dir = os.path.relpath(dirpath, template_root)

        # Create this directory in the output even if it ends up
        # empty (no files directly inside it) - os.walk still visits
        # it, but the file-writing loop below would otherwise only
        # create directories that happen to contain a file.
        dst_dir = output_dir if rel_dir == "." else os.path.join(output_dir, substitute(rel_dir, values))
        os.makedirs(dst_dir, exist_ok=True)

        for filename in filenames:
            src_path = os.path.join(dirpath, filename)
            rel_path = filename if rel_dir == "." else os.path.join(rel_dir, filename)
            dst_rel_path = substitute(rel_path, values)
            dst_path = os.path.join(output_dir, dst_rel_path)

            os.makedirs(os.path.dirname(dst_path), exist_ok=True)

            try:
                with open(src_path, "r", encoding="utf-8") as f:
                    content = f.read()
                content = substitute(content, values)
                with open(dst_path, "w", encoding="utf-8") as f:
                    f.write(content)
            except (UnicodeDecodeError, OSError):
                # binary file: copy verbatim, no substitution
                with open(src_path, "rb") as f:
                    data = f.read()
                with open(dst_path, "wb") as f:
                    f.write(data)

            generated.append(dst_path)

    print(f"\nGenerated files (in {output_dir}):")
    for path in generated:
        print(f"  {os.path.relpath(path, output_dir)}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate files from a template under ./templates/<name>/."
    )
    parser.add_argument(
        "template", nargs="?", default=None,
        help="template name (a subdirectory of ./templates/). "
             "If omitted, available templates are listed interactively.",
    )
    parser.add_argument(
        "-o", "--output", dest="output_dir", default=None,
        help="directory to generate files into "
             "(default: current working directory)",
    )
    args = parser.parse_args()

    template_name = args.template
    if template_name is None:
        available = list_templates()
        if not available:
            print("No templates available.")
            sys.exit(1)
        print("Available templates:")
        for t in available:
            print(f"  {t}")
        template_name = input("\nTemplate to generate: ").strip()

    output_dir = args.output_dir if args.output_dir is not None else os.getcwd()

    generate(template_name, output_dir)


if __name__ == "__main__":
    main()