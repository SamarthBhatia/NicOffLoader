#ifndef NICLOADOFF_WORKLOAD_HH
#define NICLOADOFF_WORKLOAD_HH

#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/task.hh"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nicloadoff {

enum class ResourceClass {
    kHostCpu,
    kHostDram,
    kHostLink,
    kNicCpu,
    kNicDram,
    kNicLink
};

struct StageResourceDemand {
    ResourceClass resource;
    double units{0.0};
};

struct StageSpec {
    std::optional<Duration> deterministic_service_time;
    std::optional<ServiceTimeProfileRef> service_profile;
    std::vector<StageResourceDemand> demands;
};

struct TaskSpec {
    TaskId id{};
    SimTime arrival_time{0.0};
    std::vector<StageSpec> stages;
};

struct WorkloadSpec {
    std::vector<TaskSpec> tasks;
};

class WorkloadSpecError : public std::runtime_error {
  public:
    explicit WorkloadSpecError(const std::string& message) : std::runtime_error(message) {}
};

Task make_task_from_spec(const TaskSpec& spec, const ProfileResourceIds& ids);
std::vector<Task> make_tasks_from_spec(const WorkloadSpec& spec, const ProfileResourceIds& ids);

} // namespace nicloadoff

#endif // NICLOADOFF_WORKLOAD_HH
