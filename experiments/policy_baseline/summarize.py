#!/usr/bin/env python3
"""Summarize policy_baseline batch results."""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import List, Dict


def load_rows(csv_path: pathlib.Path) -> List[Dict[str, str]]:
    if not csv_path.exists():
        raise SystemExit(f"CSV not found: {csv_path}")
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def format_table(rows: List[Dict[str, str]]) -> str:
    headers = ["run_name", "policy", "throughput", "mean_latency", "p95_latency", "p99_latency", "peak_q"]
    formatted = []
    for row in rows:
        formatted.append({
            "run_name": row["run_name"],
            "policy": row["policy"],
            "throughput": f'{float(row["throughput_per_sec"]):.2f}',
            "mean_latency": f'{float(row["mean_latency_us"]):.2f}',
            "p95_latency": f'{float(row["p95_latency_us"]):.2f}',
            "p99_latency": f'{float(row["p99_latency_us"]):.2f}',
            "peak_q": row["peak_waiting_queue_depth"],
        })
    widths = {h: max(len(h), *(len(entry[h]) for entry in formatted)) for h in headers}

    def render_row(entry: Dict[str, str]) -> str:
        return " | ".join(entry[h].ljust(widths[h]) for h in headers)

    header_line = " | ".join(h.ljust(widths[h]) for h in headers)
    divider = "-+-".join("-" * widths[h] for h in headers)
    body = "\n".join(render_row(entry) for entry in formatted)
    return f"{header_line}\n{divider}\n{body}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/policy_baseline/results/policy_baseline.csv")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Path to batch CSV")
    parser.add_argument("--sort", choices=["throughput", "mean_latency"], default="throughput",
                        help="Sort rows by a metric (descending for throughput, ascending for latency)")
    args = parser.parse_args()

    rows = load_rows(args.csv)
    if not rows:
        print("CSV is empty.")
        return 0

    if args.sort == "throughput":
        rows.sort(key=lambda r: float(r["throughput_per_sec"]), reverse=True)
    else:
        rows.sort(key=lambda r: float(r["mean_latency_us"]))

    print(format_table(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
