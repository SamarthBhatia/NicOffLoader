#include "nicloadoff/profile_resources.hh"

#include <utility>

namespace nicloadoff {

namespace {

double clamp_positive(double value, double fallback) {
    return value > 0.0 ? value : fallback;
}

} // namespace

ProfileResourceInventory make_resource_inventory_from_profile(const config::Profile& profile) {
    ProfileResourceInventory inventory{};

    const ProfileResourceIds& ids = inventory.ids;

    ResourcePool pool;
    pool.add_resource(Resource{ids.host_cpu, ResourceType::kHostCpu,
                               clamp_positive(static_cast<double>(profile.host_cpu.cores_total), 1.0)});
    pool.add_resource(Resource{ids.host_dram, ResourceType::kHostDram,
                               clamp_positive(static_cast<double>(profile.host_dram.capacity_gb), 1.0)});

    const double host_link_capacity =
        clamp_positive(static_cast<double>(profile.host_nic_link.max_inflight_bytes), 1'000'000.0);
    pool.add_resource(Resource{ids.host_link, ResourceType::kHostLink, host_link_capacity});

    pool.add_resource(Resource{ids.nic_cpu, ResourceType::kNicCpu,
                               clamp_positive(static_cast<double>(profile.nic_cpu.cores_total), 1.0)});
    pool.add_resource(Resource{ids.nic_dram, ResourceType::kNicDram,
                               clamp_positive(static_cast<double>(profile.nic_dram.capacity_gb), 1.0)});

    const double nic_link_capacity =
        clamp_positive(static_cast<double>(profile.nic_network_link.max_inflight_bytes), 1'000'000.0);
    pool.add_resource(Resource{ids.nic_link, ResourceType::kNicLink, nic_link_capacity});

    inventory.pool = std::move(pool);
    return inventory;
}

} // namespace nicloadoff
