#include "nicloadoff/workload.hh"

#include <queue>
#include <unordered_set>

namespace nicloadoff {

namespace {

[[nodiscard]] ResourceId map_resource(ResourceClass resource, const ProfileResourceIds& ids) {
    switch (resource) {
    case ResourceClass::kHostCpu:
        return ids.host_cpu;
    case ResourceClass::kHostDram:
        return ids.host_dram;
    case ResourceClass::kHostLink:
        return ids.host_link;
    case ResourceClass::kNicCpu:
        return ids.nic_cpu;
    case ResourceClass::kNicDram:
        return ids.nic_dram;
    case ResourceClass::kNicLink:
        return ids.nic_link;
    default:
        break;
    }
    throw WorkloadSpecError("unknown resource class");
}

[[nodiscard]] TaskStage make_stage_from_spec(const StageSpec& spec, const ProfileResourceIds& ids) {
    TaskStage stage{};
    stage.service_time = spec.deterministic_service_time.value_or(0.0);
    stage.service_profile = spec.service_profile;

    for (const auto& demand : spec.demands) {
        ResourceId id = map_resource(demand.resource, ids);
        stage.requirements.push_back(TaskRequirement{.resource_id = id, .units = demand.units});
    }
    return stage;
}

void validate_stage(const StageSpec& spec) {
    if (!spec.service_profile && !spec.deterministic_service_time) {
        throw WorkloadSpecError("stage spec must supply deterministic service time or service profile");
    }
    if (spec.deterministic_service_time && *spec.deterministic_service_time < 0.0) {
        throw WorkloadSpecError("stage deterministic service time cannot be negative");
    }
    for (const auto& demand : spec.demands) {
        if (demand.units < 0.0) {
            throw WorkloadSpecError("resource demand units cannot be negative");
        }
    }
}

} // namespace

Task make_task_from_spec(const TaskSpec& spec, const ProfileResourceIds& ids) {
    if (spec.stages.empty()) {
        throw WorkloadSpecError("task spec must contain at least one stage");
    }
    if (spec.arrival_time < 0.0) {
        throw WorkloadSpecError("task arrival time cannot be negative");
    }
    Task task{};
    task.id = spec.id;
    task.arrival_time = spec.arrival_time;
    task.stages.reserve(spec.stages.size());
    for (const auto& stage_spec : spec.stages) {
        validate_stage(stage_spec);
        task.stages.push_back(make_stage_from_spec(stage_spec, ids));
    }
    return task;
}

std::vector<Task> make_tasks_from_spec(const WorkloadSpec& spec, const ProfileResourceIds& ids) {
    std::vector<Task> tasks;
    tasks.reserve(spec.tasks.size());
    for (const auto& task_spec : spec.tasks) {
        tasks.push_back(make_task_from_spec(task_spec, ids));
    }
    return tasks;
}

void validate_task_dag_spec(const TaskDAGSpec& spec) {
    if (spec.arrival_time < 0.0) {
        throw WorkloadSpecError("task arrival time cannot be negative");
    }
    if (spec.nodes.empty()) {
        throw WorkloadSpecError("task DAG must declare at least one node");
    }

    std::unordered_map<std::string, std::size_t> name_to_index;
    name_to_index.reserve(spec.nodes.size());

    for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
        const auto& node = spec.nodes[i];
        if (!name_to_index.emplace(node.name, i).second) {
            throw WorkloadSpecError("duplicate node name '" + node.name + "'");
        }
        validate_stage(node.stage);
    }

    for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
        const auto& node = spec.nodes[i];
        for (const auto& succ : node.successors) {
            if (name_to_index.count(succ) == 0) {
                throw WorkloadSpecError("node '" + node.name + "' references unknown successor '" + succ + "'");
            }
        }
    }

    if (spec.entry_points.empty()) {
        throw WorkloadSpecError("task DAG must declare at least one entry point");
    }

    std::vector<std::size_t> entry_indexes;
    entry_indexes.reserve(spec.entry_points.size());
    std::unordered_set<std::string> entry_names;

    for (const auto& entry : spec.entry_points) {
        if (!entry_names.insert(entry).second) {
            throw WorkloadSpecError("duplicate entry point '" + entry + "'");
        }
        auto it = name_to_index.find(entry);
        if (it == name_to_index.end()) {
            throw WorkloadSpecError("entry point '" + entry + "' not found in DAG nodes");
        }
        entry_indexes.push_back(it->second);
    }

    enum class VisitState { kUnvisited, kVisiting, kVisited };
    std::vector<VisitState> visit_state(spec.nodes.size(), VisitState::kUnvisited);
    std::vector<bool> reachable(spec.nodes.size(), false);

    auto dfs = [&](auto&& self, std::size_t index) -> void {
        visit_state[index] = VisitState::kVisiting;
        reachable[index] = true;
        const auto& node = spec.nodes[index];
        for (const auto& succ_name : node.successors) {
            std::size_t succ_index = name_to_index.at(succ_name);
            if (visit_state[succ_index] == VisitState::kVisiting) {
                throw WorkloadSpecError("cycle detected between '" + node.name + "' and '" + succ_name + "'");
            }
            if (visit_state[succ_index] == VisitState::kUnvisited) {
                self(self, succ_index);
            }
        }
        visit_state[index] = VisitState::kVisited;
    };

    for (std::size_t index : entry_indexes) {
        if (visit_state[index] == VisitState::kUnvisited) {
            dfs(dfs, index);
        }
    }

    for (std::size_t i = 0; i < reachable.size(); ++i) {
        if (!reachable[i]) {
            throw WorkloadSpecError("node '" + spec.nodes[i].name + "' is not reachable from any entry point");
        }
    }
}

TaskDag make_task_dag_from_spec(const TaskDAGSpec& spec, const ProfileResourceIds& ids) {
    validate_task_dag_spec(spec);

    TaskDag dag;
    dag.id = spec.id;
    dag.arrival_time = spec.arrival_time;
    dag.nodes.reserve(spec.nodes.size());
    dag.index_by_name.reserve(spec.nodes.size());

    for (const auto& node_spec : spec.nodes) {
        TaskDagNode node;
        node.name = node_spec.name;
        node.stage = make_stage_from_spec(node_spec.stage, ids);
        dag.index_by_name.emplace(node.name, dag.nodes.size());
        dag.nodes.push_back(std::move(node));
    }

    for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
        const auto& node_spec = spec.nodes[i];
        auto& node = dag.nodes[i];
        node.successors.reserve(node_spec.successors.size());
        for (const auto& succ_name : node_spec.successors) {
            node.successors.push_back(dag.index_by_name.at(succ_name));
        }
    }

    dag.entry_nodes.reserve(spec.entry_points.size());
    for (const auto& entry : spec.entry_points) {
        dag.entry_nodes.push_back(dag.index_by_name.at(entry));
    }

    return dag;
}

std::vector<std::size_t> topological_order(const TaskDag& dag) {
    std::vector<std::size_t> order;
    order.reserve(dag.nodes.size());
    std::vector<std::size_t> indegree(dag.nodes.size(), 0);

    for (const auto& node : dag.nodes) {
        for (std::size_t succ : node.successors) {
            if (succ >= indegree.size()) {
                throw WorkloadSpecError("task DAG successor index out of range");
            }
            ++indegree[succ];
        }
    }

    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    while (!ready.empty()) {
        std::size_t index = ready.front();
        ready.pop();
        order.push_back(index);
        for (std::size_t succ : dag.nodes[index].successors) {
            auto& degree = indegree[succ];
            if (degree == 0) {
                continue;
            }
            --degree;
            if (degree == 0) {
                ready.push(succ);
            }
        }
    }

    if (order.size() != dag.nodes.size()) {
        throw WorkloadSpecError("task DAG contains a cycle; unable to compute topological order");
    }

    return order;
}

} // namespace nicloadoff
