# policies/

Dynamic offload policies and the DSL parser/runtime modules are grouped here. Sample rule files that can be consumed via `--policy dsl --policy-config <path>` live under `examples/`; see `examples/adaptive.dsl.yaml` for a queue/utilization-driven reorder/admission policy. Rules can mix metric-based conditions with metadata/stage matches (`match:` blocks) so policies can target specific DAG nodes or manifest labels, and rule ordering provides fallbacks (first matching reorder + first matching admission each win their respective directive).
