#!/usr/bin/env python3
"""Ensure the skew-tier baseline table in the README mirrors calc_skew_baselines output."""

from __future__ import annotations

import csv
import pathlib
import sys
from typing import Dict, List, Tuple

POLICY_BASELINE_DIR = pathlib.Path(__file__).resolve().parents[1]
REPO_ROOT = POLICY_BASELINE_DIR.parents[1]
README_PATH = POLICY_BASELINE_DIR / "README.md"
EXPECTED_CSV = POLICY_BASELINE_DIR / "tests" / "data" / "skew_tier_baselines_expected.csv"

TABLE_HEADER_PREFIX = "| Workload"


def parse_table(readme: pathlib.Path) -> Dict[Tuple[str, str, str], Dict[str, float]]:
    if not readme.exists():
        raise SystemExit(f"README not found at {readme}")
    lines = readme.read_text().splitlines()
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(TABLE_HEADER_PREFIX) and "Host Tput" in line:
            start = idx
            break
    if start is None:
        raise SystemExit("Unable to locate skew-tier baseline table header in README.md")
    data_lines: List[str] = []
    for line in lines[start + 2 :]:
        stripped = line.strip()
        if not stripped.startswith("|"):
            break
        if set(stripped) <= {"|", "-"}:
            continue
        data_lines.append(stripped)
    table: Dict[Tuple[str, str, str], Dict[str, float]] = {}
    for line in data_lines:
        cells = [cell.strip() for cell in line.split("|")[1:-1]]
        if len(cells) != 10:
            raise SystemExit(f"Unexpected column count ({len(cells)}) in row: {line}")
        workload = cells[0].strip("`")
        arrival = cells[1]
        background = cells[2]
        key = (workload, arrival, background)
        table[key] = {
            "scale": cells[3],
            "host_kops": float(cells[4]),
            "nic_kops": float(cells[5]),
            "delta_kops": float(cells[6].lstrip("+")),
            "host_mean": float(cells[7]),
            "nic_mean": float(cells[8]),
            "delta_mean": float(cells[9].lstrip("+")),
        }
    return table


def load_expected(csv_path: pathlib.Path) -> Dict[Tuple[str, str, str], Dict[str, float]]:
    if not csv_path.exists():
        raise SystemExit(f"Expected baseline CSV missing: {csv_path}")
    table: Dict[Tuple[str, str, str], Dict[str, float]] = {}
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            key = (row["workload_label"], row["arrival_label"], row["background_load"])
            table[key] = {
                "scale": row["arrival_scale"],
                "host_kops": float(row["host_throughput_per_sec"]) / 1000.0,
                "nic_kops": float(row["nic_throughput_per_sec"]) / 1000.0,
                "delta_kops": float(row["throughput_delta_per_sec"]) / 1000.0,
                "host_mean": float(row["host_mean_latency_us"]),
                "nic_mean": float(row["nic_mean_latency_us"]),
                "delta_mean": float(row["latency_delta_us"]),
            }
    return table


def close_enough(actual: float, expected: float, tolerance: float) -> bool:
    return abs(actual - expected) <= tolerance


def main() -> int:
    readme_rows = parse_table(README_PATH)
    expected_rows = load_expected(EXPECTED_CSV)
    failures: List[str] = []
    for key, expected in expected_rows.items():
        doc_row = readme_rows.get(key)
        if not doc_row:
            failures.append(f"{key} missing from README table")
            continue
        if doc_row["scale"] != expected["scale"]:
            failures.append(f"{key} scale {doc_row['scale']} != expected {expected['scale']}")
        for column, tolerance in (
            ("host_kops", 0.2),
            ("nic_kops", 0.2),
            ("delta_kops", 0.2),
            ("host_mean", 0.01),
            ("nic_mean", 0.01),
            ("delta_mean", 0.02),
        ):
            if not close_enough(doc_row[column], expected[column], tolerance):
                failures.append(
                    f"{key} {column} {doc_row[column]:.3f} != expected {expected[column]:.3f} (tol {tolerance})"
                )
    for key in readme_rows:
        if key not in expected_rows:
            failures.append(f"{key} present in README but missing from expected CSV")
    if failures:
        print("[skew_table_readme] FAIL")
        for failure in failures:
            print("  -", failure)
        return 1
    print("[skew_table_readme] OK — README table matches skew_tier_baselines CSV")
    return 0


if __name__ == "__main__":
    sys.exit(main())
