#!/usr/bin/env python3
"""Validate rolling-window preset playback plus metadata tagging."""

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
            preset: str,
            preset_file: pathlib.Path,
            output_path: pathlib.Path) -> None:
    cmd = [
        str(cli),
        "--profile",
        str(profile),
        "--workload",
        str(workload),
        "--output",
        str(output_path),
        "--rolling-window-preset",
        preset,
        "--rolling-window-preset-file",
        str(preset_file),
    ]
    subprocess.run(cmd, check=True)


def verify_report(report_path: pathlib.Path, preset: str) -> None:
    with report_path.open() as handle:
        data = json.load(handle)
    metadata = data.get("metadata", {})
    if metadata.get("rolling_window_preset") != preset:
        raise SystemExit(f"metadata missing rolling_window_preset={preset}")
    rolling_window = data.get("rolling_window", {})
    if rolling_window.get("preset") != preset:
        raise SystemExit("rolling_window block missing preset entry")
    if not rolling_window.get("schedule_label"):
        raise SystemExit("rolling_window block missing schedule_label")
    events = data.get("rolling_window_events", [])
    if not events:
        raise SystemExit("preset run emitted no rolling_window_events")
    if not any(abs(evt["queue_window_us"] - 10000.0) < 1e-6 for evt in events):
        raise SystemExit("burst_sweep preset never reconfigured the queue window to 10_000 us")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True, help="Path to nicloadoff_cli binary")
    parser.add_argument("--profile",
                        type=pathlib.Path,
                        default=pathlib.Path("profiles/bf2_default.yaml").resolve())
    parser.add_argument("--workload",
                        type=pathlib.Path,
                        default=pathlib.Path("workloads/tests/rolling_spike.yaml").resolve())
    parser.add_argument("--preset-file",
                        type=pathlib.Path,
                        default=pathlib.Path("tools/cli/rolling_window_presets.yaml").resolve())
    parser.add_argument("--preset", type=str, default="burst_sweep")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    with tempfile.TemporaryDirectory() as tmpdir:
        result_path = pathlib.Path(tmpdir) / "preset_run.json"
        run_cli(args.cli, args.profile, args.workload, args.preset, args.preset_file, result_path)
        verify_report(result_path, args.preset)
    print("[rolling-window-preset] OK — presets load and propagate metadata")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
