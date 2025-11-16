#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

has_module() {
    python3 - "$1" <<'PY'
import importlib.util
import sys
module = sys.argv[1]
sys.exit(0 if importlib.util.find_spec(module) else 1)
PY
}

echo "[analysis] exporting normalized CSV (export_normalized.py)"
python3 "$ROOT/experiments/policy_baseline/export_normalized.py"

if has_module matplotlib; then
    echo "[analysis] rendering plots/policy_baseline.py"
    python3 "$ROOT/plots/policy_baseline.py"
else
    echo "[analysis] skipping plots/policy_baseline.py (missing matplotlib; run \`python3 -m pip install matplotlib\`)"
fi

if has_module pandas; then
    if has_module matplotlib; then
        echo "[analysis] running pandas rolling reorder example"
        python3 "$ROOT/experiments/policy_baseline/notebooks/rolling_reorder_example.py"
    else
        echo "[analysis] running pandas table-only example (matplotlib missing)"
        python3 "$ROOT/experiments/policy_baseline/notebooks/rolling_reorder_example.py" --no-plot
    fi
else
    echo "[analysis] skipping pandas rolling example (missing pandas; install via \`python3 -m pip install pandas\`)"
fi

PLACEMENT_CSV="$ROOT/experiments/placement_baseline/results/placement_sweep.csv"
if [[ -f "$PLACEMENT_CSV" ]]; then
    echo "[analysis] joining policy vs placement tables"
    python3 "$ROOT/experiments/policy_baseline/join_placement.py" --placement "$PLACEMENT_CSV"
    if has_module matplotlib; then
        echo "[analysis] rendering plots/policy_vs_placement.py"
        python3 "$ROOT/plots/policy_vs_placement.py"
    else
        echo "[analysis] skipping plots/policy_vs_placement.py (missing matplotlib; run \`python3 -m pip install matplotlib\`)"
    fi
    if has_module pandas; then
        echo "[analysis] printing joined table slice (policy_vs_placement_example.py)"
        python3 "$ROOT/experiments/policy_baseline/notebooks/policy_vs_placement_example.py"
    else
        echo "[analysis] skipping joined table slice (missing pandas; install via \`python3 -m pip install pandas\`)"
    fi
else
    echo "[analysis] skipping join_placement.py (placement sweep CSV missing; run experiments/placement_baseline/run.py first)"
fi

echo "[analysis] done"
