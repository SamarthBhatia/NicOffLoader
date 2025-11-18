#!/usr/bin/env python3
"""Summarize host vs. NIC placement deltas for skewed DAG tiers."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, Iterable, Tuple

Key = Tuple[str, str, str]
PlacementMetrics = Dict[str, float]
TARGET_WORKLOADS = {"skew_dag", "skew_dag_heavy", "skew_dag_zipf14", "skew_dag_zipf18"}


def normalize_scale(raw: str) -> str:
    try:
        return f"{float(raw):.1f}"
    except (TypeError, ValueError):
        return raw


def load_rows(path: pathlib.Path) -> Iterable[Dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"placement CSV not found at {path}; run experiments/placement_baseline/run.py first.")
    with path.open() as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            yield row


def aggregate(rows: Iterable[Dict[str, str]]) -> Dict[Key, Dict[str, PlacementMetrics]]:
    grouped: Dict[Key, Dict[str, PlacementMetrics]] = defaultdict(dict)
    for row in rows:
        label = row.get("workload_label") or pathlib.Path(row["workload_path"]).stem
        if label not in TARGET_WORKLOADS:
            continue
        arrival_label = row.get("arrival_label", "")
        background = row.get("background_load", "")
        placement_mode = row["placement_mode"]
        grouped[(label, arrival_label, background)][placement_mode] = {
            "throughput_per_sec": float(row["throughput_per_sec"]),
            "mean_latency_us": float(row["mean_latency_us"]),
            "arrival_scale": normalize_scale(row.get("arrival_scale", "")),
        }
    return grouped


def write_summary(grouped: Dict[Key, Dict[str, PlacementMetrics]], output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "workload_label",
        "arrival_label",
        "background_load",
        "arrival_scale",
        "host_throughput_per_sec",
        "nic_throughput_per_sec",
        "throughput_delta_per_sec",
        "host_mean_latency_us",
        "nic_mean_latency_us",
        "latency_delta_us",
    ]
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for (label, arrival_label, background), placements in sorted(grouped.items()):
            host = placements.get("host_pinned")
            nic = placements.get("nic_pinned")
            if not host or not nic:
                continue
            host_t = host["throughput_per_sec"]
            nic_t = nic["throughput_per_sec"]
            host_mean = host["mean_latency_us"]
            nic_mean = nic["mean_latency_us"]
            scale = host.get("arrival_scale") or nic.get("arrival_scale") or ""
            writer.writerow(
                {
                    "workload_label": label,
                    "arrival_label": arrival_label,
                    "background_load": background,
                    "arrival_scale": scale,
                    "host_throughput_per_sec": f"{host_t:.2f}",
                    "nic_throughput_per_sec": f"{nic_t:.2f}",
                    "throughput_delta_per_sec": f"{nic_t - host_t:.2f}",
                    "host_mean_latency_us": f"{host_mean:.2f}",
                    "nic_mean_latency_us": f"{nic_mean:.2f}",
                    "latency_delta_us": f"{host_mean - nic_mean:.2f}",
                }
            )
    print(f"[skew-baselines] wrote {output}")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv")
    default_output = pathlib.Path("experiments/policy_baseline/results/skew_tier_baselines.csv")
    parser.add_argument("--placement-csv", type=pathlib.Path, default=default_csv, help="Source placement sweep CSV")
    parser.add_argument("--output", type=pathlib.Path, default=default_output, help="Destination summary CSV")
    args = parser.parse_args(list(argv) if argv is not None else None)

    rows = load_rows(args.placement_csv)
    grouped = aggregate(rows)
    write_summary(grouped, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
