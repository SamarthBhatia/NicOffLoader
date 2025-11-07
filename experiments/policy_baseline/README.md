# Policy baseline sweep

This experiment demonstrates the `nicloadoff_cli --batch` workflow for running multiple policy
configurations in one shot. The batch manifest writes each run's JSON report under `results/` and
maintains a CSV summary that downstream analysis scripts can ingest directly.

## Files
- `batch.yaml` — batch manifest consumed by `nicloadoff_cli --batch`.
- `results/` — per-run JSON outputs plus the aggregated CSV (ignored from version control).

## Running the batch
```bash
cmake --build build --target nicloadoff_cli
./build/tools/cli/nicloadoff_cli --batch experiments/policy_baseline/batch.yaml
```

Each run entry inherits defaults for the profile, workload, seed, and output directory. Feel free to
add new `runs:` entries for additional policies or workloads—the CLI validates policy names and will
append new rows to the CSV automatically.
