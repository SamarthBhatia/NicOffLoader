#!/usr/bin/env python3
"""Extract host- vs NIC-pinned placement traces for DAG workloads from placement_sweep.csv."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, List

TARGET_WORKLOADS = {"skew_dag", "skew_dag_heavy", "skew_dag_zipf14"}


def load_rows(csv_path: pathlib.Path) -> List[Dict[str, str]]:
    if not csv_path.exists():
        raise SystemExit(f"placement CSV not found: {csv_path}")
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        if not reader.fieldnames:
            raise SystemExit(f"{csv_path} is empty or missing header")
    return rows


def aggregate(rows: List[Dict[str, str]]) -> List[Dict[str, str]]:
    grouped: Dict[str, Dict[str, List[Dict[str, str]]]] = defaultdict(lambda: defaultdict(list))
    for row in rows:
        workload = row.get("workload_label") or pathlib.Path(row.get("workload_path", "")).stem
        if workload not in TARGET_WORKLOADS:
            continue
        mode = row.get("placement_mode", "hint_respect")
        grouped[workload][mode].append(row)

    summaries: List[Dict[str, str]] = []
    for workload, modes in grouped.items():
        for mode, entries in modes.items():
            count = len(entries)
            throughput = sum(float(entry["throughput_per_sec"]) for entry in entries) / count
            mean_latency = sum(float(entry["mean_latency_us"]) for entry in entries) / count
            p95_latency = sum(float(entry.get("latency_p95_us", entry["mean_latency_us"])) for entry in entries) / count
            summaries.append(
                {
                    "workload": workload,
                    "placement_mode": mode,
                    "runs": str(count),
                    "throughput_per_sec": f"{throughput:.2f}",
                    "mean_latency_us": f"{mean_latency:.2f}",
                    "p95_latency_us": f"{p95_latency:.2f}",
                }
            )
    summaries.sort(key=lambda row: (row["workload"], row["placement_mode"]))
    return summaries


def write_csv(rows: List[Dict[str, str]], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = ["workload", "placement_mode", "runs", "throughput_per_sec", "mean_latency_us", "p95_latency_us"]
    with output_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    print(f"[import] wrote {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--placement-csv",
        type=pathlib.Path,
        default=pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv"),
        help="Source placement sweep CSV",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("experiments/policy_baseline/results/dag_static_summary.csv"),
        help="Destination CSV for policy regression consumption",
    )
    args = parser.parse_args()

    rows = load_rows(args.placement_csv)
    summaries = aggregate(rows)
    if not summaries:
        print("[import] no matching DAG runs found; did you run the placement sweep?")
        return 0
    write_csv(summaries, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
