#!/usr/bin/env python3
"""Exercise the skew_dag_nic_balance DSL example (reorder + admission clamp)."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def run_cli(cli: pathlib.Path, profile: pathlib.Path, workload: pathlib.Path, policy: str, output: pathlib.Path) -> dict:
    subprocess.run(
        [
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
        ],
        check=True,
    )
    with output.open() as handle:
        return json.load(handle)


def run_manifest(cli: pathlib.Path, profile: pathlib.Path, workload: pathlib.Path, config: pathlib.Path) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)
        manifest = tmpdir_path / "skew_dag_nic_manifest.yaml"
        output = tmpdir_path / "skew_dag_nic_report.json"
        manifest.write_text(
            "\n".join(
                [
                    f"profile: {profile}",
                    f"workload: {workload}",
                    "policy: dsl",
                    f"policy_config: {config}",
                    f"output: {output}",
                    "metadata:",
                    "  workload_label: skew_dag",
                    "  arrival_label: stress",
                    "  background_load: heavy",
                ]
            )
        )
        subprocess.run([str(cli), "--config", str(manifest)], check=True)
        with output.open() as handle:
            return json.load(handle)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, required=True)
    parser.add_argument("--workload", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)
        baseline = run_cli(
            args.cli,
            args.profile,
            args.workload,
            "none",
            tmpdir_path / "baseline_nic_stress.json",
        )

    report = run_manifest(args.cli, args.profile, args.workload, args.config)
    metrics = report.get("policy_metrics", {})
    waiting = float(metrics.get("waiting_reorders", 0.0) or 0.0)
    if waiting <= 0.0:
        raise SystemExit("skew_dag_nic_balance did not produce waiting reorders")
    if float(metrics.get("waiting_reorders_per_task", 0.0) or 0.0) <= 0.0:
        raise SystemExit("skew_dag_nic_balance missing per-task reorder ratio")
    admission_blocks = float(metrics.get("admission_limited_tasks", 0.0) or 0.0)
    if admission_blocks <= 0.0:
        raise SystemExit("skew_dag_nic_balance did not block any tasks via admission control")
    admission_limit = metrics.get("admission_limit_last")
    if admission_limit is None:
        raise SystemExit("skew_dag_nic_balance missing admission_limit_last metric")
    if int(admission_limit) != 3:
        raise SystemExit(f"Expected admission limit of 3 but saw {admission_limit}")
    if metrics.get("admission_limit_active") not in (True, "true", "True"):
        raise SystemExit("skew_dag_nic_balance admission limit inactive unexpectedly")

    baseline_tasks = baseline.get("tasks", [])
    policy_tasks = report.get("tasks", [])
    if len(baseline_tasks) != len(policy_tasks):
        raise SystemExit("NIC balance run completed a different number of tasks than baseline")

    print("[policy-dsl-skew-dag-nic] OK — NIC utilization rule reordered and clamped admission")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
