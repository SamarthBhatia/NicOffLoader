#ifndef NICLOADOFF_DAG_RUNTIME_HH
#define NICLOADOFF_DAG_RUNTIME_HH

#include "nicloadoff/sim_types.hh"
#include "nicloadoff/task.hh"
#include "nicloadoff/workload.hh"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace nicloadoff {

class DagRuntimeError : public std::runtime_error {
  public:
    explicit DagRuntimeError(const std::string& message) : std::runtime_error(message) {}
};

struct PlannedDagNode {
    Task task;
    std::vector<TaskId> successors;
    std::size_t remaining_dependencies{0};
    bool released{false};
    bool completed{false};
};

class TaskDagRuntime {
  public:
    TaskDagRuntime(TaskDag dag, TaskId base_task_id = 0);

    [[nodiscard]] TaskId base_task_id() const noexcept { return base_task_id_; }
    [[nodiscard]] const TaskDag& task_dag() const noexcept { return dag_; }

    std::vector<Task> initial_tasks();
    std::vector<Task> release_successors(TaskId completed_task_id, SimTime current_time);

    [[nodiscard]] bool contains(TaskId task_id) const noexcept;
    [[nodiscard]] const PlannedDagNode& node(TaskId task_id) const;
    [[nodiscard]] PlannedDagNode& node(TaskId task_id);
    [[nodiscard]] bool all_completed() const noexcept;

  private:
    TaskDag dag_;
    TaskId base_task_id_{0};
    std::vector<PlannedDagNode> nodes_;
    std::unordered_map<TaskId, std::size_t> index_by_task_id_;

    static TaskId make_task_id(TaskId base, std::size_t offset);
    std::vector<std::size_t> compute_indegrees() const;
};

TaskDagRuntime make_task_dag_runtime_from_spec(const TaskDAGSpec& spec,
                                               const ProfileResourceIds& ids,
                                               TaskId base_task_id = 0);

} // namespace nicloadoff

#endif // NICLOADOFF_DAG_RUNTIME_HH
