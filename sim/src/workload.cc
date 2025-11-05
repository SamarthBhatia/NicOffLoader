#include "nicloadoff/workload.hh"

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
    if (!spec.dag_tasks.empty()) {
        throw WorkloadSpecError("graph-based tasks are not yet supported for make_tasks_from_spec");
    }
    std::vector<Task> tasks;
    tasks.reserve(spec.tasks.size());
    for (const auto& task_spec : spec.tasks) {
        tasks.push_back(make_task_from_spec(task_spec, ids));
    }
    return tasks;
}

} // namespace nicloadoff
