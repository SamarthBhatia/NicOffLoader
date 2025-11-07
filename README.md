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
- `thesis/` — writing assets for the final report.
- `AGENTS.md` — contributor quickstart and workflow notes.
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

### Static placement benchmark harness
Phase 3 introduces a placement benchmark tool that replays the KV/TCP workload templates under deterministic and bursty arrivals. Example invocations:

```bash
./build/tools/placement/placement_benchmark \
  --profile profiles/bf2_default.yaml \
  --workload workloads/examples/kv_read_template.yaml \
  --arrival workloads/arrivals/periodic_sweep.yaml \
  --arrival-scale 1.5 \
  --output placement_kv_periodic.json \
  --csv placement_results.csv

./build/tools/placement/placement_benchmark \
  --profile profiles/bf2_default.yaml \
  --workload workloads/examples/tcp_split_template.yaml \
  --arrival workloads/arrivals/poisson_bursty.yaml \
  --arrival-scale 0.75 \
  --output placement_tcp_poisson.json \
  --csv placement_results.csv
```

The optional `--arrival-scale` argument rescales the arrival schedule after it is generated (values >1 tighten inter-arrival gaps, <1 stretches them), letting you sweep background load without editing the YAML fixtures. Each run emits a JSON summary (makespan, throughput, latency aggregates) so we can compare deterministic vs. bursty regimes directly: in our seed run `kv_read_template + periodic_sweep` yielded ~12.5 us makespan / 240 kops/s, whereas `tcp_split_template + poisson_bursty` stretched to ~55 us makespan / 36 kops/s with slightly lower mean latency due to larger payloads.
Passing `--csv` appends the metrics to a single file, which now feeds the Phase 8 plotting/analysis scripts via `experiments/placement_baseline/run.py` (sweep driver; uses PyYAML when available but falls back to JSON-compatible manifests) and `plots/placement_baseline.py` (figure generator, depends on `matplotlib`). The CSV/JSON rows also capture latency percentiles (p50/p95/p99) and the peak waiting-queue depth to make queue buildup obvious in downstream plots. See `experiments/placement_baseline/README.md` for details.

### Run a CLI simulation
Once you have a profile and workload YAML ready, invoke the single-run CLI and optionally select a built-in policy hook:

```bash
./build/tools/cli/nicloadoff_cli \
  --profile profiles/bf2_default.yaml \
  --workload workloads/examples/sequential_host.yaml \
  --policy prefer-nic \
  --output run.json
```

Available policy identifiers match the TUI presets: `none`, `descending-id`, `limit-active-1`, `prefer-host`, and `prefer-nic`.

You can also supply defaults via a manifest:

```yaml
# run_manifest.yaml
profile: profiles/bf2_default.yaml
workload: workloads/examples/sequential_host.yaml
policy: prefer-host
output: results/run_host.json
seed: 7
service_modes:
  host: deterministic
  nic: stochastic
```

Run it with `./build/tools/cli/nicloadoff_cli --config run_manifest.yaml`. Command-line flags still override manifest settings.

### Launch the ncurses TUI
The interactive TUI lets you inspect profiles, step through workloads, and experiment with policy hooks without leaving the terminal.

1. Build the project (see above) so the `nicloadoff_tui` binary is generated.
2. From the repository root, run:
   ```bash
   ./build/tools/tui/nicloadoff_tui
   ```
3. Use the on-screen hints—`↑/↓` navigate menus, `Tab` swaps between profile/workload lists, `Space` toggles run/pause, `n` steps a single event, `H`/`N` toggle deterministic vs. stochastic sampling, `p` cycles built-in policies, `s` saves metrics, and `q` exits.

The status panel shows the active policy, admission limits (if any), and live queue/resource metrics derived from the scheduler’s policy snapshot.

## Technology Stack (current plan)
- C++20 for simulator and policy modules.
- YAML/JSON for hardware profiles and workload specifications.
- Python (matplotlib / seaborn) for data reduction and plotting.

## Contributing
Please read `AGENTS.md` for guidelines on status tracking, coding style, and PR expectations. Update `status.md` after each work session to record progress and queue follow-up tasks.

Format C++ and CMake sources with `clang-format` and `cmake-format` before pushing—the CI workflow enforces both.
