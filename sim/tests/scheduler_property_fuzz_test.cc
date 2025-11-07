#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/policy_state.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr double kEpsilon = 1e-9;

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

struct ResourceCaps {
    double host_cpu{4.0};
    double host_dram{64.0};
    double host_link{500'000.0};
    double nic_cpu{2.0};
    double nic_dram{32.0};
    double nic_link{250'000.0};
};

nicloadoff::config::Profile make_profile(const ResourceCaps& caps) {
    nicloadoff::config::Profile profile{};
    profile.host_cpu.cores_total = static_cast<std::uint32_t>(caps.host_cpu);
    profile.host_dram.capacity_gb = static_cast<std::uint32_t>(caps.host_dram);
    profile.host_nic_link.max_inflight_bytes = static_cast<std::size_t>(caps.host_link);
    profile.nic_cpu.cores_total = static_cast<std::uint32_t>(caps.nic_cpu);
    profile.nic_dram.capacity_gb = static_cast<std::uint32_t>(caps.nic_dram);
    profile.nic_network_link.max_inflight_bytes = static_cast<std::size_t>(caps.nic_link);
    return profile;
}

double capacity_for(nicloadoff::ResourceClass resource, const ResourceCaps& caps) {
    switch (resource) {
    case nicloadoff::ResourceClass::kHostCpu:
        return caps.host_cpu;
    case nicloadoff::ResourceClass::kHostDram:
        return caps.host_dram;
    case nicloadoff::ResourceClass::kHostLink:
        return caps.host_link;
    case nicloadoff::ResourceClass::kNicCpu:
        return caps.nic_cpu;
    case nicloadoff::ResourceClass::kNicDram:
        return caps.nic_dram;
    case nicloadoff::ResourceClass::kNicLink:
        return caps.nic_link;
    default:
        return 1.0;
    }
}

double sample_units(std::mt19937_64& rng, double capacity) {
    std::uniform_real_distribution<double> fraction_dist(0.05, 0.5);
    const double raw = std::min(capacity * fraction_dist(rng), capacity * 0.95);
    double scale = 0.01;
    if (capacity >= 1000.0) {
        scale = 1.0;
    } else if (capacity >= 10.0) {
        scale = 0.1;
    }
    const double quantized = std::max(scale, std::round(raw / scale) * scale);
    return std::min(quantized, capacity);
}

struct PolicyScenario {
    const char* name{"none"};
    const char* hook_id{"none"};
    bool expect_descending_wait{false};
    bool expect_single_active{false};
};

struct PolicyObservations {
    bool descending_observed{false};
    bool waiting_multi_seen{false};
    bool admission_limit_seen{false};
};

std::unique_ptr<nicloadoff::policy::PolicyHook> make_policy(const PolicyScenario& scenario) {
    return nicloadoff::policy::make_policy_hook(scenario.hook_id);
}

bool is_descending(const std::vector<nicloadoff::TaskId>& ids) {
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i - 1] < ids[i]) {
            return false;
        }
    }
    return true;
}

nicloadoff::StageSpec make_random_stage(std::mt19937_64& rng, const ResourceCaps& caps) {
    nicloadoff::StageSpec stage{};
    std::uniform_real_distribution<double> service_dist(0.05, 5.0);
    stage.deterministic_service_time = service_dist(rng);

    // Always include host CPU to guarantee scheduler contention.
    stage.demands.push_back(
        nicloadoff::StageResourceDemand{.resource = nicloadoff::ResourceClass::kHostCpu,
                                        .units = sample_units(rng, caps.host_cpu)});

    std::array<nicloadoff::ResourceClass, 5> extras = {
        nicloadoff::ResourceClass::kHostDram,
        nicloadoff::ResourceClass::kHostLink,
        nicloadoff::ResourceClass::kNicCpu,
        nicloadoff::ResourceClass::kNicDram,
        nicloadoff::ResourceClass::kNicLink,
    };
    std::shuffle(extras.begin(), extras.end(), rng);

    std::uniform_int_distribution<int> extra_count_dist(1, 3);
    const int extra_count = extra_count_dist(rng);
    for (int i = 0; i < extra_count; ++i) {
        const auto resource = extras[static_cast<std::size_t>(i)];
        const double capacity = capacity_for(resource, caps);
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource,
                                                                .units = sample_units(rng, capacity)});
    }

    return stage;
}

void verify_snapshot_invariants(const nicloadoff::PolicyStateSnapshot& snapshot,
                                const PolicyScenario& scenario,
                                PolicyObservations& observations) {
    for (const auto& resource : snapshot.resources) {
        check(resource.capacity >= 0.0, "resource capacity must be non-negative");
        check(resource.in_use >= -kEpsilon, "resource in use underflow");
        check(resource.in_use <= resource.capacity + kEpsilon, "resource usage exceeded capacity");
    }

    std::unordered_set<nicloadoff::TaskId> waiting_set;
    for (nicloadoff::TaskId id : snapshot.waiting_task_order) {
        check(waiting_set.insert(id).second, "duplicate id in waiting order");
    }

    check(snapshot.active_task_count <= snapshot.tasks.size(), "active count exceeds task set size");

    for (const auto& task : snapshot.tasks) {
        check(task.stage_index <= task.total_stages, "task stage index out of bounds");
        if (task.completed) {
            check(task.stage_index == task.total_stages, "completed task with remaining stages");
        }
        check(task.pending_host_demand >= -kEpsilon, "negative pending host demand");
        check(task.pending_nic_demand >= -kEpsilon, "negative pending NIC demand");
    }

    const auto& stats = snapshot.run_metrics.aggregate.latency_stats;
    check(stats.count == snapshot.run_metrics.tasks.size(), "latency stats count mismatch");
    check(stats.sum >= -kEpsilon, "latency stats sum negative");
    check(stats.mean >= -kEpsilon, "latency stats mean negative");

    if (scenario.expect_descending_wait) {
        if (snapshot.waiting_task_order.size() > 1) {
            observations.waiting_multi_seen = true;
            if (is_descending(snapshot.waiting_task_order)) {
                observations.descending_observed = true;
            }
        }
    }
    if (scenario.expect_single_active) {
        if (snapshot.admission_limit.has_value()) {
            check(snapshot.active_task_count <= *snapshot.admission_limit,
                  "active task count exceeded admission limit");
            check(*snapshot.admission_limit <= 1,
                  "limit-active policy reported unexpected admission limit");
            observations.admission_limit_seen = true;
        }
        check(snapshot.active_task_count <= 1, "limit-active policy exceeded single active task");
    }
}

void verify_final_metrics(const nicloadoff::RunMetrics& metrics) {
    double queue_sum = 0.0;
    double service_sum = 0.0;
    double latency_sum = 0.0;
    for (const auto& timing : metrics.tasks) {
        check(timing.queue_time >= -kEpsilon, "per-task queue time negative");
        check(timing.service_time >= -kEpsilon, "per-task service time negative");
        queue_sum += timing.queue_time;
        service_sum += timing.service_time;
        latency_sum += timing.latency();
    }

    assert_near(metrics.aggregate.total_queue_time, queue_sum, 1e-6);
    assert_near(metrics.aggregate.total_service_time, service_sum, 1e-6);
    assert_near(metrics.aggregate.total_latency, latency_sum, 1e-6);
    assert_near(metrics.aggregate.latency_stats.sum, latency_sum, 1e-6);
}

void run_trial(std::uint64_t seed, int burst_group, const ResourceCaps& caps, const PolicyScenario& scenario) {
    std::mt19937_64 rng(seed);
    auto profile = make_profile(caps);
    auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);

    const std::size_t task_count = static_cast<std::size_t>(burst_group * 4);
    std::uniform_real_distribution<double> gap_dist(0.0, 1.0);
    std::uniform_int_distribution<int> stage_count_dist(1, 3);

    nicloadoff::WorkloadSpec workload{};
    double arrival_time = 0.0;
    for (std::size_t i = 0; i < task_count; ++i) {
        const double gap_scale = 0.3 / static_cast<double>(burst_group);
        arrival_time += gap_dist(rng) * gap_scale;

        nicloadoff::TaskSpec spec{};
        spec.id = static_cast<nicloadoff::TaskId>(i + 1);
        spec.arrival_time = arrival_time;

        const int stage_count = stage_count_dist(rng);
        spec.stages.reserve(static_cast<std::size_t>(stage_count));
        for (int s = 0; s < stage_count; ++s) {
            spec.stages.push_back(make_random_stage(rng, caps));
        }
        workload.tasks.push_back(std::move(spec));
    }

    auto tasks = nicloadoff::make_tasks_from_spec(workload, inventory.ids);

    auto policy_hook = make_policy(scenario);
    nicloadoff::BasicScheduler scheduler(std::move(inventory.pool));
    scheduler.set_policy_hook(policy_hook.get());
    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }

    double previous_time = -1.0;
    std::size_t processed_events = 0;
    PolicyObservations observations{};

    while (scheduler.step_once()) {
        ++processed_events;
        const auto event = scheduler.last_event();
        check(event.has_value(), "expected last event to exist");
        check(event->timestamp + kEpsilon >= previous_time, "event timestamps not monotonic");
        previous_time = event->timestamp;

        const auto snapshot = scheduler.policy_state_snapshot();
        assert_near(snapshot.current_time, scheduler.current_time(), 1e-9);
        verify_snapshot_invariants(snapshot, scenario, observations);
    }

    check(processed_events > 0, "scheduler processed no events");
    check(!scheduler.has_pending_work(), "scheduler should have drained all work");

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == tasks.size(), "completed task count mismatch");

    const auto metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == tasks.size(), "metrics task count mismatch");
    verify_final_metrics(metrics);

    const auto resources = scheduler.resource_pool().snapshot();
    for (const auto& resource : resources) {
        assert_near(resource.in_use(), 0.0, 1e-9);
    }

    check(scheduler.events_processed() == processed_events,
          "events processed count mismatch against step loop");

    if (scenario.expect_descending_wait) {
        check(!observations.waiting_multi_seen || observations.descending_observed,
              "descending-id policy never produced a descending waiting queue snapshot");
    }
    if (scenario.expect_single_active) {
        check(observations.admission_limit_seen,
              "limit-active policy never surfaced an admission directive");
    }
}

} // namespace

int main() {
    const ResourceCaps caps{};
    const int seed_trials = 16;
    const int burst_groups = 5;

    const std::array<PolicyScenario, 3> scenarios = {
        PolicyScenario{"none", "none", false, false},
        PolicyScenario{"descending-id", "descending-id", true, false},
        PolicyScenario{"limit-active-1", "limit-active-1", false, true},
    };

    std::uint64_t base_seed = 0xBADC0FFEEULL;
    for (int trial = 0; trial < seed_trials; ++trial) {
        std::uint64_t trial_seed = base_seed + static_cast<std::uint64_t>(trial) * 0x9E3779B97F4A7C15ULL;
        for (int burst = 1; burst <= burst_groups; ++burst) {
            for (const auto& scenario : scenarios) {
                run_trial(trial_seed + static_cast<std::uint64_t>(burst), burst, caps, scenario);
            }
        }
    }

    return 0;
}
