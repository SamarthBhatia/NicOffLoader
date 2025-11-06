#ifndef NICLOADOFF_PROFILE_HH
#define NICLOADOFF_PROFILE_HH

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace nicloadoff::config {

struct CpuSpec {
    std::string model;
    std::uint32_t sockets{1};
    std::uint32_t cores_per_socket{1};
    std::uint32_t cores_total{1};
    double clock_ghz{0.0};
    double base_service_scale{1.0};
};

struct DramSpec {
    std::string type;
    std::uint32_t capacity_gb{0};
    double bandwidth_gbps{0.0};
    double latency_ns{0.0};
};

struct NicDramSpec : DramSpec {
    std::uint32_t contention_variance_pct{0};
};

struct LinkMtu {
    std::uint32_t default_mtu{1500};
    std::optional<std::uint32_t> jumbo;
};

struct LinkSpec {
    std::string type;
    double peak_bandwidth_gbps{0.0};
    double effective_bandwidth_gbps{0.0};
    double latency_us{0.0};
    LinkMtu mtu_bytes{};
    std::size_t max_inflight_bytes{0};
};

struct QueueSpec {
    std::uint32_t cpu_capacity{0};
    std::uint32_t dram_capacity_gb{0};
};

struct LinkQueueSpec {
    std::size_t max_inflight_bytes{0};
};

struct ServiceTimeOverride {
    double host_mean_us{0.0};
    double nic_mean_us{0.0};
};

struct ProfileReferences {
    struct Entry {
        std::string title;
        std::optional<std::string> publisher;
        std::optional<std::string> authors;
        std::optional<std::string> venue;
        std::optional<std::uint32_t> year;
        std::optional<std::string> url;
    };
    std::vector<Entry> entries;
};

struct Profile {
    std::string schema_version;
    std::string profile_name;
    std::string description;
    std::string last_verified;
    ProfileReferences references;
    CpuSpec host_cpu;
    DramSpec host_dram;
    CpuSpec nic_cpu;
    NicDramSpec nic_dram;
    LinkSpec host_nic_link;
    LinkSpec nic_network_link;
    QueueSpec host_queue;
    QueueSpec nic_queue;
    LinkQueueSpec host_nic_queue;
    LinkQueueSpec nic_network_queue;
    std::unordered_map<std::string, ServiceTimeOverride> service_time_overrides;
    std::vector<std::string> notes;
};

class ProfileLoaderError : public std::runtime_error {
  public:
    explicit ProfileLoaderError(const std::string& message) : std::runtime_error(message) {}
};

Profile load_profile_from_file(const std::filesystem::path& path);

} // namespace nicloadoff::config

#endif // NICLOADOFF_PROFILE_HH
