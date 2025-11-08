#!/usr/bin/env python3
"""Generate skewed DAG workload variants with consistent metadata."""

from __future__ import annotations

import argparse
import copy
import pathlib
import textwrap
from typing import Dict, Iterable, List, Mapping

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

VariantConfig = Dict[str, object]

VARIANTS: Mapping[str, VariantConfig] = {
    "skew_dag": {
        "task_id": 900,
        "workload_name": "skew_dag",
        "description": """\
Skinny–wide–skinny DAG modeling a KV read path with a skewed lookup stage.
parse_req and hash_key fan into a dominant db_lookup node (skewable via service_profile),
while auth_check joins before serialize_resp to stress dependency pressure.""",
    },
    "skew_dag_heavy": {
        "task_id": 901,
        "workload_name": "skew_dag_heavy",
        "description": """\
Heavier skew variant of the KV DAG workload where the db_lookup stage carries 80/20-style
heavy-tail instructions/bytes to stress host↔NIC transfers.""",
        "overrides": {
            "hash_key": {
                "stage": {
                    "instructions": 2.5e7,
                    "demands": [
                        {"resource": "host_cpu", "units": 0.9},
                    ],
                }
            },
            "db_lookup": {
                "stage": {
                    "instructions": 1.2e8,
                    "bytes_in": 4096,
                    "bytes_out": 16384,
                    "demands": [
                        {"resource": "host_cpu", "units": 2.5},
                        {"resource": "host_dram", "units": 3.5},
                        {"resource": "host_link", "units": 16384},
                    ],
                }
            },
            "serialize_resp": {
                "stage": {
                    "deterministic_service_time": 1.2,
                    "instructions": 1.5e7,
                    "bytes_in": 16384,
                    "bytes_out": 4096,
                    "demands": [
                        {"resource": "host_cpu", "units": 0.9},
                        {"resource": "host_dram", "units": 0.4},
                    ],
                }
            },
        },
    },
    "skew_dag_zipf14": {
        "task_id": 902,
        "workload_name": "skew_dag_zipf14",
        "description": """\
Zipf-heavy (alpha≈1.4) variant of the KV DAG workload. The db_lookup node inherits a long-tail
key distribution so host-pinned placement keeps re-touching the same hot rows (high host_link +
DRAM pressure) while NIC placement pushes lookups across the DPA fabric.""",
        "overrides": {
            "hash_key": {
                "stage": {
                    "instructions": 3.0e7,
                    "demands": [
                        {"resource": "host_cpu", "units": 1.0},
                    ],
                }
            },
            "db_lookup": {
                "stage": {
                    "service_profile": {
                        "key": "kv_lookup_zipf14",
                        "domain": "host",
                        "mode": "stochastic",
                    },
                    "instructions": 2.2e8,
                    "bytes_in": 4096,
                    "bytes_out": 32768,
                    "demands": [
                        {"resource": "host_cpu", "units": 4.2},
                        {"resource": "host_dram", "units": 5.0},
                        {"resource": "host_link", "units": 65536},
                    ],
                }
            },
            "serialize_resp": {
                "stage": {
                    "deterministic_service_time": 1.4,
                    "instructions": 1.7e7,
                    "bytes_in": 32768,
                    "bytes_out": 4096,
                    "demands": [
                        {"resource": "host_cpu", "units": 1.2},
                        {"resource": "host_dram", "units": 0.5},
                    ],
                }
            },
        },
    },
}


def format_number(value: object) -> str:
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
    return str(value)


def emit(lines: List[str], indent: int, text: str) -> None:
    lines.append(f"{' ' * indent}{text}")


def render_stage(lines: List[str], stage: Dict[str, object], indent: int) -> None:
    emit(lines, indent, "stage:")
    order = [
        "deterministic_service_time",
        "service_profile",
        "placement_default",
        "placement_eligible",
        "instructions",
        "bytes_in",
        "bytes_out",
        "demands",
    ]
    for key in order:
        if key not in stage:
            continue
        value = stage[key]
        if key == "service_profile":
            emit(lines, indent + 2, "service_profile:")
            for sub_key in ("key", "domain", "mode"):
                if sub_key in value:
                    emit(lines, indent + 4, f"{sub_key}: {value[sub_key]}")
        elif key == "placement_eligible":
            emit(lines, indent + 2, "placement_eligible:")
            for entry in value:
                emit(lines, indent + 4, f"- {entry}")
        elif key == "demands":
            emit(lines, indent + 2, "demands:")
            for demand in value:
                emit(lines, indent + 4, f"- resource: {demand['resource']}")
                emit(lines, indent + 6, f"units: {format_number(demand['units'])}")
        else:
            emit(lines, indent + 2, f"{key}: {format_number(value)}")


def render_variant(spec: Dict[str, object]) -> str:
    lines: List[str] = []
    emit(lines, 0, "schema_version: 0.1")
    emit(lines, 0, f"workload_name: {spec['workload_name']}")
    emit(lines, 0, "description: >")
    for line in spec["description"].splitlines():
        emit(lines, 2, line)
    task = spec["tasks"][0]
    emit(lines, 0, "tasks:")
    emit(lines, 2, f"- id: {task['id']}")
    emit(lines, 4, "arrival_time: 0.0")
    emit(lines, 4, "dag:")
    emit(lines, 6, "nodes:")
    for node in task["dag"]["nodes"]:
        emit(lines, 8, f"- name: {node['name']}")
        render_stage(lines, node["stage"], 10)
        successors = node.get("successors", [])
        if successors:
            emit(lines, 10, "successors:")
            for succ in successors:
                emit(lines, 12, f"- {succ}")
    emit(lines, 6, "entry_points:")
    for entry in task["dag"]["entry_points"]:
        emit(lines, 8, f"- {entry}")
    return "\n".join(lines) + "\n"


def apply_override(node: Dict[str, object], override: Mapping[str, object]) -> None:
    if "stage" in override:
        stage = node["stage"]  # type: ignore[index]
        assert isinstance(stage, dict)
        for key, value in override["stage"].items():  # type: ignore[union-attr]
            if key == "demands":
                stage[key] = [dict(entry) for entry in value]  # type: ignore[assignment]
            elif key == "service_profile":
                stage[key] = dict(value)  # type: ignore[assignment]
            else:
                stage[key] = value  # type: ignore[assignment]
    if "successors" in override:
        node["successors"] = list(override["successors"])  # type: ignore[assignment]


def build_variant(name: str) -> Dict[str, object]:
    variant = VARIANTS[name]
    spec = copy.deepcopy(BASE_TEMPLATE)
    spec["workload_name"] = variant["workload_name"]
    spec["description"] = textwrap.dedent(variant["description"]).strip()
    task = spec["tasks"][0]
    task["id"] = variant["task_id"]
    overrides = variant.get("overrides", {})
    if overrides:
        node_lookup = {node["name"]: node for node in task["dag"]["nodes"]}
        for node_name, node_override in overrides.items():
            apply_override(node_lookup[node_name], node_override)
    return spec


def write_variant(spec: Dict[str, object], output_path: pathlib.Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    yaml_text = render_variant(spec)
    output_path.write_text(yaml_text)
    print(f"[skew-generator] wrote {output_path}")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_output = pathlib.Path(__file__).resolve().parents[1] / "examples"
    parser.add_argument("--output-dir", type=pathlib.Path, default=default_output, help="Destination directory")
    parser.add_argument(
        "--variant",
        action="append",
        dest="variants",
        help="Variant(s) to generate (default: all). Can be repeated.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print YAML to stdout instead of writing files")
    args = parser.parse_args(list(argv) if argv is not None else None)

    variants = args.variants or list(VARIANTS.keys())
    unknown = [name for name in variants if name not in VARIANTS]
    if unknown:
        raise SystemExit(f"Unknown variant(s): {', '.join(unknown)}")

    for name in variants:
        spec = build_variant(name)
        output_path = args.output_dir / f"{name}.yaml"
        if args.dry_run:
            print(f"# --- {name} ({output_path}) ---")
            print(render_variant(spec))
        else:
            write_variant(spec, output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
