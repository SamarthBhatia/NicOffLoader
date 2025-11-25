#include "../runner.hh"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void assert_contains(const std::string& haystack, const std::string& needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        std::fprintf(stderr, "%s (missing: %s)\n", message, needle.c_str());
        std::abort();
    }
}

std::filesystem::path make_output_path() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> dist;
    const auto base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate = base / ("nicloadoff_cli_report_" + std::to_string(dist(rng)) + ".json");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return base / "nicloadoff_cli_report_fallback.json";
}

} // namespace

int main() {
    using namespace nicloadoff::cli;

    const std::filesystem::path source_root = NICLOADOFF_SOURCE_DIR;
    const std::filesystem::path profile = source_root / "profiles" / "bf2_default.yaml";
    const std::filesystem::path workload = source_root / "workloads" / "examples" / "sequential_host.yaml";
    check(std::filesystem::exists(profile), "profile fixture missing");
    check(std::filesystem::exists(workload), "workload fixture missing");

    CliOptions options;
    options.profile_path = profile;
    options.workload_path = workload;
    options.output_path = make_output_path();
    options.seed = 1;
    options.host_mode = nicloadoff::ServiceTimeMode::kDeterministic;
    options.nic_mode = nicloadoff::ServiceTimeMode::kDeterministic;
    options.rolling_queue_window_us = 5.0;
    options.rolling_util_window_us = 5.0;
    options.rolling_sojourn_window_tasks = 1;

    const RunSummary summary = run_simulation(options);
    check(summary.completed_tasks == 2, "expected two completed tasks");
    check(summary.metrics.aggregate.latency_stats.count == 2, "expected two latency samples");
    check(summary.metrics.tasks.size() == 2, "expected two task metric entries");
    check(summary.rolling_metrics.waiting_queue_depth.samples > 0, "expected rolling metrics to record queue depth");
    check(summary.rolling_metrics.sojourn.samples == 1, "expected rolling sojourn window override to apply");

    std::ifstream input(options.output_path);
    check(static_cast<bool>(input), "expected report file to open");
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    assert_contains(content, "\"profile\": {", "expected profile section");
    assert_contains(content, "\"workload\": {", "expected workload section");
    assert_contains(content, "\"metadata\": {", "expected metadata section");
    assert_contains(content, "\"run\": {", "expected run section");
    assert_contains(content, "\"aggregates\": {", "expected aggregates section");
    assert_contains(content, "\"rolling_metrics\": {", "expected rolling metrics section");
    assert_contains(content, "\"rolling_window_events\": [", "expected rolling window events section");
    assert_contains(content, "\"tasks\": [", "expected task list section");
    assert_contains(content, "\"completed_tasks\": 2", "expected completed task count");
    assert_contains(content, "\"throughput_tasks_per_sec\": 400000.000000", "expected throughput value");

    const std::filesystem::path dag_workload = source_root / "workloads" / "examples" / "skew_dag.yaml";
    check(std::filesystem::exists(dag_workload), "dag workload fixture missing");

    std::error_code ec;
    std::filesystem::remove(options.output_path, ec);

    CliOptions dag_options = options;
    dag_options.workload_path = dag_workload;
    dag_options.output_path = make_output_path();

    const RunSummary dag_summary = run_simulation(dag_options);
    check(dag_summary.completed_tasks == 5, "expected five DAG node completions");
    check(dag_summary.metrics.tasks.size() == 5, "expected five DAG task metrics");
    std::filesystem::remove(dag_options.output_path, ec);

    options.output_path = make_output_path();
    options.policy_id = "descending-id";
    const RunSummary policy_summary = run_simulation(options);
    check(policy_summary.completed_tasks == summary.completed_tasks,
          "policy-enabled run should complete same number of tasks");
    std::filesystem::remove(options.output_path, ec);

    const std::filesystem::path manifest_path = [&]() {
        auto path = make_output_path();
        return path.replace_extension(".yaml");
    }();
    const std::filesystem::path manifest_output = make_output_path();
    {
        std::ofstream manifest(manifest_path);
        check(static_cast<bool>(manifest), "failed to open manifest output");
        manifest << "profile: " << profile.string() << "\n";
        manifest << "workload: " << workload.string() << "\n";
        manifest << "output: " << manifest_output.string() << "\n";
        manifest << "policy: prefer-nic\n";
        manifest << "seed: 7\n";
        manifest << "rolling_queue_window_us: 120000\n";
        manifest << "rolling_util_window_us: 90000\n";
        manifest << "rolling_sojourn_window_tasks: 2\n";
        manifest << "service_modes:\n";
        manifest << "  host: deterministic\n";
        manifest << "  nic: stochastic\n";
        manifest << "metadata:\n";
        manifest << "  scenario: smoke\n";
        manifest << "  arrival_label: deterministic\n";
    }

    std::vector<std::string> argv_storage = {"nicloadoff_cli", "--config", manifest_path.string()};
    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size());
    for (auto& arg : argv_storage) {
        argv_ptrs.push_back(arg.data());
    }

    CliOptions manifest_options;
    std::string manifest_error;
    if (!parse_arguments(static_cast<int>(argv_ptrs.size()), argv_ptrs.data(), manifest_options, manifest_error)) {
        std::fprintf(stderr, "manifest parse failed: %s\n", manifest_error.c_str());
        std::abort();
    }

    check(manifest_options.profile_path == profile, "manifest should set profile path");
    check(manifest_options.workload_path == workload, "manifest should set workload path");
    check(manifest_options.output_path == manifest_output, "manifest should set output path");
    check(manifest_options.policy_id == "prefer-nic", "manifest should set policy id");
    check(manifest_options.seed == 7, "manifest should set seed");
    check(manifest_options.nic_mode == nicloadoff::ServiceTimeMode::kStochastic,
          "manifest should set NIC service mode");
    check(manifest_options.rolling_queue_window_us == 120000.0, "manifest should set rolling queue window");
    check(manifest_options.rolling_sojourn_window_tasks == 2, "manifest should set rolling sojourn window");
    check(manifest_options.metadata.at("scenario") == "smoke", "manifest should set scenario metadata");
    check(manifest_options.metadata.at("arrival_label") == "deterministic", "manifest should set arrival metadata");

    const RunSummary manifest_summary = run_simulation(manifest_options);
    check(manifest_summary.completed_tasks == summary.completed_tasks,
          "manifest-driven run should complete same number of tasks");
    std::ifstream manifest_report(manifest_output);
    check(static_cast<bool>(manifest_report), "expected manifest-driven report to open");
    std::string manifest_content((std::istreambuf_iterator<char>(manifest_report)), std::istreambuf_iterator<char>());
    assert_contains(manifest_content, "\"scenario\": \"smoke\"", "manifest metadata missing scenario key");
    assert_contains(manifest_content, "\"arrival_label\": \"deterministic\"", "manifest metadata missing arrival_label");

    std::filesystem::remove(manifest_output, ec);
    std::filesystem::remove(manifest_path, ec);

    const std::filesystem::path batch_manifest = [&]() {
        auto path = make_output_path();
        return path.replace_extension(".yaml");
    }();
    const std::filesystem::path batch_output_dir = batch_manifest.parent_path() / "batch_outputs";
    const std::filesystem::path batch_csv = batch_manifest.parent_path() / "batch_results.csv";
    {
        std::ofstream manifest(batch_manifest);
        check(static_cast<bool>(manifest), "failed to open batch manifest");
        manifest << "metadata_keys:\n";
        manifest << "  - arrival_label\n";
        manifest << "  - scenario\n";
        manifest << "defaults:\n";
        manifest << "  profile: " << profile.string() << "\n";
        manifest << "  workload: " << workload.string() << "\n";
        manifest << "  output_dir: " << batch_output_dir.string() << "\n";
        manifest << "  metadata:\n";
        manifest << "    arrival_label: batch-default\n";
        manifest << "    scenario: regression\n";
        manifest << "csv: " << batch_csv.string() << "\n";
        manifest << "runs:\n";
        manifest << "  - name: prefer-host\n";
        manifest << "    policy: prefer-host\n";
        manifest << "  - name: prefer-nic\n";
        manifest << "    policy: prefer-nic\n";
        manifest << "    seed: 9\n";
    }

    const auto batch_results = run_batch_manifest(batch_manifest);
    check(batch_results.size() == 2, "expected two batch runs");
    check(std::filesystem::exists(batch_csv), "expected batch csv to exist");

    std::ifstream batch_csv_in(batch_csv);
    std::string batch_csv_content((std::istreambuf_iterator<char>(batch_csv_in)), std::istreambuf_iterator<char>());
    assert_contains(batch_csv_content, "prefer-host", "batch csv missing first run");
    assert_contains(batch_csv_content, "prefer-nic", "batch csv missing second run");
    assert_contains(batch_csv_content, "rolling_queue_average", "batch csv missing rolling metrics column");
    assert_contains(batch_csv_content, "arrival_label", "batch csv missing metadata header");
    assert_contains(batch_csv_content, "batch-default", "batch csv missing metadata value");

    for (const auto& run : batch_results) {
        check(std::filesystem::exists(run.options.output_path), "expected batch output json");
        std::filesystem::remove(run.options.output_path, ec);
    }
    std::filesystem::remove(batch_csv, ec);
    std::filesystem::remove(batch_manifest, ec);
    std::filesystem::remove_all(batch_output_dir, ec);

    return 0;
}
