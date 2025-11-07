#!/usr/bin/env python3
"""Plot throughput and mean latency vs. arrival scale for placement benchmark sweeps."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, Iterable, List, Tuple

try:
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit("matplotlib is required; install it via pip (e.g., pip install matplotlib).") from exc


Record = Dict[str, float]
GroupKey = Tuple[str, str, str]


def load_records(csv_path: pathlib.Path) -> Dict[GroupKey, List[Record]]:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV not found at {csv_path}; run the placement sweep first.")

    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            raise ValueError(f"{csv_path} is empty or missing the CSV header.")

        groups: Dict[GroupKey, List[Record]] = defaultdict(list)
        for row in reader:
            record: Record = {
                "arrival_scale": float(row["arrival_scale"]),
                "throughput": float(row["throughput_per_sec"]),
                "mean_latency": float(row["mean_latency_us"]),
                "latency_p95": float(row.get("latency_p95_us", row["mean_latency_us"])),
                "latency_p99": float(row.get("latency_p99_us", row["mean_latency_us"])),
                "queue_depth": float(row.get("peak_waiting_queue_depth", 0.0)),
            }
            placement = row.get("placement_mode", "hint_respect")
            key: GroupKey = (row["workload"], row["arrival_model"], placement)
            groups[key].append(record)

    for records in groups.values():
        records.sort(key=lambda rec: rec["arrival_scale"])
    return groups


def plot(groups: Dict[GroupKey, List[Record]], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), sharex=True)
    throughput_axis, mean_axis = axes[0]
    p95_axis, queue_axis = axes[1]

    for (workload, arrival_model, placement), records in sorted(groups.items()):
        scales = [rec["arrival_scale"] for rec in records]
        throughput = [rec["throughput"] for rec in records]
        mean_latency = [rec["mean_latency"] for rec in records]
        p95_latency = [rec["latency_p95"] for rec in records]
        queue_depth = [rec["queue_depth"] for rec in records]
        label = f"{workload} ({arrival_model}, {placement})"
        throughput_axis.plot(scales, throughput, marker="o", label=label)
        mean_axis.plot(scales, mean_latency, marker="o", label=label)
        p95_axis.plot(scales, p95_latency, marker="o", label=label)
        queue_axis.plot(scales, queue_depth, marker="o", label=label)

    throughput_axis.set_xlabel("arrival scale (× baseline rate)")
    throughput_axis.set_ylabel("throughput (tasks/s)")
    throughput_axis.grid(True, which="both", axis="both", linestyle="--", alpha=0.4)

    mean_axis.set_xlabel("arrival scale (× baseline rate)")
    mean_axis.set_ylabel("mean latency (us)")
    mean_axis.grid(True, which="both", axis="both", linestyle="--", alpha=0.4)

    p95_axis.set_xlabel("arrival scale (× baseline rate)")
    p95_axis.set_ylabel("p95 latency (us)")
    p95_axis.grid(True, which="both", axis="both", linestyle="--", alpha=0.4)

    queue_axis.set_xlabel("arrival scale (× baseline rate)")
    queue_axis.set_ylabel("peak waiting queue depth")
    queue_axis.grid(True, which="both", axis="both", linestyle="--", alpha=0.4)

    handles, labels = throughput_axis.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncols=len(labels))
    fig.suptitle("Placement benchmark sensitivity to arrival rate scaling")
    fig.tight_layout(rect=(0, 0.08, 1, 1))

    fig.savefig(output_path, dpi=200)
    print(f"[plots] wrote {output_path}")


def main() -> int:
    default_csv = pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv")
    default_output = pathlib.Path("plots/generated/placement_baseline.png")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Path to placement_benchmark CSV output")
    parser.add_argument("--output", type=pathlib.Path, default=default_output, help="Path to the rendered figure")
    args = parser.parse_args()

    groups = load_records(args.csv)
    plot(groups, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
