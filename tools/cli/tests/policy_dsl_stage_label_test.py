#!/usr/bin/env python3
"""Exercise stage-label-aware DSL reorder on a host/NIC contention DAG."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def run_cli(cli: pathlib.Path, profile: pathlib.Path, workload: pathlib.Path, config: pathlib.Path) -> dict:
    with tempfile.TemporaryDirectory() as tmpdir:
        output = pathlib.Path(tmpdir) / "stage_label.json"
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
                "dsl",
                "--policy-config",
                str(config),
            ],
            check=True,
        )
        with output.open() as handle:
            return json.load(handle)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, required=True)
    parser.add_argument("--workload", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)

    report = run_cli(args.cli, args.profile, args.workload, args.config)
    metrics = report.get("policy_metrics", {})
    if float(metrics.get("waiting_reorders", 0.0)) <= 0:
        raise SystemExit("Stage-label DSL did not emit waiting reorders")
    if float(metrics.get("waiting_reorders_per_task", 0.0)) <= 0:
        raise SystemExit("Stage-label DSL reorder ratio missing")

    print("[policy-dsl-stage-label] OK — stage-aware reorder triggered under contention")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
