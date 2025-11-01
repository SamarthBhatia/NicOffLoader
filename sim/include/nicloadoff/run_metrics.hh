#ifndef NICLOADOFF_RUN_METRICS_HH
#define NICLOADOFF_RUN_METRICS_HH

#include "nicloadoff/scheduler.hh"
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
};

struct RunMetrics {
    std::vector<TaskTiming> tasks;
    RunAggregateMetrics aggregate{};
};

TaskTiming make_task_timing(const BasicScheduler::TaskMetrics& metric);
RunMetrics compute_run_metrics(const std::vector<BasicScheduler::TaskMetrics>& task_metrics);
RunMetrics compute_run_metrics(const BasicScheduler& scheduler);

} // namespace nicloadoff

#endif // NICLOADOFF_RUN_METRICS_HH
