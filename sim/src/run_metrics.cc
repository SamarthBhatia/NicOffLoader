#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"

#include <algorithm>
#include <cmath>

namespace nicloadoff {

namespace {

[[nodiscard]] Duration compute_percentile(const std::vector<Duration>& sorted, double quantile) {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted.front();
    }
    if (quantile <= 0.0) {
        return sorted.front();
    }
    if (quantile >= 1.0) {
        return sorted.back();
    }

    const double rank = quantile * static_cast<double>(sorted.size() - 1);
    const std::size_t lower_index = static_cast<std::size_t>(std::floor(rank));
    const std::size_t upper_index = static_cast<std::size_t>(std::ceil(rank));
    const double frac = rank - static_cast<double>(lower_index);

    if (upper_index >= sorted.size()) {
        return sorted.back();
    }
    const Duration lower = sorted[lower_index];
    const Duration upper = sorted[upper_index];
    return lower + (upper - lower) * frac;
}

} // namespace

TaskTiming make_task_timing(const BasicScheduler::TaskMetrics& metric) {
    TaskTiming timing{};
    timing.id = metric.id;
    timing.queue_time = metric.total_queue_time;
    timing.service_time = metric.total_service_time;
    timing.host_service_time = metric.host_service_time;
    timing.nic_service_time = metric.nic_service_time;
    return timing;
}

RunMetrics compute_run_metrics(const std::vector<BasicScheduler::TaskMetrics>& task_metrics) {
    RunMetrics result;
    result.tasks.reserve(task_metrics.size());

    std::vector<Duration> latencies;
    latencies.reserve(task_metrics.size());

    for (const auto& metric : task_metrics) {
        TaskTiming timing = make_task_timing(metric);
        result.aggregate.total_queue_time += timing.queue_time;
        result.aggregate.total_service_time += timing.service_time;
        result.aggregate.host_service_time += timing.host_service_time;
        result.aggregate.nic_service_time += timing.nic_service_time;
        const Duration latency = timing.latency();
        result.aggregate.total_latency += latency;

        latencies.push_back(latency);
        result.tasks.push_back(timing);
    }

    auto& stats = result.aggregate.latency_stats;
    stats.count = latencies.size();
    stats.sum = result.aggregate.total_latency;
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        stats.min = latencies.front();
        stats.max = latencies.back();
        stats.mean = stats.sum / static_cast<double>(stats.count);
        stats.p50 = compute_percentile(latencies, 0.50);
        stats.p95 = compute_percentile(latencies, 0.95);
        stats.p99 = compute_percentile(latencies, 0.99);
    }

    return result;
}

RunMetrics compute_run_metrics(const BasicScheduler& scheduler) {
    RunMetrics metrics = compute_run_metrics(scheduler.completed_metrics());
    metrics.aggregate.peak_waiting_queue_depth = scheduler.peak_waiting_queue_depth();
    metrics.policy.waiting_reorders = scheduler.policy_waiting_reorders();
    const double task_count = static_cast<double>(metrics.tasks.size());
    if (task_count > 0.0) {
        metrics.policy.waiting_reorders_per_task = static_cast<double>(metrics.policy.waiting_reorders) / task_count;
    } else {
        metrics.policy.waiting_reorders_per_task = 0.0;
    }
    const std::size_t window = BasicScheduler::kPolicyWaitingReorderWindow;
    metrics.policy.waiting_reorders_recent = scheduler.policy_waiting_reorders_recent(window);
    metrics.policy.waiting_reorder_recent_task_count = std::min<std::size_t>(window, metrics.tasks.size());
    if (metrics.policy.waiting_reorder_recent_task_count > 0) {
        metrics.policy.waiting_reorders_per_task_recent =
            static_cast<double>(metrics.policy.waiting_reorders_recent) /
            static_cast<double>(metrics.policy.waiting_reorder_recent_task_count);
    } else {
        metrics.policy.waiting_reorders_per_task_recent = 0.0;
    }
    metrics.policy.admission_limited_tasks = scheduler.policy_admission_blocked_tasks();
    metrics.policy.admission_limit_last = scheduler.policy_last_admission_limit();
    metrics.policy.admission_limit_active = scheduler.admission_limit().has_value();
    return metrics;
}

} // namespace nicloadoff
