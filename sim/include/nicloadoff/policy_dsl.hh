#ifndef NICLOADOFF_POLICY_DSL_HH
#define NICLOADOFF_POLICY_DSL_HH

#include "nicloadoff/policy_hook.hh"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace nicloadoff::policy::dsl {

enum class Metric {
    kQueueAverage,
    kHostUtilAverage,
    kNicUtilAverage,
    kSojournMean,
};

enum class CompareOp {
    kGreater,
    kGreaterEqual,
    kLess,
    kLessEqual,
};

struct Condition {
    Metric metric{Metric::kQueueAverage};
    CompareOp op{CompareOp::kGreater};
    double value{0.0};

    [[nodiscard]] bool evaluate(const PolicyStateSnapshot& snapshot) const;
};

enum class ReorderPreference {
    kPreferHost,
    kPreferNic,
};

struct Action {
    enum class Type {
        kNone,
        kReorder,
        kAdmission,
    } type{Type::kNone};
    std::optional<ReorderPreference> reorder;
    std::optional<std::size_t> max_active_tasks;
};

struct Rule {
    std::optional<Condition> condition;
    Action action;
};

class Program {
  public:
    explicit Program(std::vector<Rule> rules);
    [[nodiscard]] policy::PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) const;

  private:
    std::vector<Rule> rules_;
};

std::unique_ptr<policy::PolicyHook> load_program_from_file(const std::filesystem::path& path);

} // namespace nicloadoff::policy::dsl

#endif // NICLOADOFF_POLICY_DSL_HH

