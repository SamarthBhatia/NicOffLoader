#ifndef NICLOADOFF_BASIC_POLICY_HOOKS_HH
#define NICLOADOFF_BASIC_POLICY_HOOKS_HH

#include "nicloadoff/policy_hook.hh"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nicloadoff::policy {

struct BuiltinPolicyInfo {
    std::string id;
    std::string display_name;
    std::string description;
};

namespace detail {

class DescendingIdPolicy : public PolicyHook {
  public:
    [[nodiscard]] PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) override {
        PolicyDecision decision{};
        std::vector<TaskId> order = snapshot.waiting_task_order;
        std::sort(order.begin(), order.end(), std::greater<TaskId>());
        decision.waiting_order = std::move(order);
        return decision;
    }
};

class MaxActivePolicy : public PolicyHook {
  public:
    explicit MaxActivePolicy(std::size_t limit) : limit_(limit) {}

    [[nodiscard]] PolicyDecision evaluate(const PolicyStateSnapshot&) override {
        PolicyDecision decision{};
        AdmissionControlDirective directive{};
        directive.enabled = true;
        directive.max_active_tasks = limit_;
        decision.admission = directive;
        return decision;
    }

  private:
    std::size_t limit_;
};

class DemandSkewPolicy : public PolicyHook {
  public:
    explicit DemandSkewPolicy(bool prefer_host) : prefer_host_(prefer_host) {}

    [[nodiscard]] PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) override {
        PolicyDecision decision{};
        if (snapshot.waiting_task_order.empty()) {
            return decision;
        }

        std::unordered_map<TaskId, const PolicyTaskState*> task_index;
        task_index.reserve(snapshot.tasks.size());
        for (const auto& task_state : snapshot.tasks) {
            task_index.emplace(task_state.id, &task_state);
        }

        std::vector<TaskId> order = snapshot.waiting_task_order;
        auto score = [&](TaskId id) -> double {
            auto it = task_index.find(id);
            if (it == task_index.end()) {
                return 0.0;
            }
            const auto& state = *it->second;
            const double host = state.pending_host_demand;
            const double nic = state.pending_nic_demand;
            return host - nic;
        };

        std::sort(order.begin(), order.end(), [&](TaskId lhs, TaskId rhs) {
            const double lhs_score = score(lhs);
            const double rhs_score = score(rhs);
            if (prefer_host_) {
                if (lhs_score != rhs_score) {
                    return lhs_score > rhs_score;
                }
            } else {
                if (lhs_score != rhs_score) {
                    return lhs_score < rhs_score;
                }
            }
            return lhs < rhs;
        });

        decision.waiting_order = std::move(order);
        return decision;
    }

  private:
    bool prefer_host_;
};

inline const std::vector<BuiltinPolicyInfo>& policy_registry() {
    static const std::vector<BuiltinPolicyInfo> kPolicies = {
        {.id = "none", .display_name = "No policy", .description = "Do not register a policy hook."},
        {.id = "descending-id",
         .display_name = "Descending task ID",
         .description = "Reorders waiting tasks by descending task id."},
        {.id = "limit-active-1",
         .display_name = "Max one active task",
         .description = "Applies an admission limit allowing only one active task at a time."},
        {.id = "prefer-host",
         .display_name = "Prefer host-heavy tasks",
         .description = "Skews waiting order toward stages with higher host demand."},
        {.id = "prefer-nic",
         .display_name = "Prefer NIC-heavy tasks",
         .description = "Skews waiting order toward stages with higher NIC demand."},
    };
    return kPolicies;
}

} // namespace detail

inline const std::vector<BuiltinPolicyInfo>& builtin_policies() { return detail::policy_registry(); }

inline bool is_policy_supported(const std::string& id) {
    const auto& policies = detail::policy_registry();
    return std::any_of(policies.begin(), policies.end(), [&](const BuiltinPolicyInfo& info) { return info.id == id; });
}

inline std::unique_ptr<PolicyHook> make_policy_hook(const std::string& id) {
    if (id == "none") {
        return nullptr;
    }
    if (id == "descending-id") {
        return std::make_unique<detail::DescendingIdPolicy>();
    }
    if (id == "limit-active-1") {
        return std::make_unique<detail::MaxActivePolicy>(1);
    }
    if (id == "prefer-host") {
        return std::make_unique<detail::DemandSkewPolicy>(true);
    }
    if (id == "prefer-nic") {
        return std::make_unique<detail::DemandSkewPolicy>(false);
    }
    return nullptr;
}

} // namespace nicloadoff::policy

#endif // NICLOADOFF_BASIC_POLICY_HOOKS_HH
