# workloads/

Workload descriptors (DAG specs, arrival models, parameter files) live here. Store reusable templates such as KV read paths and optional TCP splits. See [SCHEMA.md](SCHEMA.md) for the complete YAML reference covering tasks, DAGs, instructions/byte hints, and placement metadata.

The simulator now loads YAML specs from `workloads/examples/` (see `sequential_host.yaml`, `host_nic_pipeline.yaml`, `skew_dag.yaml`, `multi_entry_dag.yaml`) in addition to built-in presets. Each workload lists tasks, their arrival times, stage service-time references, and resource demands. For DAG-oriented workloads, use the `dag:` block with named nodes, successors, and optional entry points—these are parsed into `TaskDAGSpec` (with cycle/unreachable-node validation) for the Phase 3 graph execution work.
