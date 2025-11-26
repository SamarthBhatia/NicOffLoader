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
    "waiting_reorders",
    "waiting_reorders_per_task",
    "waiting_reorders_recent",
    "waiting_reorder_recent_task_count",
    "waiting_reorders_per_task_recent",
    "admission_limited_tasks",
    "admission_limit_active",
    "admission_limit_last",
    "output_path",
    "arrival_model",
    "background_load",
    "placement",
    "workload_label",
    "rolling_queue_samples",
    "rolling_queue_latest",
    "rolling_queue_average",
    "rolling_queue_peak",
    "rolling_host_util_samples",
    "rolling_host_util_latest",
    "rolling_host_util_average",
    "rolling_host_util_peak",
    "rolling_nic_util_samples",
    "rolling_nic_util_latest",
    "rolling_nic_util_average",
    "rolling_nic_util_peak",
    "rolling_sojourn_samples",
    "rolling_sojourn_mean_queue_us",
    "rolling_sojourn_mean_service_us",
    "rolling_sojourn_mean_latency_us",
    "rolling_sojourn_p95_latency_us",
    "rolling_sojourn_p99_latency_us",
    "rolling_window_event_count",
    "rolling_window_event_log",
    "rolling_preset",
    "rolling_schedule_label",
]

NUMERIC_FIELDS = [
    "completed_tasks",
    "makespan_us",
    "throughput_per_sec",
    "mean_latency_us",
    "p95_latency_us",
    "p99_latency_us",
    "peak_waiting_queue_depth",
    "waiting_reorders",
    "waiting_reorders_per_task",
    "waiting_reorders_recent",
    "waiting_reorder_recent_task_count",
    "waiting_reorders_per_task_recent",
    "admission_limited_tasks",
    "admission_limit_last",
    "rolling_queue_samples",
    "rolling_queue_latest",
    "rolling_queue_average",
    "rolling_queue_peak",
    "rolling_host_util_samples",
    "rolling_host_util_latest",
    "rolling_host_util_average",
    "rolling_host_util_peak",
    "rolling_nic_util_samples",
    "rolling_nic_util_latest",
    "rolling_nic_util_average",
    "rolling_nic_util_peak",
    "rolling_sojourn_samples",
    "rolling_sojourn_mean_queue_us",
    "rolling_sojourn_mean_service_us",
    "rolling_sojourn_mean_latency_us",
    "rolling_sojourn_p95_latency_us",
    "rolling_sojourn_p99_latency_us",
    "rolling_window_event_count",
]

BASELINE_COLUMNS = [
    "baseline_host_throughput_per_sec",
    "baseline_nic_throughput_per_sec",
    "baseline_throughput_delta_per_sec",
    "baseline_host_mean_latency_us",
    "baseline_nic_mean_latency_us",
    "baseline_latency_delta_us",
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


def load_static_summary(path: pathlib.Path) -> Dict[str, Dict[str, float]]:
    if not path.exists():
        print(f"[export] static summary not found at {path}; skipping baseline annotations")
        return {}
    with path.open() as handle:
        reader = csv.DictReader(handle)
        summaries: Dict[str, Dict[str, Dict[str, float]]] = {}
        for row in reader:
            workload = row["workload"]
            mode = row["placement_mode"]
            summaries.setdefault(workload, {})[mode] = {
                "throughput_per_sec": float(row["throughput_per_sec"]),
                "mean_latency_us": float(row["mean_latency_us"]),
            }
    baseline: Dict[str, Dict[str, float]] = {}
    for workload, modes in summaries.items():
        host = modes.get("host_pinned")
        nic = modes.get("nic_pinned")
        if not host or not nic:
            continue
        baseline[workload] = {
            "host_throughput": host["throughput_per_sec"],
            "nic_throughput": nic["throughput_per_sec"],
            "throughput_delta": nic["throughput_per_sec"] - host["throughput_per_sec"],
            "host_latency": host["mean_latency_us"],
            "nic_latency": nic["mean_latency_us"],
            "latency_delta": host["mean_latency_us"] - nic["mean_latency_us"],
        }
    if not baseline:
        print(f"[export] no host/nic placements found in {path}; skipping baseline annotations")
    return baseline


def attach_baseline(row: Dict[str, str], baseline: Dict[str, Dict[str, float]]) -> None:
    for column in BASELINE_COLUMNS:
        row[column] = ""
    if not baseline:
        return
    label = row.get("workload_label")
    if not label:
        return
    data = baseline.get(label)
    if not data:
        return
    row["baseline_host_throughput_per_sec"] = f"{data['host_throughput']:.2f}"
    row["baseline_nic_throughput_per_sec"] = f"{data['nic_throughput']:.2f}"
    row["baseline_throughput_delta_per_sec"] = f"{data['throughput_delta']:.2f}"
    row["baseline_host_mean_latency_us"] = f"{data['host_latency']:.2f}"
    row["baseline_nic_mean_latency_us"] = f"{data['nic_latency']:.2f}"
    row["baseline_latency_delta_us"] = f"{data['latency_delta']:.3f}"


def aggregate_rows(rows: List[Dict[str, str]],
                   metadata_columns: List[str],
                   baseline: Dict[str, Dict[str, float]]) -> List[Dict[str, str]]:
    grouped: Dict[str, List[Dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["run_name"]].append(row)

    aggregated_rows: List[Dict[str, str]] = []
    for run_name, entries in grouped.items():
        aggregate: Dict[str, str] = {}
        for key in BASE_COLUMNS:
            if key in NUMERIC_FIELDS:
                continue
            aggregate[key] = entries[0].get(key, "")
        aggregate["run_name"] = run_name
        count = float(len(entries))
        numeric_avgs: Dict[str, float] = {}
        for field in NUMERIC_FIELDS:
            value = sum(float(entry.get(field, 0.0) or 0.0) for entry in entries) / count
            numeric_avgs[field] = value
            aggregate[field] = f"{value:.6f}"
        total_tasks = sum(float(entry.get("completed_tasks", 0.0) or 0.0) for entry in entries)
        total_reorders = sum(float(entry.get("waiting_reorders", 0.0) or 0.0) for entry in entries)
        per_task = (total_reorders / total_tasks) if total_tasks > 0.0 else 0.0
        aggregate["waiting_reorders_per_task"] = f"{per_task:.6f}"
        total_recent_reorders = sum(float(entry.get("waiting_reorders_recent", 0.0) or 0.0) for entry in entries)
        total_recent_tasks = sum(float(entry.get("waiting_reorder_recent_task_count", 0.0) or 0.0) for entry in entries)
        recent_ratio = (total_recent_reorders / total_recent_tasks) if total_recent_tasks > 0.0 else 0.0
        aggregate["waiting_reorders_per_task_recent"] = f"{recent_ratio:.6f}"
        for column in metadata_columns:
            value = next((entry[column] for entry in entries if entry.get(column)), "")
            aggregate[column] = value
        attach_baseline(aggregate, baseline)
        aggregated_rows.append(aggregate)
    aggregated_rows.sort(key=lambda row: row["run_name"])
    return aggregated_rows


def write_csv(rows: List[Dict[str, str]],
              metadata_columns: List[str],
              extra_columns: List[str],
              output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = BASE_COLUMNS + metadata_columns + extra_columns
    with output_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})
    print(f"[export] wrote {output_path}")


def write_parquet(rows: List[Dict[str, str]],
                  metadata_columns: List[str],
                  extra_columns: List[str],
                  output_path: pathlib.Path) -> None:
    if not HAVE_PARQUET:
        print("[export] pyarrow not installed; skipping Parquet export")
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    columns = BASE_COLUMNS + metadata_columns + extra_columns
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
    default_summary = pathlib.Path("experiments/policy_baseline/results/dag_static_summary.csv")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Source CSV path")
    parser.add_argument("--output-csv", type=pathlib.Path, default=default_out_csv, help="Normalized CSV output path")
    parser.add_argument(
        "--output-parquet", type=pathlib.Path, default=default_out_parquet, help="Normalized Parquet output path"
    )
    parser.add_argument(
        "--static-summary",
        type=pathlib.Path,
        default=default_summary,
        help="Optional static placement summary to annotate baseline host-vs-NIC deltas",
    )
    args = parser.parse_args()

    rows, metadata_columns = load_rows(args.csv)
    baseline = load_static_summary(args.static_summary)
    aggregates = aggregate_rows(rows, metadata_columns, baseline)
    baseline_columns = BASELINE_COLUMNS if baseline else []
    write_csv(aggregates, metadata_columns, baseline_columns, args.output_csv)
    write_parquet(aggregates, metadata_columns, baseline_columns, args.output_parquet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
