#!/usr/bin/env python3
"""Verify prefer-adaptive reorders under a host-saturation burst."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def run_cli(
    cli: pathlib.Path,
    profile: pathlib.Path,
    workload: pathlib.Path,
    policy: str,
    extra_args: list[str] | None = None,
) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        output = pathlib.Path(tmpdir) / f"{policy}_report.json"
        cmd = [
            str(cli),
            "--profile",
            str(profile),
            "--workload",
            str(workload),
            "--output",
            str(output),
            "--policy",
            policy,
            "--seed",
            "1",
        ]
        if extra_args:
            cmd.extend(extra_args)
        subprocess.run(cmd, check=True)
        with output.open() as handle:
            return json.load(handle)


def queue_time_us(report: dict, task_id: int) -> float:
    for task in report.get("tasks", []):
        if task.get("task_id") == task_id:
            return float(task.get("queue_time_us", 0.0))
    raise SystemExit(f"Task {task_id} missing from report")


def verify_reorder(report: dict, label: str) -> None:
    metrics = report.get("policy_metrics", {})
    reorders = float(metrics.get("waiting_reorders", 0.0))
    if reorders <= 0.0:
        raise SystemExit(f"{label} scenario: prefer-adaptive did not emit any waiting reorders")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, default=pathlib.Path("profiles/bf2_default.yaml").resolve())
    parser.add_argument(
        "--host-workload",
        type=pathlib.Path,
        default=pathlib.Path("workloads/tests/prefer_adaptive_host_burst.yaml").resolve(),
    )
    args = parser.parse_args(argv)

    rolling_args = ["--rolling-queue-window-us", "10", "--rolling-util-window-us", "10"]

    baseline_host = run_cli(args.cli, args.profile, args.host_workload, "none", rolling_args)
    adaptive_host = run_cli(args.cli, args.profile, args.host_workload, "prefer-adaptive", rolling_args)
    host_baseline_queue = queue_time_us(baseline_host, 403)
    host_adaptive_queue = queue_time_us(adaptive_host, 403)
    if host_adaptive_queue <= host_baseline_queue:
        raise SystemExit(
            f"Host burst scenario: host-heavy task was not deferred "
            f"(baseline={host_baseline_queue:.3f}us, prefer-adaptive={host_adaptive_queue:.3f}us)"
        )
    verify_reorder(adaptive_host, "host burst")

    print("[prefer-adaptive] OK — policy responds to the host burst")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
