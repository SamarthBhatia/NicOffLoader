#ifndef NICLOADOFF_TOOLS_CLI_RUNNER_HH
#define NICLOADOFF_TOOLS_CLI_RUNNER_HH

#include "nicloadoff/rolling_metrics_types.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/task.hh"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nicloadoff::cli {

struct RollingWindowScheduleEvent {
    enum class Action { kConfigure, kReset };
    double timestamp_us{0.0};
    Action action{Action::kConfigure};
    bool reset_samples{false};
    std::optional<double> queue_window_us;
    std::optional<double> util_window_us;
    std::optional<std::size_t> sojourn_window_tasks;
};

struct CliOptions {
    std::filesystem::path profile_path;
    std::filesystem::path workload_path;
    std::filesystem::path output_path{"run_metrics.json"};
    std::filesystem::path manifest_path;
    std::uint64_t seed{1};
    ServiceTimeMode host_mode{ServiceTimeMode::kDeterministic};
    ServiceTimeMode nic_mode{ServiceTimeMode::kDeterministic};
    std::string policy_id{"none"};
    std::optional<std::filesystem::path> policy_config_path;
    bool show_help{false};
    bool batch_mode{false};
    std::filesystem::path batch_manifest_path;
    std::map<std::string, std::string> metadata;
    double rolling_queue_window_us{BasicScheduler::kRollingQueueWindowUs};
    double rolling_util_window_us{BasicScheduler::kRollingUtilizationWindowUs};
    std::size_t rolling_sojourn_window_tasks{BasicScheduler::kRollingSojournWindowTasks};
    std::vector<RollingWindowScheduleEvent> rolling_window_schedule;
};

struct RunSummary {
    std::size_t completed_tasks{0};
    Duration makespan_us{0.0};
    double throughput_per_sec{0.0};
    RunMetrics metrics;
    PolicyRollingMetrics rolling_metrics;
    struct RollingWindowEventSummary {
        double timestamp_us{0.0};
        std::string type;
        bool reset_samples{false};
        double queue_window_us{0.0};
        double util_window_us{0.0};
        std::size_t sojourn_window_tasks{0};
    };
    std::vector<RollingWindowEventSummary> rolling_window_events;
};

void print_usage(std::ostream& out);
bool parse_arguments(int argc, char** argv, CliOptions& options, std::string& error);
RunSummary run_simulation(const CliOptions& options);
struct BatchRunSummary {
    std::string name;
    CliOptions options;
    RunSummary summary;
    std::map<std::string, std::string> metadata;
};

std::vector<BatchRunSummary> run_batch_manifest(const std::filesystem::path& manifest_path);

} // namespace nicloadoff::cli

#endif // NICLOADOFF_TOOLS_CLI_RUNNER_HH
