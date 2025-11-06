#ifndef NICLOADOFF_DAG_SUBMISSION_CONTROLLER_HH
#define NICLOADOFF_DAG_SUBMISSION_CONTROLLER_HH

#include "nicloadoff/dag_runtime.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/workload.hh"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace nicloadoff {

class DagSubmissionController {
  public:
    DagSubmissionController() = default;

    explicit DagSubmissionController(std::vector<TaskDagRuntime> runtimes);

    static DagSubmissionController from_spec(const WorkloadSpec& spec, const ProfileResourceIds& ids);

    void submit_initial(BasicScheduler& scheduler);
    void handle_task_completion(TaskId task_id, SimTime current_time, BasicScheduler& scheduler);

    [[nodiscard]] bool manages(TaskId task_id) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return runtimes_.empty(); }
    [[nodiscard]] SimTime earliest_arrival_time() const noexcept;

  private:
    struct DagContext {
        TaskDagRuntime runtime;
    };

    std::vector<DagContext> runtimes_;
    std::unordered_map<TaskId, std::size_t> task_to_runtime_;

    void register_tasks(std::size_t runtime_index, const std::vector<Task>& tasks, BasicScheduler& scheduler);
};

} // namespace nicloadoff

#endif // NICLOADOFF_DAG_SUBMISSION_CONTROLLER_HH
