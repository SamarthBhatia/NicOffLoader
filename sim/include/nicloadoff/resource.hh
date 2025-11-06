#ifndef NICLOADOFF_RESOURCE_HH
#define NICLOADOFF_RESOURCE_HH

#include "nicloadoff/sim_types.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace nicloadoff {

enum class ResourceType {
    kHostCpu,
    kHostDram,
    kHostLink,
    kNicCpu,
    kNicDram,
    kNicLink
};

class ResourceError : public std::runtime_error {
  public:
    explicit ResourceError(const std::string& message) : std::runtime_error(message) {}
};

class Resource {
  public:
    Resource(ResourceId id, ResourceType type, double capacity_units);

    [[nodiscard]] ResourceId id() const noexcept { return id_; }
    [[nodiscard]] ResourceType type() const noexcept { return type_; }
    [[nodiscard]] double capacity() const noexcept { return capacity_units_; }
    [[nodiscard]] double in_use() const noexcept { return in_use_units_; }
    [[nodiscard]] double available() const noexcept { return capacity_units_ - in_use_units_; }

    [[nodiscard]] bool can_allocate(double units) const noexcept;
    void allocate(double units);
    void release(double units);

  private:
    ResourceId id_;
    ResourceType type_;
    double capacity_units_{0.0};
    double in_use_units_{0.0};
};

class ResourcePool {
  public:
    void add_resource(const Resource& resource);
    [[nodiscard]] bool contains(ResourceId id) const noexcept;

    [[nodiscard]] Resource* find(ResourceId id);
    [[nodiscard]] const Resource* find(ResourceId id) const;

    [[nodiscard]] bool can_allocate(ResourceId id, double units) const;
    void allocate(ResourceId id, double units);
    void release(ResourceId id, double units);
    [[nodiscard]] std::vector<Resource> snapshot() const;

  private:
    std::unordered_map<ResourceId, Resource> resources_;
};

} // namespace nicloadoff

#endif // NICLOADOFF_RESOURCE_HH
