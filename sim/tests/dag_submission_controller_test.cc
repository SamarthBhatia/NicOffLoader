#include "nicloadoff/dag_submission_controller.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
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

nicloadoff::StageSpec make_stage(nicloadoff::Duration service_time,
                                 std::initializer_list<std::pair<nicloadoff::ResourceClass, double>> demands) {
    nicloadoff::StageSpec stage{};
    stage.deterministic_service_time = service_time;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

nicloadoff::config::Profile make_profile() {
    nicloadoff::config::Profile profile{};
    profile.host_cpu.cores_total = 4;
    profile.host_dram.capacity_gb = 16;
    profile.host_nic_link.max_inflight_bytes = 1'000'000;
    profile.nic_cpu.cores_total = 4;
    profile.nic_dram.capacity_gb = 8;
    profile.nic_network_link.max_inflight_bytes = 1'000'000;
    return profile;
}

nicloadoff::TaskDAGSpec make_chain_dag() {
    nicloadoff::TaskDAGSpec dag{};
    dag.id = 100;
    dag.arrival_time = 5.0;

    nicloadoff::StageNodeSpec first{};
    first.name = "stage_a";
    first.stage = make_stage(1.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}});
    first.successors = {"stage_b"};

    nicloadoff::StageNodeSpec second{};
    second.name = "stage_b";
    second.stage = make_stage(2.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}});
    second.successors = {"stage_c"};

    nicloadoff::StageNodeSpec third{};
    third.name = "stage_c";
    third.stage = make_stage(3.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}});

    dag.nodes = {first, second, third};
    dag.entry_points = {"stage_a"};
    return dag;
}

} // namespace

int main() {
    using namespace nicloadoff;

    WorkloadSpec workload{};
    workload.dag_tasks.push_back(make_chain_dag());

    auto inventory = make_resource_inventory_from_profile(make_profile());
    DagSubmissionController controller = DagSubmissionController::from_spec(workload, inventory.ids);
    check(!controller.empty(), "controller should contain dag runtime");
    assert_near(controller.earliest_arrival_time(), 5.0);

    std::vector<Task> linear_tasks = make_tasks_from_spec(workload, inventory.ids);
    check(linear_tasks.empty(), "linear task list should be empty for DAG-only workload");

    BasicScheduler scheduler(std::move(inventory.pool));
    controller.submit_initial(scheduler);
    check(controller.manages(100), "controller should manage initial task id");

    while (scheduler.step_once()) {
        auto last = scheduler.last_event();
        if (last && last->metadata.type == EventType::kTaskComplete) {
            controller.handle_task_completion(last->metadata.id, scheduler.current_time(), scheduler);
        }
    }

    const auto completed = scheduler.completed_tasks();
    check(completed.size() == 3, "expected three completed DAG tasks");
    check(std::find(completed.begin(), completed.end(), 100) != completed.end(), "expected task 100 completion");
    check(std::find(completed.begin(), completed.end(), 101) != completed.end(), "expected task 101 completion");
    check(std::find(completed.begin(), completed.end(), 102) != completed.end(), "expected task 102 completion");
    check(!controller.manages(100), "controller should drop completed task mapping");
    assert_near(scheduler.current_time(), 11.0);

    return 0;
}
