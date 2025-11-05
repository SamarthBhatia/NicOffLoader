#ifndef NICLOADOFF_POLICY_STATE_HH
#define NICLOADOFF_POLICY_STATE_HH

#include "nicloadoff/resource.hh"
#include "nicloadoff/run_metrics_types.hh"
#include "nicloadoff/sim_types.hh"

#include <cstddef>
#include <optional>
#include <vector>

namespace nicloadoff {

struct PolicyResourceState {
    ResourceId id{};
    ResourceType type{ResourceType::kHostCpu};
    double capacity{0.0};
    double in_use{0.0};
};

struct PolicyTaskState {
    TaskId id{};
    std::size_t stage_index{0};
    std::size_t total_stages{0};
    bool active{false};
    bool waiting{false};
    bool completed{false};
};

struct PolicyQueuesState {
    std::size_t event_queue_depth{0};
    std::size_t waiting_queue_depth{0};
    std::size_t processed_events{0};
};

struct PolicyStateSnapshot {
    SimTime current_time{0.0};
    PolicyQueuesState queues{};
    RunMetrics run_metrics{};
    std::vector<PolicyResourceState> resources;
    std::vector<PolicyTaskState> tasks;
    std::vector<TaskId> waiting_task_order;
    std::size_t active_task_count{0};
    std::optional<std::size_t> admission_limit;
};

} // namespace nicloadoff

#endif // NICLOADOFF_POLICY_STATE_HH
