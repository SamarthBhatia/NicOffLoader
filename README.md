# NicLoadOff — SmartNIC Offload Simulator

NicLoadOff is a discrete-event simulation framework for exploring dynamic SmartNIC offload policies. The project targets asymmetric host↔NIC systems (e.g., NVIDIA BlueField) and evaluates how state-aware policies compare with static, all-host or all-NIC baselines across realistic workloads.

## Project Goals
- Model hosts, SmartNIC SoCs, DRAM limits, and interconnect latencies/bandwidth with configurable profiles.
- Express workloads as task DAGs with placement annotations.
- Provide a minimal policy DSL that consumes runtime state snapshots and issues placement decisions.
- Deliver reproducible experiments that sweep arrival rates, background loads, and path degradations while collecting throughput, latency (mean/p99), and resource utilization.

Primary planning artifacts live in `scope.md` (goals, success criteria) and `status.md` (phase-by-phase progress).

## Repository Layout
- `sim/` — simulator engine (event loop, resource models).
- `policies/` — policy DSL parser/runtime and stock policies.
- `workloads/` — workload DAG specs, arrival models, templates.
- `profiles/` — hardware parameter files (e.g., BlueField-2).
- `experiments/` — experiment manifests, run logs, and results.
- `plots/` — analysis scripts and generated figures.
- `scope.md` — Phase 0 scope and success criteria.
- `status.md` — rolling project status (Done / Next / Remaining).

## Getting Started
### Prerequisites
- CMake ≥ 3.20
- Ninja (recommended) or another generator
- A C++20-compatible toolchain (Clang 17+, GCC 11+, or MSVC 2022)

### Install Dependencies
- **macOS (Homebrew)**
  ```bash
  brew update
  brew install cmake ninja llvm
  ```
  Clang from Homebrew installs under `/opt/homebrew/opt/llvm/bin/clang++`; pass `-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++` if you want to match CI.
  The CI logs show Homebrew emits a short “press Ctrl+C to cancel” countdown before continuing—hold tight and let the install proceed; it does not indicate a failure.
- **Ubuntu 22.04+**
  ```bash
  sudo apt update
  sudo apt install -y build-essential clang-17 cmake ninja-build
  sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-17 170 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-17
  ```
  GCC 11+ also works; switch compilers via `-DCMAKE_CXX_COMPILER`.
- **Windows (MSVC 2022)**
  - Install Visual Studio 2022 with the *Desktop development with C++* workload and CMake component.
  - Install Ninja via [ninja-build.org](https://ninja-build.org/) or `choco install ninja`.

Verify each tool:
```bash
cmake --version
clang++ --version   # or g++/cl.exe
ninja --version     # optional, skip if using an alternate generator
```

### Formatting Tools
- Install `clang-format` through your package manager (`brew install clang-format`, `sudo apt install clang-format`, or the Visual Studio installer).
- Create a small virtual environment for `cmake-format` to avoid system Python restrictions:
  ```bash
  python3 -m venv .cmake-format
  source .cmake-format/bin/activate
  pip install cmakelang==0.6.13
  # Optional: echo "$(pwd)/.cmake-format/bin" >> ~/.bash_profile
  ```
  Reactivate the environment (or export the path) before running `cmake-format`.

### Configure & Build
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

### Quick TUI Run (recommended for first-time verification)
1. Build as above (`cmake --build build`).
2. Launch the TUI:
   ```bash
   ./build/tools/tui/nicloadoff_tui
   ```
3. Pick a profile/workload (use arrow keys + Tab to move between lists), press Enter to load, then Space to start. Cycle policies with `p` (built-ins plus DSL configs discovered under `policies/examples/`).

TUI controls (always visible on the left panel):
- `↑/↓` navigate, `Tab` swap menu, `Enter` load, `Space` run/pause, `n` step once, `r` reset seed
- `H`/`N` toggle host/NIC stochastic modes; `m` cycles the `arrival_label` metadata sent to DSL policies
- `p` cycles policies; `s` saves the current metrics report; `q` quits
- `[`/`]` shrink/grow the queue rolling window (10 000 µs steps), `;`/`'` adjust the utilization window, `-`/`=` adjust the sojourn window (16-task steps), and `z` clears the rolling samples so you can sweep horizons mid-run

The right-hand status panel now calls out the queue/util/sojourn rolling window sizes next to their live averages and sample counts, so you can keep an eye on the policy’s inputs (and resets) while stepping through events.
When you press `s` to export metrics, the resulting JSON now includes the same `rolling_window_events` history the CLI produces, making it easy to replay adjustments from interactive sessions.

### Run Tests
```bash
ctest --test-dir build
```

The `scheduler_property_fuzz_test` target now sweeps multiple policy hooks to enforce resource/timeline invariants. It is included automatically when you run `ctest --test-dir build` (≈0.1 s on CI). For a heavier local sweep, export `NICLOADOFF_FUZZ_STRESS=1` before invoking `ctest` to enable higher seed/burst counts and intra-trial policy mixing (~0.7 s on a laptop).

The initial smoke test exercises the placeholder event queue implementation; expand the suite as simulator modules arrive.

### Pre-PR checklist
Before opening a pull request, please run:

1. `cmake --build build`
2. `ctest --test-dir build --output-on-failure`
3. `NICLOADOFF_FUZZ_STRESS=1 ctest --test-dir build -R scheduler_property_fuzz_test`

This ensures both the default and stress-mode fuzz harness sweeps stay green alongside the rest of the simulator suite.

### Run a CLI simulation
Once you have a profile and workload YAML ready, invoke the single-run CLI and optionally select a built-in policy hook:

```bash
./build/tools/cli/nicloadoff_cli \
  --profile profiles/bf2_default.yaml \
  --workload workloads/examples/sequential_host.yaml \
  --policy prefer-nic \
  --output run.json
```

Each summary line now echoes the configured queue/util/sojourn windows plus the latest rolling queue/utilization/sojourn averages so you can confirm the policy’s live inputs without cracking open the JSON report.
Completed runs also emit a `rolling_window_events` array (and corresponding batch-CSV log) that lists every configure/reset action with timestamps, so downstream policy analysis can correlate metrics with on-the-fly tuning.

Available policy identifiers match the TUI presets: `none`, `descending-id`, `limit-active-1`, `prefer-host`, `prefer-nic`, and the new `prefer-adaptive`, which leans toward NIC-heavy tasks whenever the rolling queue/utilization windows show the host saturating (and swings back toward host-heavy work once NIC contention dominates). Rolling stats flow into the CLI batch CSV/plots via the `rolling_*` columns, and the DSL can reference them directly through `when.metric` (see the list below) so scripted policies react to live queue/utilization spikes without dropping down to C++.

Rolling metrics now drive policy decisions as well, so you can tune the look-back windows directly from the CLI:

```bash
  --rolling-queue-window-us <µs>    # horizon for the waiting-queue average/peak (default 50_000)
  --rolling-util-window-us <µs>     # horizon for host/NIC utilization averages (default 50_000)
  --rolling-sojourn-window-tasks <N>  # number of most recent tasks tracked in the sojourn stats (default 128)
```

Need to replay the same adjustments without the TUI? Point `--rolling-window-schedule` at a YAML script listing timestamped events. Each entry sets `at_us`, defaults to `action: configure`, and can update `queue_us`, `util_us`, and/or `sojourn_tasks` (plus `reset_samples: true` to flush metrics). Use `action: reset` for a pure reset:

```yaml
events:
  - at_us: 0.0
    queue_us: 10000.0
    reset_samples: true
  - at_us: 80.0
    action: reset
  - at_us: 120.0
    action: configure
    util_us: 20000.0
    sojourn_tasks: 64
```

Every batch/manifest entry also accepts a `rolling_windows:` block with `queue_us`, `util_us`, and/or `sojourn_tasks` keys if you prefer YAML-based overrides, plus an optional `rolling_window_schedule:` block (either `events:` inline or `from_file: path/to/script.yaml`) so sweeps/CLI manifests replay the same configure/reset timelines as the headless CLI.

#### DSL-driven policies

The `dsl` policy id loads a YAML-based rule engine so you can author scheduling/admission heuristics without recompiling:

```bash
./build/tools/cli/nicloadoff_cli \
  --profile profiles/bf2_default.yaml \
  --workload workloads/tests/policy_queue_flip.yaml \
  --policy dsl \
  --policy-config policies/examples/adaptive.dsl.yaml
```

For a stage-aware DAG example, point the DSL at `policies/examples/stage_match.dsl.yaml` (with `workloads/examples/skew_dag.yaml`). A sample manifest (`policies/examples/stage_match_manifest.yaml`) pins the metadata used by the `match` block.
For skewed DAGs, `policies/examples/skew_dag_stage.dsl.yaml` plus `policies/examples/skew_dag_stage_manifest.yaml` biases `db_lookup` toward NIC-heavy tasks under bursty arrivals, then swings back toward host-heavy response serialization and clamps admission during bursts.
Need a queue-aware variant? `policies/examples/skew_dag_stage_balance.dsl.yaml` uses `queue_avg` to detect when `db_lookup` is backlogged, pushes NIC-heavy stages forward, and ships with `policies/examples/skew_dag_stage_balance_manifest.yaml` so you can run it directly. Prefer highlighting NIC saturation instead? `policies/examples/skew_dag_nic_balance.dsl.yaml` waits for `nic_util_avg` to spike during the skew DAG’s stress tier and temporarily prioritizes host-heavy stages (`policies/examples/skew_dag_nic_balance_manifest.yaml` wires in the metadata knobs).

Each rule has an optional `when` condition, an optional `match` block, and an `action`. Conditions compare one of the rolling metrics (`queue_avg`, `queue_peak`, `host_util_avg`, `host_util_peak`, `nic_util_avg`, `nic_util_peak`, `sojourn_mean_us`, `sojourn_p95_us`, `sojourn_p99_us`) against a threshold using `>`, `>=`, `<`, or `<=`. The `match` block can gate on scenario metadata (keys placed under `metadata:` in a manifest) and/or restrict a rule to a specific DAG stage via `stage: <name>` or `stage_index: <N>`; only waiting tasks that satisfy the stage predicate are reordered while other tasks keep their relative position. Rules are evaluated in order: the first rule that produces a waiting-order directive wins that field, and the first rule that produces an admission directive wins that field, so later rules can act as fallbacks for whichever directive is still unset. Actions currently support `reorder: prefer-host|prefer-nic` (mirroring the built-in skew policies) and `admission: { max_active: N }`. The sample config under `policies/examples/adaptive.dsl.yaml` biases toward NIC-heavy work when the host queue builds up, swings back toward host-heavy tasks when the NIC saturates, and relaxes the admission limit once queues drain. Batch manifests can set `policy_config: path/to/rules.yaml` alongside `policy: dsl`, and `defaults.policy_config` applies to every run unless overridden.
Each run summary now prints the policy’s waiting-queue reorder count and normalized per-task ratio in addition to throughput/latency so you can confirm policy hooks are active without opening the JSON report.

Want to watch the builtin `prefer-adaptive` policy react to a deterministic burst? `workloads/tests/prefer_adaptive_host_burst.yaml` launches four host-heavy stages alongside a NIC-heavy task. Running the CLI with `--policy prefer-adaptive --rolling-queue-window-us 10 --rolling-util-window-us 10` shows the host-heavy stage getting deferred (queue time increases) while the NIC-heavy task is pulled forward; the new `policy_prefer_adaptive_test` CTest wraps the same scenario so regressions trip automatically.

You can also supply defaults via a manifest:

```yaml
# run_manifest.yaml
profile: profiles/bf2_default.yaml
workload: workloads/examples/sequential_host.yaml
policy: prefer-host
output: results/run_host.json
seed: 7
metadata:
  workload_label: seq_host
  arrival_label: deterministic
service_modes:
  host: deterministic
  nic: stochastic
```

Run it with `./build/tools/cli/nicloadoff_cli --config run_manifest.yaml`. Command-line flags still override manifest settings, and any `metadata:` entries are threaded into the JSON report (and batch CSVs) so experiment dashboards can join runs by scenario labels.
To replay rolling adjustments inside a manifest, add `rolling_window_schedule: schedules/rolling_swaps.yaml` (paths are resolved relative to the manifest) and reuse the same `events:` format shown above.

### Batch CLI runs
For policy sweeps, pass a batch manifest that lists multiple runs. Batch mode executes each entry,
produces individual JSON reports, and optionally appends an aggregate CSV:

```yaml
# experiments/policy_baseline/batch.yaml
defaults:
  profile: profiles/bf2_default.yaml
  workload: workloads/examples/kv_read_template.yaml
  output_dir: experiments/policy_baseline/results
  metadata:
    workload_label: kv_read
    arrival_model: periodic
  service_modes:
    host: deterministic
    nic: deterministic
  rolling_window_schedule:
    events:
      - at_us: 0.0
        queue_us: 10000
        reset_samples: true
      - at_us: 60.0
        action: reset
csv: experiments/policy_baseline/results/policy_baseline.csv
runs:
  - name: prefer-host
    policy: prefer-host
  - name: prefer-nic
    policy: prefer-nic
  - name: dag-prefer-host
    workload: workloads/examples/skew_dag.yaml
    policy: prefer-host
  - name: dag-prefer-nic
    workload: workloads/examples/skew_dag.yaml
    policy: prefer-nic
```

Each run inherits the default `rolling_window_schedule` (or can supply its own block with `events:` or `from_file:`) so every CSV/export captures the exact configure/reset timeline you expect.

Invoke it with:

```bash
./build/tools/cli/nicloadoff_cli --batch experiments/policy_baseline/batch.yaml
python3 experiments/policy_baseline/summarize.py
python3 experiments/policy_baseline/export_normalized.py
python3 plots/policy_baseline.py --csv experiments/policy_baseline/results/policy_baseline_normalized.csv
```

Each run inherits the defaults unless a field is overridden. The CSV header captures policy, seed,
service modes, throughput, latency percentiles, the peak waiting-queue depth, and any metadata fields
you define (e.g., `arrival_model`, `background_load`) so analysis scripts can ingest one table without
re-parsing the JSON outputs. Use `experiments/policy_baseline/summarize.py` to dump a quick comparison
table, `experiments/policy_baseline/export_normalized.py` to deduplicate rows and (optionally) emit a
Parquet table, and `plots/policy_baseline.py` to render throughput/latency charts. See
`experiments/policy_baseline/README.md`
for more details.

### Launch the ncurses TUI
The interactive TUI lets you inspect profiles, step through workloads, and experiment with policy hooks without leaving the terminal.

1. Build the project (see above) so the `nicloadoff_tui` binary is generated.
2. From the repository root, run:
   ```bash
   ./build/tools/tui/nicloadoff_tui
   ```
3. Use the on-screen hints—`↑/↓` navigate menus, `Tab` swaps between profile/workload lists, `Space` toggles run/pause, `n` steps a single event, `H`/`N` toggle deterministic vs. stochastic sampling, `m` cycles the `arrival_label` metadata (handy for DSL matches), `p` cycles policies (built-ins plus any DSL configs discovered under `policies/examples/`), `s` saves metrics, and `q` exits.

The status panel shows the active policy, admission limits (if any), live queue/resource metrics, and the cumulative policy waiting-reorder count + per-task ratio so you can watch hooks make progress while stepping through events. When a DSL config is selected, the panel also prints the YAML path to confirm which rule file is driving the run.

## Technology Stack (current plan)
- C++20 for simulator and policy modules.
- YAML/JSON for hardware profiles and workload specifications.
- Python (matplotlib / seaborn) for data reduction and plotting.

## Contributing
Update `status.md` after each work session to record progress and queue follow-up tasks.

Format C++ and CMake sources with `clang-format` and `cmake-format` before pushing—the CI workflow enforces both.
