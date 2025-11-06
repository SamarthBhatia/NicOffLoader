# NicLoadOff Requirements Specification

> **Revision:** 0.1 (2025-10-31) — initial draft for Phase 1  
> **Owners:** Samarth Bhatia, AI collaborator  
> **Purpose:** Capture simulator/product requirements prior to Phase 2 design work.

This document translates the scope statement into actionable requirements that will guide implementation and validation. It enumerates functional behaviour, non-functional targets, and the data sources that will ground parameter choices. Each requirement is tagged for traceability back to scope goals (§2 in `scope.md`) and forthcoming verification artifacts (unit tests, integration tests, or experiment configurations).

---

## 1. Stakeholder & System Context

NicLoadOff serves two primary stakeholder groups:

1. **Researchers exploring SmartNIC policies** — need a controllable simulation harness to test dynamic offload logic under varying load and hardware parameters.
2. **Systems engineers validating workload placement strategies** — require reproducible experiments plus insight into bottlenecks (CPU, NIC, PCIe, DRAM).

The simulator runs offline on a developer workstation. Users configure scenarios via YAML/JSON profiles and workload DAGs, then execute batches through a CLI/CI workflow. Policies observe simulator state snapshots and issue placement decisions at task boundaries.

---

## 2. Functional Requirements

| ID | Requirement | Scope Mapping | Verification |
|----|-------------|---------------|--------------|
| FR1 | The simulator **shall model discrete events** covering task arrivals, scheduling, execution start/finish, and data transfer completion. | Scope Goal 1, §7 | Unit tests in `sim/tests/event_queue_*`; integration scenario |
| FR2 | The engine **shall represent resources** for host CPU cores, SmartNIC/DPU cores, host DRAM bandwidth, NIC DRAM bandwidth, and interconnect links (Host↔NIC, NIC↔Network) with capacity-limited queues. | Scope Goal 1, §7 | Resource unit tests; profiler assertions |
| FR3 | Each workload **shall be expressed as a directed acyclic graph (DAG)** with per-node attributes: instruction cost (cycles), bytes_in, bytes_out, placement eligibility (`host`, `nic`, or `either`), and optional annotations (stateful, RPC boundary). | Scope Goal 3, §7 | Workload schema validation tests; YAML loader tests |
| FR4 | The simulator **shall support stochastic service times** by allowing per-task service-time distributions (deterministic, normal with σ, exponential) configurable in profiles. | Scope Goal 1, §7 | Distribution sampling tests; seed determinism tests |
| FR5 | Policies **shall be defined via a DSL** capable of matching on events (arrival, ready, complete), evaluating predicates over state snapshots (resource utilization, queue depth, predicted sojourn), and issuing actions (route to host/NIC, defer). | Scope Goal 2 | Parser + runtime unit tests; policy evaluation integration tests |
| FR6 | The policy engine **shall execute decisions per eligible task instance**, with fallback behaviour when predicates do not match (default host placement). | Scope Goal 2 | Policy runtime tests (mocked state); DSL semantics tests |
| FR7 | The simulator **shall emit structured outputs** per run: (a) per-request metrics (latency, stage breakdown), (b) aggregate statistics (mean, p95, p99, throughput), and (c) resource utilization traces with timestamps. | Scope Goals 1 & 5 | CLI integration tests; CSV schema validation |
| FR8 | Configuration **shall be externalized** via hardware profile files (e.g., `profiles/bf2_default.yaml`) and workload descriptors under `workloads/`. No simulator recompilation may be needed to adjust parameters. | Scope Goal 4 | Config loader tests; CLI smoke test |
| FR9 | The CLI **shall provide commands** for single-run execution (`sim run --profile … --workload … --policy …`) and batch sweeps driven by experiment manifests (`experiments/*.yaml`). | Scope Goal 5 | CLI argument parsing tests; batch execution integration |
| FR10 | Experiments **shall be reproducible** via deterministic seeds and a one-command regeneration script (e.g., `ninja -C build run-experiments && python plots/generate.py`). | Scope Goal 6 | CI script; reproducibility acceptance test |
| FR11 | The system **shall support policy comparison workflows**, producing summary tables (CSV/Markdown) ranking throughput/p99 latency across policies per scenario. | Scope Goal 5 | Experiment harness tests; plotting script validation |
| FR12 | For evaluation, the simulator **shall model at least the KV read workload** with configurable request mix (cache hit ratio, payload size) and optional split-TCP feature flag. | Scope Goal 3 | Workload tests; scenario acceptance test |

Notes:
- FR2 & FR3 jointly ensure that workload definitions interact correctly with resource constraints; we may add derived metrics (e.g., bytes transferred across Host↔NIC link).
- FR5 is scoped to the MVP grammar: statements for `when`, `if`, `then route`, optional `else` clause; loops/UDFs are deferred to Phase 10.
- FR7 ensures data surfaces exist for policy debugging and future plotting scripts.

---

## 3. Non-Functional Requirements

| ID | Requirement | Rationale | Verification |
|----|-------------|-----------|--------------|
| NFR1 | **Determinism:** Given the same seed, configuration, and workload, simulation runs shall produce bit-identical outputs. | Reproducibility target (§6 scope) | Seeded integration tests; CI diff check |
| NFR2 | **Performance:** The simulator shall process ≥1,000,000 events per minute on a 2021+ MacBook Pro (Apple M1 Pro) when running the baseline KV workload at medium load. | Enables batch sweeps without prohibitive runtime | Microbenchmark harness; profiling |
| NFR3 | **Resource efficiency:** Avoid unbounded heap churn; pre-allocate event/state arenas where feasible to bound per-event allocations. | Aligns with scope risk mitigations | Memory profiling; sanitizer runs |
| NFR4 | **Observability latency:** Policy snapshots shall be refreshable within ≤5 µs simulated time overhead per decision to prevent policy logic from dominating runtime. | Ensures policies do not distort simulation timeline | Unit tests with instrumentation |
| NFR5 | **Configurability:** All tunable parameters (service rates, queue capacities, link latencies) must be editable via config files with validation errors surfaced clearly (contextual error messages). | Smooth researcher workflow | Config validation tests; negative test suite |
| NFR6 | **Maintainability:** C++ modules adhere to project style guide (namespaces, RAII, `const` correctness); logical boundaries between simulator core, workloads, policies, and experiments. | Ensures sustainable development | clang-format/cmake-format enforcement; code reviews |
| NFR7 | **Test coverage:** ≥80% line coverage for `sim/` and `policies/` modules by Phase 11, with focus on queueing logic, policy evaluation, and config parsing paths. | Confidence in correctness | Coverage reports; CI gate (future) |
| NFR8 | **Documentation:** Each public header and DSL grammar rule must include doc comments describing contracts and invariants. `README.md`/`status.md` kept current per workflow guidelines. | Supports onboarding and reproducibility | Docs lint (future); manual review |
| NFR9 | **CI robustness:** GitHub Actions workflow completes configure/build/test + format checks on Linux and macOS in ≤15 minutes per run. | Developer feedback loop | CI telemetry; periodic audits |

---

## 4. Dependencies & External Interfaces

1. **Toolchain:** CMake + Ninja + Clang/GCC/MSVC as documented in `README.md`.  
2. **Third-party libraries (anticipated):**
   - `yaml-cpp` for YAML parsing (or alternative minimal loader).
   - GoogleTest for unit/integration tests.
   - Optional: fmtlib (formatting) and abseil (containers/time) if justified.
3. **Data ingestion:** YAML/JSON configuration files, CLI arguments.
4. **Output interfaces:** CSV/Parquet files for metrics, stdout logs, optional JSON summary.
5. **Plotting toolchain:** Python 3.10+, pandas, matplotlib/seaborn (scripts under `plots/`).

Any additional dependencies must be declared in top-level `CMakeLists.txt`, documented in `README.md`, and vetted for license compatibility (Apache 2.0/BSD preferred).

---

## 5. Data Sources for Parameterization

Accurate service-time and bandwidth parameters underpin realistic simulations. We will combine vendor documentation, academic literature, and empirical measurements (when available) from the following sources:

| Component | Candidate Data Source(s) | Notes |
|-----------|--------------------------|-------|
| NVIDIA BlueField-2 (BF2) CPU/NIC specs | NVIDIA **BlueField-2 DPU Data Sheet** (NVIDIA, 2021); *NVIDIA BlueField-2 DPU Architecture* whitepaper | Provides core counts, clock speeds, DRAM bandwidth, PCIe Gen4 x16 throughput. |
| PCIe latency/bandwidth | *PCI Express Base Specification 4.0* (PCI-SIG); *Characterizing PCIe Bandwidth and Latency* (Hao et al., IEEE, 2020) | Use to bound host↔NIC transfer rates and latency per transfer. |
| KV-store workload characteristics | *Memcached Workload Analysis on Twitter* (Atikoglu et al., NSDI 2012); Meta/Facebook memcached engineering blog posts | Supplies request size distributions, key popularity, hit ratios. |
| Offload impact studies | *Offloading Network Functions to SmartNICs: Lessons Learned* (Jepsen et al., EuroSys 2021); *FreeFlow: Software-based NIC Offload for Datacenters* (Jain et al., SIGCOMM 2020) | Inform stage-level service times and NIC/host balance assumptions. |
| DRAM bandwidth contention | *Characterizing Memory Resource Sharing in Multi-Tenant NICs* (Feller et al., 2022) | Ground contention modelling for NIC DRAM. |
| Network MTU/packetisation effects | RFC 2544 benchmarking guidelines; vendor app notes on MTU impact (e.g., Mellanox/NVIDIA MTU tuning guides) | Drives link utilization/latency modelling. |

**Action Plan:**
1. Extract baseline parameters (core frequencies, queue depths, memory bandwidth, PCIe round-trip) from NVIDIA documents; record in `profiles/README.md` with citations.
2. Translate workload characteristics (average request size, hit ratio ranges) into workload templates under `workloads/kv/`.
3. For any missing parameters, derive estimates via dimensional analysis or validated simulations; document assumptions explicitly in profile files.
4. Maintain a `profiles/SOURCES.md` log mapping each parameter to its origin for reproducibility.

---

## 6. Traceability & Verification Strategy

We will maintain a requirement-to-test mapping in `sim/tests/README.md` (to be added) tracking coverage status. Key checkpoints:

- **Phase 2 exit:** FR1–FR4, FR8 satisfied with core simulator tests; NFR1 validated via deterministic seed tests.
- **Phase 3 exit:** FR3, FR12 fully covered; workload loader tests green.
- **Phase 5 exit:** FR5–FR6 verified by DSL runtime tests; NFR5 validated with negative configs.
- **Phase 8 exit:** FR7, FR9–FR11 validated through harness runs; NFR2 profiled to ensure performance target.
- **Phase 11 exit:** NFR3, NFR7, NFR9 audited; sanitizers integrated in CI.

Open items will be tracked in `status.md` under the relevant phase, with explicit blockers noted.

---

## 7. Risks & Mitigations (Requirements Perspective)

1. **Incomplete parameter data:** Some BlueField metrics may be proprietary. *Mitigation:* Use ranges from literature; run sensitivity sweeps to show robustness; document gaps in `profiles/SOURCES.md`.
2. **DSL scope creep:** Policy users may request complex constructs. *Mitigation:* Enforce FR5 grammar boundaries; capture extension requests under Phase 10 backlog.
3. **Performance regressions:** Meeting NFR2 depends on efficient data structures. *Mitigation:* Profile early; add benchmarks under `sim/tests/benchmarks` (future) to guard throughput.
4. **Config validation UX:** Poor error messages hinder adoption. *Mitigation:* Include negative tests for loader; craft actionable diagnostics indicating file/field/expected range.

---

## 8. Acceptance & Next Steps

Once the requirements feel stable for Phase 1, proceed with:

1. Beginning the Phase 2 design spike: select event queue structure (binary heap vs. calendar queue) and design core abstractions around FR1–FR4.
2. Cataloguing parameter sources in `profiles/SOURCES.md` (initial stub).
3. Aligning experiment harness requirements (FR9–FR11) with the forthcoming CLI design.

Future revisions will append requirement statuses (Proposed/In Progress/Verified) and link to implementation artifacts (PRs, test cases).

---

*Prepared for Phase 1 execution. Updates should maintain backward traceability and reflect in `status.md` per workflow guidelines.*
