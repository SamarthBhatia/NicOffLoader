#ifndef NICLOADOFF_PROFILE_RESOURCES_HH
#define NICLOADOFF_PROFILE_RESOURCES_HH

#include "nicloadoff/profile.hh"
#include "nicloadoff/resource.hh"

namespace nicloadoff {

struct ProfileResourceIds {
    ResourceId host_cpu{1};
    ResourceId host_dram{2};
    ResourceId host_link{3};
    ResourceId nic_cpu{4};
    ResourceId nic_dram{5};
    ResourceId nic_link{6};
};

struct ProfileResourceInventory {
    ResourcePool pool;
    ProfileResourceIds ids;
};

ProfileResourceInventory make_resource_inventory_from_profile(const config::Profile& profile);

} // namespace nicloadoff

#endif // NICLOADOFF_PROFILE_RESOURCES_HH
