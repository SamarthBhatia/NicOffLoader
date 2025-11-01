#include "nicloadoff/profile.hh"
#include "nicloadoff/profile_resources.hh"
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
#include <iomanip>
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
    Duration total_queue_time{0.0};
    Duration total_service_time{0.0};
    Duration host_service_time{0.0};
    Duration nic_service_time{0.0};
    bool finished{false};
};

class SimulationSession {
  public:
    SimulationSession(config::Profile profile, WorkloadSpec workload, std::uint64_t seed)
        : profile_(std::move(profile)), workload_(std::move(workload)), seed_(seed) {
        reset(seed_);
    }

    void reset(std::uint64_t new_seed) {
        seed_ = new_seed;
        service_model_ = std::make_unique<ServiceTimeModel>(profile_, seed_);
        auto inventory = make_resource_inventory_from_profile(profile_);
        resource_ids_ = inventory.ids;
        auto tasks = make_tasks_from_spec(workload_, resource_ids_);
        scheduler_ = std::make_unique<BasicScheduler>(std::move(inventory.pool), service_model_.get());
        for (const auto& task : tasks) {
            scheduler_->submit_task(task);
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
            snapshot.current_time = scheduler_->current_time();
            snapshot.last_event = scheduler_->last_event();
            snapshot.next_event = scheduler_->next_event();
            snapshot.event_queue_size = scheduler_->event_queue_size();
            snapshot.waiting_queue_size = scheduler_->waiting_queue_size();
            snapshot.waiting_tasks = scheduler_->waiting_tasks();
            snapshot.task_statuses = scheduler_->task_statuses();
            snapshot.completed_tasks = scheduler_->completed_tasks();
            snapshot.events_processed = scheduler_->events_processed();
            snapshot.resources = scheduler_->resource_pool().snapshot();
            for (const auto& metrics : scheduler_->completed_metrics()) {
                snapshot.total_queue_time += metrics.total_queue_time;
                snapshot.total_service_time += metrics.total_service_time;
                snapshot.host_service_time += metrics.host_service_time;
                snapshot.nic_service_time += metrics.nic_service_time;
            }
            snapshot.finished = finished_;
        } else {
            snapshot.finished = true;
        }
        return snapshot;
    }

    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const std::vector<std::string>& event_log() const noexcept { return event_log_; }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

  private:
    config::Profile profile_;
    WorkloadSpec workload_;
    ProfileResourceIds resource_ids_{};
    std::unique_ptr<ServiceTimeModel> service_model_;
    std::unique_ptr<BasicScheduler> scheduler_;
    std::vector<std::string> event_log_;
    std::size_t max_event_log_{64};
    std::uint64_t seed_{1};
    bool finished_{false};
};

struct AppState {
    std::vector<std::filesystem::path> profile_paths;
    std::vector<std::string> profile_names;
    std::vector<WorkloadPreset> workloads;
    std::vector<std::string> workload_names;
    std::vector<std::string> load_errors;

    int profile_index{0};
    int workload_index{0};

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

        WorkloadSpec spec = apply_service_modes(preset.spec);
        std::uint64_t seed_to_use = session && preserve_seed ? session->seed() : next_seed;

        try {
            session = std::make_unique<SimulationSession>(profile, std::move(spec), seed_to_use);
        } catch (const std::exception& ex) {
            status_message = std::string("Failed to initialise simulation: ") + ex.what();
            session.reset();
            return false;
        }

        status_message = "Loaded profile and workload. Press space to run or 'n' to step.";
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
    print_line("  Total queue time: " + format_double(snapshot.total_queue_time, 3) + " us");
    print_line("  Total service time: " + format_double(snapshot.total_service_time, 3) + " us");
    print_line("    Host service: " + format_double(snapshot.host_service_time, 3) + " us");
    print_line("    NIC service:  " + format_double(snapshot.nic_service_time, 3) + " us");
    print_line("  Completed tasks: " + std::to_string(snapshot.completed_tasks.size()));

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
        oss << "  Task " << status.id << " stage " << stage_display << "/" << status.total_stages << " "
            << task_state_description(status, snapshot.waiting_tasks);
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
            case 'r':
            case 'R':
                state.reset_session();
                last_step_time = steady_clock::now();
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
