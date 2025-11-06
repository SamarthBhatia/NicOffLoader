#ifndef NICLOADOFF_WORKLOAD_HH
#define NICLOADOFF_WORKLOAD_HH

#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/task.hh"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

struct StageNodeSpec {
    std::string name;
    StageSpec stage;
    std::vector<std::string> successors;
};

struct TaskDAGSpec {
    TaskId id{};
    SimTime arrival_time{0.0};
    std::vector<StageNodeSpec> nodes;
    std::vector<std::string> entry_points;
};

struct WorkloadSpec {
    std::vector<TaskSpec> tasks;
    std::vector<TaskDAGSpec> dag_tasks;
};

struct TaskDagNode {
    std::string name;
    TaskStage stage;
    std::vector<std::size_t> successors;
};

struct TaskDag {
    TaskId id{};
    SimTime arrival_time{0.0};
    std::vector<TaskDagNode> nodes;
    std::vector<std::size_t> entry_nodes;
    std::unordered_map<std::string, std::size_t> index_by_name;
};

class WorkloadSpecError : public std::runtime_error {
  public:
    explicit WorkloadSpecError(const std::string& message) : std::runtime_error(message) {}
};

Task make_task_from_spec(const TaskSpec& spec, const ProfileResourceIds& ids);
std::vector<Task> make_tasks_from_spec(const WorkloadSpec& spec, const ProfileResourceIds& ids);
void validate_task_dag_spec(const TaskDAGSpec& spec);
TaskDag make_task_dag_from_spec(const TaskDAGSpec& spec, const ProfileResourceIds& ids);
std::vector<std::size_t> topological_order(const TaskDag& dag);

} // namespace nicloadoff

#endif // NICLOADOFF_WORKLOAD_HH
