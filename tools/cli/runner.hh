#ifndef NICLOADOFF_TOOLS_CLI_RUNNER_HH
#define NICLOADOFF_TOOLS_CLI_RUNNER_HH

#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/task.hh"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace nicloadoff::cli {

struct CliOptions {
    std::filesystem::path profile_path;
    std::filesystem::path workload_path;
    std::filesystem::path output_path{"run_metrics.json"};
    std::filesystem::path manifest_path;
    std::uint64_t seed{1};
    ServiceTimeMode host_mode{ServiceTimeMode::kDeterministic};
    ServiceTimeMode nic_mode{ServiceTimeMode::kDeterministic};
    std::string policy_id{"none"};
    bool show_help{false};
    bool batch_mode{false};
    std::filesystem::path batch_manifest_path;
    std::map<std::string, std::string> metadata;
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
struct BatchRunSummary {
    std::string name;
    CliOptions options;
    RunSummary summary;
    std::map<std::string, std::string> metadata;
};

std::vector<BatchRunSummary> run_batch_manifest(const std::filesystem::path& manifest_path);

} // namespace nicloadoff::cli

#endif // NICLOADOFF_TOOLS_CLI_RUNNER_HH
