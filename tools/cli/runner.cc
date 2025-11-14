#include "runner.hh"

#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/dag_submission_controller.hh"
#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"
#include "nicloadoff/workload_loader.hh"
#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include <cctype>
#include <map>

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
    for (auto& dag : spec.dag_tasks) {
        for (auto& node : dag.nodes) {
            auto& stage = node.stage;
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

struct BatchDefaults {
    std::optional<std::filesystem::path> profile;
    std::optional<std::filesystem::path> workload;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> output_dir;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> policy_id;
    std::optional<ServiceTimeMode> host_mode;
    std::optional<ServiceTimeMode> nic_mode;
    std::map<std::string, std::string> metadata;
    struct RollingOverrides {
        std::optional<double> queue_window_us;
        std::optional<double> util_window_us;
        std::optional<std::size_t> sojourn_window_tasks;
    } rolling_windows;
};

struct BatchRunConfig {
    std::string name;
    CliOptions options;
    std::map<std::string, std::string> metadata;
};

struct BatchManifestData {
    std::filesystem::path base_dir;
    std::optional<std::filesystem::path> csv_path;
    BatchDefaults defaults;
    std::vector<BatchRunConfig> runs;
    std::vector<std::string> metadata_keys;
};

std::filesystem::path resolve_relative_path(const std::filesystem::path& base, const std::string& value) {
    std::filesystem::path path = value;
    if (!path.is_absolute() && !base.empty()) {
        path = base / path;
    }
    return path.lexically_normal();
}

std::optional<std::filesystem::path> read_optional_path(const YAML::Node& node,
                                                        const char* key,
                                                        const std::filesystem::path& base_dir) {
    const YAML::Node child = node[key];
    if (!child) {
        return std::nullopt;
    }
    if (!child.IsScalar()) {
        std::ostringstream oss;
        oss << "batch manifest field '" << key << "' must be a string";
        throw std::runtime_error(oss.str());
    }
    return resolve_relative_path(base_dir, child.as<std::string>());
}

std::optional<std::string> read_optional_string(const YAML::Node& node, const char* key) {
    const YAML::Node child = node[key];
    if (!child) {
        return std::nullopt;
    }
    if (!child.IsScalar()) {
        std::ostringstream oss;
        oss << "batch manifest field '" << key << "' must be a string";
        throw std::runtime_error(oss.str());
    }
    return child.as<std::string>();
}

std::optional<std::uint64_t> read_optional_seed(const YAML::Node& node, const char* key) {
    const YAML::Node child = node[key];
    if (!child) {
        return std::nullopt;
    }
    if (!child.IsScalar()) {
        std::ostringstream oss;
        oss << "batch manifest field '" << key << "' must be numeric";
        throw std::runtime_error(oss.str());
    }
    try {
        return child.as<std::uint64_t>();
    } catch (const std::exception&) {
        std::ostringstream oss;
        oss << "batch manifest field '" << key << "' must be numeric";
        throw std::runtime_error(oss.str());
    }
}

ServiceTimeMode parse_mode_or_throw(const std::string& value, const std::string& context) {
    auto mode = parse_mode(value);
    if (!mode) {
        std::ostringstream oss;
        oss << context << " has unknown service mode '" << value << "'";
        throw std::runtime_error(oss.str());
    }
    return *mode;
}

void apply_service_modes_override(const YAML::Node& node, CliOptions& options, const std::string& context) {
    if (!node) {
        return;
    }
    if (!node.IsMap()) {
        std::ostringstream oss;
        oss << context << " service_modes must be a mapping";
        throw std::runtime_error(oss.str());
    }
    if (const YAML::Node host = node["host"]) {
        if (!host.IsScalar()) {
            throw std::runtime_error(context + " service_modes.host must be a string");
        }
        options.host_mode = parse_mode_or_throw(host.as<std::string>(), context + " service_modes.host");
    }
    if (const YAML::Node nic = node["nic"]) {
        if (!nic.IsScalar()) {
            throw std::runtime_error(context + " service_modes.nic must be a string");
        }
        options.nic_mode = parse_mode_or_throw(nic.as<std::string>(), context + " service_modes.nic");
    }
}

std::map<std::string, std::string> parse_metadata_map(const YAML::Node& node, const std::string& context) {
    std::map<std::string, std::string> metadata;
    if (!node) {
        return metadata;
    }
    if (!node.IsMap()) {
        std::ostringstream oss;
        oss << context << " metadata must be a mapping";
        throw std::runtime_error(oss.str());
    }
    for (const auto& entry : node) {
        if (!entry.first.IsScalar() || !entry.second.IsScalar()) {
            std::ostringstream oss;
            oss << context << " metadata keys/values must be strings";
            throw std::runtime_error(oss.str());
        }
        metadata[entry.first.as<std::string>()] = entry.second.as<std::string>();
    }
    return metadata;
}

BatchDefaults::RollingOverrides parse_rolling_windows(const YAML::Node& node, const std::string& context) {
    BatchDefaults::RollingOverrides overrides;
    if (!node) {
        return overrides;
    }
    if (!node.IsMap()) {
        std::ostringstream oss;
        oss << context << " must be a mapping";
        throw std::runtime_error(oss.str());
    }
    auto parse_positive_double = [&](const YAML::Node& value_node, const char* field) -> double {
        if (!value_node.IsScalar()) {
            std::ostringstream oss;
            oss << context << "." << field << " must be numeric";
            throw std::runtime_error(oss.str());
        }
        double value = value_node.as<double>();
        if (value <= 0.0) {
            std::ostringstream oss;
            oss << context << "." << field << " must be > 0";
            throw std::runtime_error(oss.str());
        }
        return value;
    };
    if (const YAML::Node queue = node["queue_us"]) {
        overrides.queue_window_us = parse_positive_double(queue, "queue_us");
    }
    if (const YAML::Node util = node["util_us"]) {
        overrides.util_window_us = parse_positive_double(util, "util_us");
    }
    if (const YAML::Node sojourn = node["sojourn_tasks"]) {
        if (!sojourn.IsScalar()) {
            std::ostringstream oss;
            oss << context << ".sojourn_tasks must be numeric";
            throw std::runtime_error(oss.str());
        }
        const std::size_t tasks = sojourn.as<std::size_t>();
        if (tasks == 0) {
            std::ostringstream oss;
            oss << context << ".sojourn_tasks must be > 0";
            throw std::runtime_error(oss.str());
        }
        overrides.sojourn_window_tasks = tasks;
    }
    return overrides;
}

BatchDefaults parse_batch_defaults(const YAML::Node& node,
                                   const std::filesystem::path& base_dir,
                                   std::set<std::string>& metadata_keys) {
    BatchDefaults defaults;
    if (!node) {
        return defaults;
    }
    if (!node.IsMap()) {
        throw std::runtime_error("batch manifest defaults must be a mapping");
    }
    if (auto profile = read_optional_path(node, "profile", base_dir)) {
        defaults.profile = std::move(profile);
    }
    if (auto workload = read_optional_path(node, "workload", base_dir)) {
        defaults.workload = std::move(workload);
    }
    if (auto output = read_optional_path(node, "output", base_dir)) {
        defaults.output = std::move(output);
    }
    if (auto output_dir = read_optional_path(node, "output_dir", base_dir)) {
        defaults.output_dir = std::move(output_dir);
    }
    if (auto seed = read_optional_seed(node, "seed")) {
        defaults.seed = seed;
    }
    if (auto policy = read_optional_string(node, "policy")) {
        if (!policy::is_policy_supported(*policy)) {
            throw std::runtime_error("batch manifest defaults policy '" + *policy + "' is not recognised");
        }
        defaults.policy_id = std::move(policy);
    }
    if (const YAML::Node service_modes = node["service_modes"]) {
        if (!service_modes.IsMap()) {
            throw std::runtime_error("batch manifest defaults.service_modes must be a mapping");
        }
        if (const YAML::Node host = service_modes["host"]) {
            if (!host.IsScalar()) {
                throw std::runtime_error("batch manifest defaults.service_modes.host must be a string");
            }
            defaults.host_mode = parse_mode_or_throw(host.as<std::string>(), "defaults.service_modes.host");
        }
        if (const YAML::Node nic = service_modes["nic"]) {
            if (!nic.IsScalar()) {
                throw std::runtime_error("batch manifest defaults.service_modes.nic must be a string");
            }
            defaults.nic_mode = parse_mode_or_throw(nic.as<std::string>(), "defaults.service_modes.nic");
        }
    }
    if (const YAML::Node metadata = node["metadata"]) {
        defaults.metadata = parse_metadata_map(metadata, "defaults");
        for (const auto& [key, _] : defaults.metadata) {
            metadata_keys.insert(key);
        }
    }
    defaults.rolling_windows = parse_rolling_windows(node["rolling_windows"], "defaults.rolling_windows");
    return defaults;
}

CliOptions make_base_cli_options(const BatchDefaults& defaults) {
    CliOptions options;
    options.output_path = "run_metrics.json";
    options.seed = defaults.seed.value_or(options.seed);
    options.host_mode = defaults.host_mode.value_or(options.host_mode);
    options.nic_mode = defaults.nic_mode.value_or(options.nic_mode);
    if (defaults.policy_id) {
        options.policy_id = *defaults.policy_id;
    }
    if (defaults.rolling_windows.queue_window_us) {
        options.rolling_queue_window_us = *defaults.rolling_windows.queue_window_us;
    }
    if (defaults.rolling_windows.util_window_us) {
        options.rolling_util_window_us = *defaults.rolling_windows.util_window_us;
    }
    if (defaults.rolling_windows.sojourn_window_tasks) {
        options.rolling_sojourn_window_tasks = *defaults.rolling_windows.sojourn_window_tasks;
    }
    options.batch_mode = false;
    options.batch_manifest_path.clear();
    options.profile_path.clear();
    options.workload_path.clear();
    return options;
}

std::filesystem::path compute_output_path(const YAML::Node& run_node,
                                          const BatchDefaults& defaults,
                                          const std::filesystem::path& base_dir,
                                          const std::string& run_name) {
    if (auto explicit_output = read_optional_path(run_node, "output", base_dir)) {
        return *explicit_output;
    }
    if (defaults.output) {
        return *defaults.output;
    }
    std::filesystem::path target_dir;
    if (auto run_dir = read_optional_path(run_node, "output_dir", base_dir)) {
        target_dir = *run_dir;
    } else if (defaults.output_dir) {
        target_dir = *defaults.output_dir;
    } else if (!base_dir.empty()) {
        target_dir = base_dir;
    }
    if (target_dir.empty()) {
        return std::filesystem::path(run_name + ".json");
    }
    return (target_dir / (run_name + ".json")).lexically_normal();
}

std::string ensure_run_name(const YAML::Node& run_node, std::size_t index) {
    if (const YAML::Node name = run_node["name"]) {
        if (!name.IsScalar()) {
            throw std::runtime_error("batch run name must be a string");
        }
        return name.as<std::string>();
    }
    std::ostringstream oss;
    oss << "run_" << index;
    return oss.str();
}

BatchRunConfig parse_batch_run(const YAML::Node& run_node,
                               const BatchDefaults& defaults,
                               const std::filesystem::path& base_dir,
                               std::size_t index,
                               std::set<std::string>& metadata_keys) {
    if (!run_node.IsMap()) {
        std::ostringstream oss;
        oss << "batch manifest runs[" << index << "] must be a mapping";
        throw std::runtime_error(oss.str());
    }

    BatchRunConfig config;
    config.name = ensure_run_name(run_node, index);
    config.options = make_base_cli_options(defaults);

    auto profile = read_optional_path(run_node, "profile", base_dir);
    if (!profile && defaults.profile) {
        profile = defaults.profile;
    }
    if (!profile) {
        throw std::runtime_error("batch run '" + config.name + "' is missing a profile path");
    }
    config.options.profile_path = *profile;

    auto workload = read_optional_path(run_node, "workload", base_dir);
    if (!workload && defaults.workload) {
        workload = defaults.workload;
    }
    if (!workload) {
        throw std::runtime_error("batch run '" + config.name + "' is missing a workload path");
    }
    config.options.workload_path = *workload;

    config.options.output_path = compute_output_path(run_node, defaults, base_dir, config.name);

    if (auto seed = read_optional_seed(run_node, "seed")) {
        config.options.seed = *seed;
    }
    if (auto policy = read_optional_string(run_node, "policy")) {
        if (!policy::is_policy_supported(*policy)) {
            throw std::runtime_error("batch run '" + config.name + "' has unknown policy '" + *policy + "'");
        }
        config.options.policy_id = *policy;
    }
    apply_service_modes_override(run_node["service_modes"], config.options,
                                 "batch run '" + config.name + "'");

    const auto rolling_overrides = parse_rolling_windows(run_node["rolling_windows"],
                                                         "batch run '" + config.name + "'.rolling_windows");
    if (rolling_overrides.queue_window_us) {
        config.options.rolling_queue_window_us = *rolling_overrides.queue_window_us;
    }
    if (rolling_overrides.util_window_us) {
        config.options.rolling_util_window_us = *rolling_overrides.util_window_us;
    }
    if (rolling_overrides.sojourn_window_tasks) {
        config.options.rolling_sojourn_window_tasks = *rolling_overrides.sojourn_window_tasks;
    }

    std::map<std::string, std::string> metadata = defaults.metadata;
    if (const YAML::Node metadata_node = run_node["metadata"]) {
        auto override = parse_metadata_map(metadata_node, "batch run '" + config.name + "'");
        for (const auto& [key, value] : override) {
            metadata[key] = value;
        }
    }
    for (const auto& [key, _] : metadata) {
        metadata_keys.insert(key);
    }
    config.metadata = metadata;
    config.options.metadata = std::move(metadata);

    return config;
}

BatchManifestData parse_batch_manifest_data(const std::filesystem::path& manifest_path) {
    YAML::Node root = YAML::LoadFile(manifest_path.string());
    if (!root || !root.IsMap()) {
        throw std::runtime_error("batch manifest must be a YAML mapping");
    }

    BatchManifestData manifest;
    manifest.base_dir = manifest_path.parent_path();
    std::set<std::string> metadata_keys;
    if (const YAML::Node csv = root["csv"]) {
        if (!csv.IsScalar()) {
            throw std::runtime_error("batch manifest 'csv' field must be a string");
        }
        manifest.csv_path = resolve_relative_path(manifest.base_dir, csv.as<std::string>());
    }

    manifest.defaults = parse_batch_defaults(root["defaults"], manifest.base_dir, metadata_keys);

    const YAML::Node runs = root["runs"];
    if (!runs || !runs.IsSequence() || runs.size() == 0) {
        throw std::runtime_error("batch manifest must provide a non-empty 'runs' list");
    }

    manifest.runs.reserve(runs.size());
    for (std::size_t idx = 0; idx < runs.size(); ++idx) {
        manifest.runs.push_back(parse_batch_run(runs[idx], manifest.defaults, manifest.base_dir, idx, metadata_keys));
    }
    manifest.metadata_keys.assign(metadata_keys.begin(), metadata_keys.end());
    return manifest;
}

SimTime min_arrival_time(const WorkloadSpec& spec) {
    bool found = false;
    SimTime min_time = 0.0;
    for (const auto& task : spec.tasks) {
        if (!found || task.arrival_time < min_time) {
            min_time = task.arrival_time;
            found = true;
        }
    }
    for (const auto& dag : spec.dag_tasks) {
        if (!found || dag.arrival_time < min_time) {
            min_time = dag.arrival_time;
            found = true;
        }
    }
    return found ? min_time : 0.0;
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
    std::optional<double> rolling_queue_window_us;
    std::optional<double> rolling_util_window_us;
    std::optional<std::size_t> rolling_sojourn_window_tasks;
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
        } else if (key == "rolling_queue_window_us") {
            if (value.empty()) {
                error = "manifest field 'rolling_queue_window_us' requires a value";
                return false;
            }
            try {
                double parsed = std::stod(value);
                if (parsed <= 0.0) {
                    error = "manifest field 'rolling_queue_window_us' must be > 0";
                    return false;
                }
                manifest.rolling_queue_window_us = parsed;
            } catch (const std::exception&) {
                error = "manifest field 'rolling_queue_window_us' must be numeric";
                return false;
            }
        } else if (key == "rolling_util_window_us") {
            if (value.empty()) {
                error = "manifest field 'rolling_util_window_us' requires a value";
                return false;
            }
            try {
                double parsed = std::stod(value);
                if (parsed <= 0.0) {
                    error = "manifest field 'rolling_util_window_us' must be > 0";
                    return false;
                }
                manifest.rolling_util_window_us = parsed;
            } catch (const std::exception&) {
                error = "manifest field 'rolling_util_window_us' must be numeric";
                return false;
            }
        } else if (key == "rolling_sojourn_window_tasks") {
            if (value.empty()) {
                error = "manifest field 'rolling_sojourn_window_tasks' requires a value";
                return false;
            }
            try {
                std::size_t parsed = static_cast<std::size_t>(std::stoull(value));
                if (parsed == 0) {
                    error = "manifest field 'rolling_sojourn_window_tasks' must be > 0";
                    return false;
                }
                manifest.rolling_sojourn_window_tasks = parsed;
            } catch (const std::exception&) {
                error = "manifest field 'rolling_sojourn_window_tasks' must be numeric";
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
                  const PolicyRollingMetrics& rolling_metrics,
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
    out << "  \"policy_metrics\": {\n";
    out << "    \"waiting_reorders\": " << run_metrics.policy.waiting_reorders << ",\n";
    out << "    \"waiting_reorders_per_task\": " << format_double(run_metrics.policy.waiting_reorders_per_task, 6) << ",\n";
    out << "    \"waiting_reorders_recent\": " << run_metrics.policy.waiting_reorders_recent << ",\n";
    out << "    \"waiting_reorder_recent_task_count\": " << run_metrics.policy.waiting_reorder_recent_task_count << ",\n";
    out << "    \"waiting_reorders_per_task_recent\": "
        << format_double(run_metrics.policy.waiting_reorders_per_task_recent, 6) << "\n";
    out << "  },\n";
    out << "  \"rolling_metrics\": {\n";
    out << "    \"waiting_queue_depth\": {\n";
    out << "      \"samples\": " << rolling_metrics.waiting_queue_depth.samples << ",\n";
    out << "      \"latest\": " << format_double(rolling_metrics.waiting_queue_depth.latest, 6) << ",\n";
    out << "      \"average\": " << format_double(rolling_metrics.waiting_queue_depth.average, 6) << ",\n";
    out << "      \"peak\": " << format_double(rolling_metrics.waiting_queue_depth.peak, 6) << "\n";
    out << "    },\n";
    out << "    \"host_utilization\": {\n";
    out << "      \"samples\": " << rolling_metrics.host_utilization.samples << ",\n";
    out << "      \"latest\": " << format_double(rolling_metrics.host_utilization.latest, 6) << ",\n";
    out << "      \"average\": " << format_double(rolling_metrics.host_utilization.average, 6) << ",\n";
    out << "      \"peak\": " << format_double(rolling_metrics.host_utilization.peak, 6) << "\n";
    out << "    },\n";
    out << "    \"nic_utilization\": {\n";
    out << "      \"samples\": " << rolling_metrics.nic_utilization.samples << ",\n";
    out << "      \"latest\": " << format_double(rolling_metrics.nic_utilization.latest, 6) << ",\n";
    out << "      \"average\": " << format_double(rolling_metrics.nic_utilization.average, 6) << ",\n";
    out << "      \"peak\": " << format_double(rolling_metrics.nic_utilization.peak, 6) << "\n";
    out << "    },\n";
    out << "    \"sojourn\": {\n";
    out << "      \"samples\": " << rolling_metrics.sojourn.samples << ",\n";
    out << "      \"mean_queue_time_us\": " << format_double(rolling_metrics.sojourn.mean_queue_time, 6) << ",\n";
    out << "      \"mean_service_time_us\": " << format_double(rolling_metrics.sojourn.mean_service_time, 6) << ",\n";
    out << "      \"mean_latency_us\": " << format_double(rolling_metrics.sojourn.mean_latency, 6) << ",\n";
    out << "      \"p95_latency_us\": " << format_double(rolling_metrics.sojourn.p95_latency, 6) << ",\n";
    out << "      \"p99_latency_us\": " << format_double(rolling_metrics.sojourn.p99_latency, 6) << "\n";
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
        << "  --batch <path>           YAML batch manifest describing multiple runs (incompatible with other options)\n"
        << "  --seed <value>           RNG seed for stochastic service times (default: 1)\n"
        << "  --host-mode <mode>       Host service mode: deterministic|stochastic (default: deterministic)\n"
        << "  --nic-mode <mode>        NIC service mode: deterministic|stochastic (default: deterministic)\n"
        << "  --policy <id>            Policy hook to register (options: none, descending-id, limit-active-1, prefer-host, prefer-nic, prefer-adaptive)\n"
        << "  --rolling-queue-window-us <value>    Horizon (microseconds) for rolling queue depth averaging (default "
        << BasicScheduler::kRollingQueueWindowUs << ")\n"
        << "  --rolling-util-window-us <value>     Horizon (microseconds) for rolling utilization averaging (default "
        << BasicScheduler::kRollingUtilizationWindowUs << ")\n"
        << "  --rolling-sojourn-window-tasks <N>   Number of recent tasks tracked in rolling sojourn stats (default "
        << BasicScheduler::kRollingSojournWindowTasks << ")\n"
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
    bool rolling_queue_cli = false;
    bool rolling_util_cli = false;
    bool rolling_sojourn_cli = false;

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
        } else if (arg == "--rolling-queue-window-us") {
            if (i + 1 >= argc) {
                error = "--rolling-queue-window-us requires a numeric argument";
                return false;
            }
            try {
                options.rolling_queue_window_us = std::stod(argv[++i]);
            } catch (const std::exception&) {
                error = "invalid numeric value for --rolling-queue-window-us";
                return false;
            }
            if (options.rolling_queue_window_us <= 0.0) {
                error = "--rolling-queue-window-us must be > 0";
                return false;
            }
            rolling_queue_cli = true;
        } else if (arg == "--rolling-util-window-us") {
            if (i + 1 >= argc) {
                error = "--rolling-util-window-us requires a numeric argument";
                return false;
            }
            try {
                options.rolling_util_window_us = std::stod(argv[++i]);
            } catch (const std::exception&) {
                error = "invalid numeric value for --rolling-util-window-us";
                return false;
            }
            if (options.rolling_util_window_us <= 0.0) {
                error = "--rolling-util-window-us must be > 0";
                return false;
            }
            rolling_util_cli = true;
        } else if (arg == "--rolling-sojourn-window-tasks") {
            if (i + 1 >= argc) {
                error = "--rolling-sojourn-window-tasks requires a numeric argument";
                return false;
            }
            try {
                options.rolling_sojourn_window_tasks = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                error = "invalid numeric value for --rolling-sojourn-window-tasks";
                return false;
            }
            if (options.rolling_sojourn_window_tasks == 0) {
                error = "--rolling-sojourn-window-tasks must be > 0";
                return false;
            }
            rolling_sojourn_cli = true;
        } else if (arg == "--batch") {
            if (i + 1 >= argc) {
                error = "--batch requires a path argument";
                return false;
            }
            if (options.batch_mode) {
                error = "--batch specified multiple times";
                return false;
            }
            options.batch_mode = true;
            options.batch_manifest_path = argv[++i];
        } else {
            error = "unrecognised argument: " + arg;
            return false;
        }
    }

    if (options.batch_mode) {
        if (profile_cli || workload_cli || output_cli || seed_cli || host_cli || nic_cli || policy_cli || manifest_cli) {
            error = "--batch cannot be combined with other CLI options";
            return false;
        }
        if (options.batch_manifest_path.empty()) {
            error = "--batch requires a manifest path";
            return false;
        }
        return true;
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
        if (!rolling_queue_cli && manifest_options.rolling_queue_window_us) {
            options.rolling_queue_window_us = *manifest_options.rolling_queue_window_us;
        }
        if (!rolling_util_cli && manifest_options.rolling_util_window_us) {
            options.rolling_util_window_us = *manifest_options.rolling_util_window_us;
        }
        if (!rolling_sojourn_cli && manifest_options.rolling_sojourn_window_tasks) {
            options.rolling_sojourn_window_tasks = *manifest_options.rolling_sojourn_window_tasks;
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
    DagSubmissionController dag_controller = DagSubmissionController::from_spec(workload, inventory.ids);
    const auto tasks = make_tasks_from_spec(workload, inventory.ids);
    BasicScheduler::RollingWindowConfig rolling_config;
    rolling_config.queue_window_us = options.rolling_queue_window_us;
    rolling_config.utilization_window_us = options.rolling_util_window_us;
    rolling_config.sojourn_window_tasks = options.rolling_sojourn_window_tasks;
    BasicScheduler scheduler(std::move(inventory.pool), &service_model, rolling_config);
    scheduler.set_policy_metadata(options.metadata);

    std::unique_ptr<policy::PolicyHook> policy_hook = policy::make_policy_hook(options.policy_id);
    if (policy_hook) {
        scheduler.set_policy_hook(policy_hook.get());
    }

    for (const auto& task : tasks) {
        scheduler.submit_task(task);
    }
    if (!dag_controller.empty()) {
        dag_controller.submit_initial(scheduler);
    }

    while (scheduler.step_once()) {
        if (auto last = scheduler.last_event(); last && last->metadata.type == EventType::kTaskComplete) {
            dag_controller.handle_task_completion(last->metadata.id, scheduler.current_time(), scheduler);
        }
    }

    const RunMetrics run_metrics = scheduler.aggregated_metrics();
    const PolicyRollingMetrics rolling_metrics = scheduler.rolling_metrics_snapshot();
    const SimTime finish_time = scheduler.current_time();
    const SimTime start_time = min_arrival_time(workload);
    Duration makespan = finish_time - start_time;
    if (makespan < 0.0) {
        makespan = 0.0;
    }

    const std::size_t task_count = run_metrics.tasks.size();
    const double throughput = compute_throughput_per_second(task_count, makespan);

    write_report(options,
                 profile,
                 workload_doc,
                 run_metrics,
                 rolling_metrics,
                 start_time,
                 finish_time,
                 makespan,
                 scheduler.events_processed());

    return RunSummary{
        .completed_tasks = task_count,
        .makespan_us = makespan,
        .throughput_per_sec = throughput,
        .metrics = run_metrics,
        .rolling_metrics = rolling_metrics,
    };
}

namespace {

void write_batch_csv_header(std::ofstream& out, const std::vector<std::string>& metadata_keys) {
    out << "run_name,profile,workload,policy,seed,host_mode,nic_mode,completed_tasks,makespan_us,"
           "throughput_per_sec,mean_latency_us,p95_latency_us,p99_latency_us,peak_waiting_queue_depth,"
           "waiting_reorders,waiting_reorders_per_task,waiting_reorders_recent,waiting_reorder_recent_task_count,"
           "waiting_reorders_per_task_recent,output_path,"
           "rolling_queue_samples,rolling_queue_latest,rolling_queue_average,rolling_queue_peak,"
           "rolling_host_util_samples,rolling_host_util_latest,rolling_host_util_average,rolling_host_util_peak,"
           "rolling_nic_util_samples,rolling_nic_util_latest,rolling_nic_util_average,rolling_nic_util_peak,"
           "rolling_sojourn_samples,rolling_sojourn_mean_queue_us,rolling_sojourn_mean_service_us,"
           "rolling_sojourn_mean_latency_us,rolling_sojourn_p95_latency_us,rolling_sojourn_p99_latency_us";
    for (const auto& key : metadata_keys) {
        out << "," << key;
    }
    out << "\n";
}

void append_batch_csv_row(std::ofstream& out,
                          const BatchRunSummary& result,
                          const std::vector<std::string>& metadata_keys) {
    const auto& aggregate = result.summary.metrics.aggregate;
    const auto& policy_metrics = result.summary.metrics.policy;
    const auto& rolling = result.summary.rolling_metrics;
    const auto& latency = aggregate.latency_stats;
    const double waiting_ratio = policy_metrics.waiting_reorders_per_task;
    const double waiting_ratio_recent = policy_metrics.waiting_reorders_per_task_recent;
    out << result.name << ","
        << result.options.profile_path.string() << ","
        << result.options.workload_path.string() << ","
        << result.options.policy_id << ","
        << result.options.seed << ","
        << service_mode_to_string(result.options.host_mode) << ","
        << service_mode_to_string(result.options.nic_mode) << ","
        << result.summary.completed_tasks << ","
        << format_double(result.summary.makespan_us) << ","
        << format_double(result.summary.throughput_per_sec) << ","
        << format_double(latency.mean) << ","
        << format_double(latency.p95) << ","
        << format_double(latency.p99) << ","
        << aggregate.peak_waiting_queue_depth << ","
        << policy_metrics.waiting_reorders << ","
        << format_double(waiting_ratio) << ","
        << policy_metrics.waiting_reorders_recent << ","
        << policy_metrics.waiting_reorder_recent_task_count << ","
        << format_double(waiting_ratio_recent) << ","
        << result.options.output_path.string() << ","
        << rolling.waiting_queue_depth.samples << ","
        << format_double(rolling.waiting_queue_depth.latest) << ","
        << format_double(rolling.waiting_queue_depth.average) << ","
        << format_double(rolling.waiting_queue_depth.peak) << ","
        << rolling.host_utilization.samples << ","
        << format_double(rolling.host_utilization.latest) << ","
        << format_double(rolling.host_utilization.average) << ","
        << format_double(rolling.host_utilization.peak) << ","
        << rolling.nic_utilization.samples << ","
        << format_double(rolling.nic_utilization.latest) << ","
        << format_double(rolling.nic_utilization.average) << ","
        << format_double(rolling.nic_utilization.peak) << ","
        << rolling.sojourn.samples << ","
        << format_double(rolling.sojourn.mean_queue_time) << ","
        << format_double(rolling.sojourn.mean_service_time) << ","
        << format_double(rolling.sojourn.mean_latency) << ","
        << format_double(rolling.sojourn.p95_latency) << ","
        << format_double(rolling.sojourn.p99_latency);
    for (const auto& key : metadata_keys) {
        auto it = result.metadata.find(key);
        if (it != result.metadata.end()) {
            out << "," << it->second;
        } else {
            out << ",";
        }
    }
    out << "\n";
}

} // namespace

std::vector<BatchRunSummary> run_batch_manifest(const std::filesystem::path& manifest_path) {
    const BatchManifestData manifest = parse_batch_manifest_data(manifest_path);

    std::ofstream csv_stream;
    bool csv_enabled = false;
    if (manifest.csv_path) {
        const auto parent = manifest.csv_path->parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                throw std::runtime_error("failed to create directory '" + parent.string() + "': " + ec.message());
            }
        }
        const bool exists = std::filesystem::exists(*manifest.csv_path);
        csv_stream.open(*manifest.csv_path, std::ios::app);
        if (!csv_stream) {
            throw std::runtime_error("failed to open batch CSV '" + manifest.csv_path->string() + "'");
        }
        if (!exists) {
            write_batch_csv_header(csv_stream, manifest.metadata_keys);
        }
        csv_enabled = true;
    }

    std::vector<BatchRunSummary> summaries;
    summaries.reserve(manifest.runs.size());
    for (const auto& run : manifest.runs) {
        std::cout << "[batch] " << run.name << ": profile=" << run.options.profile_path
                  << " workload=" << run.options.workload_path << "\n";
        RunSummary summary = run_simulation(run.options);
        summaries.push_back(BatchRunSummary{run.name, run.options, summary, run.metadata});
        if (csv_enabled) {
            append_batch_csv_row(csv_stream, summaries.back(), manifest.metadata_keys);
            csv_stream.flush();
        }
    }

    return summaries;
}

} // namespace nicloadoff::cli
