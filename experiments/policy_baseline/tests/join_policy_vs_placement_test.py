#!/usr/bin/env python3
"""Exercise join_placement.py on sample inputs and verify placement deltas."""

from __future__ import annotations

import csv
import pathlib
import subprocess
import sys
import tempfile
from typing import Dict


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
JOIN_SCRIPT = REPO_ROOT / "experiments" / "policy_baseline" / "join_placement.py"
SAMPLE_POLICY = SCRIPT_DIR / "data" / "policy_normalized_sample.csv"
SAMPLE_PLACEMENT = SCRIPT_DIR / "data" / "placement_sweep_sample.csv"

EXTRA_FIELDS = [
    "placement_host_throughput_per_sec",
    "placement_nic_throughput_per_sec",
    "placement_host_mean_latency_us",
    "placement_nic_mean_latency_us",
    "placement_host_p95_latency_us",
    "placement_nic_p95_latency_us",
    "policy_vs_host_throughput_delta_per_sec",
    "policy_vs_nic_throughput_delta_per_sec",
    "policy_vs_host_latency_delta_us",
    "policy_vs_nic_latency_delta_us",
]


def run_join(policy: pathlib.Path, placement: pathlib.Path, output_dir: pathlib.Path) -> pathlib.Path:
    if not JOIN_SCRIPT.exists():
        raise SystemExit(f"join_placement.py missing: {JOIN_SCRIPT}")
    output_csv = output_dir / "policy_vs_placement_sample.csv"
    output_parquet = output_dir / "policy_vs_placement_sample.parquet"
    cmd = [
        sys.executable,
        str(JOIN_SCRIPT),
        "--policy",
        str(policy),
        "--placement",
        str(placement),
        "--output-csv",
        str(output_csv),
        "--output-parquet",
        str(output_parquet),
    ]
    result = subprocess.run(cmd, check=True, capture_output=True, text=True, cwd=REPO_ROOT)
    stdout = result.stdout.lower()
    stderr = result.stderr.lower()
    if "[join] warning" in stdout or "[join] warning" in stderr:
        raise SystemExit(f"join_placement.py emitted a warning unexpectedly:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return output_csv


def load_rows(path: pathlib.Path) -> Dict[str, Dict[str, str]]:
    with path.open() as handle:
        reader = csv.DictReader(handle)
        rows = {row["run_name"]: row for row in reader}
    return rows


def check_dag_row(row: Dict[str, str]) -> None:
    expected = {
        "placement_host_throughput_per_sec": "600000.000000",
        "placement_nic_throughput_per_sec": "800000.000000",
        "placement_host_mean_latency_us": "2.000000",
        "placement_nic_mean_latency_us": "1.000000",
        "placement_host_p95_latency_us": "2.600000",
        "placement_nic_p95_latency_us": "1.800000",
        "policy_vs_host_throughput_delta_per_sec": "150000.000000",
        "policy_vs_nic_throughput_delta_per_sec": "-50000.000000",
        "policy_vs_host_latency_delta_us": "-0.500000",
        "policy_vs_nic_latency_delta_us": "0.500000",
    }
    for field, value in expected.items():
        actual = row.get(field, "")
        if actual != value:
            raise SystemExit(f"{field} expected {value} but saw {actual}")


def check_queue_flip_row(row: Dict[str, str]) -> None:
    for field in EXTRA_FIELDS:
        if row.get(field, "") not in {"", None}:
            raise SystemExit(f"{field} should be empty for queue-flip row (got {row.get(field)})")


def main() -> int:
    if not SAMPLE_POLICY.exists() or not SAMPLE_PLACEMENT.exists():
        raise SystemExit("sample inputs missing; re-run git checkout?")
    with tempfile.TemporaryDirectory() as tmpdir:
        output_dir = pathlib.Path(tmpdir)
        output_csv = run_join(SAMPLE_POLICY, SAMPLE_PLACEMENT, output_dir)
        rows = load_rows(output_csv)

    dag_row = rows.get("dag-sample")
    if not dag_row:
        raise SystemExit("dag-sample row missing from joined CSV")
    check_dag_row(dag_row)

    queue_row = rows.get("queue-flip-prefer-nic")
    if not queue_row:
        raise SystemExit("queue-flip-prefer-nic row missing from joined CSV")
    check_queue_flip_row(queue_row)

    print("[policy-join-placement] OK — sample policy rows merged with placement baselines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
