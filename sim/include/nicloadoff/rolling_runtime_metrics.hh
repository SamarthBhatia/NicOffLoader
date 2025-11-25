#ifndef NICLOADOFF_ROLLING_RUNTIME_METRICS_HH
#define NICLOADOFF_ROLLING_RUNTIME_METRICS_HH

#include "nicloadoff/rolling_metrics_types.hh"
#include "nicloadoff/run_metrics_types.hh"

#include <cstddef>
#include <deque>

namespace nicloadoff {

class RollingRuntimeMetrics {
  public:
    RollingRuntimeMetrics(Duration queue_window_us, Duration utilization_window_us, std::size_t sojourn_capacity);

    void record_queue_depth(SimTime timestamp, double depth);
    void record_utilization(SimTime timestamp, double host_ratio, double nic_ratio);
    void record_task(const TaskTiming& timing);
    void set_queue_window(Duration queue_window_us, SimTime now);
    void set_utilization_window(Duration utilization_window_us, SimTime now);
    void set_sojourn_capacity(std::size_t sojourn_capacity);
    void reset_samples();

    [[nodiscard]] PolicyRollingMetrics snapshot(SimTime now) const;

  private:
    struct QueueSample {
        SimTime timestamp{0.0};
        double depth{0.0};
    };

    struct UtilizationSample {
        SimTime timestamp{0.0};
        double host_ratio{0.0};
        double nic_ratio{0.0};
    };

    Duration queue_window_{0.0};
    Duration utilization_window_{0.0};
    std::size_t sojourn_capacity_{0};

    mutable std::deque<QueueSample> queue_samples_;
    mutable std::deque<UtilizationSample> utilization_samples_;
    std::deque<TaskTiming> recent_tasks_;

    double latest_queue_depth_{0.0};
    double latest_host_utilization_{0.0};
    double latest_nic_utilization_{0.0};

    void trim_queue_samples(SimTime now) const;
    void trim_utilization_samples(SimTime now) const;
};

} // namespace nicloadoff

#endif // NICLOADOFF_ROLLING_RUNTIME_METRICS_HH
