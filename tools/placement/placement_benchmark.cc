#include "nicloadoff/dag_submission_controller.hh"
#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload_loader.hh"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class PlacementMode { kHintRespect, kHostPinned, kNicPinned };

[[nodiscard]] std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] PlacementMode placement_mode_from_string(std::string value) {
    value = to_lower(value);
    if (value == "host_pinned") {
        return PlacementMode::kHostPinned;
    }
    if (value == "nic_pinned") {
        return PlacementMode::kNicPinned;
    }
    if (value == "hint_respect") {
        return PlacementMode::kHintRespect;
    }
    throw std::runtime_error("unknown placement mode: " + value);
}

[[nodiscard]] std::string placement_mode_to_string(PlacementMode mode) {
    switch (mode) {
    case PlacementMode::kHostPinned:
        return "host_pinned";
    case PlacementMode::kNicPinned:
        return "nic_pinned";
    case PlacementMode::kHintRespect:
        return "hint_respect";
    }
    return "unknown";
}

struct Options {
    std::filesystem::path profile_path;
    std::filesystem::path workload_path;
    std::filesystem::path arrival_path;
    std::filesystem::path output_path{"placement_results.json"};
    std::optional<std::filesystem::path> csv_path;
    std::uint64_t seed{1234};
    std::string policy_id{"none"};
    double arrival_scale{1.0};
    PlacementMode placement_mode{PlacementMode::kHintRespect};
    std::vector<std::pair<std::string, std::string>> metadata;
};

struct ArrivalFixture {
    std::string model;
    std::vector<nicloadoff::SimTime> arrivals;
};

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --profile PROFILE.yaml --workload WORKLOAD.yaml --arrival ARRIVAL.yaml [--output results.json] "
                 "[--csv results.csv] [--seed N] [--arrival-scale SCALE] [--placement-mode MODE] "
                 "[--metadata key=value ...]\n";
}

bool parse_args(int argc, char** argv, Options& options) {
    if (argc < 7) {
        print_usage(argv[0]);
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto require_value = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value after ") + flag);
            }
            return argv[++i];
        };
        if (arg == "--profile") {
            options.profile_path = require_value("--profile");
        } else if (arg == "--workload") {
            options.workload_path = require_value("--workload");
        } else if (arg == "--arrival") {
            options.arrival_path = require_value("--arrival");
        } else if (arg == "--output") {
            options.output_path = require_value("--output");
        } else if (arg == "--csv") {
            options.csv_path = require_value("--csv");
        } else if (arg == "--seed") {
            options.seed = std::stoull(require_value("--seed"));
        } else if (arg == "--policy") {
            options.policy_id = require_value("--policy");
        } else if (arg == "--arrival-scale") {
            options.arrival_scale = std::stod(require_value("--arrival-scale"));
        } else if (arg == "--placement-mode") {
            options.placement_mode = placement_mode_from_string(require_value("--placement-mode"));
        } else if (arg == "--metadata") {
            std::string kv = require_value("--metadata");
            const auto pos = kv.find('=');
            if (pos == std::string::npos) {
                throw std::runtime_error("metadata flag expects key=value");
            }
            std::string key = kv.substr(0, pos);
            std::string value = kv.substr(pos + 1);
            options.metadata.emplace_back(std::move(key), std::move(value));
        } else {
            print_usage(argv[0]);
            return false;
        }
    }
    if (options.profile_path.empty() || options.workload_path.empty() || options.arrival_path.empty()) {
        print_usage(argv[0]);
        return false;
    }
    if (options.arrival_scale <= 0.0) {
        throw std::runtime_error("--arrival-scale must be > 0");
    }
    return true;
}

std::vector<nicloadoff::SimTime> generate_poisson(const YAML::Node& windows, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<nicloadoff::SimTime> arrivals;
    nicloadoff::SimTime global_time = 0.0;
    for (const auto& win : windows) {
        const double duration = win["duration_us"].as<double>();
        const double rate = win["rate_per_us"].as<double>();
        if (rate <= 0.0) {
            continue;
        }
        std::exponential_distribution<double> exp(rate);
        double elapsed = 0.0;
        while (elapsed < duration) {
            elapsed += exp(rng);
            if (elapsed <= duration) {
                arrivals.push_back(global_time + elapsed);
            }
        }
        global_time += duration;
    }
    return arrivals;
}

std::vector<nicloadoff::SimTime> generate_periodic(const YAML::Node& periods) {
    std::vector<nicloadoff::SimTime> arrivals;
    double current = 0.0;
    for (const auto& period : periods) {
        current += period.as<double>();
        arrivals.push_back(current);
    }
    return arrivals;
}

ArrivalFixture load_arrival_fixture(const std::filesystem::path& path, std::uint64_t seed) {
    YAML::Node doc = YAML::LoadFile(path.string());
    ArrivalFixture fixture{};
    fixture.model = doc["arrival_model"].as<std::string>();
    if (fixture.model == "poisson_bursty") {
        fixture.arrivals = generate_poisson(doc["windows"], seed);
    } else if (fixture.model == "periodic") {
        fixture.arrivals = generate_periodic(doc["periods_us"]);
    } else {
        throw std::runtime_error("unknown arrival_model in fixture: " + fixture.model);
    }
    return fixture;
}

void scale_arrivals(std::vector<nicloadoff::SimTime>& arrivals, double scale) {
    if (scale == 1.0) {
        return;
    }
    if (scale <= 0.0) {
        throw std::runtime_error("arrival scale must be > 0");
    }
    for (auto& time : arrivals) {
        time /= scale;
    }
}

nicloadoff::WorkloadSpec expand_workload(const nicloadoff::WorkloadSpec& base,
                                         const ArrivalFixture& fixture) {
    if (base.tasks.empty() && base.dag_tasks.empty()) {
        throw std::runtime_error("workload has no tasks to replicate");
    }
    nicloadoff::WorkloadSpec expanded;
    expanded.tasks.reserve(base.tasks.size() * fixture.arrivals.size());
    expanded.dag_tasks.reserve(base.dag_tasks.size() * fixture.arrivals.size());

    const std::uint64_t id_stride = 1'000'000;
    for (std::size_t idx = 0; idx < fixture.arrivals.size(); ++idx) {
        const nicloadoff::SimTime arrival_offset = fixture.arrivals[idx];
        const std::uint64_t base_offset = static_cast<std::uint64_t>(idx) * id_stride;
        for (const auto& task : base.tasks) {
            nicloadoff::TaskSpec copy = task;
            copy.id = task.id + base_offset;
            copy.arrival_time = arrival_offset + task.arrival_time;
            expanded.tasks.push_back(std::move(copy));
        }
        for (const auto& dag : base.dag_tasks) {
            nicloadoff::TaskDAGSpec dag_copy = dag;
            dag_copy.id = dag.id + base_offset;
            dag_copy.arrival_time = arrival_offset + dag.arrival_time;
            expanded.dag_tasks.push_back(std::move(dag_copy));
        }
    }
    return expanded;
}

enum class StagePlacement { kNone, kHost, kNic };

[[nodiscard]] bool allows_placement(const nicloadoff::StageSpec& stage, StagePlacement placement) {
    if (placement == StagePlacement::kNone) {
        return true;
    }
    if (stage.placement_eligible.empty()) {
        return true;
    }
    const std::string target = (placement == StagePlacement::kHost) ? "host" : "nic";
    return std::find(stage.placement_eligible.begin(), stage.placement_eligible.end(), target) !=
           stage.placement_eligible.end();
}

[[nodiscard]] StagePlacement placement_from_hint(const nicloadoff::StageSpec& stage) {
    if (!stage.placement_default) {
        return StagePlacement::kNone;
    }
    const std::string value = to_lower(*stage.placement_default);
    if (value == "host") {
        return StagePlacement::kHost;
    }
    if (value == "nic") {
        return StagePlacement::kNic;
    }
    return StagePlacement::kNone;
}

[[nodiscard]] StagePlacement choose_stage_placement(const nicloadoff::StageSpec& stage, PlacementMode mode) {
    switch (mode) {
    case PlacementMode::kHostPinned:
        return allows_placement(stage, StagePlacement::kHost) ? StagePlacement::kHost : StagePlacement::kNone;
    case PlacementMode::kNicPinned:
        return allows_placement(stage, StagePlacement::kNic) ? StagePlacement::kNic : StagePlacement::kNone;
    case PlacementMode::kHintRespect: {
        StagePlacement hinted = placement_from_hint(stage);
        if (hinted != StagePlacement::kNone && allows_placement(stage, hinted)) {
            return hinted;
        }
        return StagePlacement::kNone;
    }
    }
    return StagePlacement::kNone;
}

nicloadoff::ResourceClass remap_resource(nicloadoff::ResourceClass resource, StagePlacement placement) {
    if (placement == StagePlacement::kHost) {
        switch (resource) {
        case nicloadoff::ResourceClass::kNicCpu:
            return nicloadoff::ResourceClass::kHostCpu;
        case nicloadoff::ResourceClass::kNicDram:
            return nicloadoff::ResourceClass::kHostDram;
        case nicloadoff::ResourceClass::kNicLink:
            return nicloadoff::ResourceClass::kHostLink;
        default:
            break;
        }
    } else if (placement == StagePlacement::kNic) {
        switch (resource) {
        case nicloadoff::ResourceClass::kHostCpu:
            return nicloadoff::ResourceClass::kNicCpu;
        case nicloadoff::ResourceClass::kHostDram:
            return nicloadoff::ResourceClass::kNicDram;
        case nicloadoff::ResourceClass::kHostLink:
            return nicloadoff::ResourceClass::kNicLink;
        default:
            break;
        }
    }
    return resource;
}

void apply_stage_placement(nicloadoff::StageSpec& stage, StagePlacement placement) {
    if (placement == StagePlacement::kNone) {
        return;
    }
    if (stage.service_profile) {
        stage.service_profile->domain =
            (placement == StagePlacement::kNic) ? nicloadoff::ServiceTimeDomain::kNic
                                                : nicloadoff::ServiceTimeDomain::kHost;
    }
    for (auto& demand : stage.demands) {
        demand.resource = remap_resource(demand.resource, placement);
    }
}

void apply_placement_mode(nicloadoff::WorkloadSpec& spec, PlacementMode mode) {
    for (auto& task : spec.tasks) {
        for (auto& stage : task.stages) {
            StagePlacement choice = choose_stage_placement(stage, mode);
            apply_stage_placement(stage, choice);
        }
    }
    for (auto& dag : spec.dag_tasks) {
        for (auto& node : dag.nodes) {
            StagePlacement choice = choose_stage_placement(node.stage, mode);
            apply_stage_placement(node.stage, choice);
        }
    }
}

double compute_throughput(std::size_t task_count, nicloadoff::Duration makespan_us) {
    if (task_count == 0 || makespan_us <= 0.0) {
        return 0.0;
    }
    const double seconds = makespan_us / 1'000'000.0;
    return seconds > 0.0 ? static_cast<double>(task_count) / seconds : 0.0;
}

void write_summary(const std::filesystem::path& output_path,
                   const Options& options,
                   const ArrivalFixture& fixture,
                   const std::string& workload_label,
                   const std::filesystem::path& workload_path,
                   const nicloadoff::RunMetrics& metrics,
                   nicloadoff::Duration makespan_us) {
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + output_path.string());
    }
    const std::size_t task_count = metrics.tasks.size();
    const double throughput = compute_throughput(task_count, makespan_us);
    const auto& latency_stats = metrics.aggregate.latency_stats;

    out << "{\n";
    out << "  \"profile\": \"" << options.profile_path.filename().string() << "\",\n";
    out << "  \"workload\": \"" << workload_label << "\",\n";
    out << "  \"workload_path\": \"" << workload_path.string() << "\",\n";
    out << "  \"arrival_model\": \"" << fixture.model << "\",\n";
    out << "  \"arrival_scale\": " << options.arrival_scale << ",\n";
    out << "  \"placement_mode\": \"" << placement_mode_to_string(options.placement_mode) << "\",\n";
    out << "  \"arrival_count\": " << fixture.arrivals.size() << ",\n";
    out << "  \"completed_tasks\": " << task_count << ",\n";
    out << "  \"makespan_us\": " << makespan_us << ",\n";
    out << "  \"throughput_per_sec\": " << throughput << ",\n";
    out << "  \"total_latency_us\": " << metrics.aggregate.total_latency << ",\n";
    out << "  \"mean_latency_us\": " << latency_stats.mean << ",\n";
    out << "  \"latency_p50_us\": " << latency_stats.p50 << ",\n";
    out << "  \"latency_p95_us\": " << latency_stats.p95 << ",\n";
    out << "  \"latency_p99_us\": " << latency_stats.p99 << ",\n";
    out << "  \"peak_waiting_queue_depth\": " << metrics.aggregate.peak_waiting_queue_depth << ",\n";
    out << "  \"metadata\": {\n";
    for (std::size_t idx = 0; idx < options.metadata.size(); ++idx) {
        const auto& [key, value] = options.metadata[idx];
        out << "    \"" << key << "\": \"" << value << "\"";
        out << (idx + 1 < options.metadata.size() ? ",\n" : "\n");
    }
    out << "  }\n";
    out << "}\n";
}

void append_csv(const std::filesystem::path& csv_path,
                const Options& options,
                const ArrivalFixture& fixture,
                const std::string& workload_label,
                const std::filesystem::path& workload_path,
                const nicloadoff::RunMetrics& metrics,
                nicloadoff::Duration makespan_us) {
    const bool exists = std::filesystem::exists(csv_path);
    std::ofstream out(csv_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to open csv output: " + csv_path.string());
    }
    if (!exists) {
        out << "profile,workload_label,workload_path,arrival_model,arrival_scale,placement_mode,arrival_count,completed_tasks,"
               "makespan_us,throughput_per_sec,mean_latency_us,latency_p50_us,latency_p95_us,latency_p99_us,"
               "total_latency_us,peak_waiting_queue_depth,seed";
        for (const auto& [key, _] : options.metadata) {
            out << "," << key;
        }
        out << "\n";
    }
    const std::size_t task_count = metrics.tasks.size();
    const double throughput = compute_throughput(task_count, makespan_us);
    const auto& latency_stats = metrics.aggregate.latency_stats;

    out << options.profile_path.filename().string() << ","
        << workload_label << ","
        << workload_path.string() << ","
        << fixture.model << ","
        << options.arrival_scale << ","
        << placement_mode_to_string(options.placement_mode) << ","
        << fixture.arrivals.size() << ","
        << task_count << ","
        << makespan_us << ","
        << throughput << ","
        << latency_stats.mean << ","
        << latency_stats.p50 << ","
        << latency_stats.p95 << ","
        << latency_stats.p99 << ","
        << metrics.aggregate.total_latency << ","
        << metrics.aggregate.peak_waiting_queue_depth << ","
        << options.seed;
    for (const auto& [_, value] : options.metadata) {
        out << "," << value;
    }
    out << "\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options;
        if (!parse_args(argc, argv, options)) {
            return 1;
        }

        const nicloadoff::config::Profile profile =
            nicloadoff::config::load_profile_from_file(options.profile_path);
        const nicloadoff::LoadedWorkload loaded =
            nicloadoff::load_workload_from_file(options.workload_path);
        ArrivalFixture fixture = load_arrival_fixture(options.arrival_path, options.seed);
        scale_arrivals(fixture.arrivals, options.arrival_scale);
        nicloadoff::WorkloadSpec expanded = expand_workload(loaded.spec, fixture);
        apply_placement_mode(expanded, options.placement_mode);

        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
        nicloadoff::ServiceTimeModel service_model(profile, options.seed);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool), &service_model);
        nicloadoff::DagSubmissionController dag_controller =
            nicloadoff::DagSubmissionController::from_spec(expanded, inventory.ids);
        const std::vector<nicloadoff::Task> tasks = nicloadoff::make_tasks_from_spec(expanded, inventory.ids);

        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        if (!dag_controller.empty()) {
            dag_controller.submit_initial(scheduler);
        }
        while (scheduler.step_once()) {
            if (!dag_controller.empty()) {
                if (auto last = scheduler.last_event();
                    last && last->metadata.type == nicloadoff::EventType::kTaskComplete) {
                    dag_controller.handle_task_completion(last->metadata.id, scheduler.current_time(), scheduler);
                }
            }
        }

        const nicloadoff::RunMetrics metrics = scheduler.aggregated_metrics();
        const nicloadoff::Duration makespan_us = scheduler.current_time();

        write_summary(options.output_path, options, fixture, loaded.workload_name, options.workload_path, metrics, makespan_us);
        if (options.csv_path) {
            append_csv(*options.csv_path, options, fixture, loaded.workload_name, options.workload_path, metrics, makespan_us);
        }

        std::cout << "Placement benchmark complete. Results written to "
                  << options.output_path << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
