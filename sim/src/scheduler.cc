#include "nicloadoff/scheduler.hh"

#include "nicloadoff/service_time_model.hh"

#include <sstream>
#include <utility>

namespace nicloadoff {

namespace {

[[nodiscard]] std::string make_error(const std::string& message, TaskId id) {
    std::ostringstream oss;
    oss << message << " (task id=" << id << ")";
    return oss.str();
}

} // namespace

BasicScheduler::BasicScheduler(ResourcePool resources, ServiceTimeModel* service_model)
    : resources_(std::move(resources)), service_model_(service_model) {}

void BasicScheduler::submit_task(const Task& task) {
    if (tasks_.count(task.id) != 0) {
        throw SchedulerError(make_error("task already submitted", task.id));
    }
    validate_task(task);
    TaskContext context;
    context.task = task;
    tasks_.emplace(task.id, std::move(context));

    queue_.push(ScheduledEvent{.timestamp = task.arrival_time,
                               .metadata = {.type = EventType::kTaskArrival, .id = task.id}});
}

void BasicScheduler::run_until_empty() {
    while (step_once()) {
    }
}

bool BasicScheduler::step_once() {
    auto event = queue_.pop();
    if (!event) {
        return false;
    }
    current_time_ = event->timestamp;
    handle_event(*event);
    return true;
}

void BasicScheduler::handle_event(const ScheduledEvent& event) {
    switch (event.metadata.type) {
    case EventType::kTaskArrival:
        handle_task_arrival(event.metadata.id, event.timestamp);
        break;
    case EventType::kTaskReady:
        handle_task_ready(event.metadata.id, event.timestamp);
        break;
    case EventType::kTaskStart:
        handle_task_start(event.metadata.id, event.timestamp);
        break;
    case EventType::kTaskComplete:
        handle_task_complete(event.metadata.id, event.timestamp);
        break;
    default:
        break;
    }
}

void BasicScheduler::handle_task_arrival(TaskId id, SimTime timestamp) {
    auto& ctx = get_task(id);
    if (ctx.stage_index >= ctx.task.stages.size()) {
        completed_tasks_.push_back(id);
        return;
    }
    queue_.push(ScheduledEvent{.timestamp = timestamp,
                               .metadata = {.type = EventType::kTaskReady, .id = id}});
}

void BasicScheduler::handle_task_ready(TaskId id, SimTime timestamp) {
    auto& ctx = get_task(id);
    bool started = try_start_task(ctx, timestamp);
    if (!started && waiting_set_.count(id) == 0) {
        waiting_set_.insert(id);
        waiting_queue_.push(id);
    }
}

void BasicScheduler::handle_task_start(TaskId id, SimTime timestamp) {
    auto& ctx = get_task(id);
    if (!ctx.active || ctx.stage_index >= ctx.task.stages.size()) {
        return;
    }
    queue_.push(ScheduledEvent{.timestamp = timestamp + ctx.active_service_time,
                               .metadata = {.type = EventType::kTaskComplete, .id = id}});
}

void BasicScheduler::handle_task_complete(TaskId id, SimTime timestamp) {
    auto& ctx = get_task(id);
    if (ctx.stage_index >= ctx.task.stages.size()) {
        return;
    }

    const TaskStage& stage = ctx.task.stages[ctx.stage_index];
    for (const auto& requirement : stage.requirements) {
        resources_.release(requirement.resource_id, requirement.units);
    }

    ctx.active = false;
    ctx.active_service_time = 0.0;
    ++ctx.stage_index;

    if (ctx.stage_index < ctx.task.stages.size()) {
        queue_.push(ScheduledEvent{.timestamp = timestamp,
                                   .metadata = {.type = EventType::kTaskReady, .id = id}});
    } else {
        completed_tasks_.push_back(id);
    }

    drain_waiting(timestamp);
}

bool BasicScheduler::try_start_task(TaskContext& ctx, SimTime timestamp) {
    if (ctx.stage_index >= ctx.task.stages.size()) {
        return false;
    }
    if (ctx.active) {
        return true;
    }

    const TaskStage& stage = ctx.task.stages[ctx.stage_index];

    for (const auto& requirement : stage.requirements) {
        if (!resources_.can_allocate(requirement.resource_id, requirement.units)) {
            return false;
        }
    }

    for (const auto& requirement : stage.requirements) {
        resources_.allocate(requirement.resource_id, requirement.units);
    }

    Duration duration = resolve_service_time(stage, ctx.task.id);

    ctx.active = true;
    ctx.active_service_time = duration;
    waiting_set_.erase(ctx.task.id);
    queue_.push(ScheduledEvent{.timestamp = timestamp,
                               .metadata = {.type = EventType::kTaskStart, .id = ctx.task.id}});
    return true;
}

void BasicScheduler::validate_task(const Task& task) const {
    if (task.stages.empty()) {
        throw SchedulerError(make_error("task has no stages", task.id));
    }

    for (const auto& stage : task.stages) {
        if (stage.service_time < 0.0) {
            throw SchedulerError(make_error("task stage has negative service time", task.id));
        }
        if (stage.service_profile) {
            if (service_model_ == nullptr) {
                throw SchedulerError(make_error("task stage references service profile without model", task.id));
            }
            try {
                [[maybe_unused]] const Duration mean = service_model_->mean(*stage.service_profile);
            } catch (const ServiceTimeModelError& err) {
                throw SchedulerError(make_error(err.what(), task.id));
            }
        }
        for (const auto& requirement : stage.requirements) {
            if (requirement.units < 0.0) {
                throw SchedulerError(make_error("task requirement cannot be negative", task.id));
            }
            const Resource* resource = resources_.find(requirement.resource_id);
            if (resource == nullptr) {
                throw SchedulerError(make_error("task references unknown resource", task.id));
            }
            if (requirement.units > resource->capacity() + 1e-12) {
                throw SchedulerError(make_error("task requirement exceeds resource capacity", task.id));
            }
        }
    }
}

void BasicScheduler::drain_waiting(SimTime timestamp) {
    std::size_t pending = waiting_queue_.size();
    for (std::size_t i = 0; i < pending; ++i) {
        TaskId id = waiting_queue_.front();
        waiting_queue_.pop();
        auto& ctx = get_task(id);
        if (try_start_task(ctx, timestamp)) {
            continue;
        }
        waiting_queue_.push(id);
    }
}

BasicScheduler::TaskContext& BasicScheduler::get_task(TaskId id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        throw SchedulerError(make_error("task not found", id));
    }
    return it->second;
}

Duration BasicScheduler::resolve_service_time(const TaskStage& stage, TaskId id) {
    Duration duration = stage.service_time;
    if (stage.service_profile) {
        if (service_model_ == nullptr) {
            throw SchedulerError(make_error("service profile requires configured model", id));
        }
        try {
            duration = service_model_->sample(*stage.service_profile);
        } catch (const ServiceTimeModelError& err) {
            throw SchedulerError(make_error(err.what(), id));
        }
    }
    if (duration < 0.0) {
        throw SchedulerError(make_error("stage service time cannot be negative", id));
    }
    return duration;
}

} // namespace nicloadoff
