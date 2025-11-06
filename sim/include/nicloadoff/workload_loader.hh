#ifndef NICLOADOFF_WORKLOAD_LOADER_HH
#define NICLOADOFF_WORKLOAD_LOADER_HH

#include "nicloadoff/workload.hh"

#include <filesystem>
#include <stdexcept>

namespace nicloadoff {

class WorkloadLoaderError : public std::runtime_error {
  public:
    explicit WorkloadLoaderError(const std::string& message) : std::runtime_error(message) {}
};

struct LoadedWorkload {
    std::string schema_version;
    std::string workload_name;
    std::string description;
    WorkloadSpec spec;
};

LoadedWorkload load_workload_from_file(const std::filesystem::path& path);

} // namespace nicloadoff

#endif // NICLOADOFF_WORKLOAD_LOADER_HH
