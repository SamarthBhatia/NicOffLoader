# Placement baseline sweep

This experiment replays the static placement benchmark across the KV-read, TCP-split, and skewed DAG
workloads while scaling the arrival schedule. The sweep populates a CSV that downstream plotting
scripts consume when generating throughput/latency figures and comparing static placement modes
(`host_pinned`, `nic_pinned`, or `hint_respect`).

## Files
- `manifest.yaml` — declares the profile, workloads, arrivals, arrival-scale factors, and placement modes to test.
- `run.py` — orchestrates the sweep by invoking `placement_benchmark` with the requested arguments.
- `results/` — houses the JSON summaries per run plus the combined `placement_sweep.csv`.

## Running the sweep
1. Build the benchmark target:
   ```bash
   cmake --build build --target placement_benchmark
   ```
2. Execute the sweep (this rewrites the CSV by default):
   ```bash
   python3 experiments/placement_baseline/run.py
   ```
   Pass `--append` if you would like to keep the existing CSV and continue appending rows.
3. Render the plots:
   ```bash
   python3 plots/placement_baseline.py
   ```

`run.py` will create `experiments/placement_baseline/results/placement_sweep.csv` (columns now include
mean/p50/p95/p99 latency, peak waiting-queue depth, and the placement mode) and stash the JSON
summaries produced by `placement_benchmark` alongside it. The manifest can enumerate multiple
`placement_modes` per workload entry so the sweep emits HostPinned vs. NICPinned comparisons in a
single pass (the skewed DAG scenario demonstrates this). The plotting step reads the CSV and writes
figures to `plots/generated/`. PyYAML is optional: when it is not installed the script falls back to
loading `manifest.yaml` as JSON, so keep custom manifests JSON-compatible or install PyYAML.
