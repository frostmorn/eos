#!/usr/bin/env python3
"""EOS Static & Runtime Memory Budget Audit.

Checks build/eos.map against tools/mem_baseline.json and fails if internal SRAM
(DIRAM / DRAM + IRAM) grows by more than 512 bytes (configurable), or if runtime
PSRAM drops by more than 10 KB / internal SRAM drops by more than 512 bytes.
"""

import argparse
import json
import os
import subprocess
import sys

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE_PATH = os.path.join(ROOT_DIR, "tools", "mem_baseline.json")
ACTIVE_BOARD_PATH = os.path.join(ROOT_DIR, ".active_board")
DEFAULT_MAP_PATH = os.path.join(ROOT_DIR, "build", "eos.map")

DEFAULT_SRAM_THRESHOLD_BYTES = 512
DEFAULT_PSRAM_THRESHOLD_BYTES = 10 * 1024


def detect_board(cli_board: str | None) -> str:
    if cli_board:
        return cli_board
    if os.path.exists(ACTIVE_BOARD_PATH):
        with open(ACTIVE_BOARD_PATH, "r", encoding="utf-8") as f:
            board = f.read().strip()
            if board:
                return board
    raise RuntimeError("Cannot determine active board; pass --board <name>.")


def read_map_sizes(map_path: str) -> dict[str, int]:
    if not os.path.exists(map_path):
        raise FileNotFoundError(f"Map file not found: {map_path}")
    res = subprocess.run(
        [sys.executable, "-m", "esp_idf_size", "--format", "json2", map_path],
        capture_output=True,
        text=True,
        check=True,
    )
    data = json.loads(res.stdout)
    layout_list = data.get("layout", [])
    by_name = {
        entry["name"]: int(entry.get("used", 0))
        for entry in layout_list
        if isinstance(entry, dict) and "name" in entry
    }

    diram_used = sum(by_name.get(k, 0) for k in ("DIRAM", "DRAM", "IRAM"))
    flash_used = sum(
        by_name.get(k, 0) for k in ("Flash", "Flash Code", "Flash Data", "DROM", "IROM")
    )

    return {
        "internal_sram_bytes": diram_used,
        "flash_bytes": flash_used,
    }


def load_baselines() -> dict:
    if not os.path.exists(BASELINE_PATH):
        return {}
    with open(BASELINE_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def save_baselines(data: dict) -> None:
    with open(BASELINE_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="EOS Memory Budget Audit")
    parser.add_argument("--board", help="Target board name (default: .active_board)")
    parser.add_argument("--map", default=DEFAULT_MAP_PATH, help="Path to build/eos.map")
    parser.add_argument(
        "--sram-threshold",
        type=int,
        default=DEFAULT_SRAM_THRESHOLD_BYTES,
        help="Max allowed internal SRAM growth in bytes (default: 512)",
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="Update baseline for the target board with current measurements",
    )
    parser.add_argument(
        "--internal-free-kb",
        type=int,
        default=None,
        help="Optional runtime InternalFree (kB) from /proc/meminfo",
    )
    parser.add_argument(
        "--spiram-free-kb",
        type=int,
        default=None,
        help="Optional runtime SPIRAMFree (kB) from /proc/meminfo",
    )
    parser.add_argument(
        "--psram-threshold-kb",
        type=int,
        default=DEFAULT_PSRAM_THRESHOLD_BYTES // 1024,
        help="Max allowed runtime PSRAM drop in kB (default: 10)",
    )
    args = parser.parse_args()

    board = detect_board(args.board)
    current = read_map_sizes(args.map)
    baselines = load_baselines()

    if args.update:
        entry = baselines.get(board, {})
        entry["internal_sram_bytes"] = current["internal_sram_bytes"]
        entry["flash_bytes"] = current["flash_bytes"]
        if args.internal_free_kb is not None:
            entry["runtime_internal_free_kb"] = args.internal_free_kb
        if args.spiram_free_kb is not None:
            entry["runtime_spiram_free_kb"] = args.spiram_free_kb
        baselines[board] = entry
        save_baselines(baselines)
        print(
            f"[mem_audit] Updated baseline for '{board}': "
            f"internal_sram={current['internal_sram_bytes']} B, "
            f"flash={current['flash_bytes']} B"
        )
        return 0

    if board not in baselines:
        print(
            f"[mem_audit] No baseline recorded for '{board}'. "
            f"Run: python3 tools/mem_audit.py --board {board} --update"
        )
        return 1

    base = baselines[board]
    sram_delta = current["internal_sram_bytes"] - base["internal_sram_bytes"]
    flash_delta = current["flash_bytes"] - base["flash_bytes"]
    print(
        f"[mem_audit] {board}: internal_sram={current['internal_sram_bytes']} B "
        f"(delta {sram_delta:+d} B, limit +{args.sram_threshold} B), "
        f"flash={current['flash_bytes']} B (delta {flash_delta:+d} B)"
    )

    failed = False
    if sram_delta > args.sram_threshold:
        print(
            f"[FAIL] Internal SRAM regression on '{board}': "
            f"+{sram_delta} B exceeds +{args.sram_threshold} B budget!"
        )
        failed = True

    if (
        args.internal_free_kb is not None
        and "runtime_internal_free_kb" in base
    ):
        drop_bytes = (base["runtime_internal_free_kb"] - args.internal_free_kb) * 1024
        if drop_bytes > args.sram_threshold:
            print(
                f"[FAIL] Runtime InternalFree dropped by {drop_bytes} B "
                f"(limit {args.sram_threshold} B)"
            )
            failed = True

    if (
        args.spiram_free_kb is not None
        and "runtime_spiram_free_kb" in base
    ):
        drop_kb = base["runtime_spiram_free_kb"] - args.spiram_free_kb
        if drop_kb > args.psram_threshold_kb:
            print(
                f"[FAIL] Runtime SPIRAMFree dropped by {drop_kb} kB "
                f"(limit {args.psram_threshold_kb} kB)"
            )
            failed = True

    if failed:
        return 1

    print(f"[PASS] Memory budget check passed for '{board}'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
