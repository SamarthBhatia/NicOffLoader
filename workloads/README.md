# workloads/

Workload descriptors (DAG specs, arrival models, parameter files) live here. Store reusable templates such as KV read paths and optional TCP splits. See [SCHEMA.md](SCHEMA.md) for the complete YAML reference covering tasks, DAGs, instructions/byte hints, and placement metadata.

The simulator now loads YAML specs from `workloads/examples/` (see `sequential_host.yaml`, `host_nic_pipeline.yaml`, `skew_dag.yaml`, `skew_dag_heavy.yaml`, `skew_dag_zipf14.yaml`, `skew_dag_zipf18.yaml`, `multi_entry_dag.yaml`, `kv_read_template.yaml`, `tcp_split_template.yaml`) in addition to built-in presets. Each workload lists tasks, their arrival times, stage service-time references, and resource demands. For DAG-oriented workloads, use the `dag:` block with named nodes, successors, and optional entry points—these are parsed into `TaskDAGSpec` (with cycle/unreachable-node validation) for the Phase 3 graph execution work.

Need to tweak the skewed DAG variants? Update `workloads/tools/skew_dag_config.json` (stage scalers, metadata, manifest/batch wiring) and run `python3 workloads/tools/generate_skew_dags.py`. The helper regenerates the `skew_dag*.yaml` fixtures plus refreshes the placement manifest and policy batch so new tiers feed experiments automatically; pass `--variant` to limit regeneration to a subset or `--skip-manifests` if you only want the YAML.

Arrival-model fixtures live under `workloads/arrivals/`. Use `poisson_bursty.yaml` for alternating low/high-rate windows or `periodic_sweep.yaml` when you need deterministic inter-arrival scans (e.g., the static placement benchmark in Phase 3).

Test fixtures that exercise specific scheduler/DSL behavior live under `workloads/tests/`. For example,
`policy_queue_flip.yaml` launches two host-heavy tasks plus a NIC-heavy task simultaneously so the
policy acceptance test can prove queue reordering still occurs under heavy load.
