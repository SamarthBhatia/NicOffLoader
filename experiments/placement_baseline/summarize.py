#!/usr/bin/env python3
"""Summarize placement_sweep.csv with metadata columns surfaced by default."""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import Dict, List, Tuple

DEFAULT_METADATA_COLUMNS = ["arrival_label", "background_load", "zipf_alpha"]
NUMERIC_METRICS = [
    ("throughput", "throughput_per_sec"),
    ("mean_latency", "mean_latency_us"),
    ("p95_latency", "latency_p95_us"),
    ("p99_latency", "latency_p99_us"),
]
PEAK_FIELD = ("peak_q", "peak_waiting_queue_depth")


def load_rows(csv_path: pathlib.Path) -> List[Dict[str, str]]:
    if not csv_path.exists():
        raise SystemExit(f"CSV not found at {csv_path}. Run the placement sweep first.")
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def apply_filters(rows: List[Dict[str, str]], filters: List[Tuple[str, str]]) -> List[Dict[str, str]]:
    if not filters:
        return rows
    filtered: List[Dict[str, str]] = []
    for row in rows:
        if all(row.get(key, "") == value for key, value in filters):
            filtered.append(row)
    return filtered


def resolve_metadata_columns(requested: List[str], available: Dict[str, None]) -> List[str]:
    columns = list(requested)
    seen = set(columns)
    for column in DEFAULT_METADATA_COLUMNS:
        if column in available and column not in seen:
            columns.append(column)
            seen.add(column)
    return columns


def format_table(rows: List[Dict[str, str]], extra_columns: List[str]) -> str:
    headers = ["workload", "arrival_model", "placement", "scale"] + [label for label, _ in NUMERIC_METRICS]
    headers.append(PEAK_FIELD[0])
    headers += extra_columns
    widths = {header: len(header) for header in headers}
    formatted: List[Dict[str, str]] = []
    for row in rows:
        entry = {
            "workload": row.get("workload_label") or row.get("workload", ""),
            "arrival_model": row.get("arrival_model", ""),
            "placement": row.get("placement_mode", ""),
            "scale": f"{float(row.get('arrival_scale', 0.0)):.2f}",
            PEAK_FIELD[0]: row.get(PEAK_FIELD[1], ""),
        }
        for label, field in NUMERIC_METRICS:
            entry[label] = f"{float(row.get(field, 0.0)):.2f}"
        for column in extra_columns:
            entry[column] = row.get(column, "")
        for key, value in entry.items():
            widths[key] = max(widths[key], len(value))
        formatted.append(entry)

    def render(entry: Dict[str, str]) -> str:
        return " | ".join(entry[h].ljust(widths[h]) for h in headers)

    header_line = " | ".join(h.ljust(widths[h]) for h in headers)
    divider = "-+-".join("-" * widths[h] for h in headers)
    body = "\n".join(render(entry) for entry in formatted)
    return f"{header_line}\n{divider}\n{body}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Path to placement_sweep.csv")
    parser.add_argument("--sort",
                        choices=["throughput", "mean_latency", "arrival_scale"],
                        default="throughput",
                        help="Metric to sort rows by (descending for throughput, ascending otherwise)")
    parser.add_argument("--filter", action="append", default=[], help="Filter rows via key=value (repeatable)")
    parser.add_argument(
        "--columns",
        nargs="*",
        default=[],
        help="Additional metadata columns to display (arrival_label/zipf_alpha included automatically when present)",
    )
    args = parser.parse_args()

    rows = load_rows(args.csv)
    if not rows:
        print("CSV is empty.")
        return 0

    available_columns = {key: None for key in rows[0].keys()}
    for column in args.columns:
        if column not in available_columns:
            raise SystemExit(f"Unknown column requested via --columns: {column}")

    filters: List[Tuple[str, str]] = []
    for raw in args.filter:
        if "=" not in raw:
            raise SystemExit(f"Invalid filter '{raw}'. Use key=value.")
        key, value = raw.split("=", 1)
        if key not in available_columns:
            raise SystemExit(f"Filter column '{key}' not found in CSV.")
        filters.append((key, value))

    rows = apply_filters(rows, filters)
    if not rows:
        print("No rows match the provided filters.")
        return 0

    extra_columns = resolve_metadata_columns(args.columns, available_columns)
    if args.sort == "throughput":
        rows.sort(key=lambda r: float(r.get("throughput_per_sec", 0.0)), reverse=True)
    elif args.sort == "arrival_scale":
        rows.sort(key=lambda r: float(r.get("arrival_scale", 0.0)))
    else:
        rows.sort(key=lambda r: float(r.get("mean_latency_us", 0.0)))

    print(format_table(rows, extra_columns))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
