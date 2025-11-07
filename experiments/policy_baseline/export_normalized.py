#!/usr/bin/env python3
"""Normalize policy baseline CSV rows (group duplicates) and emit CSV/Parquet tables."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, List, Tuple

try:
    import pyarrow as pa
    import pyarrow.parquet as pq

    HAVE_PARQUET = True
except ImportError:  # pragma: no cover - optional dependency
    HAVE_PARQUET = False

BASE_COLUMNS = [
    "run_name",
    "profile",
    "workload",
    "policy",
    "seed",
    "host_mode",
    "nic_mode",
    "completed_tasks",
    "makespan_us",
    "throughput_per_sec",
    "mean_latency_us",
    "p95_latency_us",
    "p99_latency_us",
    "peak_waiting_queue_depth",
    "output_path",
]

NUMERIC_FIELDS = [
    "completed_tasks",
    "makespan_us",
    "throughput_per_sec",
    "mean_latency_us",
    "p95_latency_us",
    "p99_latency_us",
    "peak_waiting_queue_depth",
]


def load_rows(csv_path: pathlib.Path) -> Tuple[List[Dict[str, str]], List[str]]:
    if not csv_path.exists():
        raise SystemExit(f"CSV not found at {csv_path}. Run the batch sweep first.")
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        if not reader.fieldnames:
            raise SystemExit(f"{csv_path} is empty.")
        metadata_columns = [col for col in reader.fieldnames if col not in BASE_COLUMNS]
        return rows, metadata_columns


def aggregate_rows(rows: List[Dict[str, str]], metadata_columns: List[str]) -> List[Dict[str, str]]:
    grouped: Dict[str, List[Dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["run_name"]].append(row)

    aggregated_rows: List[Dict[str, str]] = []
    for run_name, entries in grouped.items():
        aggregate: Dict[str, str] = {key: entries[0][key] for key in BASE_COLUMNS if key not in NUMERIC_FIELDS}
        aggregate["run_name"] = run_name
        count = float(len(entries))
        for field in NUMERIC_FIELDS:
            aggregate[field] = f"{sum(float(entry[field]) for entry in entries) / count:.6f}"
        for column in metadata_columns:
            value = next((entry[column] for entry in entries if entry.get(column)), "")
            aggregate[column] = value
        aggregated_rows.append(aggregate)
    aggregated_rows.sort(key=lambda row: row["run_name"])
    return aggregated_rows


def write_csv(rows: List[Dict[str, str]], metadata_columns: List[str], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = BASE_COLUMNS + metadata_columns
    with output_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})
    print(f"[export] wrote {output_path}")


def write_parquet(rows: List[Dict[str, str]], metadata_columns: List[str], output_path: pathlib.Path) -> None:
    if not HAVE_PARQUET:
        print("[export] pyarrow not installed; skipping Parquet export")
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    columns = BASE_COLUMNS + metadata_columns
    arrays = {}
    for column in columns:
        values = [row.get(column, "") for row in rows]
        if column in NUMERIC_FIELDS:
            arrays[column] = pa.array([float(value) for value in values])
        else:
            arrays[column] = pa.array(values)
    table = pa.Table.from_pydict(arrays)
    pq.write_table(table, output_path)
    print(f"[export] wrote {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/policy_baseline/results/policy_baseline.csv")
    default_out_csv = pathlib.Path("experiments/policy_baseline/results/policy_baseline_normalized.csv")
    default_out_parquet = pathlib.Path("experiments/policy_baseline/results/policy_baseline.parquet")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Source CSV path")
    parser.add_argument("--output-csv", type=pathlib.Path, default=default_out_csv, help="Normalized CSV output path")
    parser.add_argument(
        "--output-parquet", type=pathlib.Path, default=default_out_parquet, help="Normalized Parquet output path"
    )
    args = parser.parse_args()

    rows, metadata_columns = load_rows(args.csv)
    aggregates = aggregate_rows(rows, metadata_columns)
    write_csv(aggregates, metadata_columns, args.output_csv)
    write_parquet(aggregates, metadata_columns, args.output_parquet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
