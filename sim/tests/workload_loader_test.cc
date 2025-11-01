#include "nicloadoff/workload_loader.hh"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

std::filesystem::path fixture_path(const std::string& name) {
    auto here = std::filesystem::path(__FILE__).parent_path();
    auto root = std::filesystem::canonical(here / "../..");
    return root / "workloads" / "examples" / name;
}

} // namespace

int main() {
    try {
        {
            auto path = fixture_path("sequential_host.yaml");
            auto workload = nicloadoff::load_workload_from_file(path);
            assert(workload.workload_name == "sequential_host");
            assert(workload.spec.tasks.size() == 2);
        const auto& task1 [[maybe_unused]] = workload.spec.tasks[0];
        assert(task1.id == 1);
        assert(task1.arrival_time == 0.0);
        assert(task1.stages.size() == 1);
        assert(task1.stages[0].deterministic_service_time.has_value());
        assert(task1.stages[0].requirements.size() == 3);
        const auto& task2 [[maybe_unused]] = workload.spec.tasks[1];
        assert(task2.id == 2);
        assert(task2.arrival_time == 1.0);
        assert(task2.stages.size() == 1);
    }

    {
        auto path = fixture_path("host_nic_pipeline.yaml");
        auto workload = nicloadoff::load_workload_from_file(path);
        assert(workload.schema_version == "0.1");
        assert(workload.spec.tasks.size() == 2);
        const auto& stage [[maybe_unused]] = workload.spec.tasks[0].stages[0];
        assert(!stage.deterministic_service_time.has_value());
        assert(stage.service_profile.has_value());
        assert(stage.service_profile->domain == nicloadoff::ServiceTimeDomain::kHost);
        assert(stage.service_profile->mode == nicloadoff::ServiceTimeMode::kDeterministic);
        assert(stage.requirements.size() == 3);
        }

        std::cout << "workload_loader_test passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "workload_loader_test failure: " << ex.what() << "\n";
        return 1;
    }
}
