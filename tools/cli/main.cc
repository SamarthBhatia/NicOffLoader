#include "runner.hh"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string format_value(double value, int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

} // namespace

int main(int argc, char** argv) {
    using namespace nicloadoff::cli;

    CliOptions options;
    std::string error;
    if (!parse_arguments(argc, argv, options, error)) {
        std::cerr << "Error: " << error << "\n\n";
        print_usage(std::cerr);
        return 1;
    }
    if (options.show_help) {
        print_usage(std::cout);
        return 0;
    }
    if (options.list_rolling_presets) {
        print_presets_listing(std::cout, options.rolling_window_preset_file);
        return 0;
    }

    try {
        if (options.batch_mode) {
            const auto batch_results = run_batch_manifest(options.batch_manifest_path);
            std::cout << "Completed batch with " << batch_results.size() << " run(s)\n";
            return 0;
        }
        RunSummary summary = run_simulation(options);
        std::cout << "Completed " << summary.completed_tasks << " tasks in "
                  << format_value(summary.makespan_us) << " us\n";
        std::cout << "Throughput: " << format_value(summary.throughput_per_sec) << " tasks/s\n";
        std::cout << "Rolling windows (queue/util/sojourn): "
                  << format_value(options.rolling_queue_window_us, 0) << " us / "
                  << format_value(options.rolling_util_window_us, 0) << " us / "
                  << options.rolling_sojourn_window_tasks << " tasks\n";
        const auto& policy_metrics = summary.metrics.policy;
        std::cout << "Policy waiting reorders: " << policy_metrics.waiting_reorders
                  << " (per task " << format_value(policy_metrics.waiting_reorders_per_task, 6) << ")\n";
        if (policy_metrics.waiting_reorder_recent_task_count > 0) {
            std::cout << "Recent reorder rate (last " << policy_metrics.waiting_reorder_recent_task_count
                      << " tasks): " << format_value(policy_metrics.waiting_reorders_per_task_recent, 6) << "\n";
        }
        if (policy_metrics.admission_limit_last || policy_metrics.admission_limited_tasks > 0 ||
            policy_metrics.admission_limit_active) {
            std::cout << "Policy admission limit: ";
            if (policy_metrics.admission_limit_last) {
                std::cout << *policy_metrics.admission_limit_last;
            } else {
                std::cout << "none";
            }
            if (policy_metrics.admission_limited_tasks > 0) {
                std::cout << " (blocked " << policy_metrics.admission_limited_tasks << " task";
                if (policy_metrics.admission_limited_tasks != 1) {
                    std::cout << "s";
                }
                std::cout << ")";
            }
            if (!policy_metrics.admission_limit_active) {
                std::cout << " [inactive]";
            }
            std::cout << "\n";
        }
        const auto& rolling = summary.rolling_metrics;
        if (rolling.waiting_queue_depth.samples > 0) {
            std::cout << "Rolling queue avg/peak (latest): "
                      << format_value(rolling.waiting_queue_depth.average, 6) << " / "
                      << format_value(rolling.waiting_queue_depth.peak, 6) << " ("
                      << format_value(rolling.waiting_queue_depth.latest, 6) << ")\n";
        }
        if (rolling.host_utilization.samples > 0 || rolling.nic_utilization.samples > 0) {
            std::cout << "Rolling util avg (host/nic): "
                      << format_value(rolling.host_utilization.average, 6) << " / "
                      << format_value(rolling.nic_utilization.average, 6) << "\n";
        }
        if (rolling.sojourn.samples > 0) {
            std::cout << "Rolling sojourn mean/p95/p99 (us): "
                      << format_value(rolling.sojourn.mean_latency, 6) << " / "
                      << format_value(rolling.sojourn.p95_latency, 6) << " / "
                      << format_value(rolling.sojourn.p99_latency, 6) << "\n";
        }
        std::cout << "Report written to " << options.output_path << "\n";
        if (summary.metrics.aggregate.latency_stats.count > 0) {
            const auto& stats = summary.metrics.aggregate.latency_stats;
            std::cout << "Latency mean/p95/p99 (us): " << format_value(stats.mean) << " / "
                      << format_value(stats.p95) << " / "
                      << format_value(stats.p99) << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 1;
    }
}
