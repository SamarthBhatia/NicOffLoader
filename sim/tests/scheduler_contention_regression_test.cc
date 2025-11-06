#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/workload.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <optional>
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
    profile.host_cpu.cores_total = 2;
    profile.host_dram.capacity_gb = 16;
    profile.host_nic_link.max_inflight_bytes = 1'000'000;
    profile.nic_cpu.cores_total = 2;
    profile.nic_dram.capacity_gb = 8;
    profile.nic_network_link.max_inflight_bytes = 1'000'000;
    return profile;
}

nicloadoff::StageSpec make_stage(
    double service_time,
    std::initializer_list<std::pair<nicloadoff::ResourceClass, double>> demands) {
    nicloadoff::StageSpec stage{};
    stage.deterministic_service_time = service_time;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

struct ExhaustionRecord {
    bool host_full{false};
    bool nic_full{false};
    bool applied_reorder{false};
    bool applied_admission{false};
    std::size_t waiting_depth{0};
    std::size_t active_task_count{0};
    std::optional<std::size_t> admission_limit;
    std::vector<nicloadoff::TaskId> waiting_order;
};

class RecordingThrottlePolicy : public nicloadoff::policy::PolicyHook {
  public:
    [[nodiscard]] nicloadoff::policy::PolicyDecision evaluate(
        const nicloadoff::PolicyStateSnapshot& snapshot) override {
        ExhaustionRecord record{};
        record.waiting_depth = snapshot.waiting_task_order.size();
        record.active_task_count = snapshot.active_task_count;
        record.admission_limit = snapshot.admission_limit;
        record.waiting_order = snapshot.waiting_task_order;

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

        nicloadoff::policy::PolicyDecision decision{};
        if (record.host_full && record.nic_full) {
            nicloadoff::policy::AdmissionControlDirective directive{};
            directive.enabled = true;
            directive.max_active_tasks = 2;
            decision.admission = directive;

            if (!snapshot.waiting_task_order.empty()) {
                std::vector<nicloadoff::TaskId> reordered = snapshot.waiting_task_order;
                std::sort(reordered.begin(), reordered.end(), std::greater<nicloadoff::TaskId>());
                decision.waiting_order = std::move(reordered);
            }
        }

        record.applied_admission = decision.admission.has_value();
        record.applied_reorder = decision.waiting_order.has_value();
        records.push_back(std::move(record));
        return decision;
    }

    std::vector<ExhaustionRecord> records;
};

nicloadoff::TaskTiming find_metric(const nicloadoff::RunMetrics& metrics, nicloadoff::TaskId id) {
    for (const auto& timing : metrics.tasks) {
        if (timing.id == id) {
            return timing;
        }
    }
    std::fprintf(stderr, "metric for task %llu not found\n", static_cast<unsigned long long>(id));
    std::abort();
}

} // namespace

int main() {
    using namespace nicloadoff;

    auto profile = make_profile();
    auto inventory = make_resource_inventory_from_profile(profile);

    StageSpec long_stage =
        make_stage(8.0, {{ResourceClass::kHostCpu, 1.0}, {ResourceClass::kNicCpu, 1.0}});
    StageSpec medium_stage =
        make_stage(4.0, {{ResourceClass::kHostCpu, 1.0}, {ResourceClass::kNicCpu, 1.0}});
    StageSpec short_stage =
        make_stage(1.0, {{ResourceClass::kHostCpu, 1.0}, {ResourceClass::kNicCpu, 1.0}});

    WorkloadSpec workload{};

    workload.tasks.push_back(TaskSpec{.id = 10, .arrival_time = 0.0, .stages = {long_stage}});
    workload.tasks.push_back(TaskSpec{.id = 20, .arrival_time = 0.01, .stages = {long_stage}});
    workload.tasks.push_back(TaskSpec{.id = 60, .arrival_time = 0.02, .stages = {long_stage}});
    workload.tasks.push_back(TaskSpec{.id = 30, .arrival_time = 0.03, .stages = {medium_stage}});
    workload.tasks.push_back(TaskSpec{.id = 90, .arrival_time = 0.04, .stages = {short_stage}});

    std::vector<Task> tasks = make_tasks_from_spec(workload, inventory.ids);

    RecordingThrottlePolicy policy;
    BasicScheduler scheduler(std::move(inventory.pool));
    scheduler.set_policy_hook(&policy);

    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }

    scheduler.run_until_empty();

    const auto& completed = scheduler.completed_tasks();
    check(completed.size() == 5, "expected five completed tasks");
    check(completed[0] == 10, "expected first long task to complete first");
    check(completed[1] == 20, "expected second long task to complete second");
    check(completed[2] == 90, "expected reordered short task to complete third");
    check(completed[3] == 30, "expected medium task to complete fourth");
    check(completed[4] == 60, "expected deferred long task to complete last");

    bool saw_simultaneous_exhaustion = false;
    bool saw_admission_application = false;
    bool saw_reorder_application = false;
    bool observed_limit = false;

    for (const auto& record : policy.records) {
        if (record.admission_limit.has_value()) {
            check(record.active_task_count <= *record.admission_limit,
                  "observed active task count exceeding admission limit");
            if (*record.admission_limit == 2) {
                observed_limit = true;
            }
        }
        if (record.host_full && record.nic_full) {
            saw_simultaneous_exhaustion = true;
            if (record.applied_admission) {
                saw_admission_application = true;
            }
            if (record.applied_reorder) {
                saw_reorder_application = true;
            }
        } else {
            if (record.applied_admission) {
                saw_admission_application = true;
            }
            if (record.applied_reorder) {
                saw_reorder_application = true;
            }
        }
    }

    check(saw_simultaneous_exhaustion, "expected simultaneous host and NIC exhaustion");
    check(observed_limit, "expected admission control limit to be observed in snapshots");
    check(saw_admission_application, "expected policy to apply admission throttling");
    check(saw_reorder_application, "expected policy to reorder waiting queue");

    const RunMetrics metrics = scheduler.aggregated_metrics();
    check(metrics.tasks.size() == 5, "expected five per-task metrics");

    const auto short_metrics = find_metric(metrics, 90);
    const auto medium_metrics = find_metric(metrics, 30);
    const auto deferred_long_metrics = find_metric(metrics, 60);
    const auto first_long_metrics = find_metric(metrics, 10);
    const auto second_long_metrics = find_metric(metrics, 20);

    check(short_metrics.queue_time < medium_metrics.queue_time,
          "reordered short task should experience less queue time than medium task");
    check(medium_metrics.queue_time < deferred_long_metrics.queue_time,
          "medium task should queue less than deferred long task");

    assert_near(metrics.aggregate.total_service_time,
                short_metrics.service_time + medium_metrics.service_time + deferred_long_metrics.service_time +
                    first_long_metrics.service_time + second_long_metrics.service_time);

    return 0;
}
