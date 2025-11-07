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
python3 experiments/policy_baseline/summarize.py
python3 experiments/policy_baseline/export_normalized.py
python3 plots/policy_baseline.py --csv experiments/policy_baseline/results/policy_baseline_normalized.csv
```

Paths inside the manifest are resolved relative to the manifest’s directory, so the sample uses `../../`
to reach repo-level fixtures. Each run entry inherits defaults for the profile, workload, seed, and
output directory. Metadata fields declared under `defaults.metadata` (or overridden per run) are copied
into the CSV so you can capture annotations like arrival models or load regimes. The example manifest
ships both the baseline KV workload and a DAG-heavy `skew_dag` workload so policy comparisons cover
single-path and dependency-driven scenarios; feel free to add new
`runs:` entries for additional policies or workloads—the CLI validates policy names and will append new
rows to the CSV automatically. The `summarize.py` helper reads the CSV and prints a quick comparison
table (sort by throughput by default or mean latency via `--sort mean_latency`), and now supports
metadata-aware filtering/grouping (`--filter workload_label=skew_dag --group-by policy`) plus
additional columns via `--columns`. `export_normalized.py`
groups repeated runs and emits both a normalized CSV and (optionally) Parquet table (requires `pyarrow`).
`plots/policy_baseline.py` consumes the normalized CSV to render throughput/latency comparison charts
and stores them under `plots/generated/`.
