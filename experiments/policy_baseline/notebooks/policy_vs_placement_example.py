#!/usr/bin/env python3
"""Simple table slice over the joined policy-vs-placement dataset (CSV or Parquet)."""

from __future__ import annotations

import argparse
import pathlib
import sys
try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit("pandas is required; install via `python3 -m pip install pandas`.") from exc


def parse_args() -> argparse.Namespace:
    repo_root = pathlib.Path(__file__).resolve().parents[3]
    default_path = repo_root / "experiments" / "policy_baseline" / "results" / "policy_vs_placement.parquet"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=pathlib.Path, default=default_path, help="Joined policy-vs-placement CSV/Parquet")
    parser.add_argument("--workload-label", default="skew_dag", help="Filter by workload_label (empty to disable)")
    parser.add_argument("--arrival-label", default="", help="Optional arrival_label filter (empty to disable)")
    return parser.parse_args()


def load_table(path: pathlib.Path) -> pd.DataFrame:
    if not path.exists():
        raise SystemExit(f"Joined table not found at {path}. Run join_placement.py first.")
    suffix = path.suffix.lower()
    if suffix == ".parquet":
        return pd.read_parquet(path)
    if suffix == ".csv":
        return pd.read_csv(path)
    raise SystemExit(f"Unsupported input format: {path.suffix}")


def filter_table(df: pd.DataFrame, workload_label: str, arrival_label: str) -> pd.DataFrame:
    mask = pd.Series(True, index=df.index, dtype=bool)
    if workload_label:
        mask &= df["workload_label"] == workload_label
    if arrival_label:
        mask &= df["arrival_label"] == arrival_label
    subset = df[mask].copy()
    if subset.empty:
        raise SystemExit("No rows matched the provided filters.")
    subset = subset.sort_values(["policy", "run_name"])
    return subset


def print_table(df: pd.DataFrame) -> None:
    columns = [
        "run_name",
        "policy",
        "arrival_label",
        "throughput_per_sec",
        "mean_latency_us",
        "placement_host_throughput_per_sec",
        "placement_nic_throughput_per_sec",
        "policy_vs_host_throughput_delta_per_sec",
        "policy_vs_nic_throughput_delta_per_sec",
        "placement_host_mean_latency_us",
        "placement_nic_mean_latency_us",
        "policy_vs_host_latency_delta_us",
        "policy_vs_nic_latency_delta_us",
    ]
    present = [col for col in columns if col in df.columns]
    print(df[present].to_string(index=False))


def main(argv: list[str] | None = None) -> int:
    args = parse_args()
    df = load_table(args.input)
    subset = filter_table(df, args.workload_label, args.arrival_label)
    print_table(subset)
    return 0


if __name__ == "__main__":
    sys.exit(main())
