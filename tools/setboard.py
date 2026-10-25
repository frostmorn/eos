#!/usr/bin/python
import os
import shutil
import sys

# Path to directory containing boards
BOARD_PATH = "main/eboard"

board_list = []
board_target_map = {
    "lilka_v23": "EOS_TARGET_LILKAV2",
    "jc4880p443c_i_w_y": "EOS_TARGET_JC4880P443C_I_W_Y",
    "esp32s3_qemu": "EOS_TARGET_ESP32S3_QEMU"
}

def rm(file):
    if os.path.exists(file):
        os.remove(file)

def ensureroot():
    root_files = ["Makefile", "CMakeLists.txt", "main"]
    candidate_files = os.listdir()
    for rfile in root_files:
        if not(rfile in candidate_files):
            print("ERROR: Should be run from project root dir")
            exit(-1)

def loadboards():
    files = os.listdir(BOARD_PATH)
    for file in files:
        cur = BOARD_PATH+"/"+file
        if os.path.isdir(cur) and "sdkconfig" in os.listdir(cur):
            board_list.append(file)
    board_list.sort()

def update_pregen(board):
    target_macro = board_target_map.get(board, f"EOS_TARGET_{board.upper()}")
    pregen_path = "main/pregen.h"
    if not os.path.exists(pregen_path):
        print(f"WARNING: {pregen_path} not found, skipping pregen update.")
        return

    with open(pregen_path, "r") as f:
        content = f.read()

    lines = content.splitlines()
    new_lines = []
    found_target = False
    for line in lines:
        if line.startswith("#define EOS_TARGET_"):
            if target_macro:
                new_lines.append(f"#define {target_macro}")
                found_target = True
            else:
                new_lines.append(line)
        else:
            new_lines.append(line)

    if not found_target and target_macro:
        inserted = False
        for i, line in enumerate(new_lines):
            if "#include <soc/soc_caps.h>" in line:
                new_lines.insert(i + 1, f"\n#define {target_macro}")
                inserted = True
                break
        if not inserted:
            new_lines.append(f"#define {target_macro}")

    with open(pregen_path, "w") as f:
        f.write("\n".join(new_lines) + "\n")
    print(f"Updated {pregen_path} with target macro {target_macro}")

def setboard(board):
    bpath = BOARD_PATH + "/" + board

    # Ensure openable
    if not(os.path.exists(bpath)):
        print(f"Unsupported board: {board}")
        print("Available boards:", ", ".join(board_list))
        exit(-1)

    print(f"Setting board {board}")

    rm("sdkconfig")
    rm("sdkconfig.old")

    shutil.copy(bpath+"/sdkconfig", ".")

def selectboard():
    if len(sys.argv) > 1:
        arg = sys.argv[1]
        if arg in ("--list", "-l"):
            for b in board_list:
                print(b)
            return
        if arg == "--json":
            import json
            print(json.dumps(board_list))
            return
        board = arg
        setboard(board)
        return

    i = 1 # users prefer to count from 1
    for board in board_list:
        print(str(i) + ". " + board)
        i = i + 1

    try:
        selected_str = input(f"Enter your choice (1 - {i-1}): ").strip()
        if not selected_str:
            print("No selection made.")
            exit(0)
        selected = int(selected_str)
        if selected < 1 or selected >= i:
            print("Invalid choice.")
            exit(-1)
        setboard(board_list[selected-1])
    except (ValueError, EOFError):
        print("Invalid input.")
        exit(-1)

ensureroot()
loadboards()
selectboard()
