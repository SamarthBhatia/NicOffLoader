#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

nicloadoff::Task make_task(nicloadoff::TaskId id,
                           nicloadoff::SimTime arrival,
                           double cpu_units,
                           double dram_units,
                           double link_units,
                           nicloadoff::Duration service_time) {
    nicloadoff::Task task;
    task.id = id;
    task.arrival_time = arrival;
    nicloadoff::TaskStage stage;
    stage.service_time = service_time;
    stage.requirements = {
        {.resource_id = 1, .units = cpu_units},
        {.resource_id = 2, .units = dram_units},
        {.resource_id = 3, .units = link_units},
    };
    task.stages.push_back(stage);
    return task;
}

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

} // namespace

int main() {
    {
        nicloadoff::ResourcePool resources;
        resources.add_resource(nicloadoff::Resource{1, nicloadoff::ResourceType::kHostCpu, 1.0});
        resources.add_resource(nicloadoff::Resource{2, nicloadoff::ResourceType::kHostDram, 8.0});
        resources.add_resource(nicloadoff::Resource{3, nicloadoff::ResourceType::kHostLink, 128.0});

        nicloadoff::BasicScheduler scheduler(std::move(resources));
        auto task_a = make_task(1, 0.0, 1.0, 2.0, 32.0, 5.0);
        auto task_b = make_task(2, 1.0, 1.0, 2.0, 32.0, 3.0);

        scheduler.submit_task(task_a);
        scheduler.submit_task(task_b);
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 8.0);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected two completed tasks");
        check(completed[0] == 1, "expected task 1 to complete first");
        check(completed[1] == 2, "expected task 2 to complete second");

        const auto* cpu = scheduler.resource_pool().find(1);
        check(cpu != nullptr, "expected CPU resource to be present");
        assert_near(cpu->in_use(), 0.0);
    }

    {
        nicloadoff::ResourcePool resources;
        resources.add_resource(nicloadoff::Resource{1, nicloadoff::ResourceType::kHostCpu, 2.0});
        resources.add_resource(nicloadoff::Resource{2, nicloadoff::ResourceType::kHostDram, 8.0});
        resources.add_resource(nicloadoff::Resource{3, nicloadoff::ResourceType::kHostLink, 128.0});

        nicloadoff::BasicScheduler scheduler(std::move(resources));
        auto task_c = make_task(3, 0.0, 1.0, 2.0, 32.0, 4.0);
        auto task_d = make_task(4, 0.0, 1.0, 2.0, 32.0, 6.0);

        scheduler.submit_task(task_c);
        scheduler.submit_task(task_d);
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 6.0);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected two completed tasks in parallel scenario");
        bool expected_order = (completed[0] == 3 && completed[1] == 4) || (completed[0] == 4 && completed[1] == 3);
        check(expected_order, "unexpected task completion order in parallel scenario");

        const auto* cpu = scheduler.resource_pool().find(1);
        check(cpu != nullptr, "expected CPU resource to be present in parallel scenario");
        assert_near(cpu->in_use(), 0.0);
    }

    {
        nicloadoff::ResourcePool resources;
        resources.add_resource(nicloadoff::Resource{10, nicloadoff::ResourceType::kHostCpu, 1.0});

        nicloadoff::config::Profile profile{};
        profile.service_time_overrides.emplace("kv_lookup",
                                               nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.5,
                                                                                       .nic_mean_us = 1.5});
        nicloadoff::ServiceTimeModel service_model(profile, /*seed=*/2025);

        nicloadoff::BasicScheduler scheduler(std::move(resources), &service_model);

        nicloadoff::Task task{};
        task.id = 5;
        task.arrival_time = 0.0;
        nicloadoff::TaskStage stage{};
        stage.requirements.push_back({.resource_id = 10, .units = 1.0});
        stage.service_profile = nicloadoff::ServiceTimeProfileRef{
            .key = "kv_lookup",
            .domain = nicloadoff::ServiceTimeDomain::kHost,
            .mode = nicloadoff::ServiceTimeMode::kDeterministic,
        };
        task.stages.push_back(stage);

        scheduler.submit_task(task);
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 2.5);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 1, "expected single completed task when using service profile");
        check(completed[0] == 5, "expected task 5 to complete");
    }

    return 0;
}
