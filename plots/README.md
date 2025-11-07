# plots/

Plotting scripts and notebooks for figure generation live here. Keep rendered figures under `plots/generated/` or within experiment-specific folders.

## placement_baseline.py
Generates throughput/latency vs. arrival-scale plots for the placement benchmark sweep.

```bash
python3 plots/placement_baseline.py \
  --csv experiments/placement_baseline/results/placement_sweep.csv \
  --output plots/generated/placement_baseline.png
```
