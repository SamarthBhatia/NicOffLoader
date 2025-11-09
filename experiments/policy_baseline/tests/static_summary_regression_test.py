#!/usr/bin/env python3
"""Regression test guarding the host-vs-NIC placement deltas for each skew tier."""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys
from dataclasses import dataclass
from typing import Dict, Tuple

SCRIPT_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.append(str(SCRIPT_DIR))

from calc_skew_baselines import aggregate, load_rows  # noqa: E402

TierKey = Tuple[str, str, str]


@dataclass(frozen=True)
class TierExpectation:
    arrival_scale: str
    throughput_delta: float
    latency_delta: float


def load_expected(path: pathlib.Path) -> Dict[TierKey, TierExpectation]:
    if not path.exists():
        raise SystemExit(f"expected baseline table missing: {path}")
    expectations: Dict[TierKey, TierExpectation] = {}
    with path.open() as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            key: TierKey = (row["workload_label"], row["arrival_label"], row["background_load"])
            expectations[key] = TierExpectation(
                arrival_scale=row["arrival_scale"],
                throughput_delta=float(row["throughput_delta_per_sec"]),
                latency_delta=float(row["latency_delta_us"]),
            )
    return expectations


def within(actual: float, expected: float, rel_tol: float = 0.01, abs_tol: float = 1e-3) -> bool:
    return abs(actual - expected) <= max(abs_tol, abs(expected) * rel_tol)


def run_regression(placement_csv: pathlib.Path, expected_csv: pathlib.Path) -> int:
    rows = list(load_rows(placement_csv))
    actual = aggregate(rows)
    expectations = load_expected(expected_csv)
    failures = []

    for key, expectation in expectations.items():
        placements = actual.get(key)
        if not placements:
            failures.append(f"{key[0]}:{key[1]}:{key[2]} missing from placement CSV")
            continue
        host = placements.get("host_pinned")
        nic = placements.get("nic_pinned")
        if not host or not nic:
            failures.append(f"{key[0]}:{key[1]}:{key[2]} missing host/nic placements")
            continue
        scale = host.get("arrival_scale") or nic.get("arrival_scale") or ""
        if scale != expectation.arrival_scale:
            failures.append(
                f"{key[0]}:{key[1]}:{key[2]} arrival_scale {scale} != expected {expectation.arrival_scale}"
            )
        throughput_delta = nic["throughput_per_sec"] - host["throughput_per_sec"]
        latency_delta = host["mean_latency_us"] - nic["mean_latency_us"]
        if throughput_delta <= 0:
            failures.append(f"{key[0]}:{key[1]}:{key[2]} NIC throughput <= host throughput ({throughput_delta:.2f})")
        elif not within(throughput_delta, expectation.throughput_delta):
            failures.append(
                f"{key[0]}:{key[1]}:{key[2]} throughput delta {throughput_delta:.2f} "
                f"!= expected {expectation.throughput_delta:.2f}"
            )
        if latency_delta <= 0:
            failures.append(f"{key[0]}:{key[1]}:{key[2]} host latency <= NIC latency ({latency_delta:.3f})")
        elif not within(latency_delta, expectation.latency_delta, rel_tol=0.02, abs_tol=1e-2):
            failures.append(
                f"{key[0]}:{key[1]}:{key[2]} latency delta {latency_delta:.3f} "
                f"!= expected {expectation.latency_delta:.3f}"
            )

    missing_keys = [key for key in actual.keys() if key not in expectations]
    if missing_keys:
        for key in missing_keys:
            failures.append(f"unexpected tier present in placement CSV: {key}")

    if failures:
        print("[policy_static_summary] FAIL")
        for failure in failures:
            print("  -", failure)
        return 1
    print("[policy_static_summary] OK — placement deltas match recorded tiers")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv")
    default_expected = pathlib.Path(__file__).resolve().parent / "data" / "skew_tier_baselines_expected.csv"
    parser.add_argument("--placement-csv", type=pathlib.Path, default=default_csv)
    parser.add_argument("--expected", type=pathlib.Path, default=default_expected)
    args = parser.parse_args()
    return run_regression(args.placement_csv, args.expected)


if __name__ == "__main__":
    raise SystemExit(main())
