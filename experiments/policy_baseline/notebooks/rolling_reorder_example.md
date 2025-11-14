# Rolling Reorder Walkthrough (Pandas)

This short example (and matching Python helper) shows how downstream dashboards/notebooks can consume the
normalized policy CSV and highlight both the cumulative and recent `waiting_reorders` metrics without touching the simulator sources.

## Prerequisites

```bash
python3 -m pip install pandas matplotlib
```

## Example session

```bash
# Table + plot (requires pandas + matplotlib)
python3 experiments/policy_baseline/notebooks/rolling_reorder_example.py

# Table only (skips matplotlib dependency)
python3 experiments/policy_baseline/notebooks/rolling_reorder_example.py --no-plot

# Custom slice
python3 experiments/policy_baseline/notebooks/rolling_reorder_example.py \
  --workload-label skew_dag_zipf14 \
  --arrival-label stress
```

The script/snippet:

1. Loads the normalized CSV (which already contains both cumulative and rolling reorder metrics plus the effective window size).
2. Filters to a specific workload/policy slice (defaults to `workload_label=skew_dag`; override via CLI flags) so comparisons stay readable.
3. Prints a ready-to-copy table that downstream dashboards can ingest.
4. Optionally renders a paired bar chart (using matplotlib) to visualize total vs. recent reorder ratios, mirroring the in-tree plot but directly within pandas so it can be adapted into notebooks or BI tools. When the normalized CSV includes the `rolling_queue_*` / `rolling_host_util_*` / `rolling_nic_util_*` / `rolling_sojourn_*` columns (now emitted by the batch CLI), the helper adds a second subplot that overlays queue averages/peaks with host/NIC utilization and annotates the rolling sojourn p95/p99 latencies for the selected slice.

Feel free to expand this notebook into a richer pandas/Altair workflow; the key detail is that both `waiting_reorders_per_task`
and `waiting_reorders_per_task_recent` are available in the normalized export, alongside the `waiting_reorder_recent_task_count`
column for context.
