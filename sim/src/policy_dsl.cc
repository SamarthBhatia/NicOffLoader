#include "nicloadoff/policy_dsl.hh"

#include "nicloadoff/basic_policy_hooks.hh"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace nicloadoff::policy::dsl {

namespace {

double metric_value(const PolicyStateSnapshot& snapshot, Metric metric) {
    const auto& rolling = snapshot.rolling_metrics;
    switch (metric) {
    case Metric::kQueueAverage:
        return rolling.waiting_queue_depth.average;
    case Metric::kQueuePeak:
        return rolling.waiting_queue_depth.peak;
    case Metric::kHostUtilAverage:
        return rolling.host_utilization.average;
    case Metric::kHostUtilPeak:
        return rolling.host_utilization.peak;
    case Metric::kNicUtilAverage:
        return rolling.nic_utilization.average;
    case Metric::kNicUtilPeak:
        return rolling.nic_utilization.peak;
    case Metric::kSojournMean:
        return rolling.sojourn.mean_latency;
    case Metric::kSojournP95:
        return rolling.sojourn.p95_latency;
    case Metric::kSojournP99:
        return rolling.sojourn.p99_latency;
    }
    return 0.0;
}

bool compare(double lhs, CompareOp op, double rhs) {
    switch (op) {
    case CompareOp::kGreater:
        return lhs > rhs;
    case CompareOp::kGreaterEqual:
        return lhs >= rhs;
    case CompareOp::kLess:
        return lhs < rhs;
    case CompareOp::kLessEqual:
        return lhs <= rhs;
    }
    return false;
}

Metric parse_metric(const std::string& value) {
    if (value == "queue_avg") {
        return Metric::kQueueAverage;
    }
    if (value == "queue_peak") {
        return Metric::kQueuePeak;
    }
    if (value == "host_util_avg") {
        return Metric::kHostUtilAverage;
    }
    if (value == "host_util_peak") {
        return Metric::kHostUtilPeak;
    }
    if (value == "nic_util_avg") {
        return Metric::kNicUtilAverage;
    }
    if (value == "nic_util_peak") {
        return Metric::kNicUtilPeak;
    }
    if (value == "sojourn_mean_us") {
        return Metric::kSojournMean;
    }
    if (value == "sojourn_p95_us") {
        return Metric::kSojournP95;
    }
    if (value == "sojourn_p99_us") {
        return Metric::kSojournP99;
    }
    throw std::runtime_error("unknown metric '" + value + "'");
}

CompareOp parse_compare(const std::string& value) {
    if (value == ">" || value == "gt") {
        return CompareOp::kGreater;
    }
    if (value == ">=" || value == "ge") {
        return CompareOp::kGreaterEqual;
    }
    if (value == "<" || value == "lt") {
        return CompareOp::kLess;
    }
    if (value == "<=" || value == "le") {
        return CompareOp::kLessEqual;
    }
    throw std::runtime_error("unknown comparison operator '" + value + "'");
}

ReorderPreference parse_reorder(const std::string& value) {
    if (value == "prefer-host") {
        return ReorderPreference::kPreferHost;
    }
    if (value == "prefer-nic") {
        return ReorderPreference::kPreferNic;
    }
    throw std::runtime_error("unknown reorder preference '" + value + "'");
}

RulePredicates parse_predicates(const YAML::Node& node) {
    if (!node.IsMap()) {
        throw std::runtime_error("rule.match must be a mapping");
    }
    RulePredicates predicates{};

    for (const auto& kv : node) {
        const std::string key = kv.first.as<std::string>();
        if (key != "metadata" && key != "stage" && key != "stage_index") {
            throw std::runtime_error("rule.match contains unknown key '" + key + "'");
        }
    }

    if (const YAML::Node metadata = node["metadata"]) {
        if (!metadata.IsMap()) {
            throw std::runtime_error("rule.match.metadata must be a mapping");
        }
        for (const auto& kv : metadata) {
            if (!kv.first.IsScalar() || !kv.second.IsScalar()) {
                throw std::runtime_error("rule.match.metadata entries must be scalar");
            }
            predicates.metadata_equals.emplace(kv.first.as<std::string>(), kv.second.as<std::string>());
        }
    }

    TaskPredicate task_predicate{};
    bool has_task_predicate = false;
    if (const YAML::Node stage = node["stage"]) {
        if (!stage.IsScalar()) {
            throw std::runtime_error("rule.match.stage must be a string");
        }
        task_predicate.stage_label = stage.as<std::string>();
        has_task_predicate = true;
    }
    if (const YAML::Node stage_index = node["stage_index"]) {
        if (!stage_index.IsScalar()) {
            throw std::runtime_error("rule.match.stage_index must be numeric");
        }
        task_predicate.stage_index = stage_index.as<std::size_t>();
        has_task_predicate = true;
    }
    if (has_task_predicate) {
        predicates.task = task_predicate;
    }

    if (!predicates.task && predicates.metadata_equals.empty()) {
        throw std::runtime_error("rule.match must specify metadata and/or task predicates");
    }

    return predicates;
}

Condition parse_condition(const YAML::Node& node) {
    if (!node.IsMap()) {
        throw std::runtime_error("rule.when must be a mapping");
    }
    Condition condition{};
    const YAML::Node metric = node["metric"];
    const YAML::Node op = node["op"];
    const YAML::Node value = node["value"];
    if (!metric || !op || !value) {
        throw std::runtime_error("rule.when must contain metric/op/value");
    }
    condition.metric = parse_metric(metric.as<std::string>());
    condition.op = parse_compare(op.as<std::string>());
    condition.value = value.as<double>();
    return condition;
}

Action parse_action(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("rule.action must be a mapping");
    }
    Action action{};
    if (const YAML::Node reorder = node["reorder"]) {
        if (!reorder.IsScalar()) {
            throw std::runtime_error("rule.action.reorder must be a string");
        }
        action.type = Action::Type::kReorder;
        action.reorder = parse_reorder(reorder.as<std::string>());
        return action;
    }
    if (const YAML::Node admission = node["admission"]) {
        if (!admission.IsMap()) {
            throw std::runtime_error("rule.action.admission must be a mapping");
        }
        const YAML::Node max_active = admission["max_active"];
        if (!max_active || !max_active.IsScalar()) {
            throw std::runtime_error("rule.action.admission.max_active must be numeric");
        }
        const std::size_t limit = max_active.as<std::size_t>();
        if (limit == 0) {
            throw std::runtime_error("rule.action.admission.max_active must be > 0");
        }
        action.type = Action::Type::kAdmission;
        action.max_active_tasks = limit;
        return action;
    }
    throw std::runtime_error("rule.action must specify either reorder or admission");
}

std::vector<Rule> parse_rules(const YAML::Node& root) {
    if (!root.IsMap()) {
        throw std::runtime_error("DSL file must be a mapping");
    }
    const YAML::Node rules_node = root["rules"];
    if (!rules_node || !rules_node.IsSequence() || rules_node.size() == 0) {
        throw std::runtime_error("DSL file must provide a non-empty 'rules' list");
    }
    std::vector<Rule> rules;
    rules.reserve(rules_node.size());
    for (std::size_t idx = 0; idx < rules_node.size(); ++idx) {
        const YAML::Node rule_node = rules_node[idx];
        if (!rule_node.IsMap()) {
            throw std::runtime_error("rules[" + std::to_string(idx) + "] must be a mapping");
        }
        Rule rule;
        if (const YAML::Node when = rule_node["when"]) {
            rule.condition = parse_condition(when);
        }
        if (const YAML::Node predicates = rule_node["match"]) {
            rule.predicates = parse_predicates(predicates);
        }
        rule.action = parse_action(rule_node["action"]);
        rules.push_back(std::move(rule));
    }
    return rules;
}

std::unordered_map<TaskId, const PolicyTaskState*> index_tasks(const PolicyStateSnapshot& snapshot) {
    std::unordered_map<TaskId, const PolicyTaskState*> index;
    index.reserve(snapshot.tasks.size());
    for (const auto& task_state : snapshot.tasks) {
        index.emplace(task_state.id, &task_state);
    }
    return index;
}

std::vector<TaskId> reorder_waiting(const PolicyStateSnapshot& snapshot,
                                    bool prefer_host,
                                    const std::optional<RulePredicates>& predicates) {
    const auto task_index = index_tasks(snapshot);
    const TaskPredicate* task_predicate = predicates && predicates->task ? &*predicates->task : nullptr;

    auto matches_task = [&](TaskId id) -> bool {
        if (!task_predicate) {
            return true;
        }
        auto it = task_index.find(id);
        if (it == task_index.end()) {
            return false;
        }
        return task_predicate->matches(*it->second);
    };

    std::vector<TaskId> filtered;
    filtered.reserve(snapshot.waiting_task_order.size());
    for (TaskId id : snapshot.waiting_task_order) {
        if (matches_task(id)) {
            filtered.push_back(id);
        }
    }

    if (filtered.empty()) {
        return {};
    }

    auto score = [&](TaskId id) -> double {
        auto it = task_index.find(id);
        if (it == task_index.end()) {
            return 0.0;
        }
        const auto& state = *it->second;
        return state.pending_host_demand - state.pending_nic_demand;
    };

    std::sort(filtered.begin(), filtered.end(), [&](TaskId lhs, TaskId rhs) {
        const double lhs_score = score(lhs);
        const double rhs_score = score(rhs);
        if (prefer_host) {
            if (lhs_score != rhs_score) {
                return lhs_score > rhs_score;
            }
        } else {
            if (lhs_score != rhs_score) {
                return lhs_score < rhs_score;
            }
        }
        return lhs < rhs;
    });

    std::vector<TaskId> ordered = snapshot.waiting_task_order;
    std::size_t cursor = 0;
    for (TaskId& id : ordered) {
        if (matches_task(id)) {
            id = filtered.at(cursor++);
        }
    }
    return ordered;
}

class DslPolicyHook : public PolicyHook {
  public:
    explicit DslPolicyHook(Program program) : program_(std::move(program)) {}

    [[nodiscard]] PolicyDecision evaluate(const PolicyStateSnapshot& snapshot) override {
        return program_.evaluate(snapshot);
    }

  private:
    Program program_;
};

} // namespace

bool Condition::evaluate(const PolicyStateSnapshot& snapshot) const {
    const double lhs = metric_value(snapshot, metric);
    return compare(lhs, op, value);
}

bool TaskPredicate::matches(const PolicyTaskState& task) const {
    if (stage_label && task.stage_label != *stage_label) {
        return false;
    }
    if (stage_index && task.stage_index != *stage_index) {
        return false;
    }
    return true;
}

bool RulePredicates::matches_metadata(const std::map<std::string, std::string>& metadata) const {
    for (const auto& [key, value] : metadata_equals) {
        auto it = metadata.find(key);
        if (it == metadata.end() || it->second != value) {
            return false;
        }
    }
    return true;
}

bool RulePredicates::matches_waiting_tasks(const PolicyStateSnapshot& snapshot) const {
    if (!task) {
        return true;
    }
    if (snapshot.waiting_task_order.empty()) {
        return false;
    }
    const auto task_index = index_tasks(snapshot);
    for (TaskId id : snapshot.waiting_task_order) {
        auto it = task_index.find(id);
        if (it == task_index.end()) {
            continue;
        }
        if (task->matches(*it->second)) {
            return true;
        }
    }
    return false;
}

Program::Program(std::vector<Rule> rules) : rules_(std::move(rules)) {}

policy::PolicyDecision Program::evaluate(const PolicyStateSnapshot& snapshot) const {
    policy::PolicyDecision decision{};
    for (const Rule& rule : rules_) {
        if (rule.condition && !rule.condition->evaluate(snapshot)) {
            continue;
        }
        if (rule.predicates) {
            if (!rule.predicates->matches_metadata(snapshot.scenario_metadata)) {
                continue;
            }
            if (!rule.predicates->matches_waiting_tasks(snapshot)) {
                continue;
            }
        }
        switch (rule.action.type) {
        case Action::Type::kReorder:
            if (rule.action.reorder && !decision.waiting_order) {
                const bool prefer_host = *rule.action.reorder == ReorderPreference::kPreferHost;
                std::vector<TaskId> order = reorder_waiting(snapshot, prefer_host, rule.predicates);
                if (!order.empty()) {
                    decision.waiting_order = std::move(order);
                }
            }
            break;
        case Action::Type::kAdmission:
            if (rule.action.max_active_tasks && !decision.admission) {
                AdmissionControlDirective directive{};
                directive.enabled = true;
                directive.max_active_tasks = *rule.action.max_active_tasks;
                decision.admission = directive;
            }
            break;
        case Action::Type::kNone:
        default:
            break;
        }
    }
    return decision;
}

std::unique_ptr<policy::PolicyHook> load_program_from_file(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::runtime_error("policy DSL file path is empty");
    }
    YAML::Node root = YAML::LoadFile(path.string());
    Program program(parse_rules(root));
    return std::make_unique<DslPolicyHook>(std::move(program));
}

} // namespace nicloadoff::policy::dsl
