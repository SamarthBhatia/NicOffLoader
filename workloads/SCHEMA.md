# Workload Schema

This document defines the YAML schema consumed by `workload_loader.cc`. The same schema applies to both single-task lists and DAG workloads.

## Top-level fields

| Field           | Type     | Required | Description |
| --------------- | -------- | -------- | ----------- |
| `schema_version`| string   | ✔        | Semantic version string so we can evolve the format (current: `0.1`). |
| `workload_name` | string   | ✔        | Short identifier used in logs/plots. |
| `description`   | string   | ✖        | Free-form description explaining the scenario. |
| `tasks`         | list     | ✖        | Ordered list of independent tasks (see below). |
| `dag_tasks`     | list     | ✖        | DAG-backed workloads (see below). |

At least one of `tasks` or `dag_tasks` must be present.

## Task entries

```yaml
tasks:
  - id: 42
    arrival_time: 5.0
    instructions: 1.5e7        # optional metadata for future policies
    bytes_in: 16384            # optional ingress payload hint
    bytes_out: 4096            # optional egress payload hint
    placement_hint: host_only  # optional string consumed by future policies
    stages:
      - deterministic_service_time: 5.0
        service_profile:
          key: host_stage
          domain: host
          mode: deterministic | stochastic
        demands:
          - resource: host_cpu
            units: 1.0
```

### Task fields

| Field            | Type   | Required | Notes |
| ---------------- | ------ | -------- | ----- |
| `id`             | int    | ✔        | Must be unique within the workload. |
| `arrival_time`   | float  | ✔        | Microseconds since simulation start. |
| `instructions`   | float  | ✖        | Optional hint (count) for policy heuristics. |
| `bytes_in/out`   | float  | ✖        | Optional payload hints (bytes). |
| `placement_hint` | string | ✖        | Optional label like `host_only`, `nic_pref`, `either`. |
| `stages`         | list   | ✔        | One or more stage definitions. |

### Stage fields

| Field                        | Type   | Required | Notes |
| ---------------------------- | ------ | -------- | ----- |
| `deterministic_service_time` | float  | ✖†       | Microseconds; required when no service profile is provided. |
| `service_profile.key`        | string | ✖†       | Reference into the profile service-time overrides. |
| `service_profile.domain`     | enum   | ✖        | `host` or `nic`. |
| `service_profile.mode`       | enum   | ✖        | `deterministic` or `stochastic`. |
| `demands`                    | list   | ✔        | Resource requirements per stage. |

†Exactly one of `deterministic_service_time` or `service_profile` must be provided. The loader enforces this constraint.

### Demand entries

| Field     | Type   | Required | Values |
| --------- | ------ | -------- | ------ |
| `resource`| enum   | ✔        | `host_cpu`, `host_dram`, `host_link`, `nic_cpu`, `nic_dram`, `nic_link`. |
| `units`   | float  | ✔        | Resource consumption in the units defined by the profile (cores, GB, bytes-in-flight, etc.). |

## DAG workloads

```yaml
dag_tasks:
  - id: 500
    arrival_time: 0.0
    nodes:
      - name: decode
        stage: { ...same stage schema... }
        successors: [offload]
    entry_points: [decode]
```

Each `node.stage` follows the same schema as a task stage. `successors` enumerates downstream node names, and `entry_points` lists the starting nodes. The loader validates:

* unique node names
* all successors exist
* at least one entry point
* no cycles and every node reachable from an entry point

Optional metadata (`instructions`, `bytes_in/out`, `placement_hint`) can be attached to DAG nodes by placing those keys alongside the `stage` block. These hints are preserved in the generated `TaskDagNode` and will feed future Phase 3 policy logic.
