#include "nicloadoff/policy_dsl.hh"

#include "nicloadoff/basic_policy_hooks.hh"
#include "yaml-cpp/yaml.h"

#include <stdexcept>

namespace nicloadoff::policy::dsl {

namespace {

double metric_value(const PolicyStateSnapshot& snapshot, Metric metric) {
    const auto& rolling = snapshot.rolling_metrics;
    switch (metric) {
    case Metric::kQueueAverage:
        return rolling.waiting_queue_depth.average;
    case Metric::kHostUtilAverage:
        return rolling.host_utilization.average;
    case Metric::kNicUtilAverage:
        return rolling.nic_utilization.average;
    case Metric::kSojournMean:
        return rolling.sojourn.mean_latency;
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
    if (value == "host_util_avg") {
        return Metric::kHostUtilAverage;
    }
    if (value == "nic_util_avg") {
        return Metric::kNicUtilAverage;
    }
    if (value == "sojourn_mean_us") {
        return Metric::kSojournMean;
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
        rule.action = parse_action(rule_node["action"]);
        rules.push_back(std::move(rule));
    }
    return rules;
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

Program::Program(std::vector<Rule> rules) : rules_(std::move(rules)) {}

policy::PolicyDecision Program::evaluate(const PolicyStateSnapshot& snapshot) const {
    for (const Rule& rule : rules_) {
        if (rule.condition && !rule.condition->evaluate(snapshot)) {
            continue;
        }
        policy::PolicyDecision decision{};
        switch (rule.action.type) {
        case Action::Type::kReorder:
            if (rule.action.reorder) {
                const bool prefer_host = *rule.action.reorder == ReorderPreference::kPreferHost;
                std::vector<TaskId> order = reorder_waiting_by_demand(snapshot, prefer_host);
                if (!order.empty()) {
                    decision.waiting_order = std::move(order);
                    return decision;
                }
            }
            break;
        case Action::Type::kAdmission:
            if (rule.action.max_active_tasks) {
                AdmissionControlDirective directive{};
                directive.enabled = true;
                directive.max_active_tasks = *rule.action.max_active_tasks;
                decision.admission = directive;
                return decision;
            }
            break;
        case Action::Type::kNone:
        default:
            break;
        }
    }
    return {};
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

