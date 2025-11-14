#!/usr/bin/env python3
"""Generate a simple table (and optional plot) showing cumulative vs. rolling reorder ratios."""

from __future__ import annotations

import argparse
import pathlib
import sys
try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit("pandas is required; install it via `python3 -m pip install pandas`.") from exc


def parse_args() -> argparse.Namespace:
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    default_csv = repo_root / "experiments" / "policy_baseline" / "results" / "policy_baseline_normalized.csv"
    default_plot = repo_root / "plots" / "generated" / "rolling_reorder_example.png"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Normalized CSV input path")
    parser.add_argument(
        "--workload-label",
        default="skew_dag",
        help="Filter rows by workload_label (default: %(default)s; set empty string to disable)",
    )
    parser.add_argument(
        "--arrival-label",
        default="",
        help="Optional arrival_label filter (empty string to disable)",
    )
    parser.add_argument("--output-plot", type=pathlib.Path, default=default_plot, help="Plot output path")
    parser.add_argument("--no-plot", action="store_true", help="Skip plotting (table only)")
    return parser.parse_args()


def ensure_matplotlib() -> "module":
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:  # pragma: no cover - dependency hint
        raise SystemExit(
            "matplotlib is required for plotting; install it via `python3 -m pip install matplotlib` "
            "or re-run with --no-plot."
        ) from exc
    return plt


def load_subset(args: argparse.Namespace) -> pd.DataFrame:
    if not args.csv.exists():
        raise SystemExit(f"CSV not found at {args.csv}. Run export_normalized.py first.")
    df = pd.read_csv(args.csv)
    mask = pd.Series(True, index=df.index, dtype=bool)
    if args.workload_label:
        mask &= df["workload_label"] == args.workload_label
    if args.arrival_label:
        mask &= df["arrival_label"] == args.arrival_label
    subset = df[mask].copy()
    if subset.empty:
        raise SystemExit("Filtered dataset is empty; adjust --workload-label/--arrival-label filters.")
    subset = subset.sort_values(["arrival_label", "policy", "run_name"])
    return subset


def print_table(subset: pd.DataFrame) -> None:
    columns = [
        "run_name",
        "policy",
        "arrival_label",
        "waiting_reorders",
        "waiting_reorders_per_task",
        "waiting_reorders_recent",
        "waiting_reorder_recent_task_count",
        "waiting_reorders_per_task_recent",
    ]
    view = subset[columns]
    print(view.to_string(index=False))


def render_plot(subset: pd.DataFrame, output_path: pathlib.Path) -> None:
    plt = ensure_matplotlib()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    x = range(len(subset))
    names = subset["run_name"]
    total_ratio = subset["waiting_reorders_per_task"]
    recent_ratio = subset["waiting_reorders_per_task_recent"]

    fig, ax = plt.subplots(figsize=(max(6, len(subset) * 0.75), 4))
    width = 0.4
    ax.bar([pos - width / 2 for pos in x], total_ratio, width=width, label="per-task (entire run)", color="#c44e52")
    ax.bar([pos + width / 2 for pos in x], recent_ratio, width=width, label="per-task (recent window)", color="#8172b3")
    ax.set_xticks(list(x))
    ax.set_xticklabels(names, rotation=30, ha="right")
    ax.set_ylabel("waiting reorders per task")
    ax.set_title("Cumulative vs. rolling reorder ratios")
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    print(f"[rolling-example] wrote {output_path}")


def main() -> int:
    args = parse_args()
    subset = load_subset(args)
    print_table(subset)
    if not args.no_plot:
        render_plot(subset, args.output_plot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
