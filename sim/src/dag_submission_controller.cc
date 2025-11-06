#include "nicloadoff/dag_submission_controller.hh"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nicloadoff {

DagSubmissionController::DagSubmissionController(std::vector<TaskDagRuntime> runtimes) {
    runtimes_.reserve(runtimes.size());
    for (auto& runtime : runtimes) {
        runtimes_.push_back(DagContext{.runtime = std::move(runtime)});
    }
}

DagSubmissionController DagSubmissionController::from_spec(const WorkloadSpec& spec,
                                                           const ProfileResourceIds& ids) {
    std::vector<TaskDagRuntime> runtimes;
    runtimes.reserve(spec.dag_tasks.size());
    for (const auto& dag_spec : spec.dag_tasks) {
        runtimes.push_back(make_task_dag_runtime_from_spec(dag_spec, ids, dag_spec.id));
    }
    return DagSubmissionController(std::move(runtimes));
}

void DagSubmissionController::submit_initial(BasicScheduler& scheduler) {
    for (std::size_t index = 0; index < runtimes_.size(); ++index) {
        auto& context = runtimes_[index];
        std::vector<Task> ready = context.runtime.initial_tasks();
        register_tasks(index, ready, scheduler);
    }
}

void DagSubmissionController::handle_task_completion(TaskId task_id,
                                                     SimTime current_time,
                                                     BasicScheduler& scheduler) {
    auto it = task_to_runtime_.find(task_id);
    if (it == task_to_runtime_.end()) {
        return;
    }
    const std::size_t runtime_index = it->second;
    task_to_runtime_.erase(it);

    std::vector<Task> successors = runtimes_.at(runtime_index).runtime.release_successors(task_id, current_time);
    register_tasks(runtime_index, successors, scheduler);
}

bool DagSubmissionController::manages(TaskId task_id) const noexcept {
    return task_to_runtime_.count(task_id) != 0;
}

SimTime DagSubmissionController::earliest_arrival_time() const noexcept {
    if (runtimes_.empty()) {
        return std::numeric_limits<SimTime>::infinity();
    }
    SimTime min_time = std::numeric_limits<SimTime>::infinity();
    for (const auto& context : runtimes_) {
        min_time = std::min(min_time, context.runtime.task_dag().arrival_time);
    }
    return min_time;
}

void DagSubmissionController::register_tasks(std::size_t runtime_index,
                                             const std::vector<Task>& tasks,
                                             BasicScheduler& scheduler) {
    for (const Task& task : tasks) {
        auto [it, inserted] = task_to_runtime_.emplace(task.id, runtime_index);
        if (!inserted) {
            throw DagRuntimeError("duplicate task id encountered while registering DAG task");
        }
        scheduler.submit_task(task);
    }
}

} // namespace nicloadoff
