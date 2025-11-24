#!/usr/bin/env python3
"""Regenerate the README skew-tier baseline table from the latest CSV summary."""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import Iterable, List, Sequence

POLICY_BASELINE_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = POLICY_BASELINE_DIR.parents[1]
DEFAULT_SOURCE = POLICY_BASELINE_DIR / "results" / "skew_tier_baselines.csv"
DEFAULT_README = POLICY_BASELINE_DIR / "README.md"

TABLE_HEADER = (
    "| Workload          | Arrival Label | Background | Scale | Host Tput (kops/s) | "
    "NIC Tput (kops/s) | Δ Tput (kops/s) | Host Mean (us) | NIC Mean (us) | Δ Mean (us) |"
)
TABLE_SEPARATOR = (
    "|-------------------|---------------|------------|-------|--------------------|"
    "-------------------|-----------------|----------------|---------------|-------------|"
)
TABLE_HEADER_PREFIX = "| Workload"


def load_rows(csv_path: pathlib.Path) -> List[dict]:
    if not csv_path.exists():
        rel = csv_path.relative_to(REPO_ROOT) if csv_path.is_absolute() else csv_path
        raise SystemExit(
            f"Skew-tier baseline CSV missing at {rel}; run experiments/policy_baseline/calc_skew_baselines.py first."
        )
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def format_table(rows: Sequence[dict]) -> str:
    def kops(value: float) -> str:
        return f"{value / 1000.0:.1f}"

    def delta_kops(value: float) -> str:
        return f"{value / 1000.0:+.1f}"

    def latency(value: float) -> str:
        return f"{value:.2f}"

    def latency_delta(value: float) -> str:
        return f"{value:+.2f}"

    lines: List[str] = [TABLE_HEADER, TABLE_SEPARATOR]
    for row in sorted(
        rows,
        key=lambda r: (r["workload_label"], r["arrival_label"], r["background_load"]),
    ):
        host_t = float(row["host_throughput_per_sec"])
        nic_t = float(row["nic_throughput_per_sec"])
        delta_t = float(row["throughput_delta_per_sec"])
        host_mean = float(row["host_mean_latency_us"])
        nic_mean = float(row["nic_mean_latency_us"])
        delta_mean = float(row["latency_delta_us"])
        lines.append(
            "| `{workload}` | {arrival} | {background} | {scale} | {host} | {nic} | {delta} | "
            "{host_mean} | {nic_mean} | {delta_mean} |".format(
                workload=row["workload_label"],
                arrival=row["arrival_label"],
                background=row["background_load"],
                scale=row["arrival_scale"],
                host=kops(host_t),
                nic=kops(nic_t),
                delta=delta_kops(delta_t),
                host_mean=latency(host_mean),
                nic_mean=latency(nic_mean),
                delta_mean=latency_delta(delta_mean),
            )
        )
    return "\n".join(lines)


def replace_table(readme_path: pathlib.Path, new_table: str) -> None:
    if not readme_path.exists():
        raise SystemExit(f"README not found at {readme_path}")
    lines = readme_path.read_text().splitlines()
    header_idx = None
    for idx, line in enumerate(lines):
        if line.startswith(TABLE_HEADER_PREFIX) and "Host Tput" in line:
            header_idx = idx
            break
    if header_idx is None:
        raise SystemExit("Unable to locate skew-tier baseline table header in README.md")
    end_idx = header_idx
    while end_idx < len(lines) and lines[end_idx].strip().startswith("|"):
        end_idx += 1
    new_lines = lines[:header_idx] + new_table.splitlines() + lines[end_idx:]
    readme_path.write_text("\n".join(new_lines) + "\n")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=pathlib.Path,
        default=DEFAULT_SOURCE,
        help="Path to skew_tier_baselines CSV (default: %(default)s)",
    )
    parser.add_argument(
        "--readme",
        type=pathlib.Path,
        default=DEFAULT_README,
        help="README file to update (default: %(default)s)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the generated table instead of modifying the README",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    rows = load_rows(args.source)
    table = format_table(rows)
    if args.dry_run:
        print(table)
        return 0
    replace_table(args.readme, table)
    rel_readme = args.readme.relative_to(REPO_ROOT) if args.readme.is_absolute() else args.readme
    rel_source = args.source.relative_to(REPO_ROOT) if args.source.is_absolute() else args.source
    print(f"[update_skew_table] Updated {rel_readme} from {rel_source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
