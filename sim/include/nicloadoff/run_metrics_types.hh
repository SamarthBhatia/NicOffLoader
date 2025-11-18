#ifndef NICLOADOFF_RUN_METRICS_TYPES_HH
#define NICLOADOFF_RUN_METRICS_TYPES_HH

#include "nicloadoff/sim_types.hh"

#include <cstddef>
#include <vector>

namespace nicloadoff {

struct TaskTiming {
    TaskId id{};
    Duration queue_time{0.0};
    Duration service_time{0.0};
    Duration host_service_time{0.0};
    Duration nic_service_time{0.0};

    [[nodiscard]] Duration latency() const noexcept { return queue_time + service_time; }
};

struct AggregateStatistic {
    std::size_t count{0};
    Duration sum{0.0};
    Duration mean{0.0};
    Duration min{0.0};
    Duration max{0.0};
    Duration p50{0.0};
    Duration p95{0.0};
    Duration p99{0.0};
};

struct RunAggregateMetrics {
    Duration total_queue_time{0.0};
    Duration total_service_time{0.0};
    Duration total_latency{0.0};
    Duration host_service_time{0.0};
    Duration nic_service_time{0.0};
    AggregateStatistic latency_stats{};
    std::size_t peak_waiting_queue_depth{0};
};

struct PolicyRunMetrics {
    std::size_t waiting_reorders{0};
    double waiting_reorders_per_task{0.0};
    std::size_t waiting_reorders_recent{0};
    std::size_t waiting_reorder_recent_task_count{0};
    double waiting_reorders_per_task_recent{0.0};
};

struct RunMetrics {
    std::vector<TaskTiming> tasks;
    RunAggregateMetrics aggregate{};
    PolicyRunMetrics policy{};
};

} // namespace nicloadoff

#endif // NICLOADOFF_RUN_METRICS_TYPES_HH
