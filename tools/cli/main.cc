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

    try {
        RunSummary summary = run_simulation(options);
        std::cout << "Completed " << summary.completed_tasks << " tasks in "
                  << format_value(summary.makespan_us) << " us\n";
        std::cout << "Throughput: " << format_value(summary.throughput_per_sec) << " tasks/s\n";
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
