#include "nicloadoff/resource.hh"

#include <algorithm>
#include <sstream>

namespace nicloadoff {

namespace {

[[nodiscard]] std::string make_error(const std::string& context, ResourceId id) {
    std::ostringstream oss;
    oss << context << " (resource id=" << id << ")";
    return oss.str();
}

} // namespace

Resource::Resource(ResourceId id, ResourceType type, double capacity_units)
    : id_(id), type_(type), capacity_units_(capacity_units) {
    if (capacity_units_ <= 0.0) {
        throw ResourceError("resource capacity must be positive");
    }
}

bool Resource::can_allocate(double units) const noexcept { return units >= 0.0 && available() + 1e-12 >= units; }

void Resource::allocate(double units) {
    if (units < 0.0) {
        throw ResourceError(make_error("cannot allocate negative units", id_));
    }
    if (!can_allocate(units)) {
        throw ResourceError(make_error("insufficient capacity to allocate", id_));
    }
    in_use_units_ += units;
}

void Resource::release(double units) {
    if (units < 0.0) {
        throw ResourceError(make_error("cannot release negative units", id_));
    }
    if (units > in_use_units_ + 1e-12) {
        throw ResourceError(make_error("release exceeds resource usage", id_));
    }
    in_use_units_ -= units;
    if (in_use_units_ < 0.0) {
        in_use_units_ = 0.0;
    }
}

void ResourcePool::add_resource(const Resource& resource) {
    auto [it, inserted] = resources_.emplace(resource.id(), resource);
    if (!inserted) {
        throw ResourceError("resource already exists in pool");
    }
}

bool ResourcePool::contains(ResourceId id) const noexcept { return resources_.count(id) != 0; }

Resource* ResourcePool::find(ResourceId id) {
    auto it = resources_.find(id);
    return it == resources_.end() ? nullptr : &it->second;
}

const Resource* ResourcePool::find(ResourceId id) const {
    auto it = resources_.find(id);
    return it == resources_.end() ? nullptr : &it->second;
}

bool ResourcePool::can_allocate(ResourceId id, double units) const {
    const Resource* resource = find(id);
    return resource != nullptr && resource->can_allocate(units);
}

void ResourcePool::allocate(ResourceId id, double units) {
    Resource* resource = find(id);
    if (resource == nullptr) {
        throw ResourceError(make_error("resource not found for allocation", id));
    }
    resource->allocate(units);
}

void ResourcePool::release(ResourceId id, double units) {
    Resource* resource = find(id);
    if (resource == nullptr) {
        throw ResourceError(make_error("resource not found for release", id));
    }
    resource->release(units);
}

std::vector<Resource> ResourcePool::snapshot() const {
    std::vector<Resource> resources;
    resources.reserve(resources_.size());
    for (const auto& [id, resource] : resources_) {
        resources.push_back(resource);
    }
    std::sort(resources.begin(), resources.end(),
              [](const Resource& lhs, const Resource& rhs) { return lhs.id() < rhs.id(); });
    return resources;
}

} // namespace nicloadoff
