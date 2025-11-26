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
      value: 0.5
    match:
      metadata:
        scenario: hot
    action:
      reorder: prefer-nic
  - when:
      metric: nic_util_avg
      op: ">="
      value: 0.8
    match:
      metadata:
        scenario: surge
      stage_index: 2
    action:
      reorder: prefer-host
  - when:
      metric: host_util_peak
      op: ">="
      value: 0.9
    action:
      admission:
        max_active: 1
  - match:
      stage_index: 1
    action:
      admission:
        max_active: 2
  - match:
      metadata:
        scenario: cold
    action:
      reorder: prefer-host
  - action:
      admission:
        max_active: 4
)";

    std::filesystem::path path = std::filesystem::temp_directory_path() / "policy_dsl_mocked_state.yaml";
    std::ofstream out(path);
    out << yaml;
    out.close();
    return path;
}

nicloadoff::PolicyTaskState make_task(nicloadoff::TaskId id,
                                     std::size_t stage_index,
                                     double host_demand,
                                     double nic_demand) {
    nicloadoff::PolicyTaskState task{};
    task.id = id;
    task.stage_index = stage_index;
    task.pending_host_demand = host_demand;
    task.pending_nic_demand = nic_demand;
    task.waiting = true;
    return task;
}

nicloadoff::PolicyStateSnapshot base_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot{};
    snapshot.waiting_task_order = {101, 102, 103};
    snapshot.tasks = {
        make_task(101, 0, 5.0, 1.0), // host-heavy
        make_task(102, 1, 2.0, 5.0), // NIC-heavy
        make_task(103, 2, 4.0, 3.0),
    };
    snapshot.rolling_metrics.waiting_queue_depth.average = 0.0;
    snapshot.rolling_metrics.waiting_queue_depth.samples = 1;
    snapshot.rolling_metrics.host_utilization.average = 0.25;
    snapshot.rolling_metrics.host_utilization.peak = 0.3;
    snapshot.rolling_metrics.host_utilization.samples = 1;
    snapshot.rolling_metrics.nic_utilization.average = 0.2;
    snapshot.rolling_metrics.nic_utilization.peak = 0.25;
    snapshot.rolling_metrics.nic_utilization.samples = 1;
    snapshot.scenario_metadata.emplace("scenario", "warm");
    return snapshot;
}

} // namespace

int main() {
    using namespace nicloadoff;
    auto config_path = write_config();
    auto hook = policy::dsl::load_program_from_file(config_path);

    {
        PolicyStateSnapshot snapshot = base_snapshot();
        snapshot.scenario_metadata["scenario"] = "hot";
        snapshot.rolling_metrics.waiting_queue_depth.average = 0.75;

        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.waiting_order.has_value(), "hot scenario should trigger prefer-nic reorder");
        const auto& order = *decision.waiting_order;
        check(order.size() == snapshot.waiting_task_order.size(), "reorder width mismatch");
        check(order[0] == 102 && order[1] == 103 && order[2] == 101,
              "prefer-nic should surface NIC-heavy task first");
        check(decision.admission.has_value(), "stage-index admission rule should fire");
        check(decision.admission->max_active_tasks == 2, "admission limit should be 2 when stage_index matches");
    }

    {
        PolicyStateSnapshot snapshot = base_snapshot();
        snapshot.scenario_metadata["scenario"] = "cold";
        snapshot.rolling_metrics.waiting_queue_depth.average = 0.2;

        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.waiting_order.has_value(), "cold scenario should trigger fallback host reorder");
        const auto& order = *decision.waiting_order;
        check(order[0] == 101 && order[1] == 103 && order[2] == 102,
              "prefer-host fallback should keep host-heavy work first");
        check(decision.admission.has_value(), "stage-index admission rule should still fire for cold snapshot");
        check(decision.admission->max_active_tasks == 2, "cold snapshot admission limit should remain 2");
    }

    {
        PolicyStateSnapshot snapshot = base_snapshot();
        snapshot.scenario_metadata["scenario"] = "surge";
        snapshot.rolling_metrics.nic_utilization.average = 0.85;
        snapshot.rolling_metrics.nic_utilization.samples = 5;
        PolicyTaskState extra = make_task(104, 2, 6.0, 2.0);
        snapshot.tasks.push_back(extra);
        snapshot.waiting_task_order.push_back(104);

        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(decision.waiting_order.has_value(), "surge scenario should trigger NIC-surge reorder");
        const auto& order = *decision.waiting_order;
        check(order[0] == 101 && order[1] == 102 && order[2] == 104 && order[3] == 103,
              "stage-index + metadata reorder should favor host-heavy stage first");
        check(decision.admission.has_value(), "stage-index admission rule should still apply on surge snapshot");
        check(decision.admission->max_active_tasks == 2, "surge snapshot admission limit should stay at 2");
    }

    {
        PolicyStateSnapshot snapshot = base_snapshot();
        snapshot.scenario_metadata["scenario"] = "warm";
        // Remove stage-index matches so only the global admission rule fires.
        for (auto& task : snapshot.tasks) {
            task.stage_index = 0;
        }

        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(!decision.waiting_order.has_value(), "neutral scenario should not reorder");
        check(decision.admission.has_value(), "global admission fallback should trigger");
        check(decision.admission->max_active_tasks == 4, "fallback admission limit should be 4");
    }

    {
        PolicyStateSnapshot snapshot = base_snapshot();
        snapshot.rolling_metrics.host_utilization.peak = 0.95;
        snapshot.rolling_metrics.host_utilization.samples = 5;

        policy::PolicyDecision decision = hook->evaluate(snapshot);
        check(!decision.waiting_order.has_value(), "host-util clamp should not reorder by itself");
        check(decision.admission.has_value(), "host-util clamp should enable admission control");
        check(decision.admission->max_active_tasks == 1, "host-util spike should force stricter admission limit");
    }

    return 0;
}
