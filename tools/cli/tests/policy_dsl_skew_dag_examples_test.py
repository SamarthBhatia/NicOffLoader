#!/usr/bin/env python3
"""Verify the stock skew DAG stage-balance DSL reorders waiting queues."""

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
    output: pathlib.Path,
    policy: str,
    extra_args: list[str] | None = None,
) -> dict:
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


def run_skew_manifest(cli: pathlib.Path, profile: pathlib.Path, workload: pathlib.Path, config: pathlib.Path) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)
        manifest = tmpdir_path / "skew_stage_manifest.yaml"
        output = tmpdir_path / "skew_stage_report.json"
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
                    "  arrival_label: burst",
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
        baseline_report = run_cli(
            args.cli,
            args.profile,
            args.workload,
            tmpdir_path / "baseline.json",
            "none",
        )

    policy_report = run_skew_manifest(args.cli, args.profile, args.workload, args.config)
    metrics = policy_report.get("policy_metrics", {})
    if float(metrics.get("waiting_reorders", 0.0)) <= 0.0:
        raise SystemExit("skew_dag_stage_balance DSL did not emit any waiting reorders")
    if float(metrics.get("waiting_reorders_per_task", 0.0)) <= 0.0:
        raise SystemExit("skew_dag_stage_balance DSL missing per-task reorder count")

    if len(baseline_report.get("tasks", [])) != len(policy_report.get("tasks", [])):
        raise SystemExit("baseline vs DSL task counts diverged on skew DAG example")

    print("[policy-dsl-skew-dag] OK — skew_dag_stage_balance reorders the waiting queue")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
