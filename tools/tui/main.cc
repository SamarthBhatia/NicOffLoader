#include "nicloadoff/basic_policy_hooks.hh"
#include "nicloadoff/policy_dsl.hh"
#include "nicloadoff/dag_submission_controller.hh"
#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
#include "nicloadoff/policy_hook.hh"
#include "nicloadoff/run_metrics.hh"
#include "nicloadoff/scheduler.hh"
#include "nicloadoff/service_time_model.hh"
#include "nicloadoff/workload.hh"
#include "nicloadoff/workload_loader.hh"

#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace nicloadoff::tui {

using nicloadoff::config::load_profile_from_file;

struct WorkloadPreset {
    std::string name;
    std::string description;
    WorkloadSpec spec;
    std::optional<std::filesystem::path> source_path;
};

struct PolicyChoice {
    std::string id;
    std::string display_name;
    std::string description;
    std::optional<std::filesystem::path> dsl_path;
};

[[nodiscard]] std::string resource_type_to_string(ResourceType type) {
    switch (type) {
    case ResourceType::kHostCpu:
        return "Host CPU";
    case ResourceType::kHostDram:
        return "Host DRAM";
    case ResourceType::kHostLink:
        return "Host↔NIC link";
    case ResourceType::kNicCpu:
        return "NIC CPU";
    case ResourceType::kNicDram:
        return "NIC DRAM";
    case ResourceType::kNicLink:
        return "NIC↔Network";
    }
    return "Unknown";
}

[[nodiscard]] std::string event_type_to_string(EventType type) {
    switch (type) {
    case EventType::kTaskArrival:
        return "TaskArrival";
    case EventType::kTaskReady:
        return "TaskReady";
    case EventType::kTaskStart:
        return "TaskStart";
    case EventType::kTaskComplete:
        return "TaskComplete";
    case EventType::kTransferStart:
        return "TransferStart";
    case EventType::kTransferComplete:
        return "TransferComplete";
    }
    return "Unknown";
}

[[nodiscard]] std::string format_double(double value, int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

[[nodiscard]] std::string format_event(const ScheduledEvent& event) {
    std::ostringstream oss;
    oss << "t=" << format_double(event.timestamp, 3) << " " << event_type_to_string(event.metadata.type) << " (#"
        << event.metadata.id << ")";
    return oss.str();
}

[[nodiscard]] std::string json_escape(const std::string& value) {
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

[[nodiscard]] std::vector<std::filesystem::path> discover_profiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    if (std::filesystem::exists(root)) {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".yaml" || ext == ".yml") {
                paths.push_back(entry.path());
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

[[nodiscard]] std::vector<std::filesystem::path> discover_policy_configs(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::exists(root)) {
        return paths;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".yaml" || ext == ".yml") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

[[nodiscard]] StageSpec make_stage_with_duration(Duration duration,
                                                 std::initializer_list<std::pair<ResourceClass, double>> demands) {
    StageSpec stage{};
    stage.deterministic_service_time = duration;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

[[nodiscard]] StageSpec make_stage_with_profile(const ServiceTimeProfileRef& ref,
                                                std::initializer_list<std::pair<ResourceClass, double>> demands) {
    StageSpec stage{};
    stage.service_profile = ref;
    for (const auto& [resource, units] : demands) {
        stage.demands.push_back(StageResourceDemand{.resource = resource, .units = units});
    }
    return stage;
}

[[nodiscard]] WorkloadSpec make_sequential_workload() {
    WorkloadSpec spec{};
    spec.tasks.push_back(TaskSpec{
        .id = 1,
        .arrival_time = 0.0,
        .stages = {make_stage_with_duration(5.0,
                                            {{ResourceClass::kHostCpu, 1.0},
                                             {ResourceClass::kHostDram, 2.0},
                                             {ResourceClass::kHostLink, 1'024.0}})},
    });
    spec.tasks.push_back(TaskSpec{
        .id = 2,
        .arrival_time = 1.0,
        .stages = {make_stage_with_duration(3.0,
                                            {{ResourceClass::kHostCpu, 1.0},
                                             {ResourceClass::kHostDram, 2.0},
                                             {ResourceClass::kHostLink, 1'024.0}})},
    });
    return spec;
}

[[nodiscard]] WorkloadSpec make_parallel_workload() {
    WorkloadSpec spec{};
    spec.tasks.push_back(TaskSpec{
        .id = 3,
        .arrival_time = 0.0,
        .stages = {make_stage_with_duration(4.0,
                                            {{ResourceClass::kHostCpu, 1.0},
                                             {ResourceClass::kHostDram, 2.0},
                                             {ResourceClass::kHostLink, 1'024.0}})},
    });
    spec.tasks.push_back(TaskSpec{
        .id = 4,
        .arrival_time = 0.0,
        .stages = {make_stage_with_duration(6.0,
                                            {{ResourceClass::kHostCpu, 1.0},
                                             {ResourceClass::kHostDram, 2.0},
                                             {ResourceClass::kHostLink, 1'024.0}})},
    });
    return spec;
}

[[nodiscard]] WorkloadSpec make_host_nic_pipeline_workload() {
    WorkloadSpec spec{};
    ServiceTimeProfileRef host_ref{
        .key = "kv_lookup",
        .domain = ServiceTimeDomain::kHost,
        .mode = ServiceTimeMode::kDeterministic,
    };
    ServiceTimeProfileRef nic_ref{
        .key = "kv_lookup",
        .domain = ServiceTimeDomain::kNic,
        .mode = ServiceTimeMode::kDeterministic,
    };

    StageSpec stage1 = make_stage_with_profile(
        host_ref,
        {{ResourceClass::kHostCpu, 4.0}, {ResourceClass::kHostDram, 32.0}, {ResourceClass::kHostLink, 4'096.0}});
    StageSpec stage2 = make_stage_with_profile(
        nic_ref, {{ResourceClass::kNicCpu, 6.0}, {ResourceClass::kNicDram, 8.0}, {ResourceClass::kNicLink, 2'048.0}});

    spec.tasks.push_back(TaskSpec{.id = 5, .arrival_time = 0.0, .stages = {stage1, stage2}});
    spec.tasks.push_back(TaskSpec{.id = 6, .arrival_time = 0.0, .stages = {stage1, stage2}});
    return spec;
}

[[nodiscard]] std::vector<WorkloadPreset> build_presets() {
    std::vector<WorkloadPreset> presets;
    presets.push_back(WorkloadPreset{
        .name = "Sequential host tasks",
        .description = "Two tasks sharing host resources; showcases queue ordering and backlog.",
        .spec = make_sequential_workload(),
        .source_path = std::nullopt,
    });
    presets.push_back(WorkloadPreset{
        .name = "Parallel host pair",
        .description = "Parallel tasks sharing a two-core host, demonstrating concurrent service.",
        .spec = make_parallel_workload(),
        .source_path = std::nullopt,
    });
    presets.push_back(WorkloadPreset{
        .name = "Host→NIC pipeline",
        .description = "Two multi-stage tasks that consume host and NIC resources, stressing contention.",
        .spec = make_host_nic_pipeline_workload(),
        .source_path = std::nullopt,
    });
    return presets;
}

std::string infer_arrival_label(const WorkloadPreset& preset, const std::vector<std::string>& labels) {
    if (labels.empty()) {
        return "steady";
    }
    std::string name = preset.name;
    if (preset.source_path) {
        name = preset.source_path->stem().string();
    }
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find("burst") != std::string::npos || name.find("spike") != std::string::npos) {
        if (std::find(labels.begin(), labels.end(), "burst") != labels.end()) {
            return "burst";
        }
    }
    return labels.front();
}

[[nodiscard]] std::vector<PolicyChoice> build_policy_choices() {
    std::vector<PolicyChoice> choices;
    for (const auto& info : policy::builtin_policies()) {
        choices.push_back(PolicyChoice{
            .id = info.id,
            .display_name = info.display_name,
            .description = info.description,
            .dsl_path = std::nullopt,
        });
    }
    const std::filesystem::path examples_root = std::filesystem::current_path() / "policies" / "examples";
    for (const auto& path : discover_policy_configs(examples_root)) {
        PolicyChoice choice;
        choice.id = "dsl";
        choice.display_name = "DSL: " + path.stem().string();
        choice.description = "DSL policy loaded from " + path.string();
        choice.dsl_path = std::filesystem::absolute(path);
        choices.push_back(std::move(choice));
    }
    if (choices.empty()) {
        choices.push_back(PolicyChoice{
            .id = "none",
            .display_name = "No policy",
            .description = "No policy loaded.",
            .dsl_path = std::nullopt,
        });
    }
    return choices;
}

std::vector<WorkloadPreset> load_workloads_from_directory(const std::filesystem::path& dir,
                                                          std::vector<std::string>& errors) {
    std::vector<WorkloadPreset> presets;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (ec) {
            errors.push_back("Failed to access workloads directory: " + dir.string() + " (" + ec.message() + ")");
        }
        return presets;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".yaml" || ext == ".yml") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        try {
            auto loaded = load_workload_from_file(path);
            WorkloadPreset preset;
            preset.name = !loaded.workload_name.empty() ? loaded.workload_name : path.stem().string();
            preset.description = loaded.description.empty() ? ("Loaded from " + path.string()) : loaded.description;
            preset.spec = std::move(loaded.spec);
            preset.source_path = path;
            presets.push_back(std::move(preset));
        } catch (const std::exception& ex) {
            errors.push_back(std::string("Failed to load workload '") + path.string() + "': " + ex.what());
        }
    }
    return presets;
}

struct SimulationSnapshot {
    SimTime current_time{0.0};
    std::optional<ScheduledEvent> last_event;
    std::optional<ScheduledEvent> next_event;
    std::size_t event_queue_size{0};
    std::size_t waiting_queue_size{0};
    std::size_t events_processed{0};
    std::vector<TaskId> waiting_tasks;
    std::vector<BasicScheduler::TaskStatus> task_statuses;
    std::vector<TaskId> completed_tasks;
    std::vector<Resource> resources;
    std::map<TaskId, std::string> task_stage_labels;
    Duration total_queue_time{0.0};
    Duration total_service_time{0.0};
    Duration host_service_time{0.0};
    Duration nic_service_time{0.0};
    std::size_t active_task_count{0};
    std::optional<std::size_t> admission_limit;
    bool finished{false};
    std::size_t policy_waiting_reorders{0};
    double policy_waiting_reorders_per_task{0.0};
    std::size_t policy_waiting_reorders_recent{0};
    std::size_t policy_waiting_reorder_recent_task_count{0};
    double policy_waiting_reorders_per_task_recent{0.0};
    std::size_t rolling_queue_samples{0};
    double rolling_queue_latest{0.0};
    double rolling_queue_average{0.0};
    double rolling_queue_peak{0.0};
    std::size_t rolling_host_util_samples{0};
    double rolling_host_util_average{0.0};
    double rolling_host_util_peak{0.0};
    std::size_t rolling_nic_util_samples{0};
    double rolling_nic_util_average{0.0};
    double rolling_nic_util_peak{0.0};
    std::size_t rolling_sojourn_samples{0};
    double rolling_sojourn_mean_latency{0.0};
    double rolling_sojourn_p95{0.0};
    double rolling_sojourn_p99{0.0};
};

class SimulationSession {
  public:
    SimulationSession(config::Profile profile,
                      WorkloadSpec workload,
                      std::map<std::string, std::string> metadata,
                      std::uint64_t seed,
                      std::unique_ptr<policy::PolicyHook> policy = nullptr)
        : profile_(std::move(profile)),
          workload_(std::move(workload)),
          policy_hook_(std::move(policy)),
          seed_(seed),
          metadata_(std::move(metadata)) {
        reset(seed_);
    }

    void set_metadata(std::map<std::string, std::string> metadata) { metadata_ = std::move(metadata); }

    void reset(std::uint64_t new_seed) {
        seed_ = new_seed;
        service_model_ = std::make_unique<ServiceTimeModel>(profile_, seed_);
        auto inventory = make_resource_inventory_from_profile(profile_);
        resource_ids_ = inventory.ids;
        dag_controller_ = DagSubmissionController::from_spec(workload_, resource_ids_);
        auto tasks = make_tasks_from_spec(workload_, resource_ids_);
        scheduler_ = std::make_unique<BasicScheduler>(std::move(inventory.pool), service_model_.get());
        if (policy_hook_) {
            scheduler_->set_policy_hook(policy_hook_.get());
        }
        scheduler_->set_policy_metadata(metadata_);
        for (const auto& task : tasks) {
            scheduler_->submit_task(task);
        }
        if (!dag_controller_.empty()) {
            dag_controller_.submit_initial(*scheduler_);
        }
        event_log_.clear();
        finished_ = false;
    }

    bool step() {
        if (finished_ || !scheduler_) {
            return false;
        }
        if (!scheduler_->step_once()) {
            finished_ = true;
            return false;
        }
        if (auto last = scheduler_->last_event()) {
            if (!dag_controller_.empty() && last->metadata.type == EventType::kTaskComplete) {
                dag_controller_.handle_task_completion(last->metadata.id, scheduler_->current_time(), *scheduler_);
            }
            event_log_.push_back(format_event(*last));
            if (event_log_.size() > max_event_log_) {
                event_log_.erase(event_log_.begin());
            }
        }
        return true;
    }

    [[nodiscard]] SimulationSnapshot snapshot() const {
        SimulationSnapshot snapshot{};
        if (scheduler_) {
            const PolicyStateSnapshot policy_snapshot = scheduler_->policy_state_snapshot();
            snapshot.current_time = policy_snapshot.current_time;
            snapshot.last_event = scheduler_->last_event();
            snapshot.next_event = scheduler_->next_event();
            snapshot.event_queue_size = policy_snapshot.queues.event_queue_depth;
            snapshot.waiting_queue_size = policy_snapshot.queues.waiting_queue_depth;
            snapshot.events_processed = policy_snapshot.queues.processed_events;
            snapshot.waiting_tasks = policy_snapshot.waiting_task_order;
            snapshot.task_statuses = scheduler_->task_statuses();
            snapshot.completed_tasks = scheduler_->completed_tasks();
            snapshot.resources = scheduler_->resource_pool().snapshot();
            const auto& aggregate = policy_snapshot.run_metrics.aggregate;
            snapshot.total_queue_time = aggregate.total_queue_time;
            snapshot.total_service_time = aggregate.total_service_time;
            snapshot.host_service_time = aggregate.host_service_time;
            snapshot.nic_service_time = aggregate.nic_service_time;
            snapshot.active_task_count = policy_snapshot.active_task_count;
            snapshot.admission_limit = policy_snapshot.admission_limit;
            for (const auto& task_state : policy_snapshot.tasks) {
                if (!task_state.stage_label.empty()) {
                    snapshot.task_stage_labels.emplace(task_state.id, task_state.stage_label);
                }
            }
            snapshot.finished = finished_;
            snapshot.policy_waiting_reorders = policy_snapshot.run_metrics.policy.waiting_reorders;
            snapshot.policy_waiting_reorders_per_task = policy_snapshot.run_metrics.policy.waiting_reorders_per_task;
            snapshot.policy_waiting_reorders_recent = policy_snapshot.run_metrics.policy.waiting_reorders_recent;
            snapshot.policy_waiting_reorder_recent_task_count =
                policy_snapshot.run_metrics.policy.waiting_reorder_recent_task_count;
            snapshot.policy_waiting_reorders_per_task_recent =
                policy_snapshot.run_metrics.policy.waiting_reorders_per_task_recent;
            const auto& rolling = policy_snapshot.rolling_metrics;
            snapshot.rolling_queue_samples = rolling.waiting_queue_depth.samples;
            snapshot.rolling_queue_latest = rolling.waiting_queue_depth.latest;
            snapshot.rolling_queue_average = rolling.waiting_queue_depth.average;
            snapshot.rolling_queue_peak = rolling.waiting_queue_depth.peak;
            snapshot.rolling_host_util_samples = rolling.host_utilization.samples;
            snapshot.rolling_host_util_average = rolling.host_utilization.average;
            snapshot.rolling_host_util_peak = rolling.host_utilization.peak;
            snapshot.rolling_nic_util_samples = rolling.nic_utilization.samples;
            snapshot.rolling_nic_util_average = rolling.nic_utilization.average;
            snapshot.rolling_nic_util_peak = rolling.nic_utilization.peak;
            snapshot.rolling_sojourn_samples = rolling.sojourn.samples;
            snapshot.rolling_sojourn_mean_latency = rolling.sojourn.mean_latency;
            snapshot.rolling_sojourn_p95 = rolling.sojourn.p95_latency;
            snapshot.rolling_sojourn_p99 = rolling.sojourn.p99_latency;
        } else {
            snapshot.finished = true;
        }
        return snapshot;
    }

    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const std::vector<std::string>& event_log() const noexcept { return event_log_; }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    [[nodiscard]] const std::vector<BasicScheduler::TaskMetrics>& metrics() const noexcept {
        static const std::vector<BasicScheduler::TaskMetrics> kEmpty;
        if (!scheduler_) {
            return kEmpty;
        }
        return scheduler_->completed_metrics();
    }

  private:
    config::Profile profile_;
    WorkloadSpec workload_;
    ProfileResourceIds resource_ids_{};
    DagSubmissionController dag_controller_;
    std::unique_ptr<ServiceTimeModel> service_model_;
    std::unique_ptr<BasicScheduler> scheduler_;
    std::unique_ptr<policy::PolicyHook> policy_hook_;
    std::vector<std::string> event_log_;
    std::size_t max_event_log_{64};
    std::uint64_t seed_{1};
    std::map<std::string, std::string> metadata_;
    bool finished_{false};
};

struct AppState {
    std::vector<std::filesystem::path> profile_paths;
    std::vector<std::string> profile_names;
    std::vector<WorkloadPreset> workloads;
    std::vector<std::string> workload_names;
    std::vector<PolicyChoice> policy_entries;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> load_errors;
    std::vector<std::string> arrival_labels{"steady", "burst"};
    int arrival_label_index{0};

    int profile_index{0};
    int workload_index{0};
    int policy_index{0};

    bool auto_run{false};
    std::uint64_t next_seed{1};
    std::string status_message{};
    std::string workload_description{};

    std::unique_ptr<SimulationSession> session;
    bool host_stochastic{false};
    bool nic_stochastic{false};

    bool load_selection(bool preserve_seed = false) {
        if (workloads.empty()) {
            status_message = "No workload presets available.";
            return false;
        }

        config::Profile profile{};
        if (profile_paths.empty()) {
            profile = config::Profile{
                .schema_version = "builtin",
                .profile_name = "builtin_default",
                .description = "In-memory sample profile for NicLoadOff TUI.",
                .last_verified = "N/A",
            };
            profile.host_cpu.cores_total = 8;
            profile.host_dram.capacity_gb = 128;
            profile.host_nic_link.max_inflight_bytes = 64'000'000;
            profile.nic_cpu.cores_total = 8;
            profile.nic_dram.capacity_gb = 16;
            profile.nic_network_link.max_inflight_bytes = 32'000'000;
            profile.service_time_overrides.emplace("kv_lookup",
                                                   config::ServiceTimeOverride{.host_mean_us = 2.5, .nic_mean_us = 1.5});
        } else {
            profile_index = std::clamp(profile_index, 0, static_cast<int>(profile_paths.size()) - 1);
            const auto& path = profile_paths[profile_index];
            try {
                profile = load_profile_from_file(path);
            } catch (const std::exception& ex) {
                status_message = std::string("Failed to load profile: ") + ex.what();
                return false;
            }
        }

        workload_index = std::clamp(workload_index, 0, static_cast<int>(workloads.size()) - 1);
        const auto& preset = workloads[workload_index];
        workload_description = preset.description;
        if (preset.source_path) {
            workload_description += "\n\nSource: " + preset.source_path->string();
        }

        if (!preserve_seed) {
            const std::string label = infer_arrival_label(preset, arrival_labels);
            auto it = std::find(arrival_labels.begin(), arrival_labels.end(), label);
            if (it != arrival_labels.end()) {
                arrival_label_index = static_cast<int>(std::distance(arrival_labels.begin(), it));
            } else {
                arrival_label_index = 0;
            }
        }

        metadata.clear();
        metadata.emplace("workload_label", preset.name);
        if (!arrival_labels.empty()) {
            arrival_label_index =
                std::clamp(arrival_label_index, 0, static_cast<int>(arrival_labels.size()) - 1);
            metadata.emplace("arrival_label", arrival_labels[arrival_label_index]);
        }

        WorkloadSpec spec = apply_service_modes(preset.spec);
        std::uint64_t seed_to_use = session && preserve_seed ? session->seed() : next_seed;

        std::string policy_id = "none";
        std::optional<std::filesystem::path> policy_config;
        std::string policy_label = "none";
        if (!policy_entries.empty()) {
            policy_index = std::clamp(policy_index, 0, static_cast<int>(policy_entries.size()) - 1);
            const auto& choice = policy_entries[policy_index];
            policy_id = choice.id;
            policy_label = choice.display_name;
            policy_config = choice.dsl_path;
        }
        std::unique_ptr<policy::PolicyHook> policy_hook;
        try {
            if (policy_config) {
                policy_hook = policy::dsl::load_program_from_file(*policy_config);
            } else {
                policy_hook = policy::make_policy_hook(policy_id);
            }
        } catch (const std::exception& ex) {
            status_message = std::string("Failed to load policy: ") + ex.what();
            session.reset();
            return false;
        }

        try {
            session = std::make_unique<SimulationSession>(
                profile, std::move(spec), metadata, seed_to_use, std::move(policy_hook));
        } catch (const std::exception& ex) {
            status_message = std::string("Failed to initialise simulation: ") + ex.what();
            session.reset();
            return false;
        }

        std::string policy_suffix;
        if (!policy_entries.empty() && policy_entries[policy_index].dsl_path) {
            policy_suffix = " [" + policy_entries[policy_index].dsl_path->filename().string() + "]";
        }
        status_message =
            "Loaded profile and workload (policy: " + policy_label + policy_suffix + "). Press space to run or 'n' to step.";
        if (!load_errors.empty()) {
            status_message += " (" + std::to_string(load_errors.size()) + " loader warning";
            if (load_errors.size() > 1) {
                status_message += "s";
            }
            status_message += ")";
        }
        auto_run = false;
        if (!preserve_seed) {
            next_seed += 1;
        }
        return true;
    }

    bool step_once() {
        if (!session) {
            status_message = "Load a profile/workload first.";
            return false;
        }
        if (!session->step()) {
            auto_run = false;
            status_message = "Simulation complete. Press 'r' to reset.";
            return false;
        }
        return true;
    }

    WorkloadSpec apply_service_modes(const WorkloadSpec& base) const {
        WorkloadSpec spec = base;
        for (auto& task : spec.tasks) {
            for (auto& stage : task.stages) {
                if (!stage.service_profile) {
                    continue;
                }
                if (stage.service_profile->domain == ServiceTimeDomain::kHost) {
                    stage.service_profile->mode =
                        host_stochastic ? ServiceTimeMode::kStochastic : ServiceTimeMode::kDeterministic;
                } else if (stage.service_profile->domain == ServiceTimeDomain::kNic) {
                    stage.service_profile->mode =
                        nic_stochastic ? ServiceTimeMode::kStochastic : ServiceTimeMode::kDeterministic;
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
                    stage.service_profile->mode =
                        host_stochastic ? ServiceTimeMode::kStochastic : ServiceTimeMode::kDeterministic;
                } else if (stage.service_profile->domain == ServiceTimeDomain::kNic) {
                    stage.service_profile->mode =
                        nic_stochastic ? ServiceTimeMode::kStochastic : ServiceTimeMode::kDeterministic;
                }
            }
        }
        return spec;
    }

    void reset_session() {
        if (!session) {
            status_message = "Load a profile/workload first.";
            return;
        }
        session->reset(next_seed++);
        status_message = "Simulation reset.";
        auto_run = false;
    }

    [[nodiscard]] SimulationSnapshot snapshot() const {
        if (!session) {
            return {};
        }
        return session->snapshot();
    }
};

std::string service_mode_string(bool stochastic) { return stochastic ? "stochastic" : "deterministic"; }

std::string sanitize_filename(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
            result.push_back(ch);
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        result = "workload";
    }
    return result;
}

std::filesystem::path make_metrics_path(const SimulationSession& session, const AppState& state) {
    std::string workload_tag = "workload";
    if (!state.workloads.empty() && state.workload_index >= 0 &&
        state.workload_index < static_cast<int>(state.workloads.size())) {
        workload_tag = sanitize_filename(state.workloads[state.workload_index].name);
    }
    std::string base_name = "metrics_seed" + std::to_string(session.seed()) + "_" + workload_tag + ".json";
    std::filesystem::path candidate = std::filesystem::current_path() / base_name;
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = std::filesystem::current_path() /
                    ("metrics_seed" + std::to_string(session.seed()) + "_" + workload_tag + "_" +
                     std::to_string(suffix) + ".json");
        ++suffix;
    }
    return candidate;
}

bool write_metrics_report(const SimulationSession& session,
                          const AppState& state,
                          const SimulationSnapshot& snapshot,
                          const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << "{\n";
    out << "  \"seed\": " << session.seed() << ",\n";
    out << "  \"host_service_mode\": \"" << service_mode_string(state.host_stochastic) << "\",\n";
    out << "  \"nic_service_mode\": \"" << service_mode_string(state.nic_stochastic) << "\",\n";
    out << "  \"finished\": " << (snapshot.finished ? "true" : "false") << ",\n";

    if (!state.workloads.empty() && state.workload_index >= 0 &&
        state.workload_index < static_cast<int>(state.workloads.size())) {
        const auto& preset = state.workloads[state.workload_index];
        out << "  \"workload\": {\n";
        out << "    \"name\": \"" << json_escape(preset.name) << "\",\n";
        out << "    \"source\": \"";
        if (preset.source_path) {
            out << json_escape(preset.source_path->string());
        } else {
            out << "builtin";
        }
        out << "\",\n";
        out << "    \"description\": \"" << json_escape(preset.description) << "\"\n";
        out << "  },\n";
    }

    out << "  \"totals\": {\n";
    const nicloadoff::RunMetrics run_metrics = nicloadoff::compute_run_metrics(session.metrics());
    const auto& aggregate = run_metrics.aggregate;
    const auto& latency_stats = aggregate.latency_stats;
    out << "    \"queue_time_us\": " << format_double(aggregate.total_queue_time, 6) << ",\n";
    out << "    \"service_time_us\": " << format_double(aggregate.total_service_time, 6) << ",\n";
    out << "    \"latency_time_us\": " << format_double(aggregate.total_latency, 6) << ",\n";
    out << "    \"host_service_time_us\": " << format_double(aggregate.host_service_time, 6) << ",\n";
    out << "    \"nic_service_time_us\": " << format_double(aggregate.nic_service_time, 6) << ",\n";
    out << "    \"latency_stats\": {\n";
    out << "      \"mean_us\": " << format_double(latency_stats.mean, 6) << ",\n";
    out << "      \"p50_us\": " << format_double(latency_stats.p50, 6) << ",\n";
    out << "      \"p95_us\": " << format_double(latency_stats.p95, 6) << ",\n";
    out << "      \"p99_us\": " << format_double(latency_stats.p99, 6) << ",\n";
    out << "      \"min_us\": " << format_double(latency_stats.min, 6) << ",\n";
    out << "      \"max_us\": " << format_double(latency_stats.max, 6) << "\n";
    out << "    },\n";
    out << "    \"events_processed\": " << snapshot.events_processed << ",\n";
    out << "    \"completed_tasks\": " << run_metrics.tasks.size() << "\n";
    out << "  },\n";
    out << "  \"policy_metrics\": {\n";
    out << "    \"waiting_reorders\": " << run_metrics.policy.waiting_reorders << ",\n";
    out << "    \"waiting_reorders_per_task\": " << format_double(run_metrics.policy.waiting_reorders_per_task, 6) << "\n";
    out << "  },\n";
    out << "  \"rolling_metrics\": {\n";
    out << "    \"queue\": {\n";
    out << "      \"samples\": " << snapshot.rolling_queue_samples << ",\n";
    out << "      \"latest\": " << format_double(snapshot.rolling_queue_latest, 6) << ",\n";
    out << "      \"average\": " << format_double(snapshot.rolling_queue_average, 6) << ",\n";
    out << "      \"peak\": " << format_double(snapshot.rolling_queue_peak, 6) << "\n";
    out << "    },\n";
    out << "    \"host_util\": {\n";
    out << "      \"samples\": " << snapshot.rolling_host_util_samples << ",\n";
    out << "      \"average\": " << format_double(snapshot.rolling_host_util_average, 6) << ",\n";
    out << "      \"peak\": " << format_double(snapshot.rolling_host_util_peak, 6) << "\n";
    out << "    },\n";
    out << "    \"nic_util\": {\n";
    out << "      \"samples\": " << snapshot.rolling_nic_util_samples << ",\n";
    out << "      \"average\": " << format_double(snapshot.rolling_nic_util_average, 6) << ",\n";
    out << "      \"peak\": " << format_double(snapshot.rolling_nic_util_peak, 6) << "\n";
    out << "    },\n";
    out << "    \"sojourn\": {\n";
    out << "      \"samples\": " << snapshot.rolling_sojourn_samples << ",\n";
    out << "      \"mean_us\": " << format_double(snapshot.rolling_sojourn_mean_latency, 6) << ",\n";
    out << "      \"p95_us\": " << format_double(snapshot.rolling_sojourn_p95, 6) << ",\n";
    out << "      \"p99_us\": " << format_double(snapshot.rolling_sojourn_p99, 6) << "\n";
    out << "    }\n";
    out << "  },\n";

    out << "  \"tasks\": [\n";
    const auto& task_timings = run_metrics.tasks;
    for (std::size_t i = 0; i < task_timings.size(); ++i) {
        const auto& timing = task_timings[i];
        out << "    {\n";
        out << "      \"task_id\": " << timing.id << ",\n";
        out << "      \"queue_time_us\": " << format_double(timing.queue_time, 6) << ",\n";
        out << "      \"service_time_us\": " << format_double(timing.service_time, 6) << ",\n";
        out << "      \"latency_us\": " << format_double(timing.latency(), 6) << ",\n";
        out << "      \"host_service_time_us\": " << format_double(timing.host_service_time, 6) << ",\n";
        out << "      \"nic_service_time_us\": " << format_double(timing.nic_service_time, 6) << "\n";
        out << "    }";
        if (i + 1 < task_timings.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    return out.good();
}
enum class Focus { Profiles, Workloads };

void draw_menu_section(WINDOW* win,
                       int& row,
                       const std::string& title,
                       const std::vector<std::string>& entries,
                       int selected,
                       bool focused) {
    mvwprintw(win, row++, 2, "%s", title.c_str());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (row >= getmaxy(win) - 2) {
            break;
        }
        if (i == selected) {
            if (focused) {
                wattron(win, A_REVERSE);
            } else {
                wattron(win, A_BOLD);
            }
        }
        mvwprintw(win, row++, 4, "%s", entries[i].c_str());
        if (i == selected) {
            if (focused) {
                wattroff(win, A_REVERSE);
            } else {
                wattroff(win, A_BOLD);
            }
        }
    }
    row += 1;
}

void draw_instructions(WINDOW* win, int start_row) {
    std::vector<std::string> lines = {
        "Controls:",
        "  ↑/↓   Navigate menu",
        "  ←/→   Switch menu",
        "  Enter Load selection",
        "  Space Run/Pause",
        "  n     Step event",
        "  r     Reset (new seed)",
        "  H     Toggle host service mode",
        "  N     Toggle NIC service mode",
        "  m     Cycle arrival_label metadata",
        "  p/P   Cycle policy hook (built-ins + DSL configs under policies/examples/)",
        "  s     Save metrics to JSON",
        "  q     Quit",
    };
    int row = start_row;
    for (const auto& line : lines) {
        if (row >= getmaxy(win) - 1) {
            break;
        }
        mvwprintw(win, row++, 2, "%s", line.c_str());
    }
}

void draw_left_panel(WINDOW* win,
                     const AppState& state,
                     Focus focus,
                     const std::vector<std::string>& profiles,
                     const std::vector<std::string>& workloads) {
    werase(win);
    box(win, 0, 0);
    int row = 1;
    draw_menu_section(win, row, "Profiles", profiles, state.profile_index, focus == Focus::Profiles);
    draw_menu_section(win, row, "Workloads", workloads, state.workload_index, focus == Focus::Workloads);
    draw_instructions(win, row);
    wrefresh(win);
}

std::string task_state_description(const BasicScheduler::TaskStatus& status,
                                   const std::vector<TaskId>& waiting_tasks) {
    if (status.completed) {
        return "Completed";
    }
    if (status.active) {
        return "Running";
    }
    if (std::find(waiting_tasks.begin(), waiting_tasks.end(), status.id) != waiting_tasks.end()) {
        return "Waiting";
    }
    return "Idle";
}

void draw_right_panel(WINDOW* win, const AppState& state, const SimulationSnapshot& snapshot) {
    werase(win);
    box(win, 0, 0);
    int row = 1;

    auto print_line = [&](const std::string& text) {
        if (row >= getmaxy(win) - 1) {
            return;
        }
        mvwprintw(win, row++, 2, "%s", text.c_str());
    };

    print_line("Run status:");
    print_line("  Current time: " + format_double(snapshot.current_time, 3) + " us");
    print_line("  Events processed: " + std::to_string(snapshot.events_processed));
    print_line("  Event queue size: " + std::to_string(snapshot.event_queue_size) +
               " | Waiting tasks: " + std::to_string(snapshot.waiting_queue_size));
    if (snapshot.next_event) {
        print_line("  Next event: " + format_event(*snapshot.next_event));
    } else {
        print_line("  Next event: None");
    }
    if (snapshot.last_event) {
        print_line("  Last event: " + format_event(*snapshot.last_event));
    } else {
        print_line("  Last event: None");
    }
    std::string run_state = state.auto_run ? "RUNNING" : "PAUSED";
    if (snapshot.finished) {
        run_state = "FINISHED";
    }
    print_line("  State: " + run_state);
    print_line(std::string("  Host service mode: ") +
               (state.host_stochastic ? "stochastic" : "deterministic"));
    print_line(std::string("  NIC service mode: ") +
               (state.nic_stochastic ? "stochastic" : "deterministic"));
    std::string policy_label = "None";
    std::optional<std::filesystem::path> policy_path;
    if (!state.policy_entries.empty()) {
        policy_label = state.policy_entries[state.policy_index].display_name;
        policy_path = state.policy_entries[state.policy_index].dsl_path;
    }
    print_line("  Policy: " + policy_label);
    if (policy_path) {
        print_line("    DSL config: " + policy_path->string());
    }
    if (!state.metadata.empty()) {
        print_line("  Scenario metadata:");
        for (const auto& [key, value] : state.metadata) {
            print_line("    " + key + ": " + value);
        }
    }
    if (snapshot.admission_limit) {
        print_line("    Active tasks: " + std::to_string(snapshot.active_task_count) + " / " +
                   std::to_string(*snapshot.admission_limit));
    } else {
        print_line("    Active tasks: " + std::to_string(snapshot.active_task_count));
    }
    print_line("  Total queue time: " + format_double(snapshot.total_queue_time, 3) + " us");
    print_line("  Total service time: " + format_double(snapshot.total_service_time, 3) + " us");
    print_line("    Host service: " + format_double(snapshot.host_service_time, 3) + " us");
    print_line("    NIC service:  " + format_double(snapshot.nic_service_time, 3) + " us");
    print_line("  Completed tasks: " + std::to_string(snapshot.completed_tasks.size()));
    print_line("  Policy waiting reorders: " + std::to_string(snapshot.policy_waiting_reorders) +
               " (per task " + format_double(snapshot.policy_waiting_reorders_per_task, 4) + ")");
    if (snapshot.policy_waiting_reorder_recent_task_count > 0) {
        print_line("    Recent (" + std::to_string(snapshot.policy_waiting_reorder_recent_task_count) +
                   " tasks): " + format_double(snapshot.policy_waiting_reorders_per_task_recent, 4));
    }
    if (snapshot.rolling_queue_samples > 0) {
        print_line("  Rolling queue avg: " + format_double(snapshot.rolling_queue_average, 3) + " (peak " +
                   format_double(snapshot.rolling_queue_peak, 3) + ", latest " +
                   format_double(snapshot.rolling_queue_latest, 3) + ")");
    }
    if (snapshot.rolling_host_util_samples > 0) {
        print_line("  Rolling util avg (host/nic): " + format_double(snapshot.rolling_host_util_average, 3) + " / " +
                   format_double(snapshot.rolling_nic_util_average, 3));
    }
    if (snapshot.rolling_sojourn_samples > 0) {
        print_line("  Rolling sojourn mean/p95/p99 (us): " + format_double(snapshot.rolling_sojourn_mean_latency, 3) +
                   " / " + format_double(snapshot.rolling_sojourn_p95, 3) + " / " +
                   format_double(snapshot.rolling_sojourn_p99, 3));
    }

    if (!state.status_message.empty()) {
        print_line("  Message: " + state.status_message);
    }
    if (!state.load_errors.empty()) {
        print_line("  Loader warnings:");
        int displayed = 0;
        for (const auto& error : state.load_errors) {
            if (row >= getmaxy(win) - 1) {
                break;
            }
            if (displayed >= 3) {
                print_line("    … (" + std::to_string(state.load_errors.size() - displayed) + " more)");
                break;
            }
            print_line("    - " + error);
            ++displayed;
        }
    }

    row += 1;
    print_line("Resources:");
    for (const auto& resource : snapshot.resources) {
        if (row >= getmaxy(win) - 1) {
            break;
        }
        std::ostringstream oss;
        oss << "  " << std::setw(16) << std::left << resource_type_to_string(resource.type()) << " "
            << format_double(resource.in_use(), 2) << " / " << format_double(resource.capacity(), 2);
        print_line(oss.str());
    }

    row += 1;
    print_line("Tasks:");
    for (const auto& status : snapshot.task_statuses) {
        if (row >= getmaxy(win) - 1) {
            break;
        }
        std::ostringstream oss;
        std::size_t stage_display = std::min(status.stage_index + 1, status.total_stages);
        oss << "  Task " << status.id << " stage " << stage_display << "/" << status.total_stages;
        auto label_it = snapshot.task_stage_labels.find(status.id);
        if (label_it != snapshot.task_stage_labels.end()) {
            oss << " [" << label_it->second << "]";
        }
        oss << " " << task_state_description(status, snapshot.waiting_tasks);
        print_line(oss.str());
    }

    row += 1;
    print_line("Workload description:");
    std::istringstream desc(state.workload_description);
    std::string line;
    while (std::getline(desc, line)) {
        if (row >= getmaxy(win) - 1) {
            break;
        }
        print_line("  " + line);
    }

    row += 1;
    print_line("Recent events:");
    static const std::vector<std::string> kEmptyLog{};
    const auto& log = state.session ? state.session->event_log() : kEmptyLog;
    int max_events = getmaxy(win) - row - 2;
    if (max_events > 0 && !log.empty()) {
        int start_index = static_cast<int>(log.size()) - max_events;
        if (start_index < 0) {
            start_index = 0;
        }
        for (int i = start_index; i < static_cast<int>(log.size()) && row < getmaxy(win) - 1; ++i) {
            print_line("  " + log[i]);
        }
    } else if (max_events > 0) {
        print_line("  (no events yet)");
    }

    wrefresh(win);
}

} // namespace nicloadoff::tui

int main() {
    using namespace nicloadoff::tui;
    using namespace std::chrono;

    AppState state;
    state.policy_entries = build_policy_choices();
    state.profile_paths = discover_profiles("profiles");
    state.workloads = build_presets();

    const auto workloads_dir = std::filesystem::current_path() / "workloads" / "examples";
    auto file_presets = load_workloads_from_directory(workloads_dir, state.load_errors);
    state.workloads.insert(state.workloads.end(), file_presets.begin(), file_presets.end());

    state.workload_names.reserve(state.workloads.size());
    for (const auto& preset : state.workloads) {
        std::string display = preset.name;
        if (preset.source_path) {
            display += " [" + preset.source_path->filename().string() + "]";
        } else {
            display += " [builtin]";
        }
        state.workload_names.push_back(std::move(display));
    }

    state.profile_names.reserve(state.profile_paths.size());
    for (const auto& path : state.profile_paths) {
        state.profile_names.push_back(path.filename().string());
    }
    if (state.profile_paths.empty()) {
        state.profile_names.push_back("Built-in sample profile");
    }

    if (!state.workloads.empty()) {
        const auto& preset = state.workloads.front();
        state.workload_description = preset.description;
        if (preset.source_path) {
            state.workload_description += "\n\nSource: " + preset.source_path->string();
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(50);

    int left_width = 36;
    if (left_width >= COLS) {
        left_width = COLS / 3;
    }

    WINDOW* left_win = newwin(LINES, left_width, 0, 0);
    WINDOW* right_win = newwin(LINES, COLS - left_width, 0, left_width);

    state.load_selection();

    Focus focus = Focus::Profiles;
    auto last_step_time = steady_clock::now();
    const auto step_interval = 150ms;
    bool running = true;

    while (running) {
        int ch = getch();
        if (ch != ERR) {
            switch (ch) {
            case KEY_UP:
                if (focus == Focus::Profiles && !state.profile_names.empty()) {
                    state.profile_index = std::max(0, state.profile_index - 1);
                } else if (focus == Focus::Workloads && !state.workload_names.empty()) {
                    state.workload_index = std::max(0, state.workload_index - 1);
                }
                break;
            case KEY_DOWN:
                if (focus == Focus::Profiles && !state.profile_names.empty()) {
                    state.profile_index =
                        std::min(state.profile_index + 1, static_cast<int>(state.profile_names.size()) - 1);
                } else if (focus == Focus::Workloads && !state.workload_names.empty()) {
                    state.workload_index =
                        std::min(state.workload_index + 1, static_cast<int>(state.workload_names.size()) - 1);
                }
                break;
            case KEY_LEFT:
            case KEY_RIGHT:
            case '\t':
                focus = (focus == Focus::Profiles) ? Focus::Workloads : Focus::Profiles;
                break;
            case '\n':
            case KEY_ENTER:
                state.load_selection();
                break;
            case ' ':
                if (!state.session) {
                    state.status_message = "Load a profile/workload first.";
                } else {
                    state.auto_run = !state.auto_run;
                    if (state.auto_run) {
                        state.status_message = "Running simulation...";
                    } else {
                        state.status_message = "Paused.";
                    }
                }
                break;
            case 'n':
                state.step_once();
                last_step_time = steady_clock::now();
                break;
            case 'H':
                state.host_stochastic = !state.host_stochastic;
                if (state.session) {
                    if (state.load_selection(true)) {
                        state.status_message = std::string("Host service mode set to ") +
                                                (state.host_stochastic ? "stochastic." : "deterministic.");
                    }
                } else {
                    state.status_message = std::string("Host service mode will be ") +
                                            (state.host_stochastic ? "stochastic" : "deterministic") +
                                            " on next load.";
                }
                break;
            case 'N':
                state.nic_stochastic = !state.nic_stochastic;
                if (state.session) {
                    if (state.load_selection(true)) {
                        state.status_message = std::string("NIC service mode set to ") +
                                                (state.nic_stochastic ? "stochastic." : "deterministic.");
                    }
                } else {
                    state.status_message = std::string("NIC service mode will be ") +
                                            (state.nic_stochastic ? "stochastic" : "deterministic") +
                                            " on next load.";
                }
                break;
            case 'm':
            case 'M':
                if (!state.arrival_labels.empty()) {
                    state.arrival_label_index =
                        (state.arrival_label_index + 1) % static_cast<int>(state.arrival_labels.size());
                    if (state.load_selection(true)) {
                        state.status_message = "Scenario arrival_label set to " +
                                               state.arrival_labels[state.arrival_label_index] + ".";
                    }
                }
                break;
            case 'p':
            case 'P':
                if (!state.policy_entries.empty()) {
                    state.policy_index = (state.policy_index + 1) % static_cast<int>(state.policy_entries.size());
                    if (state.load_selection(true)) {
                        state.status_message = "Policy set to " +
                                                state.policy_entries[state.policy_index].display_name + ".";
                    }
                } else {
                    state.status_message = "No policies registered.";
                }
                break;
            case 'r':
            case 'R':
                state.reset_session();
                last_step_time = steady_clock::now();
                break;
            case 's':
            case 'S':
                if (!state.session) {
                    state.status_message = "Load a profile/workload first.";
                } else if (!state.session->finished()) {
                    state.status_message = "Finish the run before exporting metrics.";
                } else if (state.session->metrics().empty()) {
                    state.status_message = "No task metrics available to export.";
                } else {
                    SimulationSnapshot snapshot = state.snapshot();
                    std::filesystem::path path = make_metrics_path(*state.session, state);
                    if (write_metrics_report(*state.session, state, snapshot, path)) {
                        state.status_message = "Metrics saved to " + path.string();
                    } else {
                        state.status_message = "Failed to write metrics file.";
                    }
                }
                break;
            case 'q':
            case 'Q':
                running = false;
                break;
            default:
                break;
            }
        }

        if (state.auto_run && state.session) {
            auto now = steady_clock::now();
            if (now - last_step_time >= step_interval) {
                if (!state.step_once()) {
                    state.auto_run = false;
                }
                last_step_time = now;
            }
        }

        SimulationSnapshot snapshot = state.snapshot();
        draw_left_panel(left_win, state, focus, state.profile_names, state.workload_names);
        draw_right_panel(right_win, state, snapshot);
    }

    delwin(left_win);
    delwin(right_win);
    endwin();
    return 0;
}
