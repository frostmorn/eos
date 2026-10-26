#!/usr/bin/env python3
"""
tools/update_dependencies.py

Dynamically updates main/idf_component.yml based on active board driver macros.
Each driver in main/edriver/<scope>/<name> can provide an idf_component.yml
declaring external ESP-IDF component dependencies.

Only drivers enabled for the active board in main/eboard/<board>/board_pregen.h
will have their component dependencies aggregated into main/idf_component.yml.
"""

import os
import shutil
import sys
import yaml

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BOARD_ROOT = os.path.join(ROOT_DIR, "main", "eboard")
DRIVER_ROOT = os.path.join(ROOT_DIR, "main", "edriver")
TARGET_MANIFEST = os.path.join(ROOT_DIR, "main", "idf_component.yml")
LOCK_FILE = os.path.join(ROOT_DIR, "dependencies.lock")
MANAGED_COMPONENTS_DIR = os.path.join(ROOT_DIR, "managed_components")


def detect_board_from_sdkconfig(sdkconfig_path):
    if not os.path.exists(sdkconfig_path):
        return None
    with open(sdkconfig_path, "r", encoding="utf-8") as f:
        content = f.read()

    for line in content.splitlines():
        line = line.strip()
        if line.startswith("CONFIG_EOS_BOARD_") and line.endswith("=y"):
            suffix = line[len("CONFIG_EOS_BOARD_"):-2].lower()
            if os.path.isdir(os.path.join(BOARD_ROOT, suffix)):
                return suffix

    for line in content.splitlines():
        line = line.strip()
        if line.startswith('CONFIG_IDF_TARGET="'):
            target = line.split('"')[1]
            if target == "esp32p4" and os.path.isdir(os.path.join(BOARD_ROOT, "jc4880p443c_i_w_y")):
                return "jc4880p443c_i_w_y"
            elif target == "esp32s3" and os.path.isdir(os.path.join(BOARD_ROOT, "lilka_v23")):
                return "lilka_v23"
    return None


def get_enabled_driver_macros(board):
    pregen_path = os.path.join(BOARD_ROOT, board, "board_pregen.h")
    if not os.path.exists(pregen_path):
        return set()
    enabled = set()
    with open(pregen_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("#define") and not line.startswith("//"):
                parts = line.split()
                if len(parts) >= 2 and parts[1].startswith("EOS_DRV_") and parts[1].endswith("_ENABLED"):
                    enabled.add(parts[1])
    return enabled


def collect_driver_dependencies(enabled_macros):
    if not os.path.isdir(DRIVER_ROOT):
        return {}

    merged_deps = {}
    for scope in os.listdir(DRIVER_ROOT):
        scope_dir = os.path.join(DRIVER_ROOT, scope)
        if not os.path.isdir(scope_dir):
            continue
        for name in os.listdir(scope_dir):
            drv_dir = os.path.join(scope_dir, name)
            if not os.path.isdir(drv_dir):
                continue

            macro = f"EOS_DRV_{scope.upper()}_{name.upper()}_ENABLED"
            if macro not in enabled_macros:
                continue

            manifest_candidates = [
                os.path.join(drv_dir, "idf_component.yml"),
                os.path.join(drv_dir, "dependencies.yml"),
            ]
            for mpath in manifest_candidates:
                if os.path.exists(mpath):
                    try:
                        with open(mpath, "r", encoding="utf-8") as f:
                            data = yaml.safe_load(f)
                        if isinstance(data, dict) and "dependencies" in data:
                            deps = data["dependencies"]
                            if isinstance(deps, dict):
                                for dep_name, dep_spec in deps.items():
                                    merged_deps[dep_name] = dep_spec
                    except Exception as e:
                        print(f"[update_dependencies] Warning: Failed to parse {mpath}: {e}")
                    break
    return merged_deps


def collect_board_dependencies(board):
    if not board:
        return {}
    bdir = os.path.join(BOARD_ROOT, board)
    if not os.path.isdir(bdir):
        return {}

    merged_deps = {}
    manifest_candidates = [
        os.path.join(bdir, "idf_component.yml"),
        os.path.join(bdir, "dependencies.yml"),
    ]
    for mpath in manifest_candidates:
        if os.path.exists(mpath):
            try:
                with open(mpath, "r", encoding="utf-8") as f:
                    data = yaml.safe_load(f)
                if isinstance(data, dict) and "dependencies" in data:
                    deps = data["dependencies"]
                    if isinstance(deps, dict):
                        for dep_name, dep_spec in deps.items():
                            merged_deps[dep_name] = dep_spec
            except Exception as e:
                print(f"[update_dependencies] Warning: Failed to parse board manifest {mpath}: {e}")
            break
    return merged_deps


def update_dependencies(board=None):
    if not board:
        sdkconfig_path = os.path.join(ROOT_DIR, "sdkconfig")
        board = detect_board_from_sdkconfig(sdkconfig_path)

    if not board:
        print("[update_dependencies] No active board detected, skipping dependency resolution.")
        return 0

    enabled_macros = get_enabled_driver_macros(board)
    merged_deps = collect_driver_dependencies(enabled_macros)
    board_deps = collect_board_dependencies(board)
    for dep_name, dep_spec in board_deps.items():
        merged_deps[dep_name] = dep_spec

    if merged_deps:
        header = (
            "# AUTO-GENERATED BY tools/update_dependencies.py - DO NOT EDIT MANUALLY\n"
            f"# Active board: {board}\n\n"
        )
        content = {"dependencies": merged_deps}
        yaml_text = header + yaml.dump(content, default_flow_style=False)

        current_content = None
        if os.path.exists(TARGET_MANIFEST):
            with open(TARGET_MANIFEST, "r", encoding="utf-8") as f:
                current_content = f.read()

        if current_content != yaml_text:
            with open(TARGET_MANIFEST, "w", encoding="utf-8") as f:
                f.write(yaml_text)
            print(f"[update_dependencies] Updated {TARGET_MANIFEST} for board '{board}' ({len(merged_deps)} dependencies).")
    else:
        # No driver dependencies required for this board
        if os.path.exists(TARGET_MANIFEST):
            os.remove(TARGET_MANIFEST)
            print(f"[update_dependencies] Removed {TARGET_MANIFEST} (no driver dependencies for board '{board}').")
        if os.path.exists(LOCK_FILE):
            os.remove(LOCK_FILE)
        if os.path.isdir(MANAGED_COMPONENTS_DIR):
            shutil.rmtree(MANAGED_COMPONENTS_DIR)
            print(f"[update_dependencies] Cleaned up managed_components/ for clean build of board '{board}'.")

    return 0


if __name__ == "__main__":
    b = sys.argv[1] if len(sys.argv) > 1 else None
    sys.exit(update_dependencies(b))
