#!/usr/bin/env python3
"""Generate skewed DAG workload variants and refresh manifest/batch configs."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import textwrap
from typing import Dict, Iterable, List, Mapping, Sequence

BASE_TEMPLATE = {
    "schema_version": "0.1",
    "workload_name": "skew_dag_base",
    "description": "Base skew DAG template; not intended for direct use.",
    "tasks": [
        {
            "id": 0,
            "arrival_time": 0.0,
            "dag": {
                "nodes": [
                    {
                        "name": "parse_req",
                        "stage": {
                            "deterministic_service_time": 0.8,
                            "placement_default": "host",
                            "placement_eligible": ["host", "nic"],
                            "instructions": 1.5e7,
                            "bytes_in": 2048,
                            "bytes_out": 1536,
                            "demands": [
                                {"resource": "host_cpu", "units": 0.6},
                                {"resource": "host_dram", "units": 0.2},
                            ],
                        },
                        "successors": ["hash_key", "auth_check"],
                    },
                    {
                        "name": "hash_key",
                        "stage": {
                            "deterministic_service_time": 1.0,
                            "placement_default": "host",
                            "placement_eligible": ["host", "nic"],
                            "instructions": 2.0e7,
                            "bytes_in": 1536,
                            "bytes_out": 512,
                            "demands": [
                                {"resource": "host_cpu", "units": 0.8},
                            ],
                        },
                        "successors": ["db_lookup"],
                    },
                    {
                        "name": "db_lookup",
                        "stage": {
                            "service_profile": {
                                "key": "kv_lookup",
                                "domain": "host",
                                "mode": "stochastic",
                            },
                            "placement_default": "host",
                            "placement_eligible": ["host", "nic"],
                            "instructions": 6.0e7,
                            "bytes_in": 2048,
                            "bytes_out": 6144,
                            "demands": [
                                {"resource": "host_cpu", "units": 1.5},
                                {"resource": "host_dram", "units": 2.0},
                                {"resource": "host_link", "units": 8192},
                            ],
                        },
                        "successors": ["serialize_resp"],
                    },
                    {
                        "name": "auth_check",
                        "stage": {
                            "deterministic_service_time": 0.6,
                            "placement_default": "host",
                            "placement_eligible": ["host", "nic"],
                            "instructions": 0.5e7,
                            "bytes_in": 512,
                            "bytes_out": 512,
                            "demands": [
                                {"resource": "host_cpu", "units": 0.4},
                            ],
                        },
                        "successors": ["serialize_resp"],
                    },
                    {
                        "name": "serialize_resp",
                        "stage": {
                            "deterministic_service_time": 0.9,
                            "placement_default": "host",
                            "placement_eligible": ["host", "nic"],
                            "instructions": 1.0e7,
                            "bytes_in": 6144,
                            "bytes_out": 2048,
                            "demands": [
                                {"resource": "host_cpu", "units": 0.7},
                                {"resource": "host_dram", "units": 0.3},
                            ],
                        },
                    },
                ],
                "entry_points": ["parse_req"],
            },
        }
    ],
}

CONFIG_PATH = pathlib.Path(__file__).with_name("skew_dag_config.json")
RESERVED_MANIFEST_METADATA = {"arrival_model", "workload_label"}


def format_scalar(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        text = f"{value:.6g}"
        if "e" in text:
            mantissa, exponent = text.split("e")
            exponent = exponent.lstrip("+0") or "0"
            text = f"{mantissa}e{exponent}"
        elif "." not in text:
            text = f"{text}.0"
        return text
    assert isinstance(value, str)
    if value == "" or value[0].isspace() or value[-1].isspace() or any(c in value for c in (":", "#", "\"")):
        escaped = value.replace('"', '\\"')
        return f'"{escaped}"'
    return value


def dump_yaml(value: object, indent: int = 0) -> List[str]:
    pad = " " * indent
    if isinstance(value, dict):
        lines: List[str] = []
        for key, child in value.items():
            if isinstance(child, str) and "\n" in child:
                lines.append(f"{pad}{key}: >")
                for block_line in child.splitlines():
                    lines.append(f"{' ' * (indent + 2)}{block_line}")
                continue
            child_lines = dump_yaml(child, indent + 2)
            complex_child = isinstance(child, (dict, list))
            if complex_child:
                lines.append(f"{pad}{key}:")
                lines.extend(child_lines)
            else:
                lines.append(f"{pad}{key}: {child_lines[0].strip()}")
                lines.extend(child_lines[1:])
        if not lines:
            lines.append(f"{pad}{{}}")
        return lines
    if isinstance(value, list):
        if not value:
            return [f"{pad}[]"]
        lines: List[str] = []
        for item in value:
            child_lines = dump_yaml(item, indent + 2)
            if not child_lines:
                lines.append(f"{pad}-")
                continue
            lines.append(f"{pad}- {child_lines[0].strip()}")
            lines.extend(child_lines[1:])
        return lines
    if isinstance(value, str) and "\n" in value:
        lines = [f"{pad}>"]
        for line in value.splitlines():
            lines.append(f"{' ' * (indent + 2)}{line}")
        return lines
    return [f"{pad}{format_scalar(value)}"]


def dump_yaml_text(value: object) -> str:
    return "\n".join(dump_yaml(value)) + "\n"


def apply_override(node: Dict[str, object], override: Mapping[str, object]) -> None:
    stage = node["stage"]  # type: ignore[index]
    assert isinstance(stage, dict)
    if "set" in override:
        for key, value in override["set"].items():  # type: ignore[union-attr]
            if key == "demands":
                stage[key] = [dict(entry) for entry in value]  # type: ignore[assignment]
            else:
                stage[key] = value  # type: ignore[assignment]
    if "scale" in override:
        scale_spec = override["scale"]  # type: ignore[assignment]
        for key, factor in scale_spec.items():
            if key == "demands":
                for demand in stage.get("demands", []):
                    res = demand["resource"]
                    if res in factor:
                        demand["units"] = demand["units"] * factor[res]
            else:
                if key in stage and isinstance(stage[key], (int, float)):
                    stage[key] = stage[key] * factor
    if "successors" in override:
        node["successors"] = list(override["successors"])  # type: ignore[assignment]


def build_variant(variant: Mapping[str, object]) -> Dict[str, object]:
    spec = copy.deepcopy(BASE_TEMPLATE)
    spec["workload_name"] = variant["workload_name"]
    spec["description"] = textwrap.dedent(variant["description"]).strip()
    task = spec["tasks"][0]
    task["id"] = variant["task_id"]
    overrides = variant.get("stages", {})
    if overrides:
        node_lookup = {node["name"]: node for node in task["dag"]["nodes"]}
        for node_name, node_override in overrides.items():
            apply_override(node_lookup[node_name], node_override)
    return spec


def write_variant(spec: Dict[str, object], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    yaml_text = dump_yaml_text(spec)
    output_path.write_text(yaml_text)
    print(f"[skew-generator] wrote {output_path}")


def load_config(path: pathlib.Path) -> Dict[str, object]:
    with path.open() as handle:
        return json.load(handle)


def update_manifest(config: Mapping[str, object], variants: Sequence[Mapping[str, object]]) -> None:
    manifest_cfg = config["placement_manifest"]
    workloads: List[Dict[str, object]] = []
    metadata_keys = set()

    def normalize_metadata(metadata: Mapping[str, object] | None) -> Dict[str, str]:
        result: Dict[str, str] = {}
        if metadata:
            for key, value in metadata.items():
                result[key] = str(value)
                if key not in RESERVED_MANIFEST_METADATA:
                    metadata_keys.add(key)
        return result

    for static_entry in manifest_cfg.get("static_workloads", []):
        entry = dict(static_entry)
        entry["metadata"] = normalize_metadata(static_entry.get("metadata"))
        workloads.append(entry)

    default_seeds = manifest_cfg.get("seeds", [manifest_cfg.get("seed", 1)])
    default_modes = manifest_cfg.get("placement_modes", ["hint_respect"])

    for variant in variants:
        variant_metadata = normalize_metadata(variant.get("metadata"))
        scenarios = variant.get("scenarios") or []
        if not scenarios:
            entry_cfg = variant.get("manifest_entry")
            if entry_cfg:
                scenarios = [entry_cfg]
        for scenario in scenarios:
            scenario_meta = dict(variant_metadata)
            scenario_meta.update(normalize_metadata(scenario.get("metadata")))
            entry = {
                "name": f"{variant['name']}_{scenario.get('name', 'default')}",
                "workload": scenario.get("workload", f"workloads/examples/{variant['name']}.yaml"),
                "arrival": scenario.get("arrival", manifest_cfg.get("arrival")),
                "arrival_scales": scenario.get("arrival_scales", [1.0]),
                "seeds": scenario.get("seeds", default_seeds),
                "placement_modes": scenario.get("placement_modes", default_modes),
                "metadata": scenario_meta,
            }
            workloads.append(entry)

    manifest = {
        "profile": manifest_cfg["profile"],
        "binary": manifest_cfg["binary"],
        "csv_path": manifest_cfg["csv_path"],
        "results_dir": manifest_cfg["results_dir"],
        "placement_modes": manifest_cfg.get("placement_modes", []),
        "metadata_keys": sorted(metadata_keys),
        "workloads": workloads,
    }
    manifest_path = pathlib.Path("experiments/placement_baseline/manifest.yaml")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"[skew-generator] updated {manifest_path}")


def update_policy_batch(config: Mapping[str, object], variants: Sequence[Mapping[str, object]]) -> None:
    batch_cfg = config["policy_batch"]
    defaults = batch_cfg["defaults"]
    default_metadata = defaults.get("metadata", {})
    runs = list(batch_cfg.get("static_runs", []))
    for variant in variants:
        for run in variant.get("batch_runs", []):
            metadata = dict(default_metadata)
            metadata.update(variant.get("metadata", {}))
            metadata.update(run.get("metadata", {}))
            run_entry = {
                "name": run["name"],
                "workload": run.get("workload", f"../../workloads/examples/{variant['name']}.yaml"),
                "policy": run["policy"],
                "metadata": metadata,
            }
            if "service_modes" in run:
                run_entry["service_modes"] = run["service_modes"]
            runs.append(run_entry)
    batch = {
        "defaults": defaults,
        "csv": batch_cfg["csv"],
        "runs": runs,
    }
    batch_path = pathlib.Path("experiments/policy_baseline/batch.yaml")
    batch_path.write_text(dump_yaml_text(batch))
    print(f"[skew-generator] updated {batch_path}")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_output = pathlib.Path(__file__).resolve().parents[1] / "examples"
    parser.add_argument("--output-dir", type=pathlib.Path, default=default_output, help="Destination directory")
    parser.add_argument("--config", type=pathlib.Path, default=CONFIG_PATH, help="Config JSON describing variants")
    parser.add_argument(
        "--variant",
        action="append",
        dest="variants",
        help="Variant(s) to generate (default: all). Can be repeated.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print YAML to stdout instead of writing files")
    parser.add_argument("--skip-manifests", action="store_true", help="Do not rewrite manifest/batch files")
    args = parser.parse_args(list(argv) if argv is not None else None)

    config = load_config(args.config)
    config_variants = {variant["name"]: variant for variant in config["variants"]}
    variant_names = args.variants or list(config_variants.keys())
    unknown = [name for name in variant_names if name not in config_variants]
    if unknown:
        raise SystemExit(f"Unknown variant(s): {', '.join(unknown)}")

    selected_variants = [config_variants[name] for name in variant_names]
    generated_specs = []
    for variant in selected_variants:
        spec = build_variant(variant)
        generated_specs.append(variant)
        output_path = args.output_dir / f"{variant['name']}.yaml"
        if args.dry_run:
            print(f"# --- {variant['name']} ({output_path}) ---")
            print(dump_yaml_text(spec))
        else:
            write_variant(spec, output_path)

    if args.dry_run:
        return 0

    if not args.skip_manifests:
        update_manifest(config, config["variants"])
        update_policy_batch(config, config["variants"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
