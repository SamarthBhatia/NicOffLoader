#ifndef NICLOADOFF_TASK_HH
#define NICLOADOFF_TASK_HH

#include "nicloadoff/resource.hh"
#include "nicloadoff/sim_types.hh"

#include <optional>
#include <string>
#include <vector>

namespace nicloadoff {

enum class ServiceTimeDomain { kHost, kNic };

enum class ServiceTimeMode { kDeterministic, kStochastic };

struct ServiceTimeProfileRef {
    std::string key;
    ServiceTimeDomain domain{ServiceTimeDomain::kHost};
    ServiceTimeMode mode{ServiceTimeMode::kDeterministic};
};

struct TaskRequirement {
    ResourceId resource_id{};
    double units{0.0};
};

struct TaskStage {
    std::string label;
    Duration service_time{0.0};
    std::optional<ServiceTimeProfileRef> service_profile;
    std::vector<TaskRequirement> requirements;
};

struct Task {
    TaskId id{};
    SimTime arrival_time{0.0};
    std::vector<TaskStage> stages;
};

} // namespace nicloadoff

#endif // NICLOADOFF_TASK_HH
