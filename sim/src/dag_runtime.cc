#include "nicloadoff/dag_runtime.hh"

#include <limits>
#include <utility>

namespace nicloadoff {

namespace {

[[nodiscard]] bool will_overflow(TaskId base, std::size_t offset) {
    constexpr TaskId kMax = std::numeric_limits<TaskId>::max();
    if (offset > static_cast<std::size_t>(kMax)) {
        return true;
    }
    return base > kMax - static_cast<TaskId>(offset);
}

} // namespace

TaskDagRuntime::TaskDagRuntime(TaskDag dag, TaskId base_task_id)
    : dag_(std::move(dag)), base_task_id_(base_task_id) {
    const std::size_t node_count = dag_.nodes.size();
    nodes_.reserve(node_count);
    index_by_task_id_.reserve(node_count);

    const std::vector<std::size_t> indegrees = compute_indegrees();

    for (std::size_t i = 0; i < node_count; ++i) {
        if (will_overflow(base_task_id_, i)) {
            throw DagRuntimeError("task id allocation overflow");
        }
        Task task;
        task.id = make_task_id(base_task_id_, i);
        task.arrival_time = dag_.arrival_time;
        task.stages.push_back(dag_.nodes[i].stage);

        PlannedDagNode node;
        node.task = task;
        node.remaining_dependencies = indegrees[i];
        node.successors.reserve(dag_.nodes[i].successors.size());
        for (std::size_t succ_index : dag_.nodes[i].successors) {
            if (will_overflow(base_task_id_, succ_index)) {
                throw DagRuntimeError("task id allocation overflow");
            }
            node.successors.push_back(make_task_id(base_task_id_, succ_index));
        }

        index_by_task_id_.emplace(task.id, nodes_.size());
        nodes_.push_back(std::move(node));
    }
}

std::vector<Task> TaskDagRuntime::initial_tasks() {
    std::vector<Task> ready;
    ready.reserve(dag_.entry_nodes.size());
    for (std::size_t index : dag_.entry_nodes) {
        auto& node = nodes_.at(index);
        if (node.remaining_dependencies != 0) {
            continue;
        }
        if (node.released) {
            continue;
        }
        node.released = true;
        node.task.arrival_time = dag_.arrival_time;
        ready.push_back(node.task);
    }
    return ready;
}

std::vector<Task> TaskDagRuntime::release_successors(TaskId completed_task_id, SimTime current_time) {
    auto it = index_by_task_id_.find(completed_task_id);
    if (it == index_by_task_id_.end()) {
        throw DagRuntimeError("unknown task id in completion");
    }
    auto& node = nodes_.at(it->second);
    if (!node.released) {
        throw DagRuntimeError("task completion received before release");
    }
    if (node.completed) {
        throw DagRuntimeError("task already marked complete");
    }
    node.completed = true;

    std::vector<Task> ready;
    ready.reserve(node.successors.size());

    for (TaskId succ_id : node.successors) {
        auto succ_it = index_by_task_id_.find(succ_id);
        if (succ_it == index_by_task_id_.end()) {
            throw DagRuntimeError("successor task id not found");
        }
        auto& successor = nodes_.at(succ_it->second);
        if (successor.completed) {
            continue;
        }
        if (successor.remaining_dependencies == 0) {
            continue;
        }
        --successor.remaining_dependencies;
        if (successor.remaining_dependencies == 0 && !successor.released) {
            successor.released = true;
            successor.task.arrival_time = current_time;
            ready.push_back(successor.task);
        }
    }

    return ready;
}

bool TaskDagRuntime::contains(TaskId task_id) const noexcept {
    return index_by_task_id_.count(task_id) != 0;
}

const PlannedDagNode& TaskDagRuntime::node(TaskId task_id) const {
    auto it = index_by_task_id_.find(task_id);
    if (it == index_by_task_id_.end()) {
        throw DagRuntimeError("task id not found");
    }
    return nodes_.at(it->second);
}

PlannedDagNode& TaskDagRuntime::node(TaskId task_id) {
    auto it = index_by_task_id_.find(task_id);
    if (it == index_by_task_id_.end()) {
        throw DagRuntimeError("task id not found");
    }
    return nodes_.at(it->second);
}

bool TaskDagRuntime::all_completed() const noexcept {
    for (const auto& node : nodes_) {
        if (!node.completed) {
            return false;
        }
    }
    return true;
}

TaskId TaskDagRuntime::make_task_id(TaskId base, std::size_t offset) {
    return base + static_cast<TaskId>(offset);
}

std::vector<std::size_t> TaskDagRuntime::compute_indegrees() const {
    std::vector<std::size_t> indegrees(dag_.nodes.size(), 0);
    for (const auto& node : dag_.nodes) {
        for (std::size_t succ : node.successors) {
            if (succ >= indegrees.size()) {
                throw DagRuntimeError("successor index out of range during indegree computation");
            }
            ++indegrees[succ];
        }
    }
    return indegrees;
}

TaskDagRuntime make_task_dag_runtime_from_spec(const TaskDAGSpec& spec,
                                               const ProfileResourceIds& ids,
                                               TaskId base_task_id) {
    TaskDag dag = make_task_dag_from_spec(spec, ids);
    return TaskDagRuntime(std::move(dag), base_task_id);
}

} // namespace nicloadoff
