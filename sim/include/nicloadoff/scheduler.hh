#ifndef NICLOADOFF_SCHEDULER_HH
#define NICLOADOFF_SCHEDULER_HH

#include "nicloadoff/event_queue.hh"
#include "nicloadoff/resource.hh"
#include "nicloadoff/task.hh"

#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nicloadoff {

class ServiceTimeModel;

class SchedulerError : public std::runtime_error {
  public:
    explicit SchedulerError(const std::string& message) : std::runtime_error(message) {}
};

class BasicScheduler {
  public:
    explicit BasicScheduler(ResourcePool resources, ServiceTimeModel* service_model = nullptr);

    void submit_task(const Task& task);
    void run_until_empty();
    bool step_once();

    [[nodiscard]] SimTime current_time() const noexcept { return current_time_; }
    [[nodiscard]] const std::vector<TaskId>& completed_tasks() const noexcept { return completed_tasks_; }
    [[nodiscard]] const ResourcePool& resource_pool() const noexcept { return resources_; }

  private:
    struct TaskContext {
        Task task;
        std::size_t stage_index{0};
        bool active{false};
        Duration active_service_time{0.0};
    };

    EventQueue queue_;
    ResourcePool resources_;
    ServiceTimeModel* service_model_{nullptr};
    SimTime current_time_{0.0};
    std::unordered_map<TaskId, TaskContext> tasks_;
    std::queue<TaskId> waiting_queue_;
    std::unordered_set<TaskId> waiting_set_;
    std::vector<TaskId> completed_tasks_;

    void handle_event(const ScheduledEvent& event);
    void handle_task_arrival(TaskId id, SimTime timestamp);
    void handle_task_ready(TaskId id, SimTime timestamp);
    void handle_task_start(TaskId id, SimTime timestamp);
    void handle_task_complete(TaskId id, SimTime timestamp);
    bool try_start_task(TaskContext& ctx, SimTime timestamp);
    void validate_task(const Task& task) const;
    void drain_waiting(SimTime timestamp);
    TaskContext& get_task(TaskId id);
    Duration resolve_service_time(const TaskStage& stage, TaskId id);
};

} // namespace nicloadoff

#endif // NICLOADOFF_SCHEDULER_HH
