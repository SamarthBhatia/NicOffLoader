#!/usr/bin/env python3
"""Ensure rolling window schedules loaded via batch manifests keep metadata/event logs intact."""

from __future__ import annotations

import argparse
import csv
import pathlib
import subprocess
import sys
import tempfile
from typing import Dict


def write_schedule(path: pathlib.Path) -> None:
    payload = """\
events:
  - at_us: 0.0
    queue_us: 10000.0
    reset_samples: true
  - at_us: 50.0
    action: reset
  - at_us: 100.0
    action: configure
    util_us: 25000.0
    sojourn_tasks: 32
"""
    path.write_text(payload, encoding="utf-8")


def write_manifest(path: pathlib.Path,
                   profile: pathlib.Path,
                   workload: pathlib.Path,
                   schedule_name: str,
                   csv_path: pathlib.Path) -> None:
    manifest = f"""\
defaults:
  profile: {profile}
  workload: {workload}
  output_dir: ./outputs
  metadata:
    workload_label: manifest_roll_schedule
    arrival_label: schedule_smoke
csv: {csv_path}
runs:
  - name: manifest-schedule-smoke
    rolling_window_schedule:
      from_file: {schedule_name}
"""
    path.write_text(manifest, encoding="utf-8")


def run_batch(cli: pathlib.Path, manifest: pathlib.Path) -> None:
    cmd = [str(cli), "--batch", str(manifest)]
    subprocess.run(cmd, check=True)


def load_rows(path: pathlib.Path) -> Dict[str, Dict[str, str]]:
    with path.open() as handle:
        reader = csv.DictReader(handle)
        return {row["run_name"]: row for row in reader}


def verify_schedule_metadata(rows: Dict[str, Dict[str, str]], schedule: pathlib.Path) -> None:
    target = rows.get("manifest-schedule-smoke")
    if not target:
        raise SystemExit("manifest-schedule-smoke row missing from CSV")
    label = target.get("rolling_window_schedule_label", "")
    if not label.startswith("file:"):
        raise SystemExit(f"rolling_window_schedule_label missing file: prefix (got '{label}')")
    schedule_text = schedule.resolve().as_posix()
    if schedule_text not in label:
        raise SystemExit(f"schedule label '{label}' does not reference '{schedule_text}'")
    event_count = float(target.get("rolling_window_event_count", "0") or "0")
    if event_count <= 0:
        raise SystemExit("rolling_window_event_count <= 0 (schedule never applied?)")
    event_log = target.get("rolling_window_event_log", "")
    if not event_log:
        raise SystemExit("rolling_window_event_log is empty")


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
                        help="Workload path (default: rolling_spike)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = pathlib.Path(tmpdir)
        schedule_path = tmp_path / "schedule.yaml"
        manifest_path = tmp_path / "manifest.yaml"
        csv_path = tmp_path / "results.csv"
        write_schedule(schedule_path)
        write_manifest(manifest_path, args.profile.resolve(), args.workload.resolve(), schedule_path.name, csv_path)
        run_batch(args.cli, manifest_path)
        if not csv_path.exists():
            raise SystemExit("batch run did not emit the CSV output")
        rows = load_rows(csv_path)
        verify_schedule_metadata(rows, schedule_path)
    print("[rolling-window-manifest-schedule] OK — manifest-driven schedules propagate metadata/event logs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
