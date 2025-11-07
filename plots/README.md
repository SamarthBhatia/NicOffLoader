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
Consumes the `experiments/policy_baseline/results/policy_baseline.csv` batch output and renders
side-by-side throughput and latency (mean + p95) comparisons per policy, annotated with the peak
waiting-queue depth.

```bash
python3 plots/policy_baseline.py \
  --csv experiments/policy_baseline/results/policy_baseline.csv \
  --output plots/generated/policy_baseline.png
```
