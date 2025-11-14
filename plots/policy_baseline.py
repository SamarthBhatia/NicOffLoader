#!/usr/bin/env python3
"""Render throughput and latency comparison plots for policy_baseline batch results."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, List, Tuple

try:
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit("matplotlib is required; install it via pip (e.g., pip install matplotlib).") from exc


Metrics = Dict[str, float]


def load_metrics(csv_path: pathlib.Path) -> List[Tuple[str, Metrics]]:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV not found at {csv_path}. Run the batch sweep first.")

    grouped: Dict[str, List[Metrics]] = defaultdict(list)
    with csv_path.open() as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            raise ValueError(f"{csv_path} is empty or missing a header row.")
        for row in reader:
            key = row["run_name"]
            grouped[key].append(
                {
                    "throughput": float(row["throughput_per_sec"]),
                    "mean_latency": float(row["mean_latency_us"]),
                    "p95_latency": float(row["p95_latency_us"]),
                    "p99_latency": float(row["p99_latency_us"]),
                    "peak_queue": float(row["peak_waiting_queue_depth"]),
                    "waiting_reorders": float(row.get("waiting_reorders", 0.0) or 0.0),
                    "waiting_ratio_total": float(row.get("waiting_reorders_per_task", 0.0) or 0.0),
                    "waiting_reorders_recent": float(row.get("waiting_reorders_recent", 0.0) or 0.0),
                    "waiting_ratio_recent": float(row.get("waiting_reorders_per_task_recent", 0.0) or 0.0),
                    "waiting_recent_window": float(row.get("waiting_reorder_recent_task_count", 0.0) or 0.0),
                    "rolling_queue_avg": float(row.get("rolling_queue_average", 0.0) or 0.0),
                    "rolling_queue_peak": float(row.get("rolling_queue_peak", 0.0) or 0.0),
                    "rolling_host_util_avg": float(row.get("rolling_host_util_average", 0.0) or 0.0),
                    "rolling_host_util_peak": float(row.get("rolling_host_util_peak", 0.0) or 0.0),
                    "rolling_nic_util_avg": float(row.get("rolling_nic_util_average", 0.0) or 0.0),
                    "rolling_nic_util_peak": float(row.get("rolling_nic_util_peak", 0.0) or 0.0),
                    "rolling_sojourn_mean": float(row.get("rolling_sojourn_mean_latency_us", 0.0) or 0.0),
                    "rolling_sojourn_p95": float(row.get("rolling_sojourn_p95_latency_us", 0.0) or 0.0),
                    "rolling_sojourn_p99": float(row.get("rolling_sojourn_p99_latency_us", 0.0) or 0.0),
                }
            )

    summaries: List[Tuple[str, Metrics]] = []
    for name, entries in grouped.items():
        count = float(len(entries))
        summary = {}
        for metric in [
            "throughput",
            "mean_latency",
            "p95_latency",
            "p99_latency",
            "peak_queue",
            "waiting_reorders",
            "waiting_ratio_total",
            "waiting_reorders_recent",
            "waiting_ratio_recent",
            "waiting_recent_window",
            "rolling_queue_avg",
            "rolling_queue_peak",
            "rolling_host_util_avg",
            "rolling_host_util_peak",
            "rolling_nic_util_avg",
            "rolling_nic_util_peak",
            "rolling_sojourn_mean",
            "rolling_sojourn_p95",
            "rolling_sojourn_p99",
        ]:
            summary[metric] = sum(entry[metric] for entry in entries) / count
        summaries.append((name, summary))
    summaries.sort(key=lambda item: item[0])
    return summaries


def annotate_bars(axis, bars):
    for bar in bars:
        height = bar.get_height()
        axis.text(
            bar.get_x() + bar.get_width() / 2.0,
            height,
            f"{height:.0f}",
            ha="center",
            va="bottom",
            fontsize=9,
        )


def plot(results: List[Tuple[str, Metrics]], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    names = [name for name, _ in results]
    throughput = [metrics["throughput"] for _, metrics in results]
    mean_latency = [metrics["mean_latency"] for _, metrics in results]
    p95_latency = [metrics["p95_latency"] for _, metrics in results]
    peak_queue = [metrics["peak_queue"] for _, metrics in results]
    waiting_reorders = [metrics["waiting_reorders"] for _, metrics in results]
    waiting_reorders_recent = [metrics["waiting_reorders_recent"] for _, metrics in results]
    waiting_ratio_total = [metrics["waiting_ratio_total"] for _, metrics in results]
    waiting_ratio_recent = [metrics["waiting_ratio_recent"] for _, metrics in results]
    waiting_recent_window = [metrics["waiting_recent_window"] for _, metrics in results]
    rolling_queue_avg = [metrics["rolling_queue_avg"] for _, metrics in results]
    rolling_queue_peak = [metrics["rolling_queue_peak"] for _, metrics in results]
    host_util_avg = [metrics["rolling_host_util_avg"] for _, metrics in results]
    nic_util_avg = [metrics["rolling_nic_util_avg"] for _, metrics in results]
    sojourn_mean = [metrics["rolling_sojourn_mean"] for _, metrics in results]
    sojourn_p95 = [metrics["rolling_sojourn_p95"] for _, metrics in results]
    sojourn_p99 = [metrics["rolling_sojourn_p99"] for _, metrics in results]

    x = range(len(names))
    width = 0.35

    fig, axes = plt.subplots(2, 2, figsize=(16, 8))
    throughput_axis, latency_axis, reorder_axis, queue_axis = axes.flatten()

    throughput_bars = throughput_axis.bar(x, throughput, color="#4c72b0")
    throughput_axis.set_xticks(x)
    throughput_axis.set_xticklabels(names, rotation=20, ha="right")
    throughput_axis.set_ylabel("throughput (tasks/s)")
    throughput_axis.set_title("Policy throughput")
    throughput_axis.grid(axis="y", linestyle="--", alpha=0.4)
    annotate_bars(throughput_axis, throughput_bars)

    latency_axis.bar(
        [pos - width / 2 for pos in x],
        mean_latency,
        width=width,
        label="mean latency",
        color="#dd8452",
    )
    latency_axis.bar(
        [pos + width / 2 for pos in x],
        p95_latency,
        width=width,
        label="p95 latency",
        color="#55a868",
    )
    for pos, value in zip(x, peak_queue):
        latency_axis.text(pos, max(mean_latency + p95_latency) * 1.02, f"peak q={value:.0f}", ha="center", fontsize=9)
    latency_axis.set_xticks(x)
    latency_axis.set_xticklabels(names, rotation=20, ha="right")
    latency_axis.set_ylabel("latency (us)")
    latency_axis.set_title("Latency comparison")
    latency_axis.grid(axis="y", linestyle="--", alpha=0.4)
    latency_axis.legend()

    reorder_bars_total = reorder_axis.bar(
        [pos - width / 2 for pos in x],
        waiting_ratio_total,
        width=width,
        label="per-task (entire run)",
        color="#c44e52",
    )
    reorder_bars_recent = reorder_axis.bar(
        [pos + width / 2 for pos in x],
        waiting_ratio_recent,
        width=width,
        label="per-task (recent window)",
        color="#8172b3",
    )
    reorder_axis.set_xticks(x)
    reorder_axis.set_xticklabels(names, rotation=20, ha="right")
    reorder_axis.set_ylabel("waiting reorders per task")
    reorder_axis.set_title("Policy reorder rate (total vs recent)")
    reorder_axis.grid(axis="y", linestyle="--", alpha=0.4)
    reorder_axis.legend()
    for bar, count in zip(reorder_bars_total, waiting_reorders):
        height = bar.get_height()
        reorder_axis.text(
            bar.get_x() + bar.get_width() / 2.0,
            height,
            f"{height:.4f}\n({count:.0f} total)",
            ha="center",
            va="bottom",
            fontsize=8,
        )
    for bar, count, window in zip(reorder_bars_recent, waiting_reorders_recent, waiting_recent_window):
        height = bar.get_height()
        window_label = f"{int(window):d}" if window > 0 else "-"
        reorder_axis.text(
            bar.get_x() + bar.get_width() / 2.0,
            height,
            f"{height:.4f}\n({count:.0f}/{window_label})",
            ha="center",
            va="bottom",
            fontsize=8,
        )

    max_queue_height = max(rolling_queue_peak + [1.0])
    queue_bars = queue_axis.bar(x, rolling_queue_avg, color="#ccb974", label="waiting queue avg")
    queue_axis.set_xticks(x)
    queue_axis.set_xticklabels(names, rotation=20, ha="right")
    queue_axis.set_ylabel("waiting queue (tasks)")
    queue_axis.set_title("Rolling queue depth & utilization")
    queue_axis.grid(axis="y", linestyle="--", alpha=0.4)
    for bar, peak in zip(queue_bars, rolling_queue_peak):
        queue_axis.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height(),
            f"peak {peak:.1f}",
            ha="center",
            va="bottom",
            fontsize=8,
        )
    util_axis = queue_axis.twinx()
    util_axis.plot(x, host_util_avg, marker="o", color="#64b5cd", label="host util avg")
    util_axis.plot(x, nic_util_avg, marker="s", color="#dd8452", label="nic util avg")
    util_axis.set_ylabel("utilization (0–1)")
    util_axis.set_ylim(0.0, 1.05)
    handles, labels = queue_axis.get_legend_handles_labels()
    handles2, labels2 = util_axis.get_legend_handles_labels()
    queue_axis.legend(handles + handles2, labels + labels2, loc="upper left", fontsize=8)
    for pos, mean, p95, p99 in zip(x, sojourn_mean, sojourn_p95, sojourn_p99):
        queue_axis.text(
            pos,
            max_queue_height * 1.02,
            f"soj μ={mean:.1f} p95={p95:.1f} p99={p99:.1f}us",
            ha="center",
            va="bottom",
            fontsize=7,
            rotation=30,
        )

    fig.suptitle("Policy baseline comparison")
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    print(f"[plots] wrote {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/policy_baseline/results/policy_baseline_normalized.csv")
    default_output = pathlib.Path("plots/generated/policy_baseline.png")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Path to policy baseline CSV input")
    parser.add_argument("--output", type=pathlib.Path, default=default_output, help="Output image path")
    args = parser.parse_args()

    results = load_metrics(args.csv)
    if not results:
        print("No rows available in the CSV; skipping plot generation.")
        return 0
    plot(results, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
