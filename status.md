# NicLoadOff Project Status

_Status is tracked per phase. Update the **Done / Next / Remaining** bullet lists whenever work ships or plans change._

## Phase 0 — Project setup
- **Focus:** Scope definition, repo scaffolding, CI/tooling.
- **Done:** Established contributor guide (`AGENTS.md`); created status tracking workflow; captured scope & success criteria in `scope.md`.
- **Next:** Finalize repo layout (sim/, policies/, workloads/, profiles/, experiments/, plots/, thesis/).
- **Remaining:** Stand up CI + formatting hooks, ensure `make test` (or equivalent) passes on clean checkout.

## Phase 1 — Requirements & baseline research
- **Focus:** Requirements spec and related-work matrix.
- **Done:** Not started.
- **Next:** Outline functional/non-functional requirements (target ≥3 pages) and identify data sources for service-time parameters.
- **Remaining:** Complete related-work comparison matrix; summarize gaps motivating NicLoadOff.

## Phase 2 — Minimal simulator core
- **Focus:** Discrete-event engine and initial system model.
- **Done:** Not started.
- **Next:** Select event queue structure (calendar queue vs binary heap) and sketch core abstractions (Resource, Task, Flow, Event).
- **Remaining:** Implement deterministic/stochastic service times, add unit tests, load hardware profile from YAML/JSON.

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
- **Done:** Not started.
- **Next:** Gather baseline service-time/throughput estimates for BF2.
- **Remaining:** Encode `bf2_default.yaml`, add contention variance knobs, run sensitivity sweep for MTU effects.

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
- **Remaining:** Produce full draft, finalize figures, prepare reproducibility package and advisor checklist.

## Deliverables & Success Criteria Snapshot
- Requirements doc, engine/tests, workload model, observability, policy DSL + policies, BF2 profile, validation plots, experiment harness & results, optional BF3 draft, hardening, thesis & artifacts.
- Success hinges on demonstrating dynamic policy dominance, trend-faithful sensitivity, and fully reproducible outputs.
