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
    struct TaskStatus {
        TaskId id{};
        std::size_t stage_index{0};
        std::size_t total_stages{0};
        bool active{false};
        bool waiting{false};
        bool completed{false};
    };

    struct TaskMetrics {
        TaskId id{};
        Duration total_queue_time{0.0};
        Duration total_service_time{0.0};
        Duration host_service_time{0.0};
        Duration nic_service_time{0.0};
    };

    explicit BasicScheduler(ResourcePool resources, ServiceTimeModel* service_model = nullptr);

    void submit_task(const Task& task);
    void run_until_empty();
    bool step_once();

    [[nodiscard]] SimTime current_time() const noexcept { return current_time_; }
    [[nodiscard]] const std::vector<TaskId>& completed_tasks() const noexcept { return completed_tasks_; }
    [[nodiscard]] const ResourcePool& resource_pool() const noexcept { return resources_; }
    [[nodiscard]] std::optional<ScheduledEvent> last_event() const noexcept { return last_event_; }
    [[nodiscard]] std::optional<ScheduledEvent> next_event() const;
    [[nodiscard]] std::size_t event_queue_size() const noexcept { return queue_.size(); }
    [[nodiscard]] std::size_t waiting_queue_size() const noexcept { return waiting_queue_.size(); }
    [[nodiscard]] std::vector<TaskId> waiting_tasks() const;
    [[nodiscard]] std::vector<TaskStatus> task_statuses() const;
    [[nodiscard]] std::size_t events_processed() const noexcept { return events_processed_; }
    [[nodiscard]] bool has_pending_work() const noexcept { return !queue_.empty() || !waiting_queue_.empty(); }
    [[nodiscard]] const std::vector<TaskMetrics>& completed_metrics() const noexcept { return completed_metrics_; }

  private:
    struct StageRuntime {
        bool ready_recorded{false};
        bool started{false};
        SimTime ready_time{0.0};
        SimTime start_time{0.0};
        SimTime completion_time{0.0};
        Duration queue_duration{0.0};
        Duration service_duration{0.0};
        std::optional<ServiceTimeDomain> domain;
    };

    struct TaskContext {
        Task task;
        std::size_t stage_index{0};
        bool active{false};
        Duration active_service_time{0.0};
        std::vector<StageRuntime> stage_runtimes;
    };

    EventQueue queue_;
    ResourcePool resources_;
    ServiceTimeModel* service_model_{nullptr};
    SimTime current_time_{0.0};
    std::unordered_map<TaskId, TaskContext> tasks_;
    std::queue<TaskId> waiting_queue_;
    std::unordered_set<TaskId> waiting_set_;
    std::vector<TaskId> completed_tasks_;
    std::optional<ScheduledEvent> last_event_;
    std::size_t events_processed_{0};
    std::vector<TaskMetrics> completed_metrics_;

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
    void finalize_task_metrics(const TaskContext& ctx);
};

} // namespace nicloadoff

#endif // NICLOADOFF_SCHEDULER_HH
