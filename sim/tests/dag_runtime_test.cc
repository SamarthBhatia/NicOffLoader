#include "nicloadoff/dag_runtime.hh"
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
        nicloadoff::ProfileResourceIds ids;

        {
            auto workload = nicloadoff::load_workload_from_file(fixture_path("skew_dag.yaml"));
            assert(workload.spec.dag_tasks.size() == 1);
            auto runtime = nicloadoff::make_task_dag_runtime_from_spec(workload.spec.dag_tasks[0], ids, 1000);

            auto ready = runtime.initial_tasks();
            assert(ready.size() == 1);
            const auto& host_task = ready[0];
            assert(host_task.id == 1000);
            assert(host_task.arrival_time == workload.spec.dag_tasks[0].arrival_time);
            assert(host_task.stages.size() == 1);

            auto next = runtime.release_successors(host_task.id, 10.0);
            assert(next.size() == 1);
            const auto& nic_task = next[0];
            assert(nic_task.id == 1001);
            assert(nic_task.arrival_time == 10.0);
            assert(nic_task.stages.size() == 1);
            assert(runtime.contains(nic_task.id));

            auto none = runtime.release_successors(nic_task.id, 11.0);
            assert(none.empty());
            assert(runtime.all_completed());
        }

        {
            auto workload = nicloadoff::load_workload_from_file(fixture_path("multi_entry_dag.yaml"));
            assert(workload.spec.dag_tasks.size() == 1);
            auto runtime = nicloadoff::make_task_dag_runtime_from_spec(workload.spec.dag_tasks[0], ids, 2000);

            auto ready = runtime.initial_tasks();
            assert(ready.size() == 2);
            auto task_a = ready[0];
            auto task_b = ready[1];
            assert(task_a.id != task_b.id);
            assert(task_a.arrival_time == workload.spec.dag_tasks[0].arrival_time);

            auto post_a = runtime.release_successors(task_a.id, 5.0);
            assert(post_a.empty());
            auto post_b = runtime.release_successors(task_b.id, 6.0);
            assert(post_b.size() == 1);
            const auto& merge_task [[maybe_unused]] = post_b[0];
            assert(merge_task.arrival_time == 6.0);
            assert(runtime.contains(merge_task.id));
        }

        std::cout << "dag_runtime_test passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dag_runtime_test failure: " << ex.what() << "\n";
        return 1;
    }
}
