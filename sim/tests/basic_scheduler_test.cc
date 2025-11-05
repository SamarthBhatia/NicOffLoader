#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/policy_hook.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

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

nicloadoff::StageSpec make_profile_stage(const nicloadoff::ServiceTimeProfileRef& profile_ref,
                                         std::initializer_list<std::pair<nicloadoff::ResourceClass, double>> demands) {
    nicloadoff::StageSpec stage{};
    stage.service_profile = profile_ref;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

nicloadoff::config::Profile make_profile(double host_cpu,
                                         double host_dram,
                                         double host_link,
                                         double nic_cpu = 1.0,
                                         double nic_dram = 1.0,
                                         double nic_link = 1.0) {
    nicloadoff::config::Profile profile{};
    profile.host_cpu.cores_total = static_cast<std::uint32_t>(host_cpu);
    profile.host_dram.capacity_gb = static_cast<std::uint32_t>(host_dram);
    profile.host_nic_link.max_inflight_bytes = static_cast<std::size_t>(host_link);
    profile.nic_cpu.cores_total = static_cast<std::uint32_t>(nic_cpu);
    profile.nic_dram.capacity_gb = static_cast<std::uint32_t>(nic_dram);
    profile.nic_network_link.max_inflight_bytes = static_cast<std::size_t>(nic_link);
    return profile;
}

nicloadoff::config::Profile make_profile_with_service() {
    auto profile = make_profile(8.0, 128.0, 64'000'000.0, 8.0, 16.0, 32'000'000.0);
    profile.service_time_overrides.emplace("kv_lookup",
                                           nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.5,
                                                                                   .nic_mean_us = 1.5});
    return profile;
}

} // namespace

int main() {
    {
        auto profile = make_profile(1.0, 8.0, 128.0);
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);

        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 1,
            .arrival_time = 0.0,
            .stages = {make_stage(5.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kHostDram, 2.0},
                                   {nicloadoff::ResourceClass::kHostLink, 32.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 2,
            .arrival_time = 1.0,
            .stages = {make_stage(3.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kHostDram, 2.0},
                                   {nicloadoff::ResourceClass::kHostLink, 32.0}})},
        });

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 8.0);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected two completed tasks");
        check(completed[0] == 1, "expected task 1 to complete first");
        check(completed[1] == 2, "expected task 2 to complete second");

        const auto* cpu = scheduler.resource_pool().find(inventory.ids.host_cpu);
        check(cpu != nullptr, "expected CPU resource to be present");
        assert_near(cpu->in_use(), 0.0);
    }

    {
        auto profile = make_profile(2.0, 8.0, 128.0);
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);

        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 3,
            .arrival_time = 0.0,
            .stages = {make_stage(4.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kHostDram, 2.0},
                                   {nicloadoff::ResourceClass::kHostLink, 32.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 4,
            .arrival_time = 0.0,
            .stages = {make_stage(6.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kHostDram, 2.0},
                                   {nicloadoff::ResourceClass::kHostLink, 32.0}})},
        });

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 6.0);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected two completed tasks in parallel scenario");
        bool expected_order =
            (completed[0] == 3 && completed[1] == 4) || (completed[0] == 4 && completed[1] == 3);
        check(expected_order, "unexpected task completion order in parallel scenario");

        const auto* cpu = scheduler.resource_pool().find(inventory.ids.host_cpu);
        check(cpu != nullptr, "expected CPU resource to be present in parallel scenario");
        assert_near(cpu->in_use(), 0.0);
    }

    {
        auto profile = make_profile(1.0, 1.0, 1'000'000.0);
        profile.service_time_overrides.emplace("kv_lookup",
                                               nicloadoff::config::ServiceTimeOverride{.host_mean_us = 2.5,
                                                                                       .nic_mean_us = 1.5});
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
        nicloadoff::ServiceTimeModel service_model(profile, /*seed=*/2025);

        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 5,
            .arrival_time = 0.0,
            .stages = {make_profile_stage(
                nicloadoff::ServiceTimeProfileRef{.key = "kv_lookup",
                                                  .domain = nicloadoff::ServiceTimeDomain::kHost,
                                                  .mode = nicloadoff::ServiceTimeMode::kDeterministic},
                {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool), &service_model);
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 2.5);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 1, "expected single completed task when using service profile");
        check(completed[0] == 5, "expected task 5 to complete");
    }

    {
        auto profile = make_profile_with_service();
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
        nicloadoff::ServiceTimeModel service_model(profile, /*seed=*/99);

        nicloadoff::ServiceTimeProfileRef host_profile{.key = "kv_lookup",
                                                       .domain = nicloadoff::ServiceTimeDomain::kHost,
                                                       .mode = nicloadoff::ServiceTimeMode::kDeterministic};
        nicloadoff::ServiceTimeProfileRef nic_profile{.key = "kv_lookup",
                                                      .domain = nicloadoff::ServiceTimeDomain::kNic,
                                                      .mode = nicloadoff::ServiceTimeMode::kDeterministic};

        nicloadoff::WorkloadSpec workload{};
        nicloadoff::StageSpec stage1 = make_profile_stage(
            host_profile,
            {{nicloadoff::ResourceClass::kHostCpu, 4.0}, {nicloadoff::ResourceClass::kHostDram, 32.0}});
        nicloadoff::StageSpec stage2 = make_profile_stage(
            nic_profile,
            {{nicloadoff::ResourceClass::kNicCpu, 6.0}, {nicloadoff::ResourceClass::kNicDram, 8.0}});

        workload.tasks.push_back(nicloadoff::TaskSpec{.id = 6,
                                                      .arrival_time = 0.0,
                                                      .stages = {stage1, stage2}});
        workload.tasks.push_back(nicloadoff::TaskSpec{.id = 7,
                                                      .arrival_time = 0.0,
                                                      .stages = {stage1, stage2}});

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool), &service_model);
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 5.5);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected both multi-stage tasks to complete");
        check(completed[0] == 6, "expected task 6 to finish first due to NIC contention");
        check(completed[1] == 7, "expected task 7 to finish second due to NIC contention");

        const auto* host_cpu = scheduler.resource_pool().find(inventory.ids.host_cpu);
        check(host_cpu != nullptr, "expected host CPU resource to be present after profile setup");
        assert_near(host_cpu->in_use(), 0.0);

        const auto* nic_cpu = scheduler.resource_pool().find(inventory.ids.nic_cpu);
        check(nic_cpu != nullptr, "expected NIC CPU resource to be present after profile setup");
        assert_near(nic_cpu->in_use(), 0.0);
    }

    {
        auto profile = make_profile(1.0, 8.0, 128.0);
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);

        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 10,
            .arrival_time = 0.0,
            .stages = {make_stage(5.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 11,
            .arrival_time = 0.1,
            .stages = {make_stage(5.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 12,
            .arrival_time = 0.2,
            .stages = {make_stage(5.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
        auto policy = nicloadoff::policy::make_policy_hook("descending-id");
        scheduler.set_policy_hook(policy.get());
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 3, "expected three completed tasks with policy hook");
        check(completed[0] == 10, "expected first completion to remain task 10");
        check(completed[1] == 12, "expected policy to prioritise higher task id");
        check(completed[2] == 11, "expected lowest priority task to run last");
    }

    {
        auto profile = make_profile(2.0, 8.0, 128.0);
        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);

        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 20,
            .arrival_time = 0.0,
            .stages = {make_stage(4.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 21,
            .arrival_time = 0.0,
            .stages = {make_stage(6.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });

        auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
        auto policy = nicloadoff::policy::make_policy_hook("limit-active-1");
        scheduler.set_policy_hook(policy.get());
        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        assert_near(scheduler.current_time(), 10.0);
        const auto& completed = scheduler.completed_tasks();
        check(completed.size() == 2, "expected two completed tasks with admission control");
        check(completed[0] == 20, "expected task 20 to complete first under throttle");
        check(completed[1] == 21, "expected task 21 to complete second under throttle");
    }

    {
        auto profile = make_profile(1.0, 8.0, 128.0, 1.0, 8.0, 128.0);
        nicloadoff::WorkloadSpec workload{};
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 29,
            .arrival_time = 0.0,
            .stages = {make_stage(6.0, {{nicloadoff::ResourceClass::kHostCpu, 1.0}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 30,
            .arrival_time = 1.0,
            .stages = {make_stage(4.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kNicCpu, 0.1}})},
        });
        workload.tasks.push_back(nicloadoff::TaskSpec{
            .id = 31,
            .arrival_time = 1.0,
            .stages = {make_stage(4.0,
                                  {{nicloadoff::ResourceClass::kHostCpu, 1.0},
                                   {nicloadoff::ResourceClass::kNicCpu, 0.8}})},
        });

        {
            auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
            auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
            nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
            auto policy = nicloadoff::policy::make_policy_hook("prefer-host");
            scheduler.set_policy_hook(policy.get());
            for (const auto& task : tasks) {
                scheduler.submit_task(task);
            }
            scheduler.run_until_empty();

            const auto& completed = scheduler.completed_tasks();
            check(completed.size() == 3, "expected three completed tasks with host-prefer policy");
            check(completed[0] == 29, "expected blocker task to finish first");
            check(completed[1] == 30, "expected host-heavy task to complete second under host preference");
            check(completed[2] == 31, "expected NIC-heavy task to complete last under host preference");
        }

        {
            auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
            auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);
            nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
            auto policy = nicloadoff::policy::make_policy_hook("prefer-nic");
            scheduler.set_policy_hook(policy.get());
            for (const auto& task : tasks) {
                scheduler.submit_task(task);
            }
            scheduler.run_until_empty();

            const auto& completed = scheduler.completed_tasks();
            check(completed.size() == 3, "expected three completed tasks with nic-prefer policy");
            check(completed[0] == 29, "expected blocker task to finish first");
            check(completed[1] == 31, "expected NIC-heavy task to complete second under nic preference");
            check(completed[2] == 30, "expected host-heavy task to complete last under nic preference");
        }
    }

    return 0;
}
