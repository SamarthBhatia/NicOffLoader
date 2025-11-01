# workloads/

Workload descriptors (DAG specs, arrival models, parameter files) live here. Store reusable templates such as KV read paths and optional TCP splits.

The simulator now loads YAML specs from `workloads/examples/` (see `sequential_host.yaml`, `host_nic_pipeline.yaml`) in addition to built-in presets. Each workload lists tasks, their arrival times, stage service-time references, and resource demands.
