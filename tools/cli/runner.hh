#ifndef NICLOADOFF_TOOLS_CLI_RUNNER_HH
#define NICLOADOFF_TOOLS_CLI_RUNNER_HH

#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/task.hh"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace nicloadoff::cli {

struct CliOptions {
    std::filesystem::path profile_path;
    std::filesystem::path workload_path;
    std::filesystem::path output_path{"run_metrics.json"};
    std::uint64_t seed{1};
    ServiceTimeMode host_mode{ServiceTimeMode::kDeterministic};
    ServiceTimeMode nic_mode{ServiceTimeMode::kDeterministic};
    std::string policy_id{"none"};
    bool show_help{false};
};

struct RunSummary {
    std::size_t completed_tasks{0};
    Duration makespan_us{0.0};
    double throughput_per_sec{0.0};
    RunMetrics metrics;
};

void print_usage(std::ostream& out);
bool parse_arguments(int argc, char** argv, CliOptions& options, std::string& error);
RunSummary run_simulation(const CliOptions& options);

} // namespace nicloadoff::cli

#endif // NICLOADOFF_TOOLS_CLI_RUNNER_HH
