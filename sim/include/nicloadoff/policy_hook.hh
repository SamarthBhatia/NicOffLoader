#ifndef NICLOADOFF_POLICY_HOOK_HH
#define NICLOADOFF_POLICY_HOOK_HH

#include "nicloadoff/policy_state.hh"

#include <optional>
#include <vector>

namespace nicloadoff::policy {

struct AdmissionControlDirective {
    bool enabled{false};
    std::size_t max_active_tasks{0};
};

struct PolicyDecision {
    std::optional<std::vector<TaskId>> waiting_order;
    std::optional<AdmissionControlDirective> admission;
};

class PolicyHook {
  public:
    virtual ~PolicyHook() = default;
    [[nodiscard]] virtual PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) = 0;
};

} // namespace nicloadoff::policy

#endif // NICLOADOFF_POLICY_HOOK_HH
