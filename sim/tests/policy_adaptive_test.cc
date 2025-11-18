#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/policy_state.hh"

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

nicloadoff::PolicyStateSnapshot make_snapshot(double queue_avg,
                                              double host_util_avg,
                                              double nic_util_avg,
                                              double host_pending,
                                              double nic_pending) {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot{};
    snapshot.waiting_task_order = {101, 102};

    PolicyTaskState host_heavy{};
    host_heavy.id = 101;
    host_heavy.pending_host_demand = host_pending;
    host_heavy.pending_nic_demand = 1.0;

    PolicyTaskState nic_heavy{};
    nic_heavy.id = 102;
    nic_heavy.pending_host_demand = 1.0;
    nic_heavy.pending_nic_demand = nic_pending;

    snapshot.tasks = {host_heavy, nic_heavy};

    snapshot.rolling_metrics.waiting_queue_depth.samples = 4;
    snapshot.rolling_metrics.waiting_queue_depth.average = queue_avg;
    snapshot.rolling_metrics.host_utilization.samples = 4;
    snapshot.rolling_metrics.host_utilization.average = host_util_avg;
    snapshot.rolling_metrics.nic_utilization.samples = 4;
    snapshot.rolling_metrics.nic_utilization.average = nic_util_avg;
    return snapshot;
}

std::vector<nicloadoff::TaskId> evaluate_order(nicloadoff::policy::PolicyHook& policy,
                                               const nicloadoff::PolicyStateSnapshot& snapshot) {
    nicloadoff::policy::PolicyDecision decision = policy.evaluate(snapshot);
    if (!decision.waiting_order) {
        return {};
    }
    return *decision.waiting_order;
}

} // namespace

int main() {
    using namespace nicloadoff;
    nicloadoff::policy::detail::RollingAdaptivePolicy policy(/*queue_threshold=*/0.5, /*util_margin=*/0.15);

    {
        // Host saturation (high queue/host util) -> prioritize NIC-heavy tasks.
        PolicyStateSnapshot snapshot = make_snapshot(/*queue_avg=*/0.8,
                                                     /*host_util_avg=*/0.85,
                                                     /*nic_util_avg=*/0.10,
                                                     /*host_pending=*/6.0,
                                                     /*nic_pending=*/6.0);
        const auto order = evaluate_order(policy, snapshot);
        check(order.size() == 2, "expected reorder under host saturation");
        check(order[0] == 102, "NIC-heavy task should be preferred when host is saturated");
    }

    {
        // NIC saturation (low queue, high NIC util) -> prioritize host-heavy tasks.
        PolicyStateSnapshot snapshot = make_snapshot(/*queue_avg=*/0.1,
                                                     /*host_util_avg=*/0.10,
                                                     /*nic_util_avg=*/0.92,
                                                     /*host_pending=*/6.0,
                                                     /*nic_pending=*/6.0);
        const auto order = evaluate_order(policy, snapshot);
        check(order.size() == 2, "expected reorder under NIC saturation");
        check(order[0] == 101, "Host-heavy task should be preferred when NIC is saturated");
    }

    {
        // Balanced utilization/queue should yield no directive.
        PolicyStateSnapshot snapshot = make_snapshot(/*queue_avg=*/0.2,
                                                     /*host_util_avg=*/0.30,
                                                     /*nic_util_avg=*/0.32,
                                                     /*host_pending=*/4.0,
                                                     /*nic_pending=*/5.0);
        const auto order = evaluate_order(policy, snapshot);
        check(order.empty(), "balanced scenario should not reorder tasks");
    }

    return 0;
}
