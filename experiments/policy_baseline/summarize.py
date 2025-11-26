#!/usr/bin/env python3
"""Summarize policy_baseline batch results."""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import List, Dict, Tuple

BASE_COLUMNS = {
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
}
NUMERIC_METRICS = [
    ("throughput", "throughput_per_sec"),
    ("mean_latency", "mean_latency_us"),
    ("p95_latency", "p95_latency_us"),
    ("p99_latency", "p99_latency_us"),
]
PEAK_FIELD = ("peak_q", "peak_waiting_queue_depth")
WAITING_FIELD = ("reorders", "waiting_reorders")
RATIO_FIELD = ("reorders_per_task", "waiting_reorders_per_task")
RECENT_WAITING_FIELD = ("reorders_recent", "waiting_reorders_recent")
RECENT_RATIO_FIELD = ("reorders_per_task_recent", "waiting_reorders_per_task_recent")
RECENT_WINDOW_FIELD = ("recent_window", "waiting_reorder_recent_task_count")
ADMISSION_BLOCKS_FIELD = ("admission_blocked", "admission_limited_tasks")
ADMISSION_LIMIT_FIELD = ("admission_limit", "admission_limit_last")
ADMISSION_ACTIVE_FIELD = ("admission_active", "admission_limit_active")
ROLLING_COLUMNS = [
    ("queue_avg", "rolling_queue_average"),
    ("queue_peak", "rolling_queue_peak"),
    ("host_util_avg", "rolling_host_util_average"),
    ("nic_util_avg", "rolling_nic_util_average"),
    ("sojourn_p95", "rolling_sojourn_p95_latency_us"),
    ("sojourn_p99", "rolling_sojourn_p99_latency_us"),
]
DEFAULT_METADATA_COLUMNS = [
    "arrival_label",
    "background_load",
    "zipf_alpha",
    "rolling_preset",
    "rolling_schedule_label",
]


def compute_reorder_ratio(row: Dict[str, str]) -> float:
    if ratio := row.get(RATIO_FIELD[1]):
        try:
            return float(ratio)
        except ValueError:
            return 0.0
    waiting = float(row.get(WAITING_FIELD[1], 0.0) or 0.0)
    completed = float(row.get("completed_tasks", 0.0) or 0.0)
    return waiting / completed if completed > 0.0 else 0.0


def compute_recent_reorder_ratio(row: Dict[str, str]) -> float:
    if ratio := row.get(RECENT_RATIO_FIELD[1]):
        try:
            return float(ratio)
        except ValueError:
            return 0.0
    waiting_recent = float(row.get(RECENT_WAITING_FIELD[1], 0.0) or 0.0)
    window = float(row.get(RECENT_WINDOW_FIELD[1], 0.0) or 0.0)
    return waiting_recent / window if window > 0.0 else 0.0


def get_float(row: Dict[str, str], field: str) -> float | None:
    value = row.get(field)
    if value in (None, ""):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def coerce_float(value: object, default: float = 0.0) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if value in (None, ""):
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default

def load_rows(csv_path: pathlib.Path) -> List[Dict[str, str]]:
    if not csv_path.exists():
        raise SystemExit(f"CSV not found: {csv_path}")
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def apply_filters(rows: List[Dict[str, str]], filters: List[Tuple[str, str]]) -> List[Dict[str, str]]:
    if not filters:
        return rows
    filtered = []
    for row in rows:
        if all(row.get(key, "") == value for key, value in filters):
            filtered.append(row)
    return filtered


def format_table(rows: List[Dict[str, str]], extra_columns: List[str]) -> str:
    headers = ["run_name", "policy"] + [label for label, _ in NUMERIC_METRICS]
    headers += [
        PEAK_FIELD[0],
        WAITING_FIELD[0],
        RATIO_FIELD[0],
        RECENT_WAITING_FIELD[0],
        RECENT_RATIO_FIELD[0],
        RECENT_WINDOW_FIELD[0],
        ADMISSION_BLOCKS_FIELD[0],
        ADMISSION_LIMIT_FIELD[0],
        ADMISSION_ACTIVE_FIELD[0],
    ]
    headers += [label for label, _ in ROLLING_COLUMNS]
    headers += extra_columns
    formatted = []
    for row in rows:
        entry = {
            "run_name": row["run_name"],
            "policy": row["policy"],
            PEAK_FIELD[0]: row[PEAK_FIELD[1]],
        }
        for label, field in NUMERIC_METRICS:
            entry[label] = f"{float(row[field]):.2f}"
        entry[WAITING_FIELD[0]] = row.get(WAITING_FIELD[1], "")
        entry[RATIO_FIELD[0]] = f"{compute_reorder_ratio(row):.4f}"
        entry[RECENT_WAITING_FIELD[0]] = row.get(RECENT_WAITING_FIELD[1], "")
        entry[RECENT_RATIO_FIELD[0]] = f"{compute_recent_reorder_ratio(row):.4f}"
        entry[RECENT_WINDOW_FIELD[0]] = row.get(RECENT_WINDOW_FIELD[1], "")
        entry[ADMISSION_BLOCKS_FIELD[0]] = row.get(ADMISSION_BLOCKS_FIELD[1], "")
        entry[ADMISSION_LIMIT_FIELD[0]] = row.get(ADMISSION_LIMIT_FIELD[1], "")
        entry[ADMISSION_ACTIVE_FIELD[0]] = normalize_bool(row.get(ADMISSION_ACTIVE_FIELD[1]))
        for label, field in ROLLING_COLUMNS:
            value = get_float(row, field)
            entry[label] = f"{value:.3f}" if value is not None else ""

        for column in extra_columns:
            entry[column] = row.get(column, "")
        formatted.append(entry)
    widths = {h: max(len(h), *(len(entry[h]) for entry in formatted)) for h in headers}

    def render_row(entry: Dict[str, str]) -> str:
        return " | ".join(entry[h].ljust(widths[h]) for h in headers)

    header_line = " | ".join(h.ljust(widths[h]) for h in headers)
    divider = "-+-".join("-" * widths[h] for h in headers)
    body = "\n".join(render_row(entry) for entry in formatted)
    return f"{header_line}\n{divider}\n{body}"


def format_grouped_table(rows: List[Dict[str, str]],
                         group_by: str,
                         extra_columns: List[str]) -> str:
    headers = (
        [group_by, "count"]
        + [label for label, _ in NUMERIC_METRICS]
        + [
            PEAK_FIELD[0],
            WAITING_FIELD[0],
            RATIO_FIELD[0],
            RECENT_WAITING_FIELD[0],
            RECENT_RATIO_FIELD[0],
            RECENT_WINDOW_FIELD[0],
            ADMISSION_BLOCKS_FIELD[0],
            ADMISSION_LIMIT_FIELD[0],
            ADMISSION_ACTIVE_FIELD[0],
        ]
        + [label for label, _ in ROLLING_COLUMNS]
        + extra_columns
    )
    widths = {h: len(h) for h in headers}

    def format_value(value: str, header: str) -> str:
        widths[header] = max(widths[header], len(value))
        return value

    lines = []
    for row in rows:
        formatted_row = {
            group_by: row[group_by],
            "count": str(row["count"]),
        }
        for label, field in NUMERIC_METRICS:
            formatted_row[label] = f"{row[field]:.2f}"
        formatted_row[PEAK_FIELD[0]] = f"{row[PEAK_FIELD[0]]:.2f}"
        formatted_row[WAITING_FIELD[0]] = f"{row[WAITING_FIELD[0]]:.2f}"
        formatted_row[RATIO_FIELD[0]] = f"{row[RATIO_FIELD[0]]:.4f}"
        formatted_row[RECENT_WAITING_FIELD[0]] = f"{row[RECENT_WAITING_FIELD[0]]:.2f}"
        formatted_row[RECENT_RATIO_FIELD[0]] = f"{row[RECENT_RATIO_FIELD[0]]:.4f}"
        formatted_row[RECENT_WINDOW_FIELD[0]] = f"{row[RECENT_WINDOW_FIELD[0]]:.2f}"
        formatted_row[ADMISSION_BLOCKS_FIELD[0]] = f"{row[ADMISSION_BLOCKS_FIELD[0]]:.2f}"
        formatted_row[ADMISSION_LIMIT_FIELD[0]] = f"{row[ADMISSION_LIMIT_FIELD[0]]:.2f}"
        formatted_row[ADMISSION_ACTIVE_FIELD[0]] = f"{row[ADMISSION_ACTIVE_FIELD[0]]:.2f}"
        for label, field in ROLLING_COLUMNS:
            value = coerce_float(row.get(field))
            formatted_row[label] = f"{value:.3f}"
        for column in extra_columns:
            formatted_row[column] = row.get(column, "")
        for key, value in formatted_row.items():
            formatted_row[key] = format_value(value, key)
        lines.append(formatted_row)

    header_line = " | ".join(h.ljust(widths[h]) for h in headers)
    divider = "-+-".join("-" * widths[h] for h in headers)
    body = "\n".join(" | ".join(row[h].ljust(widths[h]) for h in headers) for row in lines)
    return f"{header_line}\n{divider}\n{body}"


def aggregate_rows(rows: List[Dict[str, str]],
                   group_by: str,
                   extra_columns: List[str]) -> List[Dict[str, str]]:
    grouped: Dict[str, List[Dict[str, str]]] = {}
    for row in rows:
        grouped.setdefault(row[group_by], []).append(row)
    aggregated_rows: List[Dict[str, str]] = []
    for key, entries in grouped.items():
        aggregate: Dict[str, str] = {group_by: key, "count": len(entries)}
        for label, field in NUMERIC_METRICS:
            aggregate[field] = sum(float(entry[field]) for entry in entries) / len(entries)
        aggregate[PEAK_FIELD[0]] = sum(float(entry[PEAK_FIELD[1]]) for entry in entries) / len(entries)
        waiting_values = [float(entry.get(WAITING_FIELD[1], 0.0) or 0.0) for entry in entries]
        aggregate[WAITING_FIELD[0]] = sum(waiting_values) / len(entries) if entries else 0.0
        total_tasks = sum(float(entry.get("completed_tasks", 0.0) or 0.0) for entry in entries)
        total_reorders = sum(waiting_values)
        aggregate[RATIO_FIELD[0]] = total_reorders / total_tasks if total_tasks > 0.0 else 0.0
        recent_waiting_values = [float(entry.get(RECENT_WAITING_FIELD[1], 0.0) or 0.0) for entry in entries]
        aggregate[RECENT_WAITING_FIELD[0]] = sum(recent_waiting_values) / len(entries) if entries else 0.0
        total_recent_tasks = sum(float(entry.get(RECENT_WINDOW_FIELD[1], 0.0) or 0.0) for entry in entries)
        total_recent_reorders = sum(recent_waiting_values)
        aggregate[RECENT_RATIO_FIELD[0]] = total_recent_reorders / total_recent_tasks if total_recent_tasks > 0.0 else 0.0
        aggregate[RECENT_WINDOW_FIELD[0]] = total_recent_tasks / len(entries) if entries else 0.0
        blocks = [float(entry.get(ADMISSION_BLOCKS_FIELD[1], 0.0) or 0.0) for entry in entries]
        aggregate[ADMISSION_BLOCKS_FIELD[0]] = sum(blocks) / len(entries) if entries else 0.0
        limits = [float(entry.get(ADMISSION_LIMIT_FIELD[1], 0.0) or 0.0) for entry in entries]
        aggregate[ADMISSION_LIMIT_FIELD[0]] = sum(limits) / len(entries) if entries else 0.0
        active_flags = [
            1.0 if str(entry.get(ADMISSION_ACTIVE_FIELD[1], "")).strip().lower() in {"true", "1", "yes"} else 0.0
            for entry in entries
        ]
        aggregate[ADMISSION_ACTIVE_FIELD[0]] = sum(active_flags) / len(entries) if entries else 0.0
        for _, field in ROLLING_COLUMNS:
            values = [float(entry.get(field, 0.0) or 0.0) for entry in entries]
            aggregate[field] = sum(values) / len(entries) if entries else 0.0
        for column in extra_columns:
            values = {entry.get(column, "") for entry in entries if entry.get(column, "")}
            if not values:
                aggregate[column] = ""
            elif len(values) == 1:
                aggregate[column] = values.pop()
            else:
                aggregate[column] = "mixed"
        aggregated_rows.append(aggregate)
    aggregated_rows.sort(key=lambda row: row[group_by])
    return aggregated_rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/policy_baseline/results/policy_baseline.csv")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Path to batch CSV")
    parser.add_argument("--sort", choices=["throughput", "mean_latency"], default="throughput",
                        help="Sort rows by a metric (descending for throughput, ascending for latency)")
    parser.add_argument("--filter", action="append", default=[], help="Filter rows by key=value (can repeat)")
    parser.add_argument("--group-by", help="Column to group by (e.g., workload_label, arrival_model)")
    parser.add_argument(
        "--columns",
        nargs="*",
        default=[],
        help="Additional columns to show (arrival_label/zipf_alpha are included automatically when present)",
    )
    args = parser.parse_args()

    rows = load_rows(args.csv)
    if not rows:
        print("CSV is empty.")
        return 0

    available_columns = set(rows[0].keys())
    available_columns = set(rows[0].keys())
    for column in args.columns:
        if column not in available_columns:
            raise SystemExit(f"Unknown column requested via --columns: {column}")
    extra_columns = list(args.columns)
    seen_columns = set(extra_columns)
    for column in DEFAULT_METADATA_COLUMNS:
        if column in available_columns and column not in seen_columns:
            extra_columns.append(column)
            seen_columns.add(column)
    filters: List[Tuple[str, str]] = []
    for raw in args.filter:
        if "=" not in raw:
            raise SystemExit(f"Invalid filter '{raw}'. Use key=value syntax.")
        key, value = raw.split("=", 1)
        if key not in available_columns:
            raise SystemExit(f"Filter column '{key}' not found in CSV.")
        filters.append((key, value))

    rows = apply_filters(rows, filters)
    if not rows:
        print("No rows match the provided filters.")
        return 0

    if args.group_by:
        if args.group_by not in available_columns:
            raise SystemExit(f"group-by column '{args.group_by}' not found in CSV.")
        aggregated = aggregate_rows(rows, args.group_by, extra_columns)
        print(format_grouped_table(aggregated, args.group_by, extra_columns))
        return 0

    if args.sort == "throughput":
        rows.sort(key=lambda r: float(r["throughput_per_sec"]), reverse=True)
    else:
        rows.sort(key=lambda r: float(r["mean_latency_us"]))

    print(format_table(rows, extra_columns))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
def normalize_bool(value: str | None) -> str:
    if value is None or value == "":
        return ""
    lower = value.strip().lower()
    if lower in {"1", "true", "yes"}:
        return "yes"
    if lower in {"0", "false", "no"}:
        return "no"
    return value
