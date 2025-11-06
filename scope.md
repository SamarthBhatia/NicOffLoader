# SmartNIC Offload Simulator — Scope & Success Criteria

**Project working title:** *A Co‑Design Simulation Framework for Dynamic SmartNIC Offload Policies*

**Owner(s):** <Your Name> (MSc CSE) — with collaboration support from AI partner

**Document purpose:** Lock scope, success criteria, and evaluation plan **before** implementation. This one‑pager (okay, slightly longer) is the source of truth for Phase 0 and governs deferrals/non‑goals during development.

---

## 1) Primary Claim (Thesis Statement)

Dynamic, state‑aware offload policies—implemented and evaluated in a system‑level simulator that models asymmetric host↔SmartNIC paths and resource contention—**outperform static, all‑host or all‑NIC baselines** across realistic load regimes, as measured by higher throughput and/or lower tail latency, while reducing host CPU usage under constrained SmartNIC resources.

> *We aim to show strict or practical dominance on at least one key SLO (p99 latency or throughput) without unacceptable regressions on others, over a range of workloads and operating conditions.*

---

## 2) Goals

1. **Deliver a reusable discrete‑event simulator** for host+SmartNIC systems that captures:

   * Host CPU cores, SmartNIC SoC cores, host/NIC DRAM service limits.
   * Asymmetric communication paths: Host↔SoC, SoC↔Network, Host↔Network with latency, bandwidth, MTU/packetization overhead.
   * Queueing, contention, and scheduling effects.
2. **Provide a minimal, declarative policy language (DSL)** enabling dynamic placement decisions driven by observable state (utilization, queue depth, estimated delays) at task boundaries.
3. **Model at least one concrete workload** with a task‑graph (DAG) abstraction:

   * KV‑store read path (parse→hash→lookup→serialize). Optional: split‑TCP control/data example.
4. **Externalize hardware parameters** via profile files (e.g., `bf2_default.yaml`) to support sensitivity analyses (PCIe MTU, NIC DRAM bw, core speed).
5. **Produce a rigorous evaluation** that compares dynamic vs. static policies across varying arrival rates, host background load, and path degradations, reporting throughput, mean/p99 latency, and resource utilization.
6. **Artifact quality:** public repo (or submission package) with scripts to reproduce all figures from raw simulation outputs.

---

## 3) Non‑Goals (Explicit Deferrals)

* **No cycle‑accurate micro‑architecture modeling** of CPUs, DRAM controllers, NIC pipelines, or DPUs.
* **No full protocol‑stack emulation** (e.g., full TCP/IP implementation). We simulate service times/queues, not packet‑level correctness.
* **No on‑hardware deployment** required for thesis acceptance (trend‑validation only against public results; hardware experiments are optional future work).
* **No reinforcement learning / online training** in v1; policies are heuristic/declarative. (We may provide a pluggable scoring hook.)
* **No multi‑host network‑wide topology modeling**; single host + one off‑path SmartNIC + external network abstraction only.

---

## 4) Evaluation Metrics & Measurement Plan

**Core metrics** (reported per experiment condition):

* **Throughput (RPS).** Sustainable completed requests per second.
* **Latency:** mean and **p99** end‑to‑end per request; optional p50/p95.
* **Host CPU utilization (%).** Average across host cores; reported vs. baseline.
* **SmartNIC SoC utilization (%).** Average and peak; used to identify NIC bottlenecks.
* **Path utilization (% of bw)** for Host↔SoC and SoC↔Net links; queue lengths at each resource.

**Secondary metrics** (for analysis):

* **Time‑in‑stage breakdowns** (per DAG node), scheduling decisions per policy, and number of cross‑path transfers per request.

**Presentation:**

* Throughput vs. arrival‑rate curves; latency CDFs; p99 vs. arrival‑rate; utilization timelines under transients; bar charts for CPU saved vs. policy.

---

## 5) Experimental Design (What We Will Sweep)

* **Arrival process / request rate (λ):** low → saturation, multiple seeds.
* **Host background load:** {0%, 30%, 60%} synthetic CPU occupancy.
* **PCIe/MTU/path knobs:** “optimal” vs. “degraded” Host↔SoC effective throughput.
* **NIC contention:** co‑located noisy neighbor (on‑NIC CPU/DRAM use): {none, moderate}.
* **Policies compared:** `NeverOffload`, `AlwaysOffload`, `LoadBalanceThreshold`, `LatencyAware`, `HostAvoidance` (+ optional tuned variant).

**Workloads:**

* **KV‑read path** as the primary workload (read‑heavy; NIC‑cacheable lookup stage optional knob). Optional secondary: split‑TCP skeleton.

---

## 6) Success Criteria (Pass/Fail Gates)

To claim success, **all** must hold:

1. **Dominance result:** At least one dynamic policy **strictly or practically dominates** both static baselines on **p99 latency** across a non‑trivial λ range **or** achieves **≥10% higher throughput** at equal p99, without >10% regression on the other metric in the same regime.
2. **CPU savings:** Under moderate load, dynamic policy reduces **host CPU utilization by ≥20%** vs. all‑host baseline for comparable throughput.
3. **Trend validity:** Sensitivity experiments reproduce **qualitative trends** reported publicly (e.g., smaller MTU lowers Host↔SoC effective bandwidth and worsens offload when transfers dominate). Exact absolute numbers are **not** required.
4. **Reproducibility:** A single command regenerates every figure from checked‑in configs and random seeds; results are deterministic per seed.

Stretch goals (nice‑to‑have, not required for pass):

* A draft profile approximating a newer SmartNIC/DPU generation.
* A pluggable offline‑trained score function for policy decisions.

---

## 7) Scope of the Simulator (What It Must Model)

* **Resources:** Host cores (parallel servers), SmartNIC cores, host/NIC DRAM service caps, links with latency/bandwidth/MTU effects.
* **Workload model:** DAG of tasks with (instructions, bytes_in/out), placement eligibility, and data‑dependency edges.
* **Events:** Task arrival, schedule, start, finish; data transfer start/finish; queueing at resources.
* **State & observability:** Moving‑window utilization, queue depths, simple delay estimators; exposed as read‑only snapshots to the policy engine.
* **Configuration:** YAML/JSON profiles for hardware and workload; CLI for batch runs; CSV/Parquet outputs.

---

## 8) Assumptions & Simplifications

* Service times are modeled at **task granularity** (no per‑instruction simulation).
* Links enforce MTU‑driven packetization overhead via simple per‑packet cost and bandwidth sharing; no retransmission modeling.
* CPU performance differences (host vs. NIC) represented via **per‑task service rates** (e.g., IPC‑scaled constants) with optional variance.
* NIC DRAM and PCIe are shared resources modeled as capacity‑limited servers; contention is first‑come/first‑served unless specified.

---

## 9) Risks & Mitigations

1. **Parameter uncertainty / fidelity risk.**

   * *Mitigation:* externalize all parameters; run **sensitivity sweeps**; validate **trends** rather than absolutes; clearly document assumptions and citations in `profiles/` README.
2. **Scope creep into micro‑architectural detail.**

   * *Mitigation:* guardrail: *simulate what policies can observe.* Any deeper modeling requires explicit sign‑off and a cut elsewhere.
3. **Policy DSL time sink.**

   * *Mitigation:* MVP grammar only (events, predicates, actions, a fixed set of state functions). Defer UDFs/learning to extensions.
4. **Performance of the simulator.**

   * *Mitigation:* profiling early; arena allocators; avoid per‑event heap churn; target ≥1e6 events/min on a dev laptop; write property tests for queue invariants.
5. **Evaluation sprawl / figure explosion.**

   * *Mitigation:* pre‑approve an **experiment matrix**; enforce a budgets table (max scenarios × seeds × λ points).

---

## 10) Deliverables (Minimum for Completion)

* **Simulator core** with unit/property tests.
* **Policy engine + DSL MVP** with 5 stock policies.
* **Workload(s):** KV read path DAG with documented parameters.
* **Profiles:** `bf2_default.yaml` plus sensitivity variants.
* **Experiment harness** (CLI) + plotting scripts producing all paper figures.
* **Evaluation report** (figures + narratives) demonstrating criteria in §6.
* **Thesis write‑up** chapters: design, implementation, evaluation, limitations.

---

## 11) Out‑of‑Scope (for v1)

* Multi‑tenant fairness mechanisms beyond simple capacity sharing.
* Detailed NIC offload pipelines (e.g., parser microcode stages) or DP‑accelerators modeling.
* Power modeling and cost/€ analysis (optional appendix if time permits).

---

## 12) Review Checklist (Before Coding)

* [ ] Primary claim reviewed and accepted.
* [ ] Goals/non‑goals acknowledged.
* [ ] Metrics and success thresholds accepted.
* [ ] Experiment matrix sketched and sized.
* [ ] Risks understood; no additional hidden dependencies.

*End of scope document.*
