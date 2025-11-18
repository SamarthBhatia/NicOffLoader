#!/usr/bin/env python3
"""Exercise the policy DSL with the queue-flip workload."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

def run_cli(cli: pathlib.Path,
            profile: pathlib.Path,
            workload: pathlib.Path,
            config: pathlib.Path,
            policy: str,
            extra_args: list[str]) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        output = pathlib.Path(tmpdir) / f"{policy}.json"
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
        ]
        cmd.extend(extra_args)
        subprocess.run(cmd, check=True)
        with output.open() as handle:
            return json.load(handle)


def queue_time(report: dict, task_id: int) -> float:
    for task in report.get("tasks", []):
        if task.get("task_id") == task_id:
            return float(task.get("queue_time_us", 0.0))
    raise SystemExit(f"task {task_id} missing from report")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, required=True)
    parser.add_argument("--workload", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)

    baseline = run_cli(args.cli, args.profile, args.workload, args.config, "none", [])
    dsl_report = run_cli(args.cli,
                         args.profile,
                         args.workload,
                         args.config,
                         "dsl",
                         ["--policy-config", str(args.config)])

    nic_task = queue_time(baseline, 202)
    nic_task_dsl = queue_time(dsl_report, 202)
    host_task = queue_time(baseline, 201)
    host_task_dsl = queue_time(dsl_report, 201)

    if not nic_task_dsl < nic_task:
        raise SystemExit("DSL policy failed to reduce NIC-heavy queue time")
    if not host_task_dsl > host_task:
        raise SystemExit("DSL policy failed to defer host-heavy task")

    policy_metrics = dsl_report.get("policy_metrics", {})
    if float(policy_metrics.get("waiting_reorders", 0)) <= 0:
        raise SystemExit("DSL policy did not emit waiting reorders")

    print("[policy-dsl] OK — DSL policy reorders queue-flip scenario")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
