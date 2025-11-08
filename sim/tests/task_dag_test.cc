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
            assert(spec.id == 900);
            auto dag = nicloadoff::make_task_dag_from_spec(spec, ids);
            assert(dag.nodes.size() == 5);
            assert(dag.entry_nodes.size() == 1);
            const std::size_t parse_index [[maybe_unused]] = dag.index_by_name.at("parse_req");
            const std::size_t hash_index [[maybe_unused]] = dag.index_by_name.at("hash_key");
            const std::size_t auth_index [[maybe_unused]] = dag.index_by_name.at("auth_check");
            const std::size_t lookup_index [[maybe_unused]] = dag.index_by_name.at("db_lookup");
            const std::size_t serialize_index [[maybe_unused]] = dag.index_by_name.at("serialize_resp");
            assert(dag.nodes[parse_index].successors.size() == 2);
            assert(dag.nodes[parse_index].successors[0] == hash_index);
            assert(dag.nodes[parse_index].successors[1] == auth_index);
            assert(dag.nodes[hash_index].successors.size() == 1);
            assert(dag.nodes[hash_index].successors[0] == lookup_index);
            assert(dag.nodes[auth_index].successors.size() == 1);
            assert(dag.nodes[auth_index].successors[0] == serialize_index);
            assert(dag.nodes[lookup_index].successors.size() == 1);
            assert(dag.nodes[lookup_index].successors[0] == serialize_index);
            auto order = nicloadoff::topological_order(dag);
            assert(order.size() == dag.nodes.size());
            assert(order.front() == parse_index);
            assert(order.back() == serialize_index);
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
