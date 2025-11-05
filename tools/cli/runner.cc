#include "runner.hh"

#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"
#include "nicloadoff/workload_loader.hh"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace nicloadoff::cli {
namespace {

std::string service_mode_to_string(ServiceTimeMode mode) {
    switch (mode) {
    case ServiceTimeMode::kDeterministic:
        return "deterministic";
    case ServiceTimeMode::kStochastic:
        return "stochastic";
    }
    return "unknown";
}

std::string json_escape(const std::string& value) {
    std::ostringstream oss;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << ch;
            break;
        }
    }
    return oss.str();
}

std::string format_double(double value, int precision = 6) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::optional<ServiceTimeMode> parse_mode(const std::string& value) {
    if (value == "deterministic") {
        return ServiceTimeMode::kDeterministic;
    }
    if (value == "stochastic") {
        return ServiceTimeMode::kStochastic;
    }
    return std::nullopt;
}

WorkloadSpec apply_service_modes(const WorkloadSpec& base, ServiceTimeMode host_mode, ServiceTimeMode nic_mode) {
    WorkloadSpec spec = base;
    for (auto& task : spec.tasks) {
        for (auto& stage : task.stages) {
            if (!stage.service_profile) {
                continue;
            }
            if (stage.service_profile->domain == ServiceTimeDomain::kHost) {
                stage.service_profile->mode = host_mode;
            } else if (stage.service_profile->domain == ServiceTimeDomain::kNic) {
                stage.service_profile->mode = nic_mode;
            }
        }
    }
    return spec;
}

SimTime min_arrival_time(const WorkloadSpec& spec) {
    if (spec.tasks.empty()) {
        return 0.0;
    }
    SimTime min_time = spec.tasks.front().arrival_time;
    for (const auto& task : spec.tasks) {
        if (task.arrival_time < min_time) {
            min_time = task.arrival_time;
        }
    }
    return min_time;
}

double compute_throughput_per_second(std::size_t task_count, Duration makespan_us) {
    if (task_count == 0 || makespan_us <= 0.0) {
        return 0.0;
    }
    const double seconds = makespan_us / 1'000'000.0;
    if (seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(task_count) / seconds;
}

void write_report(const CliOptions& options,
                  const config::Profile& profile,
                  const LoadedWorkload& workload_doc,
                  const RunMetrics& run_metrics,
                  SimTime start_time,
                  SimTime finish_time,
                  Duration makespan,
                  std::size_t events_processed) {
    if (!options.output_path.empty()) {
        const auto parent = options.output_path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(parent) && !std::filesystem::create_directories(parent, ec)) {
                throw std::runtime_error("failed to create parent directory '" + parent.string() + "': " + ec.message());
            }
        }
    }

    std::ofstream out(options.output_path);
    if (!out) {
        throw std::runtime_error("failed to open output path '" + options.output_path.string() + "' for writing");
    }

    const auto& aggregate = run_metrics.aggregate;
    const auto& latency = aggregate.latency_stats;
    const std::size_t task_count = run_metrics.tasks.size();
    const double throughput = compute_throughput_per_second(task_count, makespan);

    out << "{\n";
    out << "  \"profile\": {\n";
    out << "    \"path\": \"" << json_escape(options.profile_path.string()) << "\",\n";
    out << "    \"name\": \"" << json_escape(profile.profile_name) << "\",\n";
    out << "    \"schema_version\": \"" << json_escape(profile.schema_version) << "\"\n";
    out << "  },\n";
    out << "  \"workload\": {\n";
    out << "    \"path\": \"" << json_escape(options.workload_path.string()) << "\",\n";
    out << "    \"schema_version\": \"" << json_escape(workload_doc.schema_version) << "\",\n";
    out << "    \"name\": \"" << json_escape(workload_doc.workload_name) << "\",\n";
    out << "    \"description\": \"" << json_escape(workload_doc.description) << "\"\n";
    out << "  },\n";
    out << "  \"seed\": " << options.seed << ",\n";
    out << "  \"service_modes\": {\n";
    out << "    \"host\": \"" << service_mode_to_string(options.host_mode) << "\",\n";
    out << "    \"nic\": \"" << service_mode_to_string(options.nic_mode) << "\"\n";
    out << "  },\n";
    out << "  \"run\": {\n";
    out << "    \"start_time_us\": " << format_double(start_time, 6) << ",\n";
    out << "    \"finish_time_us\": " << format_double(finish_time, 6) << ",\n";
    out << "    \"makespan_us\": " << format_double(makespan, 6) << ",\n";
    out << "    \"events_processed\": " << events_processed << ",\n";
    out << "    \"completed_tasks\": " << task_count << ",\n";
    out << "    \"throughput_tasks_per_sec\": " << format_double(throughput, 6) << "\n";
    out << "  },\n";
    out << "  \"aggregates\": {\n";
    out << "    \"queue_time_us\": " << format_double(aggregate.total_queue_time, 6) << ",\n";
    out << "    \"service_time_us\": " << format_double(aggregate.total_service_time, 6) << ",\n";
    out << "    \"latency_time_us\": " << format_double(aggregate.total_latency, 6) << ",\n";
    out << "    \"host_service_time_us\": " << format_double(aggregate.host_service_time, 6) << ",\n";
    out << "    \"nic_service_time_us\": " << format_double(aggregate.nic_service_time, 6) << ",\n";
    out << "    \"latency_stats\": {\n";
    out << "      \"mean_us\": " << format_double(latency.mean, 6) << ",\n";
    out << "      \"p50_us\": " << format_double(latency.p50, 6) << ",\n";
    out << "      \"p95_us\": " << format_double(latency.p95, 6) << ",\n";
    out << "      \"p99_us\": " << format_double(latency.p99, 6) << ",\n";
    out << "      \"min_us\": " << format_double(latency.min, 6) << ",\n";
    out << "      \"max_us\": " << format_double(latency.max, 6) << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"tasks\": [\n";
    for (std::size_t i = 0; i < run_metrics.tasks.size(); ++i) {
        const auto& timing = run_metrics.tasks[i];
        out << "    {\n";
        out << "      \"task_id\": " << timing.id << ",\n";
        out << "      \"queue_time_us\": " << format_double(timing.queue_time, 6) << ",\n";
        out << "      \"service_time_us\": " << format_double(timing.service_time, 6) << ",\n";
        out << "      \"latency_us\": " << format_double(timing.latency(), 6) << ",\n";
        out << "      \"host_service_time_us\": " << format_double(timing.host_service_time, 6) << ",\n";
        out << "      \"nic_service_time_us\": " << format_double(timing.nic_service_time, 6) << "\n";
        out << "    }";
        if (i + 1 < run_metrics.tasks.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    if (!out.good()) {
        throw std::runtime_error("failed while writing report to '" + options.output_path.string() + "'");
    }
}

} // namespace

void print_usage(std::ostream& out) {
    out << "Usage: nicloadoff_cli --profile <path> --workload <path> [options]\n"
        << "Options:\n"
        << "  --output <path>          Output JSON report path (default: run_metrics.json)\n"
        << "  --seed <value>           RNG seed for stochastic service times (default: 1)\n"
        << "  --host-mode <mode>       Host service mode: deterministic|stochastic (default: deterministic)\n"
        << "  --nic-mode <mode>        NIC service mode: deterministic|stochastic (default: deterministic)\n"
        << "  -h, --help               Show this message\n";
}

bool parse_arguments(int argc, char** argv, CliOptions& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            return true;
        }
        if (arg == "--profile") {
            if (i + 1 >= argc) {
                error = "--profile requires a path argument";
                return false;
            }
            options.profile_path = argv[++i];
        } else if (arg == "--workload") {
            if (i + 1 >= argc) {
                error = "--workload requires a path argument";
                return false;
            }
            options.workload_path = argv[++i];
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                error = "--output requires a path argument";
                return false;
            }
            options.output_path = argv[++i];
        } else if (arg == "--seed") {
            if (i + 1 >= argc) {
                error = "--seed requires a numeric argument";
                return false;
            }
            try {
                options.seed = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                error = "invalid numeric value for --seed";
                return false;
            }
        } else if (arg == "--host-mode") {
            if (i + 1 >= argc) {
                error = "--host-mode requires a value";
                return false;
            }
            std::string value = argv[++i];
            auto mode = parse_mode(value);
            if (!mode) {
                error = "unknown host mode: " + value;
                return false;
            }
            options.host_mode = *mode;
        } else if (arg == "--nic-mode") {
            if (i + 1 >= argc) {
                error = "--nic-mode requires a value";
                return false;
            }
            std::string value = argv[++i];
            auto mode = parse_mode(value);
            if (!mode) {
                error = "unknown NIC mode: " + value;
                return false;
            }
            options.nic_mode = *mode;
        } else {
            error = "unrecognised argument: " + arg;
            return false;
        }
    }

    if (options.profile_path.empty()) {
        error = "missing required --profile argument";
        return false;
    }
    if (options.workload_path.empty()) {
        error = "missing required --workload argument";
        return false;
    }
    return true;
}

RunSummary run_simulation(const CliOptions& options) {
    const config::Profile profile = config::load_profile_from_file(options.profile_path);
    const LoadedWorkload workload_doc = load_workload_from_file(options.workload_path);
    const WorkloadSpec workload = apply_service_modes(workload_doc.spec, options.host_mode, options.nic_mode);

    ServiceTimeModel service_model(profile, options.seed);
    auto inventory = make_resource_inventory_from_profile(profile);
    const auto tasks = make_tasks_from_spec(workload, inventory.ids);
    BasicScheduler scheduler(std::move(inventory.pool), &service_model);

    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }
    scheduler.run_until_empty();

    const RunMetrics run_metrics = scheduler.aggregated_metrics();
    const SimTime finish_time = scheduler.current_time();
    const SimTime start_time = min_arrival_time(workload);
    Duration makespan = finish_time - start_time;
    if (makespan < 0.0) {
        makespan = 0.0;
    }

    const std::size_t task_count = run_metrics.tasks.size();
    const double throughput = compute_throughput_per_second(task_count, makespan);

    write_report(options, profile, workload_doc, run_metrics, start_time, finish_time, makespan, scheduler.events_processed());

    return RunSummary{
        .completed_tasks = task_count,
        .makespan_us = makespan,
        .throughput_per_sec = throughput,
        .metrics = run_metrics,
    };
}

} // namespace nicloadoff::cli
