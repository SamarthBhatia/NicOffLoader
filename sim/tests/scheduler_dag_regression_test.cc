#include "nicloadoff/dag_submission_controller.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
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

    profile.service_time_overrides.emplace("host_preprocess",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.0,
                                                                                   .nic_mean_us = 2.0});
    profile.service_time_overrides.emplace("nic_offload",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.0,
                                                                                   .nic_mean_us = 1.0});
    profile.service_time_overrides.emplace("host_finalize",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 1.5,
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
        .key = key, .domain = domain, .mode = nicloadoff::ServiceTimeMode::kDeterministic};
}

} // namespace

int main() {
    using namespace nicloadoff;

    auto profile = make_profile();
    auto inventory = make_resource_inventory_from_profile(profile);
    ServiceTimeModel service_model(profile, /*seed=*/42);

    WorkloadSpec workload{};

    const ServiceTimeProfileRef host_stage = make_ref("host_preprocess", ServiceTimeDomain::kHost);
    const ServiceTimeProfileRef nic_stage = make_ref("nic_offload", ServiceTimeDomain::kNic);
    const ServiceTimeProfileRef host_tail = make_ref("host_finalize", ServiceTimeDomain::kHost);

    StageSpec linear_stage_host =
        make_profile_stage(host_stage, {{ResourceClass::kHostCpu, 1.0}});
    StageSpec linear_stage_nic =
        make_profile_stage(nic_stage, {{ResourceClass::kNicCpu, 1.0}});

    TaskSpec linear_one{};
    linear_one.id = 1;
    linear_one.arrival_time = 0.5;
    linear_one.stages = {linear_stage_host, linear_stage_nic};

    TaskSpec linear_two{};
    linear_two.id = 2;
    linear_two.arrival_time = 1.0;
    linear_two.stages = {linear_stage_host, linear_stage_nic};

    workload.tasks.push_back(linear_one);
    workload.tasks.push_back(linear_two);

    TaskDAGSpec dag{};
    dag.id = 100;
    dag.arrival_time = 0.0;

    StageNodeSpec decode{};
    decode.name = "decode";
    decode.stage = make_profile_stage(host_stage, {{ResourceClass::kHostCpu, 1.0}});
    decode.successors = {"offload"};

    StageNodeSpec offload{};
    offload.name = "offload";
    offload.stage = make_profile_stage(nic_stage, {{ResourceClass::kNicCpu, 1.0}});
    offload.successors = {"finalize"};

    StageNodeSpec finalize{};
    finalize.name = "finalize";
    finalize.stage = make_profile_stage(host_tail, {{ResourceClass::kHostCpu, 1.0}});

    dag.nodes = {decode, offload, finalize};
    dag.entry_points = {"decode"};
    workload.dag_tasks.push_back(dag);

    BasicScheduler scheduler(std::move(inventory.pool), &service_model);
    DagSubmissionController controller = DagSubmissionController::from_spec(workload, inventory.ids);
    controller.submit_initial(scheduler);

    const std::vector<Task> linear_tasks = make_tasks_from_spec(workload, inventory.ids);
    for (const auto& task : linear_tasks) {
        scheduler.submit_task(task);
    }

    while (scheduler.step_once()) {
        auto last = scheduler.last_event();
        if (last && last->metadata.type == EventType::kTaskComplete) {
            controller.handle_task_completion(last->metadata.id, scheduler.current_time(), scheduler);
        }
    }

    const auto completed = scheduler.completed_tasks();
    check(completed.size() == 5, "expected three DAG-derived completions plus two linear tasks");
    check(completed[0] == 100, "expected DAG entry node to complete first");
    check(completed[1] == 101, "expected DAG nic node to complete second");
    check(completed[2] == 1, "expected first linear task completion third");
    check(completed[3] == 2, "expected second linear task completion fourth");
    check(completed[4] == 102, "expected DAG tail node to complete last");

    check(!controller.manages(100), "controller should release DAG entry mapping after completion");
    check(!controller.manages(101), "controller should release DAG middle mapping after completion");
    check(!controller.manages(102), "controller should release DAG tail mapping after completion");

    assert_near(scheduler.current_time(), 7.5);

    const RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == 5, "expected run metrics for five completed tasks");

    const auto find_metric = [](const RunMetrics& run_metrics, TaskId id) {
        for (const auto& metric : run_metrics.tasks) {
            if (metric.id == id) {
                return metric;
            }
        }
        std::fprintf(stderr, "metric for task %d not found\n", static_cast<int>(id));
        std::abort();
    };

    const auto dag_tail_metrics = find_metric(metrics, 102);
    assert_near(dag_tail_metrics.queue_time, 3.0, 1e-9);
    assert_near(dag_tail_metrics.service_time, 1.5, 1e-9);

    const auto linear_two_metrics = find_metric(metrics, 2);
    assert_near(linear_two_metrics.queue_time, 3.0, 1e-9);
    assert_near(linear_two_metrics.service_time, 3.0, 1e-9);

    assert_near(metrics.aggregate.host_service_time, 7.5, 1e-9);
    assert_near(metrics.aggregate.nic_service_time, 3.0, 1e-9);
    assert_near(metrics.aggregate.total_queue_time, 7.5, 1e-9);

    return 0;
}
