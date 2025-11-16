#!/usr/bin/env python3
"""Join policy_baseline normalized results with placement sweep outputs."""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import Dict, Iterable, List, Tuple

try:
    import pyarrow.parquet as pq  # type: ignore

    HAVE_PARQUET = True
except ImportError:  # pragma: no cover - optional dependency
    HAVE_PARQUET = False


PolicyRow = Dict[str, str]
PlacementMetrics = Dict[str, float]
PlacementEntry = Dict[str, PlacementMetrics]
Key = Tuple[str, str, str, str]


def parse_float(value: str | float | None) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def load_policy_rows(path: pathlib.Path) -> Tuple[List[PolicyRow], List[str]]:
    if not path.exists():
        raise SystemExit(f"policy results not found at {path}. Run export_normalized.py first.")
    if path.suffix == ".parquet":
        if not HAVE_PARQUET:
            raise SystemExit("pyarrow is required to read Parquet inputs; install it or pass a CSV file.")
        table = pq.read_table(path)
        columns = table.column_names
        data = table.to_pydict()
        rows: List[PolicyRow] = []
        for idx in range(table.num_rows):
            row: PolicyRow = {}
            for col in columns:
                value = data[col][idx]
                row[col] = "" if value is None else str(value)
            rows.append(row)
        return rows, columns

    with path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader), reader.fieldnames or []


def load_placement_rows(path: pathlib.Path) -> List[Dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"placement sweep CSV not found at {path}. Run experiments/placement_baseline/run.py first.")
    with path.open() as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def make_key(row: Dict[str, str]) -> Key:
    return (
        row.get("workload_label") or row.get("workload") or row.get("workload_path") or "",
        row.get("arrival_label", ""),
        row.get("background_load", ""),
        row.get("zipf_alpha", ""),
    )


def build_placement_index(rows: List[Dict[str, str]]) -> Dict[Key, PlacementEntry]:
    index: Dict[Key, PlacementEntry] = {}
    for row in rows:
        mode = row.get("placement_mode")
        if not mode:
            continue
        key = make_key(row)
        metrics = {
            "throughput_per_sec": parse_float(row.get("throughput_per_sec", "")),
            "mean_latency_us": parse_float(row.get("mean_latency_us", "")),
            "p95_latency_us": parse_float(row.get("latency_p95_us", row.get("p95_latency_us", ""))),
        }
        index.setdefault(key, {})[mode] = {k: v for k, v in metrics.items() if v is not None}
    return index


def attach_placement(row: PolicyRow, placement: PlacementEntry) -> None:
    host = placement.get("host_pinned", {})
    nic = placement.get("nic_pinned", {})

    host_t = host.get("throughput_per_sec")
    nic_t = nic.get("throughput_per_sec")
    host_lat = host.get("mean_latency_us")
    nic_lat = nic.get("mean_latency_us")
    host_p95 = host.get("p95_latency_us")
    nic_p95 = nic.get("p95_latency_us")

    row.update(
        {
            "placement_host_throughput_per_sec": f"{host_t:.6f}" if host_t is not None else "",
            "placement_nic_throughput_per_sec": f"{nic_t:.6f}" if nic_t is not None else "",
            "placement_host_mean_latency_us": f"{host_lat:.6f}" if host_lat is not None else "",
            "placement_nic_mean_latency_us": f"{nic_lat:.6f}" if nic_lat is not None else "",
            "placement_host_p95_latency_us": f"{host_p95:.6f}" if host_p95 is not None else "",
            "placement_nic_p95_latency_us": f"{nic_p95:.6f}" if nic_p95 is not None else "",
        }
    )

    policy_t = parse_float(row.get("throughput_per_sec"))
    policy_lat = parse_float(row.get("mean_latency_us"))
    row["policy_vs_host_throughput_delta_per_sec"] = (
        f"{policy_t - host_t:.6f}" if policy_t is not None and host_t is not None else ""
    )
    row["policy_vs_nic_throughput_delta_per_sec"] = (
        f"{policy_t - nic_t:.6f}" if policy_t is not None and nic_t is not None else ""
    )
    row["policy_vs_host_latency_delta_us"] = (
        f"{policy_lat - host_lat:.6f}" if policy_lat is not None and host_lat is not None else ""
    )
    row["policy_vs_nic_latency_delta_us"] = (
        f"{policy_lat - nic_lat:.6f}" if policy_lat is not None and nic_lat is not None else ""
    )


def join(policy_rows: List[PolicyRow], placement_index: Dict[Key, PlacementEntry]) -> Tuple[List[PolicyRow], List[Key]]:
    joined: List[PolicyRow] = []
    missing: List[Key] = []
    for row in policy_rows:
        combined = dict(row)
        key = make_key(combined)
        if key in placement_index:
            attach_placement(combined, placement_index[key])
        else:
            missing.append(key)
            combined.update(
                {
                    "placement_host_throughput_per_sec": "",
                    "placement_nic_throughput_per_sec": "",
                    "placement_host_mean_latency_us": "",
                    "placement_nic_mean_latency_us": "",
                    "placement_host_p95_latency_us": "",
                    "placement_nic_p95_latency_us": "",
                    "policy_vs_host_throughput_delta_per_sec": "",
                    "policy_vs_nic_throughput_delta_per_sec": "",
                    "policy_vs_host_latency_delta_us": "",
                    "policy_vs_nic_latency_delta_us": "",
                }
            )
        joined.append(combined)
    return joined, missing


def write_csv(rows: List[PolicyRow], fieldnames: List[str], output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})
    print(f"[join] wrote {output}")


def write_parquet(rows: List[PolicyRow], fieldnames: List[str], output: pathlib.Path) -> None:
    if not HAVE_PARQUET:
        print("[join] pyarrow not installed; skipping Parquet output")
        return
    import pyarrow as pa  # type: ignore

    output.parent.mkdir(parents=True, exist_ok=True)
    columns = {field: [] for field in fieldnames}
    for row in rows:
        for field in fieldnames:
            columns[field].append(row.get(field, ""))
    table = pa.Table.from_pydict(columns)
    pq.write_table(table, output)
    print(f"[join] wrote {output}")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_policy = pathlib.Path("experiments/policy_baseline/results/policy_baseline_normalized.csv")
    default_placement = pathlib.Path("experiments/placement_baseline/results/placement_sweep.csv")
    default_out_csv = pathlib.Path("experiments/policy_baseline/results/policy_vs_placement.csv")
    default_out_parquet = pathlib.Path("experiments/policy_baseline/results/policy_vs_placement.parquet")
    parser.add_argument("--policy", type=pathlib.Path, default=default_policy, help="Normalized policy CSV/Parquet path")
    parser.add_argument(
        "--placement", type=pathlib.Path, default=default_placement, help="Placement sweep CSV (placement_baseline)"
    )
    parser.add_argument("--output-csv", type=pathlib.Path, default=default_out_csv, help="Joined CSV output path")
    parser.add_argument(
        "--output-parquet", type=pathlib.Path, default=default_out_parquet, help="Joined Parquet output path"
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    policy_rows, policy_fields = load_policy_rows(args.policy)
    placement_rows = load_placement_rows(args.placement)
    placement_index = build_placement_index(placement_rows)

    joined_rows, missing_keys = join(policy_rows, placement_index)
    extra_fields = [
        "placement_host_throughput_per_sec",
        "placement_nic_throughput_per_sec",
        "placement_host_mean_latency_us",
        "placement_nic_mean_latency_us",
        "placement_host_p95_latency_us",
        "placement_nic_p95_latency_us",
        "policy_vs_host_throughput_delta_per_sec",
        "policy_vs_nic_throughput_delta_per_sec",
        "policy_vs_host_latency_delta_us",
        "policy_vs_nic_latency_delta_us",
    ]
    fieldnames = policy_fields + [field for field in extra_fields if field not in policy_fields]

    write_csv(joined_rows, fieldnames, args.output_csv)
    write_parquet(joined_rows, fieldnames, args.output_parquet)
    if missing_keys:
        sample = ["/".join(key) for key in missing_keys[:5]]
        print(f"[join] warning: missing placement entries for {len(missing_keys)} policy rows; sample={sample}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
