#include "nicloadoff/policy_dsl.hh"
#include "nicloadoff/policy_state.hh"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

std::filesystem::path write_config() {
    const char* yaml = R"(rules:
  - when:
      metric: queue_peak
      op: ">="
      value: 5.0
    action:
      reorder: prefer-nic
  - when:
      metric: host_util_peak
      op: ">="
      value: 0.8
    action:
      reorder: prefer-nic
  - when:
      metric: nic_util_peak
      op: ">="
      value: 0.8
    action:
      reorder: prefer-host
  - when:
      metric: sojourn_p99_us
      op: ">="
      value: 4.0
    action:
      admission:
        max_active: 1
)";
    auto path = std::filesystem::temp_directory_path() / "policy_dsl_rolling.yaml";
    std::ofstream out(path);
    out << yaml;
    return path;
}

nicloadoff::PolicyStateSnapshot make_snapshot() {
    using namespace nicloadoff;
    PolicyStateSnapshot snapshot{};
    snapshot.waiting_task_order = {1, 2, 3};

    PolicyTaskState host{};
    host.id = 1;
    host.pending_host_demand = 5.0;
    host.pending_nic_demand = 1.0;

    PolicyTaskState nic{};
    nic.id = 2;
    nic.pending_host_demand = 1.0;
    nic.pending_nic_demand = 5.0;

    PolicyTaskState balanced{};
    balanced.id = 3;
    balanced.pending_host_demand = 3.0;
    balanced.pending_nic_demand = 3.0;

    snapshot.tasks = {host, nic, balanced};
    return snapshot;
}

void expect_nic_order(const nicloadoff::policy::PolicyDecision& decision) {
    check(decision.waiting_order.has_value(), "expected reorder decision");
    const auto& order = *decision.waiting_order;
    check(order[0] == 2, "NIC-heavy task should rise when preferring NIC");
    check(order.back() == 1 || order.back() == 3, "host-heavy work should fall back when NIC preferred");
}

void expect_host_order(const nicloadoff::policy::PolicyDecision& decision) {
    check(decision.waiting_order.has_value(), "expected reorder decision");
    const auto& order = *decision.waiting_order;
    check(order[0] == 1, "host-heavy task should rise when preferring host");
    check(order.back() == 2 || order.back() == 3, "NIC-heavy work should fall later when host is saturated");
}

} // namespace

int main() {
    using namespace nicloadoff;
    auto hook = policy::dsl::load_program_from_file(write_config());

    {
        auto snapshot = make_snapshot();
        snapshot.rolling_metrics.waiting_queue_depth.peak = 5.5;
        auto decision = hook->evaluate(snapshot);
        expect_nic_order(decision);
    }

    {
        auto snapshot = make_snapshot();
        snapshot.rolling_metrics.host_utilization.peak = 0.85;
        auto decision = hook->evaluate(snapshot);
        expect_nic_order(decision);
    }

    {
        auto snapshot = make_snapshot();
        snapshot.rolling_metrics.nic_utilization.peak = 0.9;
        auto decision = hook->evaluate(snapshot);
        expect_host_order(decision);
    }

    {
        auto snapshot = make_snapshot();
        snapshot.rolling_metrics.sojourn.p99_latency = 5.0;
        auto decision = hook->evaluate(snapshot);
        check(decision.waiting_order == std::nullopt, "sojourn rule should only set admission");
        check(decision.admission.has_value(), "sojourn rule should enable admission");
        check(decision.admission->max_active_tasks == 1, "admission rule should cap active tasks");
    }

    return 0;
}
