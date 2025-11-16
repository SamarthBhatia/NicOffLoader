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
python3 plots/policy_baseline.py --csv experiments/policy_baseline/results/policy_baseline_normalized.csv --workload-label skew_dag --arrival-label stress
python3 experiments/policy_baseline/join_placement.py
python3 plots/policy_vs_placement.py
python3 experiments/policy_baseline/notebooks/policy_vs_placement_example.py
```

Paths inside the manifest are resolved relative to the manifest’s directory, so the sample uses `../../`
to reach repo-level fixtures. Each run entry inherits defaults for the profile, workload, seed, and
output directory. Metadata fields declared under `defaults.metadata` (or overridden per run) are copied
into the CSV so you can capture annotations like arrival models or load regimes. If you want to pin a stable
schema for downstream joins (e.g., with placement sweeps), declare `metadata_keys` at the manifest root to force those columns into the header even when a run omits them. The example manifest
ships both the baseline KV workload and DAG-heavy `skew_dag` mixes (including the Zipf>1.2
`skew_dag_zipf14` and even harsher `skew_dag_zipf18` variants) so policy comparisons cover single-path
and dependency-driven scenarios;
feel free to add new `runs:` entries for additional policies or workloads—the CLI validates policy names
and will append new rows to the CSV automatically. The `summarize.py` helper reads the CSV and prints a quick comparison
 table (sort by throughput by default or mean latency via `--sort mean_latency`), and now surfaces
`arrival_label`/`background_load`/`zipf_alpha` columns automatically while still supporting metadata-aware filtering/grouping
(`--filter workload_label=skew_dag --group-by policy`) plus extra columns via `--columns`. Its output now includes both a
`reorders` column sourced from `policy_metrics.waiting_reorders` and both normalized ratios:
`reorders_per_task` (entire run) and `reorders_per_task_recent`, which captures the last 100 completed tasks (plus the effective window length) straight from the batch CSV so you can immediately spot spikes without waiting for the full run to finish (grouped views average the counts). `export_normalized.py`
groups repeated runs, emits both a normalized CSV and (optionally) Parquet table (requires `pyarrow`),
mirrors the `policy_metrics.waiting_reorders` counter into those exports, derives a `waiting_reorders_per_task`
metric so notebooks can reason about reorder rates independent of throughput, and can join the static placement summary (`--static-summary`, defaults to `results/dag_static_summary.csv`)
to annotate each DAG workload with host/NIC baseline throughput and latency deltas. The notebook helper
(`notebooks/rolling_reorder_example.py`) now auto-detects CSV vs. Parquet and accepts `--input-format parquet`
so downstream dashboards can read the normalized schema without extra conversions.
`join_placement.py` consumes the normalized policy export (CSV or Parquet) and the placement sweep CSV to emit a joined table (CSV + Parquet) that annotates each policy run with host- vs. NIC-pinned placement metrics and deltas, making cross-policy/placement comparisons one command away (it now warns if a policy row is missing a placement match so you know to refresh placement results).
`plots/policy_vs_placement.py` renders the joined table as throughput/latency deltas versus host/NIC placements so you can visualize how policies shift performance relative to static placement baselines.
If you prefer a quick table over the joined data, `notebooks/policy_vs_placement_example.py` reads the CSV/Parquet, filters by workload/arrival, and prints a ready-to-copy slice (no plotting dependency).
The batch CSV (and therefore the normalized export, summarizer, and plots) now also records a concise snapshot of the rolling metrics surfaced by the simulator: queue depth samples/averages/peaks plus host/NIC utilization and sojourn mean/p95/p99 values.
Those values live under the `rolling_queue_*`, `rolling_host_util_*`, `rolling_nic_util_*`, and `rolling_sojourn_*` columns so downstream analysis can pivot on short-horizon congestion/utilization without parsing the per-run JSON.
Skew-DAG tiers (baseline vs. stress) defined in `workloads/tools/skew_dag_config.json` are expanded into both
the placement manifest and this batch file whenever you rerun `python3 workloads/tools/generate_skew_dags.py`,
so adding a new tier only requires editing the config once.
Variant metadata such as `zipf_alpha` and scenario annotations like `arrival_label` are captured in the
batch CSV (and therefore in the normalized export) so downstream tooling can pivot on light/stress tiers
without guessing from the workload name. The manifest now also ships the `policy_queue_flip` workload (prefer-host vs.
prefer-nic), which deterministically produces waiting-queue reorders so that the new ratio columns have non-zero coverage in every sweep.
`plots/policy_baseline.py` consumes the normalized CSV to render throughput/latency comparison charts plus a waiting-reorder subplot that now charts both the cumulative and recent per-task ratios (annotating total and recent-window counts). A fourth panel overlays the new rolling metrics (queue averages/peaks alongside host/NIC utilization and sojourn quantiles) so you can see how policies impact short-horizon congestion. All figures land under `plots/generated/`. If you prefer working directly in pandas/BI tooling, see `notebooks/rolling_reorder_example.md` for a lightweight walkthrough that loads `policy_baseline_normalized.csv`, prints the relevant columns, and recreates the side-by-side ratio plot from a notebook; it now also produces a queue/utilization subplot whenever the rolling columns are present.
`import_static_traces.py` ingests the static placement sweep
CSV and emits `dag_static_summary.csv`, capturing host- vs. NIC-pinned baselines for the skewed DAG
scenarios so policy experiments can reference the fixed placements directly; the generated summary now
feeds `tests/static_summary_regression_test.py`, which runs via `ctest` to keep those deltas pinned.

### One-shot analysis helper

Use `experiments/policy_baseline/run_analysis.sh` to regenerate the normalized CSV, plot, and pandas walkthrough in one go:

```bash
./experiments/policy_baseline/run_analysis.sh
# or via CMake/Ninja:
cmake --build build --target policy_analysis
```

The script runs `export_normalized.py`, then `plots/policy_baseline.py`, and finally the pandas helper (`notebooks/rolling_reorder_example.py`). It automatically skips steps when optional dependencies are missing (install `matplotlib`/`pandas` via `python3 -m pip install <pkg>` to enable the full pipeline).

### CI artifacts

The `policy-analysis` GitHub Actions job reuses the `cmake --build build --target policy_analysis` target and then uploads a single artifact bundle named `policy-analysis-<commit-sha>`. Each artifact contains:

- `experiments/policy_baseline/results/policy_baseline_normalized.csv`
- `experiments/policy_baseline/results/policy_baseline.parquet`
- `plots/generated/policy_baseline.png`
- `plots/generated/rolling_reorder_example.png`

To grab the latest snapshot after a CI failure:

1. Navigate to the failing workflow run → `policy-analysis` job → **Artifacts** → download `policy-analysis-<sha>.zip`.
2. Or use the GitHub CLI: `gh run download <run-id> --name policy-analysis-<sha>` and unzip locally (the paths above are preserved inside the archive).

Prefer an automated workflow? Run `python3 tools/ci/fetch_policy_artifacts.py` from the repo root. The helper looks up the most recent `CI` workflow run whose head SHA matches your current `HEAD`, downloads the `policy-analysis-<sha>` artifact, and extracts it under `artifacts/policy-analysis/extracted/`. Need the newest passing run instead? Add `--latest --status success` to grab the freshest successful job on the current branch (even if your checkout is older), or `--list` to print the last N runs (IDs, status, branch, SHA) before choosing one. By default the script shells out to the GitHub CLI (`gh`). If `gh` is unavailable (or you pass `--use-api`), it falls back to the GitHub REST API—just ensure `GITHUB_TOKEN` (or `--token`) points to a PAT with `actions:read`. Pass `--run-id <id>` (or `--branch/--sha`) to override the selection logic, `--repo owner/name` to target forks, and `--download-dir/--extract-dir` to change destinations.

Prefer using the build system instead? `cmake --build build --target fetch_policy_artifacts` runs the same helper (with the repo root as its working directory); make sure either the GitHub CLI is installed or `GITHUB_TOKEN` is set so the REST fallback can authenticate.

These artifacts mirror exactly what `run_analysis.sh` emits, so you can open the CSVs/Parquet in notebooks or inspect the rendered plots without rerunning the sweep locally. Keep the archive (or the helper’s extracted snapshot) handy when debugging CI regressions so reviewers can reproduce the failure context.

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
 and `background_load`) appears in the batch CSV with both prefer-host and prefer-NIC policies, checks that the queue-flip scenario records at least one waiting-queue reorder plus a reduced queue time for the NIC-heavy task, **and now asserts that the queue-flip prefer-NIC batch row reports both `waiting_reorders_per_task > 0` and `waiting_reorders_per_task_recent > 0` (with a non-zero recent window)** so regressions immediately trip if reordering disappears or the rolling estimator stops updating. To support that check,
every CLI JSON report now includes a `policy_metrics` object with both cumulative and rolling counters, and the batch CSV plus
normalized exports keep those columns—along with the derived ratios and window size—in sync for quick analysis, dashboards, and plots.
