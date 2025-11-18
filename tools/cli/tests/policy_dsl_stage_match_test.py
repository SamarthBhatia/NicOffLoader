#!/usr/bin/env python3
"""Exercise DSL match predicates (metadata + stage_index) end-to-end."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def run_cli_with_manifest(cli: pathlib.Path, manifest: pathlib.Path) -> dict:
    subprocess.run([str(cli), "--config", str(manifest)], check=True)
    with manifest.open() as handle:
        output = None
        for line in handle:
            if line.strip().startswith("output:"):
                output = line.split("output:", 1)[1].strip()
                break
        if output is None:
            raise SystemExit("manifest missing output path")
    with pathlib.Path(output).open() as handle:
        return json.load(handle)


def run_cli(cli: pathlib.Path, profile: pathlib.Path, workload: pathlib.Path) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        output = pathlib.Path(tmpdir) / "baseline.json"
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
                "none",
            ],
            check=True,
        )
        with output.open() as handle:
            return json.load(handle)


def queue_time(report: dict, task_id: int) -> float:
    for task in report.get("tasks", []):
        if task.get("task_id") == task_id:
            return float(task.get("queue_time_us", 0.0))
    raise SystemExit(f"task {task_id} missing from report")


def build_manifest(dsl_path: pathlib.Path, workload: pathlib.Path, profile: pathlib.Path, output: pathlib.Path) -> pathlib.Path:
    manifest = output.parent / "stage_match_manifest.yaml"
    manifest.write_text(
        "\n".join(
            [
                f"profile: {profile}",
                f"workload: {workload}",
                "policy: dsl",
                f"policy_config: {dsl_path}",
                f"output: {output}",
                "metadata:",
                "  arrival_label: burst",
            ]
        )
    )
    return manifest


def build_dsl(path: pathlib.Path) -> None:
    path.write_text(
        "\n".join(
            [
                "rules:",
                "  - match:",
                "      stage_index: 0",
                "      metadata:",
                "        arrival_label: burst",
                "    action:",
                "      reorder: prefer-nic",
                "  - match:",
                "      metadata:",
                "        arrival_label: burst",
                "    action:",
                "      admission:",
                "        max_active: 1",
                "  - match:",
                "      metadata:",
                "        arrival_label: steady",
                "    action:",
                "      reorder: prefer-host",
            ]
        )
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, required=True)
    parser.add_argument("--workload", type=pathlib.Path, required=True)
    parser.add_argument("--arrival-label", default="burst", choices=["burst", "steady"])
    args = parser.parse_args(argv)

    baseline = run_cli(args.cli, args.profile, args.workload)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)
        dsl_path = tmpdir_path / "stage_match.dsl.yaml"
        build_dsl(dsl_path)
        output_path = tmpdir_path / "stage_match.json"
        manifest = build_manifest(dsl_path, args.workload, args.profile, output_path)
        # Toggle arrival_label to exercise both the stage-targeted reorder and the fallback.
        manifest_text = manifest.read_text().replace("arrival_label: burst", f"arrival_label: {args.arrival_label}")
        manifest.write_text(manifest_text)
        dsl_report = run_cli_with_manifest(args.cli, manifest)

    nic_task_base = queue_time(baseline, 202)
    policy_metrics = dsl_report.get("policy_metrics", {})
    if float(policy_metrics.get("waiting_reorders", 0.0)) <= 0:
        raise SystemExit("Metadata + stage match DSL did not emit waiting reorders")
    if float(policy_metrics.get("waiting_reorders_per_task", 0.0)) <= 0:
        raise SystemExit("Metadata + stage match DSL reorder ratio missing")

    print("[policy-dsl-stage-match] OK — DSL match predicates reorder queue-flip scenario")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
