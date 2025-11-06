#ifndef NICLOADOFF_RUN_METRICS_HH
#define NICLOADOFF_RUN_METRICS_HH

#include "nicloadoff/run_metrics_types.hh"
#include "nicloadoff/scheduler.hh"

namespace nicloadoff {

TaskTiming make_task_timing(const BasicScheduler::TaskMetrics& metric);
RunMetrics compute_run_metrics(const std::vector<BasicScheduler::TaskMetrics>& task_metrics);
RunMetrics compute_run_metrics(const BasicScheduler& scheduler);

} // namespace nicloadoff

#endif // NICLOADOFF_RUN_METRICS_HH
