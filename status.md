# NicLoadOff Project Status

_Status is tracked per phase. Update the **Done / Next / Remaining** bullet lists whenever work ships or plans change._

## Phase 0 — Project setup
- **Focus:** Scope definition, repo scaffolding, CI/tooling.
- **Done:** Established contributor guide (`AGENTS.md`); created status tracking workflow; captured scope & success criteria in `scope.md`; scaffolded repository layout; introduced CMake+Ninja build with placeholder simulator library and smoke test (`sim/`); deployed GitHub Actions workflow covering Linux/macOS configure, build, and `ctest`; documented local toolchain/bootstrap requirements in `README.md`; added clang-format/cmake-format enforcement to CI with repo configs; switched CI cmake-format install to a virtualenv to satisfy PEP 668; verified clean macOS configure/build/test from a fresh build tree; reformatted root and simulator CMakeLists with `cmake-format` to clear the Linux check failure; re-ran CI builds on Linux/macOS to confirm `cmake-format` lint passes alongside the Homebrew `clang-format` install; documented the macOS/Homebrew cancellation countdown tip in `README.md`.
- **Next:** Watch the next CI macOS run for additional bootstrap messaging and fold any new guidance back into the docs.
- **Remaining:** Ensure clean checkout builds/tests successfully on all target platforms.

## Phase 1 — Requirements & baseline research
- **Focus:** Requirements spec and related-work matrix.
- **Done:** Drafted baseline requirements specification (`requirements.md`) and long-form outline (`thesis/requirements_outline.md`) capturing functional/non-functional targets plus initial parameter source catalog; created initial hardware/workload parameter source log (`profiles/SOURCES.md`); added baseline BlueField-2 hardware profile (`profiles/bf2_default.yaml`) and documented the schema/loader plan (`profiles/README.md`).
- **Next:** Validate BF2 parameter picks against latest datasheets/blog posts, capture precise citations, and outline the related-work comparison matrix structure with acceptance criteria for each requirement.
- **Remaining:** Complete related-work comparison matrix; summarize gaps motivating NicLoadOff.

## Phase 2 — Minimal simulator core
- **Focus:** Discrete-event engine and initial system model.
- **Done:** Added foundational simulator types (`sim_types.hh`), extended the event queue to track typed metadata, implemented the YAML-backed profile loader with schema validation (plus tests), wired the simulator build/test scaffolding, drafted concrete Resource/Task abstractions with a basic event-queue-driven scheduler (plus coverage), introduced a profile-aware service-time model with deterministic/stochastic sampling (plus integration tests), wired profile-derived resource inventories + multi-stage contention scenarios into the scheduler tests, added workload-spec helpers so tasks are composed from profile-aware stage/resource descriptors, built an ncurses-driven TUI to step simulations, inspect resources, and review recent events, introduced a YAML-backed workload loader (with fixtures/tests) now driving both the scheduler smoke and TUI menus, surfaced loader diagnostics in the TUI, added host/NIC stochastic toggles that re-seed the scheduler on demand, instrumented the scheduler with per-task queue/service metrics, enabled JSON exports straight from the TUI, introduced a reusable run-metrics aggregator now exposed via the scheduler for policy consumers (and exercised by TUI/CLI tooling), exposed a policy-facing state snapshot alongside an initial policy hook interface grounded in the aggregated metrics, wired scheduler-side hook registration with waiting-queue reorder and admission throttling directives (plus regression coverage), surfaced policy selection/telemetry through both the CLI (`--policy`) and ncurses TUI (hotkey cycling with live snapshots), added manifest-driven CLI runs plus automated coverage exercising host/NIC skew policies, codified a latency regression proving NIC-skew policies shrink queue buildup on skewed workloads, finished the workload DAG scaffolding with schema validation, conversion helpers, and regression fixtures/tests, stood up the `TaskDagRuntime` planner with dependency tracking + unit coverage, wired that runtime into both the CLI and TUI loops via a submission controller so DAG workloads release successors dynamically (with regression coverage), added an integrated scheduler regression that mixes DAG-driven releases with profile-derived multi-stage contention to validate queue/metric reporting, introduced a stochastic service-profile regression that locks queue/metric accounting against seeded exponential draws, introduced a heavy-contention regression that saturates host/NIC resources while exercising admission throttles plus waiting-queue reorders, added a high-variance burst regression that captures simultaneous resource saturation under stochastic service draws with recorded policy snapshots and queue/latency validation, delivered an admission-limit oscillation regression plus an oscillation stress test that forces rapid limiter toggles while confirming waiting-order stability, and introduced a property-based scheduler fuzz harness (with stress-mode knob) to sweep seeds/bursts and policy hooks.
- **Next:** Monitor the new oscillation/fuzz stress tests in CI; if they stay green, shift focus toward Phase 3 workload-schema design.
- **Remaining:** Extend scheduler tests to stress stochastic edge cases, high-variance sampling bursts, and extreme resource exhaustion scenarios.

## Phase 3 — Workload model
- **Focus:** Task DAGs and baseline workloads.
- **Done:** Documented the YAML workload schema (`workloads/SCHEMA.md`) covering tasks, DAG nodes, instructions/byte hints, and placement metadata referenced by `workload_loader.cc`; added example templates (`workloads/examples/kv_read_template.yaml`, `tcp_split_template.yaml`) exercising the new metadata fields; introduced arrival-model fixtures (`workloads/arrivals/poisson_bursty.yaml`, `periodic_sweep.yaml`) for bursty Poisson and periodic scenarios; implemented the `placement_benchmark` C++ harness (under `tools/placement/`) that replays templates across deterministic vs. bursty arrivals and emits throughput/latency summaries; captured baseline numbers (KV periodic ≈240 kops/s, TCP bursty ≈36 kops/s) to seed the static comparison; added an arrival-scaling knob plus `experiments/placement_baseline` (manifest + sweep driver) and `plots/placement_baseline.py` so benchmark CSVs immediately feed the plotting pipeline; extended the sweep outputs with latency percentiles (p50/95/99), peak waiting-queue depth, and placement-mode tags; anchored a skewed KV DAG (`workloads/examples/skew_dag.yaml`) that now runs in both host- and NIC-pinned configurations; piped the placement sweep into `experiments/policy_baseline/tests/static_summary_regression_test.py` so host-vs-NIC deltas stay pinned in CI; added the Zipf>1.2 `skew_dag_zipf14` workload plus manifest/policy-batch coverage to widen the gap; introduced `workloads/tools/generate_skew_dags.py` + `skew_dag_config.json` so all skew tiers (now including the more extreme `skew_dag_zipf18`) regenerate manifests/batches/YAML from one config; threaded the static placement deltas into the normalized policy exports for downstream analysis; extended the generator to emit labeled arrival-scale scenarios whose metadata now flows through the placement manifest, CSVs, and policy batches; surfaced the scenario metadata directly in the placement/policy summarizers; piped the manifest metadata map into CLI policy snapshots so forthcoming DSL hooks can gate on `arrival_label`/`zipf_alpha`; automated the light/heavy skew tiers so `generate_skew_dags.py` now updates both the placement manifest and policy batch metadata/rows in one sweep; captured the host-vs-NIC throughput/latency deltas per skew tier via `experiments/policy_baseline/calc_skew_baselines.py` (with results table documented in the README); wired those recorded deltas into a regression (`experiments/policy_baseline/tests/static_summary_regression_test.py`) so policy/DSL hook changes immediately trip when host-vs-NIC gaps shift; instrumented the CLI/run-metrics path with `policy_metrics.waiting_reorders` plus a queue-flip acceptance test so we prove `prefer-nic` actually reorders tasks under heavy load; mirrored that counter through the batch CSV + normalized exports so downstream analysis can trend reorder counts without scraping JSON; and derived a `waiting_reorders_per_task` metric that now ships via the normalized exports, summarizer, and plots so reorder-heavy runs stand out even when throughput changes.
- **Next:** Add a lightweight rolling estimator (e.g., last 100 tasks) for `waiting_reorders_per_task` so the TUI/CLI can highlight spikes without waiting for the full run to finish.
- **Remaining:** Keep the skew-tier docs/regressions current as new tiers are added and ensure the metadata surfaced downstream continues to match those documented deltas.

## Phase 4 — Observability + state API
- **Focus:** Expose runtime metrics for policies.
- **Done:** Not started.
- **Next:** Design metric collection interfaces and storage for rolling windows (utilization, queue depth, sojourn estimates).
- **Remaining:** Implement deterministic test showing utilization shift after synthetic spike.

## Phase 5 — Policy engine + mini-DSL MVP
- **Focus:** Parser, runtime, stock policies.
- **Done:** Not started.
- **Next:** Define grammar (events, predicates, actions) and AST representation.
- **Remaining:** Evaluate policies per decision, add fallback logic, implement stock policies, add unit tests with mocked state.

## Phase 6 — Hardware profiles & parameters
- **Focus:** Parameterized BlueField-2/BF3 system models.
- **Done:** Seeded BF2 baseline profile (`profiles/bf2_default.yaml`) with sourced parameters and maintained the provenance log (`profiles/SOURCES.md`).
- **Next:** Gather additional service-time/throughput estimates for BF2 (hash lookup, serialization, PCIe latency variance) and add sensitivity variants (e.g., degraded link, higher MTU).
- **Remaining:** Expand profiles to cover BF3/DPA drafts, encode contention variance knobs, run sensitivity sweep for MTU effects.

## Phase 7 — Validation microbenchmarks
- **Focus:** Trend validation against published data.
- **Done:** Not started.
- **Next:** Select benchmark scenarios (RDMA throughput vs MTU, CPU-bound vs I/O-bound offload).
- **Remaining:** Implement simulations, generate plots, document deviations from literature.

## Phase 8 — Experiment harness
- **Focus:** CLI runner, output schema, plotting scripts.
- **Done:** Seeded single-run CLI harness (`nicloadoff_cli`) that emits aggregated JSON metrics for simulator runs, locked its JSON contract with a regression test, shipped an experiment/pipeline pairing (`experiments/placement_baseline/run.py` + `plots/placement_baseline.py`) that sweeps arrival scales and renders throughput/latency (mean + p95) plus peak queue-depth figures straight from the placement benchmark CSV, and added a first-class batch workflow (`nicloadoff_cli --batch`) with YAML manifests, CSV export, regression coverage, metadata propagation, a sample policy sweep (covering both KV and DAG workloads), plus summarize/normalize/plot helpers under `experiments/policy_baseline/`. Placement sweeps now emit placement-mode tags and normalized exports, while the policy pipeline can filter/group by manifest metadata.
- **Next:** Thread the normalized CSV/Parquet schema into downstream notebooks and share the manifest metadata fields with CLI batches so policy-vs-placement comparisons live under one harness.
- **Remaining:** Produce reproducible plots for policy sweeps (throughput vs lambda, latency percentiles), add manifest-driven batches for DAG workloads, and archive results under `experiments/results/`.

## Phase 9 — Core experiments
- **Focus:** Policy comparison matrix and reporting.
- **Done:** Not started.
- **Next:** Translate experiment matrix into executable configs (profiles/, workloads/, policies/).
- **Remaining:** Run sweeps, archive CSVs/figures in `experiments/results/`, draft 1–2 page narrative.

## Phase 10 — Extensions
- **Focus:** Optional learning hooks and BF3 profile draft.
- **Done:** Not started.
- **Next:** Define interface for pluggable scorers.
- **Remaining:** Add BF3/DPA profile with adjustable compute model, run baseline comparison.

## Phase 11 — Engineering hardening
- **Focus:** Performance, determinism, coverage.
- **Done:** Not started.
- **Next:** Identify profiling tooling and target hot loops for optimization.
- **Remaining:** Implement deterministic seeds, config validation, achieve ≥80% unit coverage, 10× speedup over prototype.

## Phase 12 — Paper/thesis writing
- **Focus:** Documentation and artifact packaging.
- **Done:** Not started.
- **Next:** Draft outline (intro → motivation → design → evaluation → related work → limitations → conclusion).
- **Remaining:** Produce full draft, finalize figures, prepare reproducibility package and submission checklist.

## Deliverables & Success Criteria Snapshot
- Requirements doc, engine/tests, workload model, observability, policy DSL + policies, BF2 profile, validation plots, experiment harness & results, optional BF3 draft, hardening, thesis & artifacts.
- Success hinges on demonstrating dynamic policy dominance, trend-faithful sensitivity, and fully reproducible outputs.
