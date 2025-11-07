#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload_loader.hh"

#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path profile_path;
    std::filesystem::path workload_path;
    std::filesystem::path arrival_path;
    std::filesystem::path output_path{"placement_results.json"};
    std::optional<std::filesystem::path> csv_path;
    std::uint64_t seed{1234};
    std::string policy_id{"none"};
    double arrival_scale{1.0};
};

struct ArrivalFixture {
    std::string model;
    std::vector<nicloadoff::SimTime> arrivals;
};

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --profile PROFILE.yaml --workload WORKLOAD.yaml --arrival ARRIVAL.yaml [--output results.json] "
                 "[--csv results.csv] [--seed N] [--arrival-scale SCALE]\n";
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
    if (!base.dag_tasks.empty()) {
        throw std::runtime_error("placement benchmark currently expects task-based workloads only");
    }
    if (base.tasks.empty()) {
        throw std::runtime_error("workload has no tasks to replicate");
    }
    nicloadoff::WorkloadSpec expanded = base;
    expanded.tasks.clear();

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
    }
    return expanded;
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
                   const std::string& workload_name,
                   const nicloadoff::RunMetrics& metrics,
                   nicloadoff::Duration makespan_us) {
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + output_path.string());
    }
    const std::size_t task_count = metrics.tasks.size();
    const double throughput = compute_throughput(task_count, makespan_us);
    const double mean_latency =
        task_count > 0 ? metrics.aggregate.total_latency / static_cast<double>(task_count) : 0.0;

    out << "{\n";
    out << "  \"profile\": \"" << options.profile_path.filename().string() << "\",\n";
    out << "  \"workload\": \"" << workload_name << "\",\n";
    out << "  \"arrival_model\": \"" << fixture.model << "\",\n";
    out << "  \"arrival_scale\": " << options.arrival_scale << ",\n";
    out << "  \"arrival_count\": " << fixture.arrivals.size() << ",\n";
    out << "  \"completed_tasks\": " << task_count << ",\n";
    out << "  \"makespan_us\": " << makespan_us << ",\n";
    out << "  \"throughput_per_sec\": " << throughput << ",\n";
    out << "  \"total_latency_us\": " << metrics.aggregate.total_latency << ",\n";
    out << "  \"mean_latency_us\": " << mean_latency << "\n";
    out << "}\n";
}

void append_csv(const std::filesystem::path& csv_path,
                const Options& options,
                const ArrivalFixture& fixture,
                const std::string& workload_name,
                const nicloadoff::RunMetrics& metrics,
                nicloadoff::Duration makespan_us) {
    const bool exists = std::filesystem::exists(csv_path);
    std::ofstream out(csv_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to open csv output: " + csv_path.string());
    }
    if (!exists) {
        out << "profile,workload,arrival_model,arrival_scale,arrival_count,completed_tasks,makespan_us,"
               "throughput_per_sec,mean_latency_us,total_latency_us,seed\n";
    }
    const std::size_t task_count = metrics.tasks.size();
    const double throughput = compute_throughput(task_count, makespan_us);
    const double mean_latency =
        task_count > 0 ? metrics.aggregate.total_latency / static_cast<double>(task_count) : 0.0;

    out << options.profile_path.filename().string() << ","
        << workload_name << ","
        << fixture.model << ","
        << options.arrival_scale << ","
        << fixture.arrivals.size() << ","
        << task_count << ","
        << makespan_us << ","
        << throughput << ","
        << mean_latency << ","
        << metrics.aggregate.total_latency << ","
        << options.seed << "\n";
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
        const nicloadoff::WorkloadSpec expanded = expand_workload(loaded.spec, fixture);

        auto inventory = nicloadoff::make_resource_inventory_from_profile(profile);
        nicloadoff::ServiceTimeModel service_model(profile, options.seed);
        nicloadoff::BasicScheduler scheduler(std::move(inventory.pool), &service_model);
        const std::vector<nicloadoff::Task> tasks =
            nicloadoff::make_tasks_from_spec(expanded, inventory.ids);

        for (const auto& task : tasks) {
            scheduler.submit_task(task);
        }
        scheduler.run_until_empty();

        const nicloadoff::RunMetrics metrics = scheduler.aggregated_metrics();
        const nicloadoff::Duration makespan_us = scheduler.current_time();

        write_summary(options.output_path, options, fixture, loaded.workload_name, metrics, makespan_us);
        if (options.csv_path) {
            append_csv(*options.csv_path, options, fixture, loaded.workload_name, metrics, makespan_us);
        }

        std::cout << "Placement benchmark complete. Results written to "
                  << options.output_path << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
