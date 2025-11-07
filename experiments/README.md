# experiments/

Experiment configurations, run manifests, and captured outputs (CSV/Parquet summaries) are organized here. Use subfolders per study or sweep.

- `placement_baseline/` — scripts + manifest for the Phase 3 static placement benchmark sweep that feeds the plotting pipeline (now records mean/p50/p95/p99 latency plus peak waiting-queue depth per run; manifest loader prefers PyYAML but accepts JSON-only manifests when the dependency is missing).
- `policy_baseline/` — batch manifest + docs for `nicloadoff_cli --batch`, running multiple policy configurations, appending metadata-aware rows to a CSV, and providing helpers (`summarize.py`, `export_normalized.py`, `plots/policy_baseline.py`, `import_static_traces.py`) to normalize, visualize, and import the static placement baselines.
