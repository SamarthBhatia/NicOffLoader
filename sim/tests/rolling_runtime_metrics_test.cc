#include "nicloadoff/rolling_runtime_metrics.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    using nicloadoff::RollingRuntimeMetrics;
    using nicloadoff::TaskTiming;

    RollingRuntimeMetrics metrics(/*queue_window_us=*/10.0, /*utilization_window_us=*/10.0, /*sojourn_capacity=*/4);

    metrics.record_queue_depth(0.0, 0.0);
    metrics.record_queue_depth(5.0, 2.0);
    auto snapshot = metrics.snapshot(5.0);
    check(snapshot.waiting_queue_depth.samples == 2, "expected two queue samples before trim");
    assert_near(snapshot.waiting_queue_depth.average, 1.0);
    assert_near(snapshot.waiting_queue_depth.peak, 2.0);

    metrics.record_queue_depth(20.0, 1.0);
    snapshot = metrics.snapshot(20.0);
    check(snapshot.waiting_queue_depth.samples == 1, "queue window should trim old sample");
    assert_near(snapshot.waiting_queue_depth.latest, 1.0);
    assert_near(snapshot.waiting_queue_depth.average, 1.0);

    metrics.record_utilization(0.0, 0.5, 0.25);
    metrics.record_utilization(5.0, 0.75, 0.5);
    snapshot = metrics.snapshot(5.0);
    check(snapshot.host_utilization.samples == 2, "expected two utilization samples");
    assert_near(snapshot.host_utilization.average, 0.625);
    assert_near(snapshot.host_utilization.peak, 0.75);
    assert_near(snapshot.nic_utilization.average, 0.375);
    assert_near(snapshot.nic_utilization.peak, 0.5);

    TaskTiming t1{};
    t1.queue_time = 2.0;
    t1.service_time = 8.0;
    TaskTiming t2{};
    t2.queue_time = 4.0;
    t2.service_time = 6.0;
    TaskTiming t3{};
    t3.queue_time = 1.0;
    t3.service_time = 3.0;
    TaskTiming t4{};
    t4.queue_time = 3.0;
    t4.service_time = 5.0;
    TaskTiming t5{};
    t5.queue_time = 5.0;
    t5.service_time = 5.0;

    metrics.record_task(t1);
    metrics.record_task(t2);
    metrics.record_task(t3);
    metrics.record_task(t4);
    metrics.record_task(t5); // capacity 4 -> oldest (t1) dropped

    snapshot = metrics.snapshot(25.0);
    check(snapshot.sojourn.samples == 4, "expected sojourn capacity to cap at 4");
    assert_near(snapshot.sojourn.mean_queue_time, (4.0 + 1.0 + 3.0 + 5.0) / 4.0);
    assert_near(snapshot.sojourn.mean_service_time, (6.0 + 3.0 + 5.0 + 5.0) / 4.0);
    assert_near(snapshot.sojourn.mean_latency, (13.0 + 19.0) / 4.0);
    assert_near(snapshot.sojourn.p95_latency, 10.0);
    assert_near(snapshot.sojourn.p99_latency, 10.0);

    return 0;
}

