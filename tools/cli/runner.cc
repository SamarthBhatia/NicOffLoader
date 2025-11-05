#include "runner.hh"

#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"
#include "nicloadoff/workload_loader.hh"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>

namespace nicloadoff::cli {
namespace {

[[nodiscard]] std::string trim_copy(const std::string& input) {
    const std::string whitespace = " \t\r\n";
    const auto begin = input.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = input.find_last_not_of(whitespace);
    return input.substr(begin, end - begin + 1);
}

[[nodiscard]] std::string strip_quotes(std::string value) {
    if (value.size() >= 2) {
        char first = value.front();
        char last = value.back();
        if ((first == '"' || first == '\'') && last == first) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

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

struct ManifestOptions {
    std::optional<std::filesystem::path> profile;
    std::optional<std::filesystem::path> workload;
    std::optional<std::filesystem::path> output;
    std::optional<std::uint64_t> seed;
    std::optional<ServiceTimeMode> host_mode;
    std::optional<ServiceTimeMode> nic_mode;
    std::optional<std::string> policy_id;
};

bool parse_manifest_file(const std::filesystem::path& manifest_path,
                         ManifestOptions& manifest,
                         std::string& error) {
    std::ifstream input(manifest_path);
    if (!input) {
        error = "failed to open manifest '" + manifest_path.string() + "' for reading";
        return false;
    }

    const std::filesystem::path base_dir = manifest_path.parent_path();
    std::string line;
    std::size_t line_number = 0;
    bool in_service_modes = false;

    auto resolve_path = [&](const std::string& value) -> std::filesystem::path {
        std::filesystem::path path_value = value;
        if (path_value.is_relative()) {
            path_value = base_dir / path_value;
        }
        return path_value;
    };

    while (std::getline(input, line)) {
        ++line_number;

        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }
        bool indented = !line.empty() && std::isspace(static_cast<unsigned char>(line[0]));
        std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }

        if (!indented) {
            in_service_modes = false;
        }

        if (trimmed == "service_modes:") {
            in_service_modes = true;
            continue;
        }

        const auto colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) {
            std::ostringstream oss;
            oss << "manifest parse error at line " << line_number << ": expected 'key: value'";
            error = oss.str();
            return false;
        }

        std::string key = trim_copy(trimmed.substr(0, colon_pos));
        std::string value = trim_copy(trimmed.substr(colon_pos + 1));
        value = strip_quotes(value);

        if (in_service_modes) {
            if (key == "host") {
                auto mode = parse_mode(value);
                if (!mode) {
                    std::ostringstream oss;
                    oss << "manifest service_modes.host has unknown value '" << value << "'";
                    error = oss.str();
                    return false;
                }
                manifest.host_mode = *mode;
            } else if (key == "nic") {
                auto mode = parse_mode(value);
                if (!mode) {
                    std::ostringstream oss;
                    oss << "manifest service_modes.nic has unknown value '" << value << "'";
                    error = oss.str();
                    return false;
                }
                manifest.nic_mode = *mode;
            } else {
                std::ostringstream oss;
                oss << "manifest service_modes contains unknown key '" << key << "'";
                error = oss.str();
                return false;
            }
            continue;
        }

        if (key == "profile") {
            if (value.empty()) {
                error = "manifest field 'profile' requires a value";
                return false;
            }
            manifest.profile = resolve_path(value);
        } else if (key == "workload") {
            if (value.empty()) {
                error = "manifest field 'workload' requires a value";
                return false;
            }
            manifest.workload = resolve_path(value);
        } else if (key == "output") {
            if (value.empty()) {
                error = "manifest field 'output' requires a value";
                return false;
            }
            manifest.output = resolve_path(value);
        } else if (key == "policy") {
            if (value.empty()) {
                error = "manifest field 'policy' requires a value";
                return false;
            }
            manifest.policy_id = value;
        } else if (key == "seed") {
            if (value.empty()) {
                error = "manifest field 'seed' requires a value";
                return false;
            }
            try {
                manifest.seed = std::stoull(value);
            } catch (const std::exception&) {
                std::ostringstream oss;
                oss << "manifest field 'seed' must be an unsigned integer (line " << line_number << ")";
                error = oss.str();
                return false;
            }
        } else {
            std::ostringstream oss;
            oss << "manifest contains unknown key '" << key << "'";
            error = oss.str();
            return false;
        }
    }

    return true;
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
        << "  --config <path>          YAML manifest with profile/workload/policy defaults\n"
        << "  --seed <value>           RNG seed for stochastic service times (default: 1)\n"
        << "  --host-mode <mode>       Host service mode: deterministic|stochastic (default: deterministic)\n"
        << "  --nic-mode <mode>        NIC service mode: deterministic|stochastic (default: deterministic)\n"
        << "  --policy <id>            Policy hook to register (options: none, descending-id, limit-active-1, prefer-host, prefer-nic)\n"
        << "  -h, --help               Show this message\n";
}

bool parse_arguments(int argc, char** argv, CliOptions& options, std::string& error) {
    bool profile_cli = false;
    bool workload_cli = false;
    bool output_cli = false;
    bool seed_cli = false;
    bool host_cli = false;
    bool nic_cli = false;
    bool policy_cli = false;
    bool manifest_cli = false;

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
            profile_cli = true;
        } else if (arg == "--workload") {
            if (i + 1 >= argc) {
                error = "--workload requires a path argument";
                return false;
            }
            options.workload_path = argv[++i];
            workload_cli = true;
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                error = "--output requires a path argument";
                return false;
            }
            options.output_path = argv[++i];
            output_cli = true;
        } else if (arg == "--config" || arg == "--manifest") {
            if (i + 1 >= argc) {
                error = std::string(arg) + " requires a path argument";
                return false;
            }
            if (manifest_cli) {
                error = "manifest specified multiple times";
                return false;
            }
            options.manifest_path = argv[++i];
            manifest_cli = true;
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
            seed_cli = true;
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
            host_cli = true;
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
            nic_cli = true;
        } else if (arg == "--policy") {
            if (i + 1 >= argc) {
                error = "--policy requires a value";
                return false;
            }
            std::string value = argv[++i];
            if (!policy::is_policy_supported(value)) {
                error = "unknown policy: " + value;
                return false;
            }
            options.policy_id = std::move(value);
            policy_cli = true;
        } else {
            error = "unrecognised argument: " + arg;
            return false;
        }
    }

    if (manifest_cli) {
        ManifestOptions manifest_options;
        if (!parse_manifest_file(options.manifest_path, manifest_options, error)) {
            return false;
        }
        if (!profile_cli) {
            if (!manifest_options.profile) {
                error = "manifest does not provide a profile path";
                return false;
            }
            options.profile_path = *manifest_options.profile;
        }
        if (!workload_cli) {
            if (!manifest_options.workload) {
                error = "manifest does not provide a workload path";
                return false;
            }
            options.workload_path = *manifest_options.workload;
        }
        if (!output_cli && manifest_options.output) {
            options.output_path = *manifest_options.output;
        }
        if (!seed_cli && manifest_options.seed) {
            options.seed = *manifest_options.seed;
        }
        if (!host_cli && manifest_options.host_mode) {
            options.host_mode = *manifest_options.host_mode;
        }
        if (!nic_cli && manifest_options.nic_mode) {
            options.nic_mode = *manifest_options.nic_mode;
        }
        if (!policy_cli && manifest_options.policy_id) {
            if (!policy::is_policy_supported(*manifest_options.policy_id)) {
                error = "manifest policy '" + *manifest_options.policy_id + "' is not recognised";
                return false;
            }
            options.policy_id = *manifest_options.policy_id;
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

    std::unique_ptr<policy::PolicyHook> policy_hook = policy::make_policy_hook(options.policy_id);
    if (policy_hook) {
        scheduler.set_policy_hook(policy_hook.get());
    }

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
