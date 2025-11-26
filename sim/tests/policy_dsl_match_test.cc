#include "nicloadoff/policy_dsl.hh"
#include "nicloadoff/policy_state.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

std::filesystem::path write_config() {
    const char* yaml = R"(rules:
  - when:
      metric: queue_avg
      op: ">="
      value: 1.0
    match:
      stage: reduce
    action:
      reorder: prefer-nic
  - match:
      metadata:
        arrival_label: burst
    action:
      admission:
        max_active: 1
  - match:
      metadata:
        arrival_label: cold
    action:
      reorder: prefer-host
)";

    std::filesystem::path path = std::filesystem::temp_directory_path() / "policy_dsl_match.yaml";
    std::ofstream out(path);
    out << yaml;
    out.close();
    return path;
}

nicloadoff::PolicyStateSnapshot make_reorder_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot{};
    snapshot.waiting_task_order = {101, 102, 103};
    snapshot.rolling_metrics.waiting_queue_depth.average = 1.25;
    snapshot.scenario_metadata.emplace("arrival_label", "cold");

    PolicyTaskState map{};
    map.id = 101;
    map.stage_label = "map";
    map.pending_host_demand = 4.0;
    map.pending_nic_demand = 1.0;

    PolicyTaskState reduce_host{};
    reduce_host.id = 102;
    reduce_host.stage_label = "reduce";
    reduce_host.pending_host_demand = 6.0;
    reduce_host.pending_nic_demand = 2.0;

    PolicyTaskState reduce_nic{};
    reduce_nic.id = 103;
    reduce_nic.stage_label = "reduce";
    reduce_nic.pending_host_demand = 1.0;
    reduce_nic.pending_nic_demand = 6.0;

    snapshot.tasks = {map, reduce_host, reduce_nic};
    return snapshot;
}

nicloadoff::PolicyStateSnapshot make_admission_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot{};
    snapshot.waiting_task_order = {201};
    snapshot.scenario_metadata.emplace("arrival_label", "burst");

    PolicyTaskState waiting{};
    waiting.id = 201;
    waiting.stage_label = "reduce";
    waiting.pending_host_demand = 2.0;
    waiting.pending_nic_demand = 2.0;
    snapshot.tasks = {waiting};
    return snapshot;
}

nicloadoff::PolicyStateSnapshot make_combined_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot = make_reorder_snapshot();
    snapshot.scenario_metadata["arrival_label"] = "burst";
    return snapshot;
}

nicloadoff::PolicyStateSnapshot make_fallback_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot = make_reorder_snapshot();
    snapshot.scenario_metadata["arrival_label"] = "cold";
    for (auto& task : snapshot.tasks) {
        task.stage_label = "map";
    }
    return snapshot;
}

} // namespace

int main() {
    using namespace nicloadoff;
    auto config_path = write_config();
    auto hook = policy::dsl::load_program_from_file(config_path);

    {
        auto snapshot = make_reorder_snapshot();
        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.admission == std::nullopt, "unexpected admission directive for reorder rule");
        check(decision.waiting_order.has_value(), "reorder rule should fire on reduce stage");
        const auto& order = *decision.waiting_order;
        check(order.size() == snapshot.waiting_task_order.size(), "reorder should keep queue width");
        check(order[0] == 101, "non-matching tasks keep their relative order");
        check(order[1] == 103 && order[2] == 102, "reduce tasks should be reordered to favor NIC-heavy work");
    }

    {
        auto snapshot = make_admission_snapshot();
        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(!decision.waiting_order.has_value(), "second rule should not reorder tasks");
        check(decision.admission.has_value(), "metadata-gated admission rule should fire");
        check(decision.admission->enabled, "admission rule must enable admission control");
        check(decision.admission->max_active_tasks == 1, "admission rule should set max_active_tasks to 1");
    }

    {
        auto snapshot = make_combined_snapshot();
        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.waiting_order.has_value(), "stage match should reorder even when metadata also matches");
        check(decision.admission.has_value(), "metadata rule should still fire after reorder");
        check(decision.admission->max_active_tasks == 1, "admission rule should still cap active tasks at 1");
    }

    {
        auto snapshot = make_fallback_snapshot();
        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.waiting_order.has_value(), "metadata fallback should reorder when stage match fails");
        const auto& order = *decision.waiting_order;
        check(order.size() == snapshot.waiting_task_order.size(), "fallback reorder should keep queue width");
        check(order[0] == 102 && order[1] == 101 && order[2] == 103,
              "fallback reorder should prefer host-heavy tasks first");
    }

    return 0;
}
