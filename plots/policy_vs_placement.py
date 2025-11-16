#!/usr/bin/env python3
"""Plot policy throughput/latency deltas against host/NIC placement baselines."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict
from typing import Dict, Iterable, List, Tuple

try:
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit("matplotlib is required; install it via pip (e.g., pip install matplotlib).") from exc


Metrics = Dict[str, float]
Key = Tuple[str, str, str]  # workload_label, policy, arrival_label


def parse_float(value: str | float | None) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def load_records(path: pathlib.Path) -> Dict[Key, List[Metrics]]:
    if not path.exists():
        raise FileNotFoundError(f"Joined policy-vs-placement CSV not found at {path}; run join_placement.py first.")
    with path.open() as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            raise ValueError(f"{path} is empty or missing a header row.")
        grouped: Dict[Key, List[Metrics]] = defaultdict(list)
        for row in reader:
            key = (
                row.get("workload_label", ""),
                row.get("policy", ""),
                row.get("arrival_label", ""),
            )
            metrics = {
                "policy_vs_host_throughput_delta_per_sec": parse_float(row.get("policy_vs_host_throughput_delta_per_sec")),
                "policy_vs_nic_throughput_delta_per_sec": parse_float(row.get("policy_vs_nic_throughput_delta_per_sec")),
                "policy_vs_host_latency_delta_us": parse_float(row.get("policy_vs_host_latency_delta_us")),
                "policy_vs_nic_latency_delta_us": parse_float(row.get("policy_vs_nic_latency_delta_us")),
            }
            if all(value is None for value in metrics.values()):
                continue
            grouped[key].append({k: v for k, v in metrics.items() if v is not None})
    return grouped


def aggregate(grouped: Dict[Key, List[Metrics]]) -> List[Tuple[str, Metrics]]:
    aggregates: List[Tuple[str, Metrics]] = []
    for (workload, policy, arrival), entries in grouped.items():
        if not entries:
            continue
        summary: Metrics = defaultdict(float)
        count = float(len(entries))
        for entry in entries:
            for key, value in entry.items():
                summary[key] += value
        for key in list(summary.keys()):
            summary[key] /= count
        label_parts = [workload or "(unknown)", policy or "(policy)", arrival or ""]
        label = " / ".join(part for part in label_parts if part)
        aggregates.append((label, summary))
    aggregates.sort(key=lambda item: item[0])
    return aggregates


def annotate_bars(axis, bars, suffix: str = ""):
    for bar in bars:
        height = bar.get_height()
        axis.text(
            bar.get_x() + bar.get_width() / 2.0,
            height,
            f"{height:.2f}{suffix}",
            ha="center",
            va="bottom",
            fontsize=8,
        )


def plot(results: List[Tuple[str, Metrics]], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    names = [name for name, _ in results]
    host_throughput = [metrics.get("policy_vs_host_throughput_delta_per_sec", 0.0) for _, metrics in results]
    nic_throughput = [metrics.get("policy_vs_nic_throughput_delta_per_sec", 0.0) for _, metrics in results]
    host_latency = [metrics.get("policy_vs_host_latency_delta_us", 0.0) for _, metrics in results]
    nic_latency = [metrics.get("policy_vs_nic_latency_delta_us", 0.0) for _, metrics in results]

    x = range(len(names))
    width = 0.35
    fig, (throughput_axis, latency_axis) = plt.subplots(1, 2, figsize=(16, 5))

    t_host = throughput_axis.bar([pos - width / 2 for pos in x], host_throughput, width=width, label="vs host placement", color="#4c72b0")
    t_nic = throughput_axis.bar([pos + width / 2 for pos in x], nic_throughput, width=width, label="vs nic placement", color="#55a868")
    throughput_axis.set_xticks(list(x))
    throughput_axis.set_xticklabels(names, rotation=25, ha="right")
    throughput_axis.set_ylabel("throughput delta (tasks/s)")
    throughput_axis.set_title("Policy throughput delta relative to placement baselines")
    throughput_axis.grid(axis="y", linestyle="--", alpha=0.4)
    throughput_axis.legend()
    annotate_bars(throughput_axis, t_host)
    annotate_bars(throughput_axis, t_nic)

    l_host = latency_axis.bar([pos - width / 2 for pos in x], host_latency, width=width, label="vs host placement", color="#c44e52")
    l_nic = latency_axis.bar([pos + width / 2 for pos in x], nic_latency, width=width, label="vs nic placement", color="#8172b3")
    latency_axis.set_xticks(list(x))
    latency_axis.set_xticklabels(names, rotation=25, ha="right")
    latency_axis.set_ylabel("latency delta (us)")
    latency_axis.set_title("Policy mean-latency delta relative to placement baselines")
    latency_axis.grid(axis="y", linestyle="--", alpha=0.4)
    latency_axis.legend()
    annotate_bars(latency_axis, l_host, suffix="us")
    annotate_bars(latency_axis, l_nic, suffix="us")

    fig.suptitle("Policy vs. placement comparison")
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    print(f"[policy-vs-placement] wrote {output_path}")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_csv = pathlib.Path("experiments/policy_baseline/results/policy_vs_placement.csv")
    default_output = pathlib.Path("plots/generated/policy_vs_placement.png")
    parser.add_argument("--csv", type=pathlib.Path, default=default_csv, help="Joined policy-vs-placement CSV path")
    parser.add_argument("--output", type=pathlib.Path, default=default_output, help="Output image path")
    args = parser.parse_args(list(argv) if argv is not None else None)

    grouped = load_records(args.csv)
    results = aggregate(grouped)
    if not results:
        print("[policy-vs-placement] no rows with placement annotations; skipping plot")
        return 0
    plot(results, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
