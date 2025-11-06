#ifndef NICLOADOFF_SERVICE_TIME_MODEL_HH
#define NICLOADOFF_SERVICE_TIME_MODEL_HH

#include "nicloadoff/profile.hh"
#include "nicloadoff/sim_types.hh"
#include "nicloadoff/task.hh"

#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>

namespace nicloadoff {

class ServiceTimeModelError : public std::runtime_error {
  public:
    explicit ServiceTimeModelError(const std::string& message) : std::runtime_error(message) {}
};

class ServiceTimeModel {
  public:
    ServiceTimeModel(const config::Profile& profile, std::uint64_t seed = 0);

    [[nodiscard]] Duration sample(const ServiceTimeProfileRef& request);
    [[nodiscard]] Duration mean(const ServiceTimeProfileRef& request) const;

  private:
    const config::Profile* profile_{nullptr};
    std::mt19937_64 rng_;

    [[nodiscard]] const config::ServiceTimeOverride& find_override(const std::string& key) const;
    [[nodiscard]] double select_mean(const config::ServiceTimeOverride& override, ServiceTimeDomain domain) const;
};

} // namespace nicloadoff

#endif // NICLOADOFF_SERVICE_TIME_MODEL_HH
