# plots/

Plotting scripts and notebooks for figure generation live here. Keep rendered figures under `plots/generated/` or within experiment-specific folders.

## placement_baseline.py
Generates throughput, latency (mean + p95), and peak waiting-queue depth vs. arrival-scale plots for the placement benchmark sweep.

```bash
python3 plots/placement_baseline.py \
  --csv experiments/placement_baseline/results/placement_sweep.csv \
  --output plots/generated/placement_baseline.png
```

## policy_baseline.py
Consumes the normalized batch output (`experiments/policy_baseline/results/policy_baseline_normalized.csv`) and renders
side-by-side throughput and latency (mean + p95) comparisons per policy, annotated with the peak
waiting-queue depth plus cumulative vs. recent waiting-reorder ratios. Run `python3 experiments/policy_baseline/export_normalized.py`
first if you only have the raw batch CSV. For notebook-based analysis or dashboard prep, the pandas walkthrough under
`experiments/policy_baseline/notebooks/rolling_reorder_example.md` mirrors the plot’s paired ratio view from a DataFrame.

```bash
python3 plots/policy_baseline.py \
  --csv experiments/policy_baseline/results/policy_baseline_normalized.csv \
  --output plots/generated/policy_baseline.png
```
