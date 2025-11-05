#include "nicloadoff/workload.hh"
#include "nicloadoff/workload_loader.hh"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

std::filesystem::path fixture_path(const std::string& name) {
    auto here = std::filesystem::path(__FILE__).parent_path();
    auto root = std::filesystem::canonical(here / "../..");
    return root / "workloads" / "examples" / name;
}

nicloadoff::StageSpec make_stage(double service_time, nicloadoff::ResourceClass resource, double units) {
    nicloadoff::StageSpec stage;
    stage.deterministic_service_time = service_time;
    stage.demands.push_back(nicloadoff::StageResourceDemand{.resource = resource, .units = units});
    return stage;
}

} // namespace

int main() {
    try {
        nicloadoff::ProfileResourceIds ids;

        {
            auto workload = nicloadoff::load_workload_from_file(fixture_path("skew_dag.yaml"));
            assert(workload.spec.dag_tasks.size() == 1);
            const auto& spec = workload.spec.dag_tasks[0];
            auto dag = nicloadoff::make_task_dag_from_spec(spec, ids);
            assert(dag.nodes.size() == 2);
            assert(dag.entry_nodes.size() == 1);
            std::size_t host_index [[maybe_unused]] = dag.index_by_name.at("host_entry");
            std::size_t nic_index [[maybe_unused]] = dag.index_by_name.at("nic_stage");
            assert(dag.nodes[host_index].stage.service_time == 10.0);
            assert(dag.nodes[nic_index].stage.service_time == 1.0);
            auto order = nicloadoff::topological_order(dag);
            assert(order.size() == 2);
            assert(order[0] == host_index);
            assert(order[1] == nic_index);
        }

        {
            auto workload = nicloadoff::load_workload_from_file(fixture_path("multi_entry_dag.yaml"));
            assert(workload.spec.dag_tasks.size() == 1);
            const auto& spec = workload.spec.dag_tasks[0];
            auto dag = nicloadoff::make_task_dag_from_spec(spec, ids);
            assert(dag.entry_nodes.size() == 2);
            auto order = nicloadoff::topological_order(dag);
            assert(order.size() == dag.nodes.size());
            for ([[maybe_unused]] std::size_t idx : dag.entry_nodes) {
                assert(std::find(order.begin(), order.end(), idx) != order.end());
            }
        }

        {
            nicloadoff::TaskDAGSpec spec;
            spec.id = 700;
            spec.arrival_time = 0.0;
            nicloadoff::StageNodeSpec root;
            root.name = "root";
            root.stage = make_stage(1.0, nicloadoff::ResourceClass::kHostCpu, 1.0);
            root.successors.push_back("child");

            nicloadoff::StageNodeSpec child;
            child.name = "child";
            child.stage = make_stage(0.5, nicloadoff::ResourceClass::kNicCpu, 1.0);

            nicloadoff::StageNodeSpec orphan;
            orphan.name = "orphan";
            orphan.stage = make_stage(2.0, nicloadoff::ResourceClass::kHostCpu, 0.5);

            spec.nodes.push_back(root);
            spec.nodes.push_back(child);
            spec.nodes.push_back(orphan);
            spec.entry_points = {"root"};

            bool caught [[maybe_unused]] = false;
            try {
                static_cast<void>(nicloadoff::make_task_dag_from_spec(spec, ids));
            } catch (const nicloadoff::WorkloadSpecError&) {
                caught = true;
            }
            assert(caught && "unreachable node should trigger validation failure");
        }

        {
            nicloadoff::TaskDAGSpec spec;
            spec.id = 701;
            spec.arrival_time = 0.0;
            nicloadoff::StageNodeSpec a;
            a.name = "a";
            a.stage = make_stage(1.0, nicloadoff::ResourceClass::kHostCpu, 1.0);
            a.successors.push_back("b");

            nicloadoff::StageNodeSpec b;
            b.name = "b";
            b.stage = make_stage(1.0, nicloadoff::ResourceClass::kNicCpu, 1.0);
            b.successors.push_back("a");

            spec.nodes.push_back(a);
            spec.nodes.push_back(b);
            spec.entry_points = {"a"};

            bool caught [[maybe_unused]] = false;
            try {
                static_cast<void>(nicloadoff::make_task_dag_from_spec(spec, ids));
            } catch (const nicloadoff::WorkloadSpecError&) {
                caught = true;
            }
            assert(caught && "cycle should trigger validation failure");
        }

        std::cout << "task_dag_test passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "task_dag_test failure: " << ex.what() << "\n";
        return 1;
    }
}
