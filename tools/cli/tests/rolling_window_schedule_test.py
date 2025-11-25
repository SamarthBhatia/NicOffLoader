#!/usr/bin/env python3
"""Validate that scheduled rolling window configure/reset events apply via the CLI."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def write_schedule(path: pathlib.Path) -> None:
    payload = """\
events:
  - at_us: 0.0
    queue_us: 10000.0
    reset_samples: true
  - at_us: 40.0
    action: reset
  - at_us: 90.0
    action: configure
    util_us: 25000.0
    sojourn_tasks: 16
"""
    path.write_text(payload, encoding="utf-8")


def run_cli(cli: pathlib.Path,
            profile: pathlib.Path,
            workload: pathlib.Path,
            schedule: pathlib.Path,
            output_path: pathlib.Path) -> None:
    cmd = [
        str(cli),
        "--profile",
        str(profile),
        "--workload",
        str(workload),
        "--output",
        str(output_path),
        "--rolling-window-schedule",
        str(schedule),
    ]
    subprocess.run(cmd, check=True)


def verify_event_log(report_path: pathlib.Path) -> None:
    with report_path.open() as handle:
        data = json.load(handle)
    events = data.get("rolling_window_events", [])
    if len(events) < 4:
        raise SystemExit(f"expected >=4 rolling_window_events, saw {len(events)}")
    configure_queue = [
        evt for evt in events if evt["type"] == "configure" and abs(evt["queue_window_us"] - 10000.0) < 1e-6
    ]
    if not configure_queue or not configure_queue[0]["reset_samples"]:
        raise SystemExit("schedule did not apply the queue window reconfigure/reset at 0 us")
    reset_events = [evt for evt in events if evt["type"] == "reset"]
    if not reset_events:
        raise SystemExit("schedule did not emit a reset event")
    util_events = [
        evt for evt in events if evt["type"] == "configure" and abs(evt["util_window_us"] - 25000.0) < 1e-6
    ]
    if not util_events or util_events[0]["sojourn_window_tasks"] != 16:
        raise SystemExit("schedule did not apply the util/sojourn reconfigure at 90 us")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True, help="Path to nicloadoff_cli binary")
    parser.add_argument("--profile",
                        type=pathlib.Path,
                        default=pathlib.Path("profiles/bf2_default.yaml").resolve(),
                        help="Profile path (default: bf2_default)")
    parser.add_argument("--workload",
                        type=pathlib.Path,
                        default=pathlib.Path("workloads/tests/rolling_spike.yaml").resolve(),
                        help="Workload exercising host spikes")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)
        schedule_path = tmpdir_path / "schedule.yaml"
        write_schedule(schedule_path)
        output_path = tmpdir_path / "run.json"
        run_cli(args.cli, args.profile, args.workload, schedule_path, output_path)
        verify_event_log(output_path)
    print("[rolling-window-schedule] OK — CLI applied scheduled rolling window events")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
