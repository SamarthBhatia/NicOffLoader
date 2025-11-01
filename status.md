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
- **Done:** Added foundational simulator types (`sim_types.hh`), extended the event queue to track typed metadata, implemented the YAML-backed profile loader with schema validation (plus tests), wired the simulator build/test scaffolding, drafted concrete Resource/Task abstractions with a basic event-queue-driven scheduler (plus coverage), and introduced a profile-aware service-time model with deterministic/stochastic sampling (plus integration tests).
- **Next:** Pull profile-derived resource/service parameters into scheduler setup (rather than hand-built fixtures) and add validation helpers for multi-stage tasks.
- **Remaining:** Broaden scheduler tests to cover profile-driven workloads, multi-stage flows, and cross-resource contention cases.

## Phase 3 — Workload model
- **Focus:** Task DAGs and baseline workloads.
- **Done:** Not started.
- **Next:** Define workload schema (instructions, bytes in/out, placement hints).
- **Remaining:** Implement KV read template, optional TCP split, run static placement benchmark for latency/throughput.

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
- **Done:** Not started.
- **Next:** Define CLI contract (`sim run --profile …`) and result schema (CSV/Parquet fields).
- **Remaining:** Implement batch execution, produce reproducible plots (throughput vs λ, latency percentiles).

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
