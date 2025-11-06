#include "nicloadoff/service_time_model.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void assert_near(double lhs, double rhs, double tolerance = 1e-9) {
    if (std::fabs(lhs - rhs) > tolerance) {
        std::fprintf(stderr, "assert_near failed: |%f - %f| > %f\n", lhs, rhs, tolerance);
        std::abort();
    }
}

} // namespace

int main() {
    nicloadoff::config::Profile profile{};
    profile.service_time_overrides["kv_lookup"] = nicloadoff::config::ServiceTimeOverride{
        .host_mean_us = 2.5,
        .nic_mean_us = 1.5,
    };

    nicloadoff::ServiceTimeModel deterministic_model(profile, /*seed=*/1234);
    nicloadoff::ServiceTimeProfileRef host_det{
        .key = "kv_lookup",
        .domain = nicloadoff::ServiceTimeDomain::kHost,
        .mode = nicloadoff::ServiceTimeMode::kDeterministic,
    };
    assert_near(deterministic_model.sample(host_det), 2.5);
    assert_near(deterministic_model.mean(host_det), 2.5);

    nicloadoff::ServiceTimeProfileRef nic_det{
        .key = "kv_lookup",
        .domain = nicloadoff::ServiceTimeDomain::kNic,
        .mode = nicloadoff::ServiceTimeMode::kDeterministic,
    };
    assert_near(deterministic_model.sample(nic_det), 1.5);

    nicloadoff::ServiceTimeModel stochastic_model(profile, /*seed=*/424242);
    nicloadoff::ServiceTimeProfileRef host_stochastic{
        .key = "kv_lookup",
        .domain = nicloadoff::ServiceTimeDomain::kHost,
        .mode = nicloadoff::ServiceTimeMode::kStochastic,
    };

    std::mt19937_64 reference_rng(424242);
    std::exponential_distribution<double> reference_dist(1.0 / 2.5);
    const double expected_sample = reference_dist(reference_rng);
    const double sampled_value = stochastic_model.sample(host_stochastic);
    assert_near(sampled_value, expected_sample, 1e-12);

    const double second_sample = stochastic_model.sample(host_stochastic);
    check(std::fabs(sampled_value - second_sample) > 1e-9, "expected stochastic samples to vary");

    return 0;
}
