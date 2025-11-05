#include "nicloadoff/scheduler.hh"

#include "nicloadoff/policy_state.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/service_time_model.hh"

#include <algorithm>
#include <queue>
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
    context.stage_runtimes.resize(task.stages.size());
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
    ++events_processed_;
    return true;
}

void BasicScheduler::handle_event(const ScheduledEvent& event) {
    last_event_ = event;
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
    if (ctx.stage_index < ctx.stage_runtimes.size()) {
        auto& runtime = ctx.stage_runtimes[ctx.stage_index];
        if (!runtime.ready_recorded) {
            runtime.ready_recorded = true;
            runtime.ready_time = timestamp;
        }
    }
    bool started = try_start_task(ctx, timestamp);
    if (!started && waiting_set_.count(id) == 0) {
        waiting_set_.insert(id);
        waiting_queue_.push_back(id);
        evaluate_policy_hook();
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
    if (ctx.stage_index < ctx.stage_runtimes.size()) {
        auto& runtime = ctx.stage_runtimes[ctx.stage_index];
        runtime.completion_time = timestamp;
        if (!runtime.ready_recorded) {
            runtime.ready_recorded = true;
            runtime.ready_time = timestamp;
            runtime.queue_duration = 0.0;
        }
        if (!runtime.started) {
            runtime.start_time = timestamp;
            runtime.service_duration = 0.0;
        }
    }
    ++ctx.stage_index;

    if (ctx.stage_index < ctx.task.stages.size()) {
        queue_.push(ScheduledEvent{.timestamp = timestamp,
                                   .metadata = {.type = EventType::kTaskReady, .id = id}});
    } else {
        completed_tasks_.push_back(id);
        finalize_task_metrics(ctx);
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
    if (ctx.stage_index < ctx.stage_runtimes.size()) {
        auto& runtime = ctx.stage_runtimes[ctx.stage_index];
        runtime.started = true;
        runtime.start_time = timestamp;
        runtime.service_duration = duration;
        if (runtime.ready_recorded) {
            runtime.queue_duration = std::max<Duration>(0.0, timestamp - runtime.ready_time);
        } else {
            runtime.queue_duration = 0.0;
        }
        if (stage.service_profile) {
            runtime.domain = stage.service_profile->domain;
        } else {
            runtime.domain.reset();
        }
    }
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
    if (waiting_queue_.empty()) {
        return;
    }

    std::size_t processed = 0;
    const std::size_t max_iterations = waiting_queue_.size();
    while (!waiting_queue_.empty() && processed < max_iterations) {
        evaluate_policy_hook();
        if (waiting_queue_.empty()) {
            break;
        }
        TaskId id = waiting_queue_.front();
        waiting_queue_.pop_front();
        auto& ctx = get_task(id);
        if (!try_start_task(ctx, timestamp)) {
            waiting_queue_.push_back(id);
        }
        ++processed;
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

void BasicScheduler::finalize_task_metrics(const TaskContext& ctx) {
    TaskMetrics metrics{};
    metrics.id = ctx.task.id;
    for (const auto& runtime : ctx.stage_runtimes) {
        metrics.total_queue_time += runtime.queue_duration;
        metrics.total_service_time += runtime.service_duration;
        if (runtime.domain.has_value()) {
            if (*runtime.domain == ServiceTimeDomain::kHost) {
                metrics.host_service_time += runtime.service_duration;
            } else if (*runtime.domain == ServiceTimeDomain::kNic) {
                metrics.nic_service_time += runtime.service_duration;
            }
        }
    }
    completed_metrics_.push_back(metrics);
}

std::optional<ScheduledEvent> BasicScheduler::next_event() const { return queue_.peek(); }

std::vector<TaskId> BasicScheduler::waiting_tasks() const {
    return std::vector<TaskId>(waiting_queue_.begin(), waiting_queue_.end());
}

std::vector<BasicScheduler::TaskStatus> BasicScheduler::task_statuses() const {
    std::vector<TaskStatus> statuses;
    statuses.reserve(tasks_.size());
    for (const auto& [id, ctx] : tasks_) {
        TaskStatus status{};
        status.id = id;
        status.stage_index = ctx.stage_index;
        status.total_stages = ctx.task.stages.size();
        status.active = ctx.active;
        status.waiting = waiting_set_.count(id) != 0;
        status.completed = ctx.stage_index >= ctx.task.stages.size();
        statuses.push_back(status);
    }
    std::sort(statuses.begin(), statuses.end(), [](const TaskStatus& lhs, const TaskStatus& rhs) {
        return lhs.id < rhs.id;
    });
    return statuses;
}

RunMetrics BasicScheduler::aggregated_metrics() const { return compute_run_metrics(*this); }

void BasicScheduler::evaluate_policy_hook() {
    if (policy_hook_ == nullptr) {
        return;
    }
    const PolicyStateSnapshot snapshot = policy_state_snapshot();
    const policy::PolicyDecision decision = policy_hook_->evaluate(snapshot);
    if (decision.type == policy::DirectiveType::kReorderWaitingQueue) {
        apply_waiting_reorder(decision.preferred_waiting_order);
    }
}

void BasicScheduler::apply_waiting_reorder(const std::vector<TaskId>& preferred_order) {
    if (waiting_queue_.empty()) {
        return;
    }

    std::unordered_set<TaskId> inserted;
    std::deque<TaskId> reordered;
    for (TaskId desired : preferred_order) {
        if (waiting_set_.count(desired) == 0) {
            continue;
        }
        if (inserted.insert(desired).second) {
            reordered.push_back(desired);
        }
    }
    for (TaskId existing : waiting_queue_) {
        if (inserted.insert(existing).second) {
            reordered.push_back(existing);
        }
    }
    waiting_queue_ = std::move(reordered);
}

PolicyStateSnapshot BasicScheduler::policy_state_snapshot() const {
    PolicyStateSnapshot snapshot{};
    snapshot.current_time = current_time_;
    snapshot.queues.event_queue_depth = queue_.size();
    snapshot.queues.waiting_queue_depth = waiting_queue_.size();
    snapshot.queues.processed_events = events_processed_;
    snapshot.run_metrics = compute_run_metrics(completed_metrics_);

    const std::vector<Resource> resource_values = resources_.snapshot();
    snapshot.resources.reserve(resource_values.size());
    for (const Resource& resource : resource_values) {
        PolicyResourceState resource_state{};
        resource_state.id = resource.id();
        resource_state.type = resource.type();
        resource_state.capacity = resource.capacity();
        resource_state.in_use = resource.in_use();
        snapshot.resources.push_back(resource_state);
    }

    snapshot.waiting_task_order = waiting_tasks();

    const std::vector<TaskStatus> statuses = task_statuses();
    snapshot.tasks.reserve(statuses.size());
    for (const TaskStatus& status : statuses) {
        PolicyTaskState task_state{};
        task_state.id = status.id;
        task_state.stage_index = status.stage_index;
        task_state.total_stages = status.total_stages;
        task_state.active = status.active;
        task_state.waiting = status.waiting;
        task_state.completed = status.completed;
        snapshot.tasks.push_back(task_state);
    }

    return snapshot;
}

} // namespace nicloadoff
