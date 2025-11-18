#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

struct PolicyLog {
    bool limit_enabled{false};
    std::size_t waiting_depth{0};
    std::size_t active_tasks{0};
    std::vector<nicloadoff::TaskId> waiting_order;
};

class OscillatingPolicy : public nicloadoff::policy::PolicyHook {
  public:
    explicit OscillatingPolicy(std::size_t toggle_period) : toggle_period_(toggle_period) {}

    [[nodiscard]] nicloadoff::policy::PolicyDecision evaluate(
        const nicloadoff::PolicyStateSnapshot& snapshot) override {
        PolicyLog log{};
        log.waiting_depth = snapshot.queues.waiting_queue_depth;
        log.active_tasks = snapshot.active_task_count;
        log.waiting_order = snapshot.waiting_task_order;

        const bool enable_limit =
            (snapshot.queues.waiting_queue_depth > 0) && ((evaluation_count_ / toggle_period_) % 2 == 0);
        ++evaluation_count_;

        nicloadoff::policy::PolicyDecision decision{};
        if (!snapshot.waiting_task_order.empty()) {
            std::vector<nicloadoff::TaskId> order = snapshot.waiting_task_order;
            std::sort(order.begin(), order.end());
            decision.waiting_order = order;
        }

        if (enable_limit) {
            nicloadoff::policy::AdmissionControlDirective directive{};
            directive.enabled = true;
            directive.max_active_tasks = 1;
            decision.admission = directive;
            log.limit_enabled = true;
        } else {
            nicloadoff::policy::AdmissionControlDirective directive{};
            directive.enabled = false;
            decision.admission = directive;
            log.limit_enabled = false;
        }

        logs_.push_back(std::move(log));
        return decision;
    }

    std::vector<PolicyLog> logs_;

  private:
    std::size_t toggle_period_{2};
    std::size_t evaluation_count_{0};
};

nicloadoff::config::Profile make_profile() {
    nicloadoff::config::Profile profile{};
    profile.host_cpu.cores_total = 2;
    profile.host_dram.capacity_gb = 16;
    profile.host_nic_link.max_inflight_bytes = 1'000'000;
    profile.nic_cpu.cores_total = 2;
    profile.nic_dram.capacity_gb = 16;
    profile.nic_network_link.max_inflight_bytes = 1'000'000;

    profile.service_time_overrides.emplace("osc_stage",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 4.0,
                                                                                   .nic_mean_us = 4.0});
    return profile;
}

nicloadoff::StageSpec make_stage(const nicloadoff::ServiceTimeProfileRef& ref) {
    nicloadoff::StageSpec stage{};
    stage.service_profile = ref;
    stage.demands.push_back(
        nicloadoff::StageResourceDemand{.resource = nicloadoff::ResourceClass::kHostCpu, .units = 1.0});
    stage.demands.push_back(
        nicloadoff::StageResourceDemand{.resource = nicloadoff::ResourceClass::kNicCpu, .units = 1.0});
    return stage;
}

bool is_sorted_non_decreasing(const std::vector<nicloadoff::TaskId>& ids) {
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i - 1] > ids[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace nicloadoff;

    auto profile = make_profile();
    auto inventory = make_resource_inventory_from_profile(profile);
    ServiceTimeModel service_model(profile, /*seed=*/5150);

    ServiceTimeProfileRef stage_ref{.key = "osc_stage",
                                    .domain = ServiceTimeDomain::kHost,
                                    .mode = ServiceTimeMode::kStochastic};

    StageSpec stage = make_stage(stage_ref);

    WorkloadSpec workload{};
    const int task_count = 10;
    for (int i = 0; i < task_count; ++i) {
        TaskSpec spec{};
        spec.id = static_cast<TaskId>(i + 1);
        spec.arrival_time = 0.0; // all tasks compete immediately
        spec.stages = {stage};
        workload.tasks.push_back(spec);
    }

    auto tasks = make_tasks_from_spec(workload, inventory.ids);

    OscillatingPolicy policy(/*toggle_period=*/2);
    BasicScheduler scheduler(std::move(inventory.pool), &service_model);
    scheduler.set_policy_hook(&policy);

    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }

    scheduler.run_until_empty();

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == static_cast<std::size_t>(task_count), "expected all tasks to complete");

    RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == static_cast<std::size_t>(task_count), "expected per-task metrics for each task");

    bool saw_limit_enabled = false;
    bool saw_limit_disabled = false;
    bool saw_limit_compliance = false;
    bool saw_sorted_waiting = false;

    for (const auto& log : policy.logs_) {
        if (log.limit_enabled) {
            saw_limit_enabled = true;
            if (log.active_tasks <= 1) {
                saw_limit_compliance = true;
            }
        } else {
            saw_limit_disabled = true;
        }
        if (log.waiting_depth > 1) {
            if (is_sorted_non_decreasing(log.waiting_order)) {
                saw_sorted_waiting = true;
            }
        }
    }

    check(saw_limit_enabled, "expected admission limit to enable at least once");
    check(saw_limit_disabled, "expected admission limit to disable at least once");
    check(saw_limit_compliance, "expected admission limit to eventually observe single active task");
    check(saw_sorted_waiting, "expected waiting queue to show sorted order under oscillation policy");

    return 0;
}
