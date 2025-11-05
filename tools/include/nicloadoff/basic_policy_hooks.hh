#ifndef NICLOADOFF_BASIC_POLICY_HOOKS_HH
#define NICLOADOFF_BASIC_POLICY_HOOKS_HH

#include "nicloadoff/policy_hook.hh"

#include <algorithm>
#include <memory>
#include <string>
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

inline const std::vector<BuiltinPolicyInfo>& policy_registry() {
    static const std::vector<BuiltinPolicyInfo> kPolicies = {
        {.id = "none", .display_name = "No policy", .description = "Do not register a policy hook."},
        {.id = "descending-id",
         .display_name = "Descending task ID",
         .description = "Reorders waiting tasks by descending task id."},
        {.id = "limit-active-1",
         .display_name = "Max one active task",
         .description = "Applies an admission limit allowing only one active task at a time."},
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
    return nullptr;
}

} // namespace nicloadoff::policy

#endif // NICLOADOFF_BASIC_POLICY_HOOKS_HH
