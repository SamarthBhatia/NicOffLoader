#ifndef NICLOADOFF_POLICY_HOOK_HH
#define NICLOADOFF_POLICY_HOOK_HH

#include "nicloadoff/policy_state.hh"

#include <vector>

namespace nicloadoff::policy {

enum class DirectiveType {
    kNoOp,
    kReorderWaitingQueue
};

struct PolicyDecision {
    DirectiveType type{DirectiveType::kNoOp};
    std::vector<TaskId> preferred_waiting_order;
};

class PolicyHook {
  public:
    virtual ~PolicyHook() = default;
    [[nodiscard]] virtual PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) = 0;
};

} // namespace nicloadoff::policy

#endif // NICLOADOFF_POLICY_HOOK_HH
