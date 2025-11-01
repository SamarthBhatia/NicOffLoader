#include "nicloadoff/run_metrics.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void assert_near(double lhs, double rhs, double tolerance = 1e-9) {
    if (std::fabs(lhs - rhs) > tolerance) {
        std::fprintf(stderr, "assert_near failed: |%f - %f| > %f\n", lhs, rhs, tolerance);
        std::abort();
    }
}

} // namespace

int main() {
    {
        std::vector<nicloadoff::BasicScheduler::TaskMetrics> metrics;
        const auto run_metrics = nicloadoff::compute_run_metrics(metrics);
        check(run_metrics.tasks.empty(), "expected no task timings");
        check(run_metrics.aggregate.latency_stats.count == 0, "expected zero latency samples");
        assert_near(run_metrics.aggregate.total_queue_time, 0.0);
        assert_near(run_metrics.aggregate.total_service_time, 0.0);
        assert_near(run_metrics.aggregate.total_latency, 0.0);
    }

    {
        std::vector<nicloadoff::BasicScheduler::TaskMetrics> metrics;
        metrics.push_back(nicloadoff::BasicScheduler::TaskMetrics{
            .id = 1,
            .total_queue_time = 1.0,
            .total_service_time = 4.0,
            .host_service_time = 3.0,
            .nic_service_time = 1.0,
        });
        metrics.push_back(nicloadoff::BasicScheduler::TaskMetrics{
            .id = 2,
            .total_queue_time = 2.0,
            .total_service_time = 5.0,
            .host_service_time = 2.0,
            .nic_service_time = 3.0,
        });

        const auto run_metrics = nicloadoff::compute_run_metrics(metrics);
        check(run_metrics.tasks.size() == 2, "expected two task timings");
        const auto& aggregate = run_metrics.aggregate;
        assert_near(aggregate.total_queue_time, 3.0);
        assert_near(aggregate.total_service_time, 9.0);
        assert_near(aggregate.total_latency, 12.0);
        assert_near(aggregate.host_service_time, 5.0);
        assert_near(aggregate.nic_service_time, 4.0);

        const auto& stats = aggregate.latency_stats;
        check(stats.count == 2, "expected two latency samples");
        assert_near(stats.sum, 12.0);
        assert_near(stats.mean, 6.0);
        assert_near(stats.min, 5.0);
        assert_near(stats.max, 7.0);
        assert_near(stats.p50, 6.0);
        assert_near(stats.p95, 6.9);
        assert_near(stats.p99, 6.98);

        check(run_metrics.tasks[0].id == 1, "expected first task id to be 1");
        assert_near(run_metrics.tasks[0].latency(), 5.0);
        check(run_metrics.tasks[1].id == 2, "expected second task id to be 2");
        assert_near(run_metrics.tasks[1].latency(), 7.0);
    }

    return 0;
}
