#!/usr/bin/env python3
"""Run the policy batch manifest and verify prefer-NIC policies behave as expected."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import subprocess
import sys
from typing import Dict, List, Tuple

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
DEFAULT_MANIFEST = REPO_ROOT / "experiments" / "policy_baseline" / "batch.yaml"
DEFAULT_CSV = REPO_ROOT / "experiments" / "policy_baseline" / "results" / "policy_baseline.csv"
DEFAULT_EXPECTED = SCRIPT_DIR / "data" / "skew_tier_baselines_expected.csv"
QUEUE_FLIP_WORKLOAD = REPO_ROOT / "workloads" / "tests" / "policy_queue_flip.yaml"
QUEUE_FLIP_PROFILE = REPO_ROOT / "profiles" / "bf2_default.yaml"
QUEUE_FLIP_OUTPUT_DIR = REPO_ROOT / "experiments" / "policy_baseline" / "results"
Key = Tuple[str, str, str]


def run_batch(cli_path: pathlib.Path, manifest_path: pathlib.Path, csv_path: pathlib.Path) -> None:
    if not cli_path.exists():
        raise SystemExit(f"nicloadoff_cli missing at {cli_path}; build the target first.")
    if csv_path.exists():
        csv_path.unlink()
    cmd = [str(cli_path), "--batch", str(manifest_path)]
    print(f"[policy-acceptance] running batch: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)
    if not csv_path.exists():
        raise SystemExit(f"batch run did not create {csv_path}")


ROLLING_POLICY_COLUMNS = (
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
)

ADMISSION_POLICY_COLUMNS = (
    "admission_limited_tasks",
    "admission_limit_active",
    "admission_limit_last",
)

REQUIRED_POLICY_COLUMNS = (
    "waiting_reorders",
    "waiting_reorders_per_task",
    "waiting_reorders_recent",
    "waiting_reorder_recent_task_count",
    "waiting_reorders_per_task_recent",
) + ADMISSION_POLICY_COLUMNS + ROLLING_POLICY_COLUMNS


def missing_policy_columns(path: pathlib.Path) -> List[str]:
    with path.open() as handle:
        reader = csv.reader(handle)
        header = next(reader, [])
    header = header or []
    missing = [column for column in REQUIRED_POLICY_COLUMNS if column not in header]
    return missing


def load_expected(path: pathlib.Path) -> Dict[Key, None]:
    if not path.exists():
        raise SystemExit(f"expected baseline table missing: {path}")
    expectations: Dict[Key, None] = {}
    with path.open() as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            key = (row["workload_label"], row["arrival_label"], row["background_load"])
            expectations[key] = None
    return expectations


def load_policy_rows(path: pathlib.Path) -> Tuple[List[Dict[str, str]], Dict[Key, Dict[str, Dict[str, float]]]]:
    with path.open() as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    grouped: Dict[Key, Dict[str, Dict[str, float]]] = {}
    for row in rows:
        placement = row.get("placement", "")
        if placement not in {"host", "nic"}:
            continue
        key = (row.get("workload_label", ""), row.get("arrival_label", ""), row.get("background_load", ""))
        grouped.setdefault(key, {})
        grouped[key][placement] = {
            "throughput_per_sec": float(row["throughput_per_sec"]),
            "mean_latency_us": float(row["mean_latency_us"]),
        }
    return rows, grouped


def ensure_queue_flip_reorders(rows: List[Dict[str, str]]) -> List[str]:
    failures: List[str] = []
    target = next((row for row in rows if row.get("run_name") == "queue-flip-prefer-nic"), None)
    if not target:
        failures.append("queue-flip-prefer-nic row missing from policy CSV")
        return failures
    waiting_reorders = float(target.get("waiting_reorders", 0.0) or 0.0)
    ratio_field = target.get("waiting_reorders_per_task")
    recent_ratio_field = target.get("waiting_reorders_per_task_recent")
    recent_window_field = target.get("waiting_reorder_recent_task_count")
    recent_reorders_field = target.get("waiting_reorders_recent")
    completed = float(target.get("completed_tasks", 0.0) or 0.0)
    ratio = waiting_reorders / completed if completed > 0.0 else 0.0
    if waiting_reorders <= 0.0:
        failures.append("queue-flip-prefer-nic waiting_reorders <= 0")
    if ratio <= 0.0:
        failures.append("queue-flip-prefer-nic waiting_reorders_per_task <= 0")
    if ratio_field is not None:
        ratio_value = float(ratio_field or 0.0)
        if ratio_value <= 0.0:
            failures.append("queue-flip-prefer-nic CSV waiting_reorders_per_task column <= 0")
    else:
        failures.append("queue-flip-prefer-nic missing waiting_reorders_per_task column value")
    if recent_ratio_field is not None:
        recent_ratio = float(recent_ratio_field or 0.0)
        if recent_ratio <= 0.0:
            failures.append("queue-flip-prefer-nic CSV waiting_reorders_per_task_recent column <= 0")
    else:
        failures.append("queue-flip-prefer-nic missing waiting_reorders_per_task_recent column value")
    if recent_window_field is None:
        failures.append("queue-flip-prefer-nic missing waiting_reorder_recent_task_count column value")
    else:
        window = float(recent_window_field or 0.0)
        if window <= 0.0:
            failures.append("queue-flip-prefer-nic waiting_reorder_recent_task_count <= 0")
    if recent_reorders_field is None:
        failures.append("queue-flip-prefer-nic missing waiting_reorders_recent column value")
    else:
        recent_reorders = float(recent_reorders_field or 0.0)
        if recent_reorders <= 0.0:
            failures.append("queue-flip-prefer-nic waiting_reorders_recent <= 0")
    return failures


def ensure_dsl_preset_metadata(rows: List[Dict[str, str]]) -> List[str]:
    failures: List[str] = []
    target = next((row for row in rows if row.get("run_name") == "dag-dsl-nic-balance"), None)
    if not target:
        failures.append("dag-dsl-nic-balance row missing from policy CSV")
        return failures
    preset = target.get("rolling_preset", "")
    if preset != "nic_util_clamp":
        failures.append(f"dag-dsl-nic-balance rolling_preset expected nic_util_clamp but saw '{preset}'")
    schedule_label = target.get("rolling_schedule_label", "")
    if schedule_label != "preset:nic_util_clamp":
        failures.append(
            f"dag-dsl-nic-balance rolling_schedule_label expected preset:nic_util_clamp but saw '{schedule_label}'"
        )
    try:
        event_count = float(target.get("rolling_window_event_count", 0.0) or 0.0)
    except ValueError:
        failures.append("dag-dsl-nic-balance rolling_window_event_count is not numeric")
        event_count = 0.0
    if event_count <= 0.0:
        failures.append("dag-dsl-nic-balance rolling_window_event_count <= 0 (preset never applied?)")
    event_log = target.get("rolling_window_event_log", "")
    if not event_log:
        failures.append("dag-dsl-nic-balance rolling_window_event_log is empty")
    return failures


def ensure_dsl_admission_metrics(rows: List[Dict[str, str]]) -> List[str]:
    failures: List[str] = []
    target = next((row for row in rows if row.get("run_name") == "dag-dsl-nic-balance"), None)
    if not target:
        failures.append("dag-dsl-nic-balance row missing from policy CSV (admission check)")
        return failures
    blocked_field = target.get("admission_limited_tasks")
    if blocked_field is None:
        failures.append("dag-dsl-nic-balance missing admission_limited_tasks column value")
    limit_value = target.get("admission_limit_last", "")
    if not limit_value:
        failures.append("dag-dsl-nic-balance missing admission_limit_last column value")
    else:
        try:
            limit = float(limit_value)
        except ValueError:
            failures.append(f"dag-dsl-nic-balance admission_limit_last not numeric: '{limit_value}'")
            limit = 0.0
        if limit < 0.0:
            failures.append(f"dag-dsl-nic-balance admission_limit_last < 0: {limit_value}")
    active_field = target.get("admission_limit_active")
    if active_field is None:
        failures.append("dag-dsl-nic-balance missing admission_limit_active column value")
    return failures


def run_acceptance(cli_path: pathlib.Path, manifest_path: pathlib.Path, csv_path: pathlib.Path, expected: pathlib.Path) -> int:
    run_batch(cli_path, manifest_path, csv_path)
    expectations = load_expected(expected)
    rows, actual = load_policy_rows(csv_path)
    failures = []

    missing_columns = missing_policy_columns(csv_path)
    if missing_columns:
        failures.append(f"policy CSV missing columns: {', '.join(missing_columns)}")

    for key in expectations.keys():
        placements = actual.get(key)
        if not placements:
            failures.append(f"{key} missing from policy CSV")
            continue
        host = placements.get("host")
        nic = placements.get("nic")
        if not host or not nic:
            failures.append(f"{key} missing host/nic runs in policy CSV")
            continue
        for placement_name, metrics in placements.items():
            if metrics["throughput_per_sec"] <= 0:
                failures.append(f"{key} placement '{placement_name}' reported non-positive throughput")
            if metrics["mean_latency_us"] <= 0:
                failures.append(f"{key} placement '{placement_name}' reported non-positive latency")

    for key in actual.keys():
        if key in expectations:
            continue
        workload_label = key[0]
        if workload_label.startswith("skew_dag"):
            failures.append(f"unexpected metadata group in policy CSV (update expectations?): {key}")

    failures.extend(verify_queue_flip(cli_path))
    failures.extend(ensure_queue_flip_reorders(rows))
    failures.extend(ensure_dsl_preset_metadata(rows))
    failures.extend(ensure_dsl_admission_metrics(rows))

    if failures:
        print("[policy_batch_acceptance] FAIL")
        for failure in failures:
            print("  -", failure)
        return 1
    print("[policy_batch_acceptance] OK — policy runs match placement baselines and queue-flip expectations")
    return 0


def load_json(path: pathlib.Path) -> Dict[str, object]:
    if not path.exists():
        raise SystemExit(f"expected JSON output missing: {path}")
    with path.open() as handle:
        return json.load(handle)


def run_queue_flip(cli_path: pathlib.Path, policy: str, output_name: str) -> Dict[str, object]:
    output_path = QUEUE_FLIP_OUTPUT_DIR / output_name
    cmd = [
        str(cli_path),
        "--profile",
        str(QUEUE_FLIP_PROFILE),
        "--workload",
        str(QUEUE_FLIP_WORKLOAD),
        "--policy",
        policy,
        "--output",
        str(output_path),
        "--seed",
        "1",
    ]
    print(f"[policy-acceptance] running queue flip scenario: {' '.join(cmd)}")
    subprocess.run(cmd, check=True, cwd=REPO_ROOT)
    return load_json(output_path)


def task_queue_time(report: Dict[str, object], task_id: int) -> float:
    for entry in report.get("tasks", []):
        if entry.get("task_id") == task_id:
            return float(entry.get("queue_time_us", 0.0))
    raise SystemExit(f"task_id {task_id} missing from report {report}")


def verify_queue_flip(cli_path: pathlib.Path) -> list[str]:
    failures: list[str] = []
    none_report = run_queue_flip(cli_path, "none", "policy_queue_flip_none.json")
    prefer_nic_report = run_queue_flip(cli_path, "prefer-nic", "policy_queue_flip_prefer_nic.json")
    adaptive_report = run_queue_flip(cli_path, "prefer-adaptive", "policy_queue_flip_prefer_adaptive.json")

    nic_task_id = 202
    host_task_id = 203
    nic_queue_none = task_queue_time(none_report, nic_task_id)
    nic_queue_policy = task_queue_time(prefer_nic_report, nic_task_id)
    host_queue_none = task_queue_time(none_report, host_task_id)
    host_queue_policy = task_queue_time(prefer_nic_report, host_task_id)

    if not nic_queue_policy < nic_queue_none:
        failures.append(
            f"prefer-nic policy did not reduce NIC-heavy task queue time "
            f"(none={nic_queue_none:.3f}us, prefer-nic={nic_queue_policy:.3f}us)"
        )
    if not host_queue_policy > host_queue_none:
        failures.append(
            f"prefer-nic policy did not defer host-heavy task "
            f"(none={host_queue_none:.3f}us, prefer-nic={host_queue_policy:.3f}us)"
        )

    waiting_reorders = prefer_nic_report.get("policy_metrics", {}).get("waiting_reorders", 0)
    if waiting_reorders < 1:
        failures.append("prefer-nic policy did not emit any waiting queue reorders in the heavy scenario")

    adaptive_nic_queue = task_queue_time(adaptive_report, nic_task_id)
    adaptive_host_queue = task_queue_time(adaptive_report, host_task_id)
    if not adaptive_nic_queue < nic_queue_none:
        failures.append(
            f"prefer-adaptive policy did not reduce NIC-heavy task queue time "
            f"(none={nic_queue_none:.3f}us, prefer-adaptive={adaptive_nic_queue:.3f}us)"
        )
    if not adaptive_host_queue > host_queue_none:
        failures.append(
            f"prefer-adaptive policy did not defer host-heavy task "
            f"(none={host_queue_none:.3f}us, prefer-adaptive={adaptive_host_queue:.3f}us)"
        )
    adaptive_reorders = adaptive_report.get("policy_metrics", {}).get("waiting_reorders", 0)
    if adaptive_reorders < 1:
        failures.append("prefer-adaptive policy did not emit any waiting queue reorders in the heavy scenario")

    return failures


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True, help="Path to nicloadoff_cli binary")
    parser.add_argument("--manifest", type=pathlib.Path, default=DEFAULT_MANIFEST, help="Batch manifest path")
    parser.add_argument("--csv", type=pathlib.Path, default=DEFAULT_CSV, help="Policy batch CSV output path")
    parser.add_argument(
        "--expected",
        type=pathlib.Path,
        default=DEFAULT_EXPECTED,
        help="Expected skew-tier baseline table",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    return run_acceptance(args.cli, args.manifest, args.csv, args.expected)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
