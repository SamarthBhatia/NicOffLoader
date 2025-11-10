# Policy baseline sweep

This experiment demonstrates the `nicloadoff_cli --batch` workflow for running multiple policy
configurations in one shot. The batch manifest writes each run's JSON report under `results/` and
maintains a CSV summary that downstream analysis scripts can ingest directly.

## Files
- `batch.yaml` — batch manifest consumed by `nicloadoff_cli --batch`.
- `results/` — per-run JSON outputs plus the aggregated CSV (ignored from version control).
- `calc_skew_baselines.py` — helper to summarize host vs. NIC placement deltas per skew tier from the placement sweep CSV.
- `../workloads/tests/policy_queue_flip.yaml` — deterministic workload used by the acceptance test to prove queue reordering under heavy load.

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
ships both the baseline KV workload and DAG-heavy `skew_dag` mixes (including the Zipf>1.2
`skew_dag_zipf14` and even harsher `skew_dag_zipf18` variants) so policy comparisons cover single-path
and dependency-driven scenarios;
feel free to add new `runs:` entries for additional policies or workloads—the CLI validates policy names
and will append new rows to the CSV automatically. The `summarize.py` helper reads the CSV and prints a quick comparison
 table (sort by throughput by default or mean latency via `--sort mean_latency`), and now surfaces
`arrival_label`/`background_load`/`zipf_alpha` columns automatically while still supporting metadata-aware filtering/grouping
(`--filter workload_label=skew_dag --group-by policy`) plus extra columns via `--columns`. Its output now includes both a
`reorders` column sourced from `policy_metrics.waiting_reorders` and a normalized `reorders_per_task` ratio
(computed on the fly when the column is missing), so you can
immediately spot reorder-heavy runs (grouped views average the counts). `export_normalized.py`
groups repeated runs, emits both a normalized CSV and (optionally) Parquet table (requires `pyarrow`),
mirrors the `policy_metrics.waiting_reorders` counter into those exports, derives a `waiting_reorders_per_task`
metric so notebooks can reason about reorder rates independent of throughput, and can join the static placement summary (`--static-summary`, defaults to `results/dag_static_summary.csv`)
to annotate each DAG workload with host/NIC baseline throughput and latency deltas.
Skew-DAG tiers (baseline vs. stress) defined in `workloads/tools/skew_dag_config.json` are expanded into both
the placement manifest and this batch file whenever you rerun `python3 workloads/tools/generate_skew_dags.py`,
so adding a new tier only requires editing the config once.
Variant metadata such as `zipf_alpha` and scenario annotations like `arrival_label` are captured in the
batch CSV (and therefore in the normalized export) so downstream tooling can pivot on light/stress tiers
without guessing from the workload name. The manifest now also ships the `policy_queue_flip` workload (prefer-host vs.
prefer-nic), which deterministically produces waiting-queue reorders so that the new ratio columns have non-zero coverage in every sweep.
`plots/policy_baseline.py` consumes the normalized CSV to render throughput/latency comparison charts
plus a waiting-reorder subplot that now charts the per-task ratio (and annotates total counts) and stores them under `plots/generated/`. `import_static_traces.py` ingests the static placement sweep
CSV and emits `dag_static_summary.csv`, capturing host- vs. NIC-pinned baselines for the skewed DAG
scenarios so policy experiments can reference the fixed placements directly; the generated summary now
feeds `tests/static_summary_regression_test.py`, which runs via `ctest` to keep those deltas pinned.

### Skew placement reference

Use the helper below to refresh placement deltas before adjusting policy logic:

```bash
python3 experiments/placement_baseline/run.py
python3 experiments/policy_baseline/calc_skew_baselines.py
python3 experiments/policy_baseline/tests/policy_batch_acceptance_test.py --cli build/tools/cli/nicloadoff_cli
```

`calc_skew_baselines.py` scans the placement CSV and writes `results/skew_tier_baselines.csv` with host vs. NIC
throughput/latency per tier. The current snapshot (seed 1, BF2 profile) is:

| Workload          | Arrival Label | Background | Scale | Host Tput (kops/s) | NIC Tput (kops/s) | Δ Tput (kops/s) | Host Mean (us) | NIC Mean (us) | Δ Mean (us) |
|-------------------|---------------|------------|-------|--------------------|-------------------|-----------------|----------------|---------------|-------------|
| `skew_dag`        | baseline      | light      | 1.0   | 257.5              | 263.0             | +5.5            | 0.97           | 0.85          | +0.13       |
| `skew_dag`        | stress        | heavy      | 1.3   | 327.4              | 336.3             | +8.9            | 0.97           | 0.85          | +0.13       |
| `skew_dag_heavy`  | baseline      | light      | 1.0   | 256.9              | 262.3             | +5.4            | 1.03           | 0.91          | +0.13       |
| `skew_dag_heavy`  | stress        | heavy      | 1.4   | 348.8              | 358.9             | +10.1           | 1.03           | 0.91          | +0.13       |
| `skew_dag_zipf14` | baseline      | light      | 1.0   | 246.2              | 263.0             | +16.7           | 1.33           | 0.92          | +0.40       |
| `skew_dag_zipf14` | stress        | heavy      | 1.4   | 329.5              | 360.1             | +30.7           | 1.33           | 0.92          | +0.40       |
| `skew_dag_zipf18` | baseline      | light      | 1.0   | 236.4              | 263.1             | +26.6           | 1.62           | 0.95          | +0.67       |
| `skew_dag_zipf18` | stress        | heavy      | 1.5   | 329.8              | 384.0             | +54.2           | 1.62           | 0.95          | +0.67       |

These numbers provide the ground truth deltas policy hooks should target when prioritising NIC placement. Update
the table after re-running the placement sweep with new profiles or arrival tiers. The acceptance test above
re-runs the policy batch and then drives `workloads/tests/policy_queue_flip.yaml` (two host-heavy tasks followed by a NIC-heavy task contending for the same host CPUs). It confirms each skew tier (identified by `workload_label`, `arrival_label`,
and `background_load`) appears in the batch CSV with both prefer-host and prefer-NIC policies and checks that the queue-flip scenario records at least one waiting-queue reorder plus a reduced queue time for the NIC-heavy task. To support that check,
every CLI JSON report now includes a `policy_metrics` object with a `waiting_reorders` counter, and the batch CSV plus
normalized exports keep that column—along with the derived `waiting_reorders_per_task` ratio—in sync for quick analysis, dashboards, and plots.
