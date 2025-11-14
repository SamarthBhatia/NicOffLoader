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

echo "[analysis] done"
