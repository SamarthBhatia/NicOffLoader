#!/usr/bin/env python3
"""Smoke test: run DSL manifests and confirm reports emit policy metrics."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys


def run_manifest(cli: pathlib.Path, manifest: pathlib.Path) -> dict:
    text = manifest.read_text().splitlines()
    output = None
    repo_root = manifest.resolve().parents[2]
    for idx, line in enumerate(text):
        if line.strip().startswith("output:"):
            output = line.split("output:", 1)[1].strip()
        if line.strip().startswith("profile:"):
            path = (repo_root / line.split("profile:", 1)[1].strip()).resolve()
            text[idx] = f"profile: {path}"
        if line.strip().startswith("workload:"):
            path = (repo_root / line.split("workload:", 1)[1].strip()).resolve()
            text[idx] = f"workload: {path}"
        if line.strip().startswith("policy_config:"):
            path = (repo_root / line.split("policy_config:", 1)[1].strip()).resolve()
            text[idx] = f"policy_config: {path}"
    if not output:
        raise SystemExit(f"manifest missing output path: {manifest}")
    tmp_manifest = manifest.parent / (manifest.stem + "_abs.yaml")
    tmp_manifest.write_text("\n".join(text))
    subprocess.run([str(cli), "--config", str(tmp_manifest)], check=True)
    with pathlib.Path(output).open() as handle:
        return json.load(handle)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--manifests", nargs="+", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)

    for manifest in args.manifests:
        report = run_manifest(args.cli, manifest)
        policy_metrics = report.get("policy_metrics")
        if policy_metrics is None:
            raise SystemExit(f"{manifest} did not produce policy_metrics in report")
    print("[policy-dsl-manifest-smoke] OK — DSL manifests run and emit policy metrics")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
