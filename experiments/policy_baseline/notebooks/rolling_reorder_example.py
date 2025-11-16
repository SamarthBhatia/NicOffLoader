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
    parser.add_argument(
        "--input-format",
        choices=["auto", "csv", "parquet"],
        default="auto",
        help="Input format (default: auto-detect from extension)",
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
        raise SystemExit(f"Input file not found at {args.csv}. Run export_normalized.py first.")
    fmt = args.input_format
    if fmt == "auto":
        fmt = "parquet" if args.csv.suffix.lower() == ".parquet" else "csv"
    try:
        if fmt == "parquet":
            df = pd.read_parquet(args.csv)
        else:
            df = pd.read_csv(args.csv)
    except ImportError as exc:  # pragma: no cover - dependency hint
        raise SystemExit(
            "Reading Parquet requires pandas with an installed engine (pyarrow or fastparquet). "
            "Install one (e.g., `python3 -m pip install pyarrow`) or pass --input-format csv."
        ) from exc
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
        "rolling_queue_average",
        "rolling_queue_peak",
        "rolling_host_util_average",
        "rolling_nic_util_average",
        "rolling_sojourn_p95_latency_us",
        "rolling_sojourn_p99_latency_us",
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

    have_rolling = {"rolling_queue_average", "rolling_queue_peak", "rolling_host_util_average", "rolling_nic_util_average"}.issubset(
        subset.columns
    )
    rows = 2 if have_rolling else 1
    fig, axes = plt.subplots(rows, 1, figsize=(max(6, len(subset) * 0.75), 4 * rows))
    if rows == 1:
        reorder_ax = axes
        queue_ax = None
    else:
        reorder_ax, queue_ax = axes

    width = 0.4
    reorder_ax.bar([pos - width / 2 for pos in x], total_ratio, width=width, label="per-task (entire run)", color="#c44e52")
    reorder_ax.bar([pos + width / 2 for pos in x], recent_ratio, width=width, label="per-task (recent window)", color="#8172b3")
    reorder_ax.set_xticks(list(x))
    reorder_ax.set_xticklabels(names, rotation=30, ha="right")
    reorder_ax.set_ylabel("waiting reorders per task")
    reorder_ax.set_title("Cumulative vs. rolling reorder ratios")
    reorder_ax.grid(axis="y", linestyle="--", alpha=0.4)
    reorder_ax.legend()

    if queue_ax is not None:
        queue_avg = subset["rolling_queue_average"]
        queue_peak = subset["rolling_queue_peak"]
        host_util = subset["rolling_host_util_average"]
        nic_util = subset["rolling_nic_util_average"]
        sojourn_p95 = subset.get("rolling_sojourn_p95_latency_us")
        queue_bars = queue_ax.bar(x, queue_avg, width=0.5, color="#ccb974", label="queue avg")
        queue_ax.set_xticks(list(x))
        queue_ax.set_xticklabels(names, rotation=30, ha="right")
        queue_ax.set_ylabel("waiting queue (tasks)")
        queue_ax.set_title("Rolling queue depth & utilization")
        queue_ax.grid(axis="y", linestyle="--", alpha=0.4)
        for bar, peak in zip(queue_bars, queue_peak):
            queue_ax.text(bar.get_x() + bar.get_width() / 2.0, bar.get_height(), f"peak {peak:.1f}", ha="center", va="bottom", fontsize=8)
        util_ax = queue_ax.twinx()
        util_ax.plot(x, host_util, marker="o", color="#64b5cd", label="host util avg")
        util_ax.plot(x, nic_util, marker="s", color="#dd8452", label="nic util avg")
        util_ax.set_ylabel("utilization (0–1)")
        util_ax.set_ylim(0.0, 1.05)
        handles, labels = queue_ax.get_legend_handles_labels()
        handles2, labels2 = util_ax.get_legend_handles_labels()
        queue_ax.legend(handles + handles2, labels + labels2, loc="upper left", fontsize=8)
        if sojourn_p95 is not None:
            max_queue = float(max(queue_peak.max(), 1.0))
            for idx, value in enumerate(sojourn_p95):
                queue_ax.text(
                    idx,
                    queue_avg.iloc[idx] + max_queue * 0.05,
                    f"p95 {value:.1f}us",
                    ha="center",
                    va="bottom",
                    fontsize=7,
                )

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
