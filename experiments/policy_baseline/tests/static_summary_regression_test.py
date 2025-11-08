#!/usr/bin/env python3
"""Regression test guarding the host-vs-NIC placement deltas for DAG workloads."""

from __future__ import annotations

import argparse
import csv
import pathlib
from dataclasses import dataclass
from typing import Dict, Tuple


@dataclass(frozen=True)
class Expectations:
    min_throughput_delta: float
    min_latency_delta: float


EXPECTED: Dict[str, Expectations] = {
    "skew_dag": Expectations(min_throughput_delta=4_000.0, min_latency_delta=0.1),
    "skew_dag_heavy": Expectations(min_throughput_delta=4_000.0, min_latency_delta=0.1),
    "skew_dag_zipf14": Expectations(min_throughput_delta=12_000.0, min_latency_delta=0.3),
    "skew_dag_zipf18": Expectations(min_throughput_delta=18_000.0, min_latency_delta=0.5),
}

PLACEMENTS = ("host_pinned", "nic_pinned")


def load_summary(path: pathlib.Path) -> Dict[Tuple[str, str], Dict[str, float]]:
    if not path.exists():
        raise SystemExit(f"dag static summary missing: {path}")
    with path.open() as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    metrics: Dict[Tuple[str, str], Dict[str, float]] = {}
    for row in rows:
        key = (row["workload"], row["placement_mode"])
        metrics[key] = {
            "throughput_per_sec": float(row["throughput_per_sec"]),
            "mean_latency_us": float(row["mean_latency_us"]),
            "p95_latency_us": float(row.get("p95_latency_us", row["mean_latency_us"])),
        }
    return metrics


def run_regression(summary_path: pathlib.Path) -> int:
    metrics = load_summary(summary_path)
    failures = []
    for workload, expectation in EXPECTED.items():
        missing = [placement for placement in PLACEMENTS if (workload, placement) not in metrics]
        if missing:
            failures.append(f"{workload}: missing placements {', '.join(missing)}")
            continue
        host = metrics[(workload, "host_pinned")]
        nic = metrics[(workload, "nic_pinned")]
        throughput_delta = nic["throughput_per_sec"] - host["throughput_per_sec"]
        latency_delta = host["mean_latency_us"] - nic["mean_latency_us"]
        if throughput_delta <= 0:
            failures.append(f"{workload}: NIC throughput {nic['throughput_per_sec']:.2f} <= host "
                            f"{host['throughput_per_sec']:.2f}")
        elif throughput_delta < expectation.min_throughput_delta:
            failures.append(f"{workload}: throughput delta {throughput_delta:.2f} < "
                            f"{expectation.min_throughput_delta:.2f}")
        if latency_delta <= 0:
            failures.append(f"{workload}: host latency {host['mean_latency_us']:.2f} <= NIC "
                            f"{nic['mean_latency_us']:.2f}")
        elif latency_delta < expectation.min_latency_delta:
            failures.append(f"{workload}: latency delta {latency_delta:.3f} < "
                            f"{expectation.min_latency_delta:.3f}")
    if failures:
        print("[policy_static_summary] FAIL")
        for failure in failures:
            print("  -", failure)
        return 1
    print("[policy_static_summary] OK — host vs NIC deltas healthy")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_summary = pathlib.Path(__file__).resolve().parents[1] / "results" / "dag_static_summary.csv"
    parser.add_argument("--summary", type=pathlib.Path, default=default_summary)
    args = parser.parse_args()
    return run_regression(args.summary)


if __name__ == "__main__":
    raise SystemExit(main())
