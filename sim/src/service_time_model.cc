#include "nicloadoff/service_time_model.hh"

#include <cmath>
#include <sstream>

namespace nicloadoff {

namespace {

[[nodiscard]] std::string make_error(const std::string& context, const std::string& key) {
    std::ostringstream oss;
    oss << context << " (service key=" << key << ")";
    return oss.str();
}

} // namespace

ServiceTimeModel::ServiceTimeModel(const config::Profile& profile, std::uint64_t seed)
    : profile_(&profile), rng_(seed) {}

Duration ServiceTimeModel::sample(const ServiceTimeProfileRef& request) {
    const auto& override_values = find_override(request.key);
    const double mean_value = select_mean(override_values, request.domain);
    if (mean_value <= 0.0 || !std::isfinite(mean_value)) {
        throw ServiceTimeModelError(make_error("invalid mean service time", request.key));
    }

    switch (request.mode) {
    case ServiceTimeMode::kDeterministic:
        return mean_value;
    case ServiceTimeMode::kStochastic: {
        const double lambda = 1.0 / mean_value;
        std::exponential_distribution<double> distribution(lambda);
        return distribution(rng_);
    }
    default:
        throw ServiceTimeModelError(make_error("unsupported service time mode", request.key));
    }
}

Duration ServiceTimeModel::mean(const ServiceTimeProfileRef& request) const {
    const auto& override_values = find_override(request.key);
    const double mean_value = select_mean(override_values, request.domain);
    if (mean_value <= 0.0 || !std::isfinite(mean_value)) {
        throw ServiceTimeModelError(make_error("invalid mean service time", request.key));
    }
    return mean_value;
}

const config::ServiceTimeOverride& ServiceTimeModel::find_override(const std::string& key) const {
    if (profile_ == nullptr) {
        throw ServiceTimeModelError("service time model not initialised with a profile");
    }
    auto it = profile_->service_time_overrides.find(key);
    if (it == profile_->service_time_overrides.end()) {
        throw ServiceTimeModelError(make_error("service time override not found", key));
    }
    return it->second;
}

double ServiceTimeModel::select_mean(const config::ServiceTimeOverride& override, ServiceTimeDomain domain) const {
    switch (domain) {
    case ServiceTimeDomain::kHost:
        return override.host_mean_us;
    case ServiceTimeDomain::kNic:
        return override.nic_mean_us;
    default:
        throw ServiceTimeModelError("unknown service time domain");
    }
}

} // namespace nicloadoff
