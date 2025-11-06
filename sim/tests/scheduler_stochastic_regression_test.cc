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
#include <string>
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
    profile.host_cpu.cores_total = 1;
    profile.host_dram.capacity_gb = 16;
    profile.host_nic_link.max_inflight_bytes = 1'000'000;
    profile.nic_cpu.cores_total = 1;
    profile.nic_dram.capacity_gb = 8;
    profile.nic_network_link.max_inflight_bytes = 1'000'000;

    profile.service_time_overrides.emplace("host_stage",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 4.0,
                                                                                   .nic_mean_us = 4.0});
    profile.service_time_overrides.emplace("nic_stage",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.0,
                                                                                   .nic_mean_us = 1.5});
    return profile;
}

nicloadoff::StageSpec make_profile_stage(
    nicloadoff::ServiceTimeProfileRef profile_ref,
    std::initializer_list<std::pair<nicloadoff::ResourceClass, double>> demands) {
    nicloadoff::StageSpec stage{};
    stage.service_profile = std::move(profile_ref);
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

nicloadoff::ServiceTimeProfileRef make_ref(const std::string& key,
                                           nicloadoff::ServiceTimeDomain domain) {
    return nicloadoff::ServiceTimeProfileRef{
        .key = key, .domain = domain, .mode = nicloadoff::ServiceTimeMode::kStochastic};
}

nicloadoff::TaskTiming find_metric(const nicloadoff::RunMetrics& run_metrics, nicloadoff::TaskId id) {
    for (const auto& metric : run_metrics.tasks) {
        if (metric.id == id) {
            return metric;
        }
    }
    std::fprintf(stderr, "metric for task %d not found\n", static_cast<int>(id));
    std::abort();
}

} // namespace

int main() {
    using namespace nicloadoff;

    auto profile = make_profile();

    ServiceTimeModel scheduling_model(profile, /*seed=*/2027);
    ServiceTimeModel expectation_model(profile, /*seed=*/2027);

    const ServiceTimeProfileRef host_ref = make_ref("host_stage", ServiceTimeDomain::kHost);
    const ServiceTimeProfileRef nic_ref = make_ref("nic_stage", ServiceTimeDomain::kNic);

    const Duration expected_host_stage_task0 = expectation_model.sample(host_ref);
    const Duration expected_host_stage_task1 = expectation_model.sample(host_ref);
    const Duration expected_nic_stage_task0 = expectation_model.sample(nic_ref);

    const SimTime task1_arrival = expected_host_stage_task0 * 0.5;
    const Duration expected_queue_task1 =
        std::max<Duration>(0.0, expected_host_stage_task0 - task1_arrival);

    auto inventory = make_resource_inventory_from_profile(profile);

    StageSpec host_stage_spec = make_profile_stage(host_ref, {{ResourceClass::kHostCpu, 1.0}});
    StageSpec nic_stage_spec = make_profile_stage(nic_ref, {{ResourceClass::kNicCpu, 1.0}});

    WorkloadSpec workload{};

    TaskSpec first{};
    first.id = 10;
    first.arrival_time = 0.0;
    first.stages = {host_stage_spec, nic_stage_spec};
    workload.tasks.push_back(first);

    TaskSpec second{};
    second.id = 11;
    second.arrival_time = task1_arrival;
    second.stages = {host_stage_spec};
    workload.tasks.push_back(second);

    auto tasks = make_tasks_from_spec(workload, inventory.ids);

    BasicScheduler scheduler(std::move(inventory.pool), &scheduling_model);
    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }
    scheduler.run_until_empty();

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == 2, "expected two completed tasks");
    check((completed[0] == 10 || completed[0] == 11), "unexpected first completion task id");
    check((completed[1] == 10 || completed[1] == 11), "unexpected second completion task id");

    const Duration expected_completion_time =
        expected_host_stage_task0 + std::max(expected_host_stage_task1, expected_nic_stage_task0);
    assert_near(scheduler.current_time(), expected_completion_time);

    const RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == 2, "expected two per-task metrics");

    const TaskTiming first_metrics = find_metric(metrics, 10);
    const TaskTiming second_metrics = find_metric(metrics, 11);

    assert_near(first_metrics.queue_time, 0.0);
    assert_near(first_metrics.service_time, expected_host_stage_task0 + expected_nic_stage_task0);
    assert_near(first_metrics.host_service_time, expected_host_stage_task0);
    assert_near(first_metrics.nic_service_time, expected_nic_stage_task0);

    assert_near(second_metrics.queue_time, expected_queue_task1);
    assert_near(second_metrics.service_time, expected_host_stage_task1);
    assert_near(second_metrics.host_service_time, expected_host_stage_task1);
    assert_near(second_metrics.nic_service_time, 0.0);

    assert_near(metrics.aggregate.host_service_time,
                expected_host_stage_task0 + expected_host_stage_task1);
    assert_near(metrics.aggregate.nic_service_time, expected_nic_stage_task0);
    assert_near(metrics.aggregate.total_service_time,
                expected_host_stage_task0 + expected_host_stage_task1 + expected_nic_stage_task0);
    assert_near(metrics.aggregate.total_queue_time, expected_queue_task1);

    const Duration expected_total_latency = (expected_host_stage_task0 + expected_nic_stage_task0) +
                                            (expected_queue_task1 + expected_host_stage_task1);
    assert_near(metrics.aggregate.total_latency, expected_total_latency);

    const auto& stats = metrics.aggregate.latency_stats;
    check(stats.count == 2, "expected latency stats for two tasks");
    assert_near(stats.sum, metrics.aggregate.total_latency);

    return 0;
}
