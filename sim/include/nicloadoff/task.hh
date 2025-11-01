#ifndef NICLOADOFF_TASK_HH
#define NICLOADOFF_TASK_HH

#include "nicloadoff/resource.hh"
#include "nicloadoff/sim_types.hh"

#include <vector>

namespace nicloadoff {

struct TaskRequirement {
    ResourceId resource_id{};
    double units{0.0};
};

struct TaskStage {
    Duration service_time{0.0};
    std::vector<TaskRequirement> requirements;
};

struct Task {
    TaskId id{};
    SimTime arrival_time{0.0};
    std::vector<TaskStage> stages;
};

} // namespace nicloadoff

#endif // NICLOADOFF_TASK_HH
