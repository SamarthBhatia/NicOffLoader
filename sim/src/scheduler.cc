#include "nicloadoff/scheduler.hh"

#include "nicloadoff/policy_state.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/service_time_model.hh"

#include <algorithm>
#include <queue>
#include <sstream>
#include <utility>
#include <vector>

namespace nicloadoff {

namespace {

[[nodiscard]] std::string make_error(const std::string& message, TaskId id) {
    std::ostringstream oss;
    oss << message << " (task id=" << id << ")";
    return oss.str();
}

} // namespace

BasicScheduler::BasicScheduler(ResourcePool resources, ServiceTimeModel* service_model)
    : BasicScheduler(std::move(resources), service_model, RollingWindowConfig{}) {}

BasicScheduler::BasicScheduler(ResourcePool resources,
                               ServiceTimeModel* service_model,
                               RollingWindowConfig rolling_config)
    : resources_(std::move(resources)),
      service_model_(service_model),
      rolling_config_(rolling_config),
      rolling_metrics_(rolling_config_.queue_window_us,
                       rolling_config_.utilization_window_us,
                       rolling_config_.sojourn_window_tasks) {
    initialize_domain_usage();
    rolling_metrics_.record_queue_depth(current_time_, 0.0);
    record_utilization_sample(current_time_);
}

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
    evaluate_policy_hook();
    bool started = try_start_task(ctx, timestamp);
    if (!started && waiting_set_.count(id) == 0) {
        waiting_set_.insert(id);
        waiting_queue_.push_back(id);
        record_waiting_queue_sample();
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
        adjust_domain_usage(requirement.resource_id, -requirement.units);
    }
    record_utilization_sample(timestamp);

    if (ctx.active && active_task_count_ > 0) {
        --active_task_count_;
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

    if (admission_limit_.has_value() && active_task_count_ >= *admission_limit_) {
        return false;
    }

    const TaskStage& stage = ctx.task.stages[ctx.stage_index];

    for (const auto& requirement : stage.requirements) {
        if (!resources_.can_allocate(requirement.resource_id, requirement.units)) {
            return false;
        }
    }

    for (const auto& requirement : stage.requirements) {
        resources_.allocate(requirement.resource_id, requirement.units);
        adjust_domain_usage(requirement.resource_id, requirement.units);
    }
    record_utilization_sample(timestamp);

    Duration duration = resolve_service_time(stage, ctx.task.id);

    ctx.active = true;
    ++active_task_count_;
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
        record_waiting_queue_sample();
        auto& ctx = get_task(id);
        if (!try_start_task(ctx, timestamp)) {
            waiting_queue_.push_back(id);
            record_waiting_queue_sample();
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
    rolling_metrics_.record_task(make_task_timing(metrics));
    prune_waiting_reorder_marks();
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

PolicyRollingMetrics BasicScheduler::rolling_metrics_snapshot() const {
    return rolling_metrics_.snapshot(current_time_);
}

void BasicScheduler::evaluate_policy_hook() {
    if (policy_hook_ == nullptr) {
        return;
    }
    const PolicyStateSnapshot snapshot = policy_state_snapshot();
    const policy::PolicyDecision decision = policy_hook_->evaluate(snapshot);
    if (decision.waiting_order) {
        apply_waiting_reorder(*decision.waiting_order);
    }
    if (decision.admission) {
        apply_admission_control(*decision.admission);
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
    bool changed = waiting_queue_.size() != reordered.size();
    if (!changed) {
        auto current = waiting_queue_.begin();
        for (TaskId id : reordered) {
            if (*current != id) {
                changed = true;
                break;
            }
            ++current;
        }
    }
    waiting_queue_ = std::move(reordered);
    if (changed && waiting_queue_.size() > 1) {
        ++policy_waiting_reorders_;
        policy_waiting_reorder_marks_.push_back(completed_tasks_.size());
        prune_waiting_reorder_marks();
    }
}

void BasicScheduler::apply_admission_control(const policy::AdmissionControlDirective& directive) {
    if (!directive.enabled) {
        admission_limit_.reset();
        return;
    }
    admission_limit_ = directive.max_active_tasks;
}

void BasicScheduler::record_waiting_queue_sample() {
    if (waiting_queue_.size() > peak_waiting_queue_depth_) {
        peak_waiting_queue_depth_ = waiting_queue_.size();
    }
    rolling_metrics_.record_queue_depth(current_time_, static_cast<double>(waiting_queue_.size()));
}

void BasicScheduler::initialize_domain_usage() {
    host_usage_ = DomainUsage{};
    nic_usage_ = DomainUsage{};
    const std::vector<Resource> snapshot = resources_.snapshot();
    for (const auto& resource : snapshot) {
        switch (resource.type()) {
        case ResourceType::kHostCpu:
            host_usage_.capacity += resource.capacity();
            break;
        case ResourceType::kNicCpu:
            nic_usage_.capacity += resource.capacity();
            break;
        default:
            break;
        }
    }
}

void BasicScheduler::adjust_domain_usage(ResourceId resource_id, double delta) {
    Resource* resource = resources_.find(resource_id);
    if (resource == nullptr) {
        return;
    }
    DomainUsage* usage = nullptr;
    switch (resource->type()) {
    case ResourceType::kHostCpu:
        usage = &host_usage_;
        break;
    case ResourceType::kNicCpu:
        usage = &nic_usage_;
        break;
    default:
        return;
    }
    usage->in_use += delta;
    if (usage->in_use < 0.0) {
        usage->in_use = 0.0;
    }
    if (usage->in_use > usage->capacity) {
        usage->in_use = usage->capacity;
    }
}

void BasicScheduler::record_utilization_sample(SimTime timestamp) {
    auto ratio = [](const DomainUsage& usage) -> double {
        if (usage.capacity <= 0.0) {
            return 0.0;
        }
        double value = usage.in_use / usage.capacity;
        if (value < 0.0) {
            value = 0.0;
        } else if (value > 1.0) {
            value = 1.0;
        }
        return value;
    };
    rolling_metrics_.record_utilization(timestamp, ratio(host_usage_), ratio(nic_usage_));
}

void BasicScheduler::prune_waiting_reorder_marks() {
    if (policy_waiting_reorder_marks_.empty()) {
        return;
    }
    const std::size_t completed = completed_tasks_.size();
    if (completed <= kPolicyWaitingReorderWindow) {
        return;
    }
    const std::size_t threshold = completed - kPolicyWaitingReorderWindow;
    auto first = std::lower_bound(policy_waiting_reorder_marks_.begin(),
                                  policy_waiting_reorder_marks_.end(),
                                  threshold);
    if (first != policy_waiting_reorder_marks_.begin()) {
        policy_waiting_reorder_marks_.erase(policy_waiting_reorder_marks_.begin(), first);
    }
}

std::size_t BasicScheduler::policy_waiting_reorders_recent(std::size_t window) const {
    if (window == 0 || policy_waiting_reorder_marks_.empty() || completed_tasks_.empty()) {
        return 0;
    }
    const std::size_t completed = completed_tasks_.size();
    const std::size_t threshold = completed > window ? completed - window : 0;
    auto first = std::lower_bound(policy_waiting_reorder_marks_.begin(),
                                  policy_waiting_reorder_marks_.end(),
                                  threshold);
    return static_cast<std::size_t>(std::distance(first, policy_waiting_reorder_marks_.end()));
}

PolicyStateSnapshot BasicScheduler::policy_state_snapshot() const {
    PolicyStateSnapshot snapshot{};
    snapshot.current_time = current_time_;
    snapshot.queues.event_queue_depth = queue_.size();
    snapshot.queues.waiting_queue_depth = waiting_queue_.size();
    snapshot.queues.processed_events = events_processed_;
    snapshot.run_metrics = compute_run_metrics(*this);
    snapshot.rolling_metrics = rolling_metrics_.snapshot(current_time_);

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
    snapshot.active_task_count = active_task_count_;
    snapshot.admission_limit = admission_limit_;

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

        auto ctx_it = tasks_.find(status.id);
        if (ctx_it != tasks_.end()) {
            const TaskContext& ctx = ctx_it->second;
            if (status.stage_index < ctx.task.stages.size()) {
                task_state.has_pending_stage = true;
                const TaskStage& stage = ctx.task.stages[status.stage_index];
                for (const TaskRequirement& req : stage.requirements) {
                    const Resource* resource = resources_.find(req.resource_id);
                    if (resource == nullptr) {
                        continue;
                    }
                    switch (resource->type()) {
                    case ResourceType::kHostCpu:
                    case ResourceType::kHostDram:
                    case ResourceType::kHostLink:
                        task_state.pending_host_demand += req.units;
                        break;
                    case ResourceType::kNicCpu:
                    case ResourceType::kNicDram:
                    case ResourceType::kNicLink:
                        task_state.pending_nic_demand += req.units;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        snapshot.tasks.push_back(task_state);
    }

    snapshot.scenario_metadata = scenario_metadata_;

    return snapshot;
}

} // namespace nicloadoff
