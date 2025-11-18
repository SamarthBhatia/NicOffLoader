#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void assert_near(double lhs, double rhs, double tolerance = 1e-9) {
    if (std::fabs(lhs - rhs) > tolerance) {
        std::fprintf(stderr, "assert_near failed: |%f - %f| > %f\n", lhs, rhs, tolerance);
        std::abort();
    }
}

nicloadoff::config::Profile make_profile() {
    nicloadoff::config::Profile profile{};
    profile.host_cpu.cores_total = 2;
    profile.host_dram.capacity_gb = 16;
    profile.host_nic_link.max_inflight_bytes = 1'000'000;
    profile.nic_cpu.cores_total = 2;
    profile.nic_dram.capacity_gb = 8;
    profile.nic_network_link.max_inflight_bytes = 1'000'000;

    profile.service_time_overrides.emplace("oscillating_stage",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 3.5,
                                                                                   .nic_mean_us = 3.5});
    return profile;
}

nicloadoff::StageSpec make_stochastic_stage(
    const nicloadoff::ServiceTimeProfileRef& ref,
    std::initializer_list<std::pair<nicloadoff::ResourceClass, double>> demands) {
    nicloadoff::StageSpec stage{};
    stage.service_profile = ref;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

struct PolicyRecord {
    bool limit_enabled{false};
    std::size_t active_count{0};
    bool waiting_sorted{true};
    std::size_t waiting_depth{0};
};

class OscillatingAdmissionPolicy : public nicloadoff::policy::PolicyHook {
  public:
    [[nodiscard]] nicloadoff::policy::PolicyDecision evaluate(
        const nicloadoff::PolicyStateSnapshot& snapshot) override {
        PolicyRecord record{};
        record.active_count = snapshot.active_task_count;
        record.waiting_depth = snapshot.waiting_task_order.size();

        std::vector<nicloadoff::TaskId> sorted = snapshot.waiting_task_order;
        std::sort(sorted.begin(), sorted.end());
        record.waiting_sorted = (sorted == snapshot.waiting_task_order);

        const bool should_enable =
            snapshot.queues.waiting_queue_depth >= 2 || snapshot.active_task_count >= 2;
        if (should_enable) {
            limit_enabled_ = true;
        } else {
            limit_enabled_ = false;
        }
        record.limit_enabled = limit_enabled_;

        nicloadoff::policy::PolicyDecision decision{};
        if (!snapshot.waiting_task_order.empty()) {
            decision.waiting_order = std::move(sorted);
        }

        nicloadoff::policy::AdmissionControlDirective directive{};
        directive.enabled = limit_enabled_;
        directive.max_active_tasks = 1;
        decision.admission = directive;

        records.push_back(record);
        return decision;
    }

    std::vector<PolicyRecord> records;

  private:
    bool limit_enabled_{false};
};

nicloadoff::TaskTiming find_metric(const nicloadoff::RunMetrics& metrics, nicloadoff::TaskId id) {
    for (const auto& timing : metrics.tasks) {
        if (timing.id == id) {
            return timing;
        }
    }
    std::fprintf(stderr, "missing metric for task %llu\n", static_cast<unsigned long long>(id));
    std::abort();
}

} // namespace

int main() {
    using namespace nicloadoff;

    auto profile = make_profile();
    ServiceTimeModel scheduler_model(profile, /*seed=*/246813579);

    auto inventory = make_resource_inventory_from_profile(profile);

    ServiceTimeProfileRef stage_ref{.key = "oscillating_stage",
                                    .domain = ServiceTimeDomain::kHost,
                                    .mode = ServiceTimeMode::kStochastic};

    StageSpec oscillating_stage =
        make_stochastic_stage(stage_ref,
                              {
                                  {ResourceClass::kHostCpu, 1.0},
                                  {ResourceClass::kNicCpu, 1.0},
                              });

    WorkloadSpec workload{};
    const std::vector<std::pair<TaskId, SimTime>> arrivals = {
        {201, 0.0},
        {202, 0.01},
        {203, 0.02},
        {204, 0.03},
        {205, 0.04},
        {206, 0.05},
    };

    for (const auto& [id, arrival] : arrivals) {
        workload.tasks.push_back(TaskSpec{.id = id, .arrival_time = arrival, .stages = {oscillating_stage}});
    }

    OscillatingAdmissionPolicy policy;
    BasicScheduler scheduler(std::move(inventory.pool), &scheduler_model);
    scheduler.set_policy_hook(&policy);

    const std::vector<Task> tasks = make_tasks_from_spec(workload, inventory.ids);
    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }

    scheduler.run_until_empty();

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == arrivals.size(), "expected all tasks to complete");

    std::size_t limit_enabled_count = 0;
    std::size_t limit_disabled_count = 0;
    bool observed_limit_with_single_active = false;
    bool pending_unsorted = false;
    std::size_t resolved_unsorted = 0;
    for (const auto& record : policy.records) {
        if (record.limit_enabled) {
            ++limit_enabled_count;
            if (record.active_count <= 1) {
                observed_limit_with_single_active = true;
            }
        } else {
            ++limit_disabled_count;
        }
        if (record.waiting_depth <= 1) {
            if (pending_unsorted) {
                ++resolved_unsorted;
                pending_unsorted = false;
            }
            continue;
        }
        if (!record.waiting_sorted) {
            pending_unsorted = true;
        } else {
            if (pending_unsorted) {
                ++resolved_unsorted;
                pending_unsorted = false;
            }
        }
    }

    check(limit_enabled_count > 0, "expected admission limit to engage at least once");
    check(limit_disabled_count > 0, "expected admission limit to release at least once");
    check(observed_limit_with_single_active, "expected admission limit to eventually observe single active task");
    check(resolved_unsorted > 0, "expected waiting queue reorder directives to resolve at least once");
    check(!pending_unsorted, "expected waiting queue reorder directives to resolve before completion");

    const RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == arrivals.size(), "expected per-task metrics for each task");

    double total_queue_time = 0.0;
    double total_service_time = 0.0;
    for (const auto& [id, _] : arrivals) {
        const TaskTiming timing = find_metric(metrics, id);
        check(timing.queue_time >= 0.0, "queue time must be non-negative");
        check(timing.service_time >= 0.0, "service time must be non-negative");
        total_queue_time += timing.queue_time;
        total_service_time += timing.service_time;
    }

    assert_near(metrics.aggregate.total_queue_time, total_queue_time);
    assert_near(metrics.aggregate.total_service_time, total_service_time);
    assert_near(metrics.aggregate.total_latency, total_queue_time + total_service_time);
    check(metrics.aggregate.latency_stats.count == arrivals.size(), "latency stats should include every task");

    return 0;
}
