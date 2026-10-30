#!/usr/bin/env python3
"""
tools/setboard.py

Board selection and workspace environment switcher for EOS.
Manages per-board caching of build artifacts, external ESP-IDF managed
components, lock files, and sdkconfig to ensure fast, zero-redownload,
and reliable target switching.
"""

import filecmp
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
from update_dependencies import update_dependencies, detect_board_from_sdkconfig

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BOARD_PATH = os.path.join(ROOT_DIR, "main", "eboard")
BOARD_CACHE_DIR = os.path.join(ROOT_DIR, ".board_cache")
ACTIVE_BOARD_FILE = os.path.join(ROOT_DIR, ".active_board")

board_list = []
board_target_map = {
    "lilka_v23": "EOS_TARGET_LILKAV2",
    "jc4880p443c_i_w_y": "EOS_TARGET_JC4880P443C_I_W_Y",
    "esp32s3_qemu": "EOS_TARGET_ESP32S3_QEMU"
}


def ensureroot():
    root_files = ["Makefile", "CMakeLists.txt", "main"]
    candidate_files = os.listdir(ROOT_DIR)
    for rfile in root_files:
        if rfile not in candidate_files:
            print("ERROR: Should be run from project root dir")
            sys.exit(-1)
    os.chdir(ROOT_DIR)


def loadboards():
    files = os.listdir(BOARD_PATH)
    for file in files:
        cur = os.path.join(BOARD_PATH, file)
        if os.path.isdir(cur) and "sdkconfig" in os.listdir(cur):
            board_list.append(file)
    board_list.sort()


def get_current_board():
    if os.path.exists(ACTIVE_BOARD_FILE):
        try:
            with open(ACTIVE_BOARD_FILE, "r", encoding="utf-8") as f:
                b = f.read().strip()
                if b and os.path.isdir(os.path.join(BOARD_PATH, b)):
                    return b
        except Exception:
            pass
    sdkconfig_path = os.path.join(ROOT_DIR, "sdkconfig")
    return detect_board_from_sdkconfig(sdkconfig_path)


def save_board_state(board):
    if not board:
        return
    cache_dir = os.path.join(BOARD_CACHE_DIR, board)
    os.makedirs(cache_dir, exist_ok=True)

    # 1. build/
    dest_build = os.path.join(cache_dir, "build")
    if os.path.exists("build"):
        if os.path.exists(dest_build):
            shutil.rmtree(dest_build)
        try:
            shutil.move("build", dest_build)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache build/ for '{board}': {e}")

    # 2. managed_components/
    dest_mc = os.path.join(cache_dir, "managed_components")
    if os.path.exists("managed_components"):
        if os.path.exists(dest_mc):
            shutil.rmtree(dest_mc)
        try:
            shutil.move("managed_components", dest_mc)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache managed_components/ for '{board}': {e}")

    # 3. dependencies.lock
    dest_lock = os.path.join(cache_dir, "dependencies.lock")
    if os.path.exists("dependencies.lock"):
        if os.path.exists(dest_lock):
            os.remove(dest_lock)
        try:
            shutil.move("dependencies.lock", dest_lock)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache dependencies.lock for '{board}': {e}")

    # 4. main/idf_component.yml
    dest_manifest = os.path.join(cache_dir, "idf_component.yml")
    manifest_path = os.path.join("main", "idf_component.yml")
    if os.path.exists(manifest_path):
        if os.path.exists(dest_manifest):
            os.remove(dest_manifest)
        try:
            shutil.move(manifest_path, dest_manifest)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache idf_component.yml for '{board}': {e}")

    # 5. sdkconfig and sdkconfig.old
    dest_sdkconfig = os.path.join(cache_dir, "sdkconfig")
    if os.path.exists("sdkconfig"):
        if os.path.exists(dest_sdkconfig):
            os.remove(dest_sdkconfig)
        try:
            shutil.move("sdkconfig", dest_sdkconfig)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache sdkconfig for '{board}': {e}")
    if os.path.exists("sdkconfig.old"):
        os.remove("sdkconfig.old")

    # 6. init.sh
    dest_init = os.path.join(cache_dir, "init.sh")
    if os.path.exists("init.sh"):
        if os.path.exists(dest_init):
            os.remove(dest_init)
        try:
            shutil.move("init.sh", dest_init)
        except Exception as e:
            print(f"[setboard] Warning: Failed to cache init.sh for '{board}': {e}")


def restore_board_state(board, clean=False):
    if not board:
        return
    cache_dir = os.path.join(BOARD_CACHE_DIR, board)
    eboard_sdkconfig = os.path.join(BOARD_PATH, board, "sdkconfig")
    cached_sdkconfig = os.path.join(cache_dir, "sdkconfig")
    eboard_init = os.path.join(BOARD_PATH, board, "init.sh")
    cached_init = os.path.join(cache_dir, "init.sh")

    if clean and os.path.exists(cache_dir):
        print(f"[setboard] Discarding cache for '{board}' (--clean requested).")
        shutil.rmtree(cache_dir)

    # Clean any leftover artifacts from root before restoring
    for d in ["build", "managed_components"]:
        if os.path.exists(d):
            shutil.rmtree(d)
    for f in ["dependencies.lock", "main/idf_component.yml", "sdkconfig", "sdkconfig.old", "init.sh"]:
        if os.path.exists(f):
            os.remove(f)

    if os.path.isdir(cache_dir):
        # 1. Restore build/
        src_build = os.path.join(cache_dir, "build")
        if os.path.exists(src_build):
            try:
                shutil.move(src_build, "build")
                print(f"[setboard] Restored cached build/ for '{board}'.")
            except Exception as e:
                print(f"[setboard] Warning: Failed to restore build/ for '{board}': {e}")

        # 2. Restore managed_components/
        src_mc = os.path.join(cache_dir, "managed_components")
        if os.path.exists(src_mc):
            try:
                shutil.move(src_mc, "managed_components")
                print(f"[setboard] Restored cached managed_components/ for '{board}'.")
            except Exception as e:
                print(f"[setboard] Warning: Failed to restore managed_components/ for '{board}': {e}")

        # 3. Restore dependencies.lock
        src_lock = os.path.join(cache_dir, "dependencies.lock")
        if os.path.exists(src_lock):
            try:
                shutil.move(src_lock, "dependencies.lock")
                print(f"[setboard] Restored cached dependencies.lock for '{board}'.")
            except Exception as e:
                print(f"[setboard] Warning: Failed to restore dependencies.lock for '{board}': {e}")

        # 4. Restore idf_component.yml
        src_manifest = os.path.join(cache_dir, "idf_component.yml")
        if os.path.exists(src_manifest):
            try:
                shutil.move(src_manifest, os.path.join("main", "idf_component.yml"))
            except Exception as e:
                print(f"[setboard] Warning: Failed to restore idf_component.yml for '{board}': {e}")

        # 5. Restore sdkconfig
        if os.path.exists(cached_sdkconfig):
            if os.path.exists(eboard_sdkconfig) and os.path.getmtime(eboard_sdkconfig) > os.path.getmtime(cached_sdkconfig):
                shutil.copy2(eboard_sdkconfig, "sdkconfig")
                os.remove(cached_sdkconfig)
            else:
                shutil.move(cached_sdkconfig, "sdkconfig")
        elif os.path.exists(eboard_sdkconfig):
            shutil.copy2(eboard_sdkconfig, "sdkconfig")

        # 6. Restore init.sh
        if os.path.exists(eboard_init):
            shutil.copy2(eboard_init, "init.sh")
        if os.path.exists(cached_init):
            os.remove(cached_init)
    else:
        # First time configuring this board
        if os.path.exists(eboard_sdkconfig):
            shutil.copy2(eboard_sdkconfig, "sdkconfig")
        if os.path.exists(eboard_init):
            shutil.copy2(eboard_init, "init.sh")

    # Safety check on target chip in restored build directory
    cache_path = os.path.join("build", "CMakeCache.txt")
    if os.path.exists(cache_path):
        old_target = None
        with open(cache_path, "r", encoding="utf-8", errors="ignore") as cf:
            for line in cf:
                if line.startswith("IDF_TARGET:STRING="):
                    old_target = line.split("=")[1].strip()
                    break
        new_target = None
        if os.path.exists("sdkconfig"):
            with open("sdkconfig", "r", encoding="utf-8", errors="ignore") as sf:
                for line in sf:
                    if line.startswith('CONFIG_IDF_TARGET="'):
                        new_target = line.split('"')[1].strip()
                        break
        if old_target and new_target and old_target != new_target:
            print(f"[setboard] Target chip mismatch in cached build ({old_target} != {new_target}), cleaning build/...")
            shutil.rmtree("build")


def setboard(board, clean=False):
    bpath = os.path.join(BOARD_PATH, board)
    if not os.path.exists(bpath):
        print(f"Unsupported board: {board}")
        print("Available boards:", ", ".join(board_list))
        sys.exit(-1)

    current_board = get_current_board()
    if current_board == board and not clean:
        print(f"Board '{board}' is already active.")
        update_dependencies(board)
        eboard_init = os.path.join(BOARD_PATH, board, "init.sh")
        if os.path.exists(eboard_init):
            shutil.copy2(eboard_init, "init.sh")
        with open(ACTIVE_BOARD_FILE, "w", encoding="utf-8") as f:
            f.write(board + "\n")
        return

    print(f"Switching board: {current_board or 'none'} -> {board}")

    if current_board:
        save_board_state(current_board)

    restore_board_state(board, clean=clean)
    update_dependencies(board)

    with open(ACTIVE_BOARD_FILE, "w", encoding="utf-8") as f:
        f.write(board + "\n")

    print(f"Active board set to '{board}'.")


def selectboard():
    clean = False
    args = [a for a in sys.argv[1:]]

    if "--clean" in args:
        clean = True
        args.remove("--clean")

    if len(args) > 0:
        arg = args[0]
        if arg in ("--list", "-l"):
            for b in board_list:
                print(b)
            return
        if arg == "--json":
            import json
            print(json.dumps(board_list))
            return
        board = arg
        setboard(board, clean=clean)
        return

    i = 1  # users prefer to count from 1
    for board in board_list:
        print(str(i) + ". " + board)
        i = i + 1

    try:
        selected_str = input(f"Enter your choice (1 - {i-1}): ").strip()
        if not selected_str:
            print("No selection made.")
            sys.exit(0)
        selected = int(selected_str)
        if selected < 1 or selected >= i:
            print("Invalid choice.")
            sys.exit(-1)
        setboard(board_list[selected - 1], clean=clean)
    except (ValueError, EOFError):
        print("Invalid input.")
        sys.exit(-1)


if __name__ == "__main__":
    ensureroot()
    loadboards()
    selectboard()
