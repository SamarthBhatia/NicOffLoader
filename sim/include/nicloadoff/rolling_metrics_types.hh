#ifndef NICLOADOFF_ROLLING_METRICS_TYPES_HH
#define NICLOADOFF_ROLLING_METRICS_TYPES_HH

#include "nicloadoff/sim_types.hh"

#include <cstddef>

namespace nicloadoff {

struct RollingValueStats {
    std::size_t samples{0};
    double latest{0.0};
    double average{0.0};
    double peak{0.0};
};

struct RollingLatencyStats {
    std::size_t samples{0};
    Duration mean_queue_time{0.0};
    Duration mean_service_time{0.0};
    Duration mean_latency{0.0};
    Duration p95_latency{0.0};
    Duration p99_latency{0.0};
};

struct PolicyRollingMetrics {
    RollingValueStats waiting_queue_depth;
    RollingValueStats host_utilization;
    RollingValueStats nic_utilization;
    RollingLatencyStats sojourn;
};

} // namespace nicloadoff

#endif // NICLOADOFF_ROLLING_METRICS_TYPES_HH

