#include "nicloadoff/rolling_runtime_metrics.hh"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace nicloadoff {

namespace {

Duration compute_percentile(std::vector<Duration>& values, double quantile) {
    if (values.empty()) {
        return 0.0;
    }
    if (values.size() == 1) {
        return values.front();
    }
    if (quantile <= 0.0) {
        return values.front();
    }
    if (quantile >= 1.0) {
        return values.back();
    }
    const double rank = quantile * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(rank));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(rank));
    const double fraction = rank - static_cast<double>(lower);
    if (upper >= values.size()) {
        return values.back();
    }
    const Duration low = values[lower];
    const Duration high = values[upper];
    return low + (high - low) * fraction;
}

} // namespace

RollingRuntimeMetrics::RollingRuntimeMetrics(Duration queue_window_us,
                                             Duration utilization_window_us,
                                             std::size_t sojourn_capacity)
    : queue_window_(queue_window_us),
      utilization_window_(utilization_window_us),
      sojourn_capacity_(sojourn_capacity) {}

void RollingRuntimeMetrics::trim_queue_samples(SimTime now) const {
    if (queue_window_ <= 0.0) {
        return;
    }
    const SimTime threshold = now - queue_window_;
    while (!queue_samples_.empty() && queue_samples_.front().timestamp < threshold) {
        queue_samples_.pop_front();
    }
}

void RollingRuntimeMetrics::trim_utilization_samples(SimTime now) const {
    if (utilization_window_ <= 0.0) {
        return;
    }
    const SimTime threshold = now - utilization_window_;
    while (!utilization_samples_.empty() && utilization_samples_.front().timestamp < threshold) {
        utilization_samples_.pop_front();
    }
}

void RollingRuntimeMetrics::record_queue_depth(SimTime timestamp, double depth) {
    latest_queue_depth_ = depth;
    queue_samples_.push_back(QueueSample{timestamp, depth});
    trim_queue_samples(timestamp);
}

void RollingRuntimeMetrics::record_utilization(SimTime timestamp, double host_ratio, double nic_ratio) {
    latest_host_utilization_ = host_ratio;
    latest_nic_utilization_ = nic_ratio;
    utilization_samples_.push_back(UtilizationSample{timestamp, host_ratio, nic_ratio});
    trim_utilization_samples(timestamp);
}

void RollingRuntimeMetrics::record_task(const TaskTiming& timing) {
    if (sojourn_capacity_ == 0) {
        return;
    }
    recent_tasks_.push_back(timing);
    if (recent_tasks_.size() > sojourn_capacity_) {
        recent_tasks_.pop_front();
    }
}

PolicyRollingMetrics RollingRuntimeMetrics::snapshot(SimTime now) const {
    PolicyRollingMetrics metrics{};

    trim_queue_samples(now);
    trim_utilization_samples(now);

    auto& queue_stats = metrics.waiting_queue_depth;
    queue_stats.latest = latest_queue_depth_;
    queue_stats.samples = queue_samples_.size();
    if (!queue_samples_.empty()) {
        double sum = 0.0;
        double peak = 0.0;
        for (const auto& sample : queue_samples_) {
            sum += sample.depth;
            peak = std::max(peak, sample.depth);
        }
        queue_stats.average = sum / static_cast<double>(queue_samples_.size());
        queue_stats.peak = peak;
    } else {
        queue_stats.average = latest_queue_depth_;
        queue_stats.peak = latest_queue_depth_;
    }

    auto accumulate_utilization = [](const std::deque<UtilizationSample>& samples, bool host) {
        double sum = 0.0;
        double peak = 0.0;
        for (const auto& sample : samples) {
            const double value = host ? sample.host_ratio : sample.nic_ratio;
            sum += value;
            peak = std::max(peak, value);
        }
        if (samples.empty()) {
            return std::pair<double, double>{0.0, 0.0};
        }
        return std::pair<double, double>{sum / static_cast<double>(samples.size()), peak};
    };

    auto [host_avg, host_peak] = accumulate_utilization(utilization_samples_, true);
    auto [nic_avg, nic_peak] = accumulate_utilization(utilization_samples_, false);

    auto& host_stats = metrics.host_utilization;
    host_stats.samples = utilization_samples_.size();
    host_stats.latest = latest_host_utilization_;
    host_stats.average = host_avg;
    host_stats.peak = host_peak;

    auto& nic_stats = metrics.nic_utilization;
    nic_stats.samples = utilization_samples_.size();
    nic_stats.latest = latest_nic_utilization_;
    nic_stats.average = nic_avg;
    nic_stats.peak = nic_peak;

    auto& sojourn = metrics.sojourn;
    sojourn.samples = recent_tasks_.size();
    if (!recent_tasks_.empty()) {
        double queue_sum = 0.0;
        double service_sum = 0.0;
        std::vector<Duration> latencies;
        latencies.reserve(recent_tasks_.size());
        for (const auto& task : recent_tasks_) {
            queue_sum += task.queue_time;
            service_sum += task.service_time;
            latencies.push_back(task.latency());
        }
        const double count = static_cast<double>(recent_tasks_.size());
        sojourn.mean_queue_time = queue_sum / count;
        sojourn.mean_service_time = service_sum / count;
        sojourn.mean_latency = (queue_sum + service_sum) / count;
        std::sort(latencies.begin(), latencies.end());
        sojourn.p95_latency = compute_percentile(latencies, 0.95);
        sojourn.p99_latency = compute_percentile(latencies, 0.99);
    }

    return metrics;
}

void RollingRuntimeMetrics::set_queue_window(Duration queue_window_us, SimTime now) {
    queue_window_ = queue_window_us;
    trim_queue_samples(now);
}

void RollingRuntimeMetrics::set_utilization_window(Duration utilization_window_us, SimTime now) {
    utilization_window_ = utilization_window_us;
    trim_utilization_samples(now);
}

void RollingRuntimeMetrics::set_sojourn_capacity(std::size_t sojourn_capacity) {
    sojourn_capacity_ = sojourn_capacity;
    if (sojourn_capacity_ == 0) {
        recent_tasks_.clear();
        return;
    }
    while (recent_tasks_.size() > sojourn_capacity_) {
        recent_tasks_.pop_front();
    }
}

void RollingRuntimeMetrics::reset_samples() {
    queue_samples_.clear();
    utilization_samples_.clear();
    recent_tasks_.clear();
    latest_queue_depth_ = 0.0;
    latest_host_utilization_ = 0.0;
    latest_nic_utilization_ = 0.0;
}

} // namespace nicloadoff
