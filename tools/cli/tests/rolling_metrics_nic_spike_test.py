#!/usr/bin/env python3
"""Deterministic NIC spike test validating rolling metrics on NIC saturation."""

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
            output_path: pathlib.Path) -> None:
    cmd = [
        str(cli),
        "--profile",
        str(profile),
        "--workload",
        str(workload),
        "--output",
        str(output_path),
    ]
    subprocess.run(cmd, check=True)


def assert_threshold(value: float, threshold: float, comparator, message: str) -> None:
    if not comparator(value, threshold):
        raise SystemExit(f"{message}: got {value:.6f}, expected threshold {threshold:.6f}")


def verify_metrics(report_path: pathlib.Path) -> None:
    with report_path.open() as handle:
        data = json.load(handle)
    rolling = data.get("rolling_metrics", {})
    queue = rolling.get("waiting_queue_depth")
    host = rolling.get("host_utilization")
    nic = rolling.get("nic_utilization")
    sojourn = rolling.get("sojourn")
    if not all([queue, host, nic, sojourn]):
        raise SystemExit("rolling metrics missing expected sections")
    assert_threshold(queue["average"], 0.5, lambda v, t: v > t, "queue average did not reflect NIC waiting spike")
    assert_threshold(queue["peak"], 2.0, lambda v, t: v >= t, "queue peak too low (NIC spike)")
    assert_threshold(nic["average"], 0.3, lambda v, t: v > t, "NIC utilization average should be elevated")
    assert_threshold(nic["peak"], 0.9, lambda v, t: v >= t, "NIC utilization peak should reach saturation")
    assert_threshold(host["average"], 0.2, lambda v, t: v < t, "Host utilization should remain low for NIC spike")
    assert_threshold(sojourn["mean_latency_us"], 60.0, lambda v, t: v > t, "mean sojourn too low for NIC queued tasks")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True, help="Path to nicloadoff_cli binary")
    default_profile = pathlib.Path("profiles/bf2_default.yaml")
    default_workload = pathlib.Path("workloads/tests/rolling_spike_nic.yaml")
    parser.add_argument("--profile", type=pathlib.Path, default=default_profile.resolve())
    parser.add_argument("--workload", type=pathlib.Path, default=default_workload.resolve())
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.profile.exists():
        raise SystemExit(f"profile not found: {args.profile}")
    if not args.workload.exists():
        raise SystemExit(f"workload not found: {args.workload}")
    with tempfile.TemporaryDirectory() as tmpdir:
        output_path = pathlib.Path(tmpdir) / "metrics.json"
        run_cli(args.cli, args.profile, args.workload, output_path)
        verify_metrics(output_path)
    print("[rolling-spike-nic] OK — rolling metrics respond to NIC saturation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

