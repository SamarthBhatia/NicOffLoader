#!/usr/bin/env python3
"""Run the placement benchmark sweep declared in manifest.yaml."""

from __future__ import annotations

import argparse
import pathlib
import shlex
import subprocess
import sys
from typing import Any, Dict, Iterable, List
import json

try:
    import yaml  # type: ignore
except ImportError:  # pragma: no cover - fallback to JSON subset
    yaml = None


def _as_list(value: Any, fallback: Iterable[Any]) -> List[Any]:
    if value is None:
        return list(fallback)
    if isinstance(value, list):
        return value
    return [value]


def load_manifest(path: pathlib.Path) -> Dict[str, Any]:
    text = path.read_text()
    data: Dict[str, Any]
    if yaml is not None:
        data = yaml.safe_load(text)
    else:
        try:
            data = json.loads(text)
        except json.JSONDecodeError as exc:
            raise SystemExit(
                "PyYAML is not installed and the manifest is not valid JSON. "
                "Either install PyYAML (pip install pyyaml) or keep the manifest JSON-compatible."
            ) from exc
    if not data:
        raise ValueError(f"manifest {path} is empty")
    if "workloads" not in data:
        raise ValueError("manifest must contain a 'workloads' list")
    return data


def run_sweep(manifest: Dict[str, Any], binary: pathlib.Path, append: bool) -> None:
    csv_path = pathlib.Path(manifest.get("csv_path", "experiments/placement_baseline/results/placement_sweep.csv"))
    results_dir = pathlib.Path(manifest.get("results_dir", csv_path.parent))
    profile = manifest["profile"]

    if not append and csv_path.exists():
        csv_path.unlink()
    results_dir.mkdir(parents=True, exist_ok=True)

    workloads = manifest["workloads"]
    default_seeds = _as_list(manifest.get("seeds"), [manifest.get("seed", 1)])
    default_placements = _as_list(manifest.get("placement_modes"), ["hint_respect"])

    for scenario in workloads:
        scenario_name = scenario.get("name") or pathlib.Path(scenario["workload"]).stem
        seeds = _as_list(scenario.get("seeds"), default_seeds)
        scales = [float(scale) for scale in _as_list(scenario.get("arrival_scales"), [1.0])]
        placements = _as_list(scenario.get("placement_modes"), default_placements)
        for scale in scales:
            for placement_mode in placements:
                for seed in seeds:
                    safe_mode = placement_mode.replace("/", "-")
                    output_path = results_dir / f"{scenario_name}_mode-{safe_mode}_scale-{scale}_seed-{seed}.json"
                    cmd = [
                        str(binary),
                        "--profile",
                    profile,
                    "--workload",
                    scenario["workload"],
                    "--arrival",
                    scenario["arrival"],
                        "--arrival-scale",
                        str(scale),
                        "--placement-mode",
                        placement_mode,
                        "--seed",
                        str(seed),
                        "--output",
                    str(output_path),
                    "--csv",
                    str(csv_path),
                ]
                    print(f"[sweep] {scenario_name} mode={placement_mode} scale={scale} seed={seed}")
                    print("        ", " ".join(shlex.quote(part) for part in cmd))
                    subprocess.run(cmd, check=True)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_manifest = pathlib.Path(__file__).with_name("manifest.yaml")
    parser.add_argument("--manifest", type=pathlib.Path, default=default_manifest)
    parser.add_argument(
        "--binary",
        type=pathlib.Path,
        help="Override path to placement_benchmark (defaults to manifest entry or build/tools/placement/placement_benchmark)",
    )
    parser.add_argument("--append", action="store_true", help="Append to existing CSV instead of rewriting it")
    args = parser.parse_args(argv)

    manifest = load_manifest(args.manifest)
    binary = args.binary or pathlib.Path(manifest.get("binary", "build/tools/placement/placement_benchmark"))
    if not binary.exists():
        raise FileNotFoundError(f"placement_benchmark not found at {binary}; build it first")

    run_sweep(manifest, binary, append=args.append)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
