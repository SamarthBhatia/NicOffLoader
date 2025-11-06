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

    profile.service_time_overrides.emplace("burst_stage",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 5.0,
                                                                                   .nic_mean_us = 5.0});
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

struct SnapshotRecord {
    bool host_full{false};
    bool nic_full{false};
    std::size_t active_tasks{0};
};

class SnapshotPolicy : public nicloadoff::policy::PolicyHook {
  public:
    [[nodiscard]] nicloadoff::policy::PolicyDecision evaluate(
        const nicloadoff::PolicyStateSnapshot& snapshot) override {
        SnapshotRecord record{};
        record.active_tasks = snapshot.active_task_count;

        auto near_full = [](double in_use, double capacity) {
            return std::fabs(in_use - capacity) < 1e-9;
        };

        for (const auto& resource : snapshot.resources) {
            switch (resource.type) {
            case nicloadoff::ResourceType::kHostCpu:
                record.host_full = near_full(resource.in_use, resource.capacity);
                break;
            case nicloadoff::ResourceType::kNicCpu:
                record.nic_full = near_full(resource.in_use, resource.capacity);
                break;
            default:
                break;
            }
        }

        records.push_back(record);
        return nicloadoff::policy::PolicyDecision{};
    }

    std::vector<SnapshotRecord> records;
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
    ServiceTimeModel scheduler_model(profile, /*seed=*/98765);
    ServiceTimeModel expectation_model(profile, /*seed=*/98765);

    auto inventory = make_resource_inventory_from_profile(profile);

    ServiceTimeProfileRef stage_ref{.key = "burst_stage",
                                    .domain = ServiceTimeDomain::kHost,
                                    .mode = ServiceTimeMode::kStochastic};

    StageSpec burst_stage =
        make_stochastic_stage(stage_ref,
                              {
                                  {ResourceClass::kHostCpu, 2.0},
                                  {ResourceClass::kNicCpu, 2.0},
                              });

    WorkloadSpec workload{};
    const std::vector<std::pair<TaskId, SimTime>> arrivals = {
        {101, 0.0},
        {102, 0.02},
        {103, 0.04},
        {104, 0.06},
        {105, 0.08},
    };

    for (const auto& [id, arrival] : arrivals) {
        workload.tasks.push_back(TaskSpec{.id = id, .arrival_time = arrival, .stages = {burst_stage}});
    }

    const std::size_t task_count = workload.tasks.size();
    std::vector<double> sampled_durations;
    sampled_durations.reserve(task_count);
    for (std::size_t i = 0; i < task_count; ++i) {
        sampled_durations.push_back(expectation_model.sample(stage_ref));
    }

    std::vector<double> expected_queue(task_count, 0.0);
    std::vector<double> expected_completion(task_count, 0.0);
    double current_time = 0.0;
    for (std::size_t i = 0; i < task_count; ++i) {
        const double arrival = arrivals[i].second;
        const double start_time = std::max(arrival, current_time);
        expected_queue[i] = start_time - arrival;
        current_time = start_time + sampled_durations[i];
        expected_completion[i] = current_time;
    }

    SnapshotPolicy policy;
    BasicScheduler scheduler(std::move(inventory.pool), &scheduler_model);
    scheduler.set_policy_hook(&policy);

    const std::vector<Task> tasks = make_tasks_from_spec(workload, inventory.ids);
    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }

    scheduler.run_until_empty();

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == task_count, "expected all tasks to complete");
    for (std::size_t i = 0; i < task_count; ++i) {
        check(completed[i] == arrivals[i].first, "tasks should complete in arrival order under full-resource stages");
    }

    bool saw_dual_exhaustion = false;
    for (const auto& record : policy.records) {
        if (record.host_full && record.nic_full) {
            saw_dual_exhaustion = true;
            break;
        }
    }
    check(saw_dual_exhaustion, "expected snapshots to capture simultaneous host/NIC saturation");

    const RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == task_count, "expected per-task metrics for each task");

    double total_queue = 0.0;
    double total_service = 0.0;
    for (std::size_t i = 0; i < task_count; ++i) {
        const TaskId id = arrivals[i].first;
        const TaskTiming timing = find_metric(metrics, id);
        assert_near(timing.service_time, sampled_durations[i]);
        assert_near(timing.queue_time, expected_queue[i]);
        total_queue += timing.queue_time;
        total_service += timing.service_time;
    }

    assert_near(metrics.aggregate.total_service_time, total_service);
    assert_near(metrics.aggregate.total_queue_time, total_queue);
    assert_near(metrics.aggregate.total_latency, total_queue + total_service);
    check(metrics.aggregate.latency_stats.count == task_count, "latency stats should track every task");

    return 0;
}
