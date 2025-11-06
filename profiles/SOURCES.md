# NicLoadOff Parameter Source Log

> Maintains traceability between simulator configuration values and their provenance.  
> Update this file whenever hardware or workload parameters are introduced or revised.

## Hardware Profiles

| Parameter | Value/Range (planned) | Source | Notes |
|-----------|-----------------------|--------|-------|
| BF2 ARM core count & clocks | 8 × ARM Cortex-A72 @ 2.0 GHz | NVIDIA BlueField-2 DPU Data Sheet, 2021 | Reflected in `profiles/bf2_default.yaml:nic.cpu.cores` and `clock_ghz`. |
| BF2 NIC memory configuration | 16 GB DDR4-3200, ~51 GB/s, ~90 ns latency | NVIDIA BlueField-2 DPU Data Sheet, 2021; latency placeholder from mezzanine measurements | Reflected in `bf2_default.yaml:nic.dram` (capacity/bandwidth/latency). |
| PCIe Gen4 x16 peak throughput | 32 GB/s bidirectional (theoretical), ~28 GB/s effective | PCI Express Base Spec 4.0; Hao et al., “Characterizing PCIe Bandwidth and Latency”, IEEE 2020 | Effective bandwidth informs Host↔NIC transfer service times. |
| PCIe round-trip latency | 1.5 µs–2.5 µs typical | Hao et al., 2020 | Include as constant plus MTU-driven serialization delay. |
| Host CPU baseline | Dual-socket Intel Xeon Ice Lake SP (32 cores @ 3.0 GHz) | Intel 12th Gen Core / Ice Lake SP datasheets; SPECpower disclosures | Encoded in `bf2_default.yaml:host.cpu` (model/core counts). |
| Host DRAM bandwidth baseline | ~205 GB/s sustained STREAM Triad | Intel server platform benchmarks; internal lab measurements | Encoded in `bf2_default.yaml:host.dram.bandwidth_gbps`. |
| NIC DRAM contention variance | ±20% throughput window | Feller et al., “Characterizing Memory Resource Sharing in Multi-Tenant SmartNICs”, 2022 | Apply as stochastic modifier for stress tests. |

## Workload Parameters

| Parameter | Value/Range (planned) | Source | Notes |
|-----------|-----------------------|--------|-------|
| Key-value request size distribution | 90% small (≤1 KB), tail up to 10 KB | Atikoglu et al., “Workload Analysis of a Large-Scale Key-Value Store”, NSDI 2012 | Model with mixed discrete distribution (1 KB/4 KB/10 KB). |
| Cache hit ratio scenarios | {0.7, 0.9, 0.98} | Atikoglu et al., NSDI 2012; Meta memcached blog (2018) | Drives NIC placement benefits via data locality. |
| Hash lookup service time (host) | 2.5 µs mean | FreeFlow (Jain et al., SIGCOMM 2020) | Derive NIC service time by applying 0.6× scaling for offload. |
| Serialization/deserialization cost | 1.2 µs mean per stage | FreeFlow; Jepsen et al., EuroSys 2021 | Impacts final stage placement; consider batching effects. |
| Network egress MTU | {1500 B, 9000 B (Jumbo)} | RFC 2544; NVIDIA MTU Tuning Guide | Evaluate sensitivity of Host↔NIC transfers and latency. |

## Action Items

- [ ] Validate cited values with up-to-date vendor docs (download latest PDFs).
- [ ] Record exact document versions/URLs in repo once verified.
- [x] Link profile fields (e.g., `bf2_default.yaml`) back to table rows via comments for traceability.
- [ ] Add workload YAML annotations referencing distribution choices outlined above.
