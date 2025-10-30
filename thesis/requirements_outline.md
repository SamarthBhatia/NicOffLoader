# NicLoadOff Requirements Outline

## Context & Intended Artifact
- Audience: project contributors, advisor, thesis readers; informs simulator scope (Phase 2+), workload design, and evaluation matrix.
- Purpose: capture functional/non-functional expectations ahead of implementation; provide traceable links to scope doc, status tracker, and forthcoming experiment matrix.
- Revision cadence: update at the end of each major milestone (event engine MVP, policy DSL MVP, evaluation readiness).

## Functional Requirements (FR)
1. **FR1 — Discrete-event simulator core:** Model host CPU, SmartNIC CPU, DRAM, and interconnect resources with queueing semantics; support deterministic and stochastic service times.
2. **FR2 — Workload DAG execution:** Accept workload graphs describing tasks (instructions, data in/out, placement hints) and simulate dependencies, parallel branches, and data transfer costs.
3. **FR3 — Policy evaluation hook:** Provide runtime interface for policies to inspect system state (utilization, queue depth, latency estimates) and return placement/actions at task boundaries.
4. **FR4 — Policy DSL:** Implement parser + interpreter for a minimal declarative policy language with predicates, arithmetic expressions, thresholds, and fallback branches.
5. **FR5 — Stock policy library:** Ship baseline policies (`NeverOffload`, `AlwaysOffload`, `LoadBalanceThreshold`, `LatencyAware`, `HostAvoidance`), configurable via YAML/JSON.
6. **FR6 — Hardware profiles:** Load hardware configuration from external files (e.g., `profiles/bf2_default.yaml`) and validate required fields with actionable error messages.
7. **FR7 — Experiment harness:** Provide CLI to run batch simulations, sweep parameters, and emit structured outputs (CSV/Parquet) plus run metadata for reproducibility.
8. **FR8 — Visualization support:** Supply Python analysis scripts that transform simulation outputs into figures used in evaluation (throughput, latency, utilization plots).
9. **FR9 — Deterministic replay:** Ensure simulations are reproducible based on explicit seeds; include state snapshot capability for debugging policies.

## Non-Functional Requirements (NFR)
1. **NFR1 — Performance:** Target ≥1e6 processed events per minute on a modern laptop (baseline M1/M2 or comparable x86 mobile CPU) for representative workloads.
2. **NFR2 — Fidelity:** Capture qualitative trends observed in published SmartNIC evaluations (e.g., MTU sensitivity, PCIe bottlenecks) within ±10% relative ordering.
3. **NFR3 — Determinism & reproducibility:** Same inputs and seeds yield identical outputs across supported platforms (macOS, Linux) and toolchain versions.
4. **NFR4 — Extensibility:** Modular architecture that allows adding new resources, policies, or workload descriptors without modifying core event loop APIs.
5. **NFR5 — Observability:** Provide metrics export (utilization, queue depths, per-stage latencies) with minimal runtime overhead (<5% slowdown vs. metrics disabled).
6. **NFR6 — Usability:** Command-line ergonomics (help text, validation errors) and documentation enabling new users to configure profiles/workloads in <1 hour.
7. **NFR7 — Testing coverage:** Maintain ≥80% line coverage on simulator core and policy engine via GoogleTest-based suites and property tests for invariants.

## Data & Parameter Sources
| Category | Parameters Needed | Candidate Sources | Notes / Actions |
| --- | --- | --- | --- |
| SmartNIC hardware | Core counts, frequencies, DRAM bandwidth, PCIe bandwidth, MTU, DMA latency | NVIDIA BlueField-2 datasheets, vendor whitepapers, VMware/NVIDIA blogs, SIGCOMM/USENIX papers | Validate against at least two independent documents; capture citations in `profiles/README.md`. |
| Host system | CPU IPC estimates, memory bandwidth, background load profiles | SPEC CPU reports, open-source benchmarks, academic measurements (e.g., Shenango, ZygOS papers) | Map host service times via normalized instruction counts; parameterize variability for sensitivity sweeps. |
| Workload characteristics | Task instruction counts, request sizes, branching probabilities | RocksDB/Redis evaluation papers, Meta KV benchmarks, academic KV store studies | Cross-check with open-source traces if available; otherwise derive ranges and document assumptions. |
| Network path | RTT, throughput, packetization overhead, congestion effects | Data center network studies (Google/Youtube papers), vendor NIC tuning guides | Model as effective latency + bandwidth; include MTU sensitivity experiments. |
| Policy thresholds | Utilization limits, latency SLOs, fallback triggers | Industry practice (SLO guidelines), internal simulations | Keep configurable via policy configs; default to sensible values consistent with evaluation targets. |

## Open Questions & Follow-Ups
- Determine preferred format for the comprehensive requirements doc (extend this outline vs. migrate into thesis Chapter 2 draft).
- Confirm whether CLI output schema should default to CSV or Parquet (affects Python dependencies).
- Investigate availability of public BlueField-2 microbenchmarks to seed baseline service times; otherwise design synthetic calibration routine.
- Align policy DSL syntax with planned parser technology (hand-written vs. parser generator) before implementation.

## Next Actions
1. Expand FR/NFR items into detailed specification (acceptance criteria) ahead of Phase 2 design.
2. Collect and catalogue source documents for hardware/workload parameters; annotate in `profiles/README.md`.
3. Draft initial system requirements section for the thesis outline, referencing this document.
