# profiles/

Hardware profile files (e.g., `bf2_default.yaml`) and accompanying documentation belong here. Capture parameter sources, assumptions, and sensitivity variants.

## Schema Outline (v0.1 Draft)

Profiles follow a YAML structure designed for direct ingestion by the simulator loader. Top-level keys:

- `schema_version`: Semantic version of the profile schema.
- `profile_name`: Short identifier used by the CLI (`--profile`).
- `description`: Human-readable summary of the hardware environment.
- `last_verified`: Date the values were last checked against source material.
- `references`: List of citation objects (`title`, `publisher`/`authors`, `year`, optional `url`).
- `host`: Host platform specification.
  - `cpu`: `model`, `sockets`, `cores_per_socket`, `cores_total`, `clock_ghz`, `base_service_scale`.
  - `dram`: `type`, `capacity_gb`, `bandwidth_gbps`, `latency_ns`.
- `nic`: SmartNIC/DPU specification.
  - `cpu`: `model`, `cores`, `clock_ghz`, `base_service_scale`.
  - `dram`: `type`, `capacity_gb`, `bandwidth_gbps`, `contention_variance_pct`.
- `links`: Connectivity objects such as `host_nic` and `nic_network` describing `type`, `peak_bandwidth_gbps`, `effective_bandwidth_gbps`, `latency_us`, and `mtu_bytes`.
- `queues`: Simulator capacity hints for host/nic resources and link buffers.
- `service_time_overrides`: Stage-specific mean service times (`host_mean_us`, `nic_mean_us`) keyed by workload stage names.
- `notes`: Free-form list of clarifications or TODOs.

Future schema revisions will introduce:

- Distribution descriptors (`distribution: { kind: normal, mean: ..., stddev: ... }`).
- Policy- or workload-specific overrides (e.g., `overrides: { policy: latency_aware, adjustments: ... }`).

## Loader Plan

Phase 2 ships an in-repo, indentation-aware YAML parser that supports the schema above (scalars, nested maps, and sequences). Longer term we may swap to `yaml-cpp` once dependency management is settled.

The loader currently:

1. Parses profile files into strongly-typed C++ structures (`CpuSpec`, `DramSpec`, `LinkSpec`, `ServiceOverrides`).
2. Enforces basic constraints (required fields, positive numeric ranges, `effective_bandwidth_gbps <= peak_bandwidth_gbps`).
3. Surfaces actionable error messages identifying the offending key/path.

Planned upgrades:

- Add sanity checks linking `cores_total` with socket counts.
- Support richer distribution descriptors (`distribution: { kind: normal, mean: ..., stddev: ... }`).
- Provide deterministic hashing/serialization for caching and test comparisons.

Validation roadmap:

- Add schema unit tests under `sim/tests/profile_loader_test.cc`. ✅ (initial pass)
- Include negative fixtures (`profiles/tests/invalid_*.yaml`) to ensure error handling remains robust.
- Document mapping between YAML fields and simulator structures in this README as loader code lands.

## Maintaining Profiles

- Update `SOURCES.md` whenever a parameter or citation changes.
- Annotate YAML fields with comments referencing the relevant row in `SOURCES.md`.
- Introduce derived variants (e.g., `bf2_highload.yaml`) when running sensitivity sweeps; keep base profiles concise.

_Last updated: 2025-10-31_
