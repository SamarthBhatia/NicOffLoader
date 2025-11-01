#include "nicloadoff/workload_loader.hh"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nicloadoff {
namespace {

[[nodiscard]] std::string make_error(const std::string& context, const std::string& detail) {
    std::ostringstream oss;
    oss << context << ": " << detail;
    return oss.str();
}

[[nodiscard]] std::string trim_left(std::string_view sv) {
    std::size_t pos = 0;
    while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos]))) {
        ++pos;
    }
    return std::string(sv.substr(pos));
}

[[nodiscard]] std::string trim_right(std::string_view sv) {
    std::size_t end = sv.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(0, end));
}

[[nodiscard]] std::string trim(std::string_view sv) { return trim_right(trim_left(sv)); }

[[nodiscard]] std::string strip_quotes(std::string_view sv) {
    if (sv.size() >= 2 && ((sv.front() == '"' && sv.back() == '"') || (sv.front() == '\'' && sv.back() == '\''))) {
        return std::string(sv.substr(1, sv.size() - 2));
    }
    return std::string(sv);
}

int count_indent(const std::string& line) {
    int indent = 0;
    for (char c : line) {
        if (c == ' ') {
            ++indent;
        } else {
            break;
        }
    }
    if (indent % 2 != 0) {
        throw WorkloadLoaderError(make_error("YAML parse error", "indentation must be multiples of two spaces"));
    }
    return indent;
}

struct YamlNode {
    enum class Kind { Scalar, Map, Sequence };

    Kind kind{Kind::Scalar};
    std::string scalar;
    std::unordered_map<std::string, YamlNode> map;
    std::vector<YamlNode> sequence;

    static YamlNode make_scalar(std::string value) {
        YamlNode node;
        node.kind = Kind::Scalar;
        node.scalar = std::move(value);
        return node;
    }

    static YamlNode make_map() {
        YamlNode node;
        node.kind = Kind::Map;
        return node;
    }

    static YamlNode make_sequence() {
        YamlNode node;
        node.kind = Kind::Sequence;
        return node;
    }
};

std::string node_kind_to_string(YamlNode::Kind kind) {
    switch (kind) {
    case YamlNode::Kind::Scalar:
        return "scalar";
    case YamlNode::Kind::Map:
        return "map";
    case YamlNode::Kind::Sequence:
        return "sequence";
    }
    return "unknown";
}

struct Token {
    enum class Kind { MapEntry, SequenceEntry };

    Kind kind;
    int indent{0};
    std::string key;
    std::string value;
    bool has_value{false};
    bool inline_map{false};
};

std::string parse_block_scalar(const std::vector<std::string>& lines, std::size_t& index, int indent, bool folded) {
    std::ostringstream oss;
    bool first_line = true;
    const int expected_indent = indent + 2;

    auto append_line = [&](const std::string& text, bool blank) {
        if (first_line) {
            first_line = false;
            oss << text;
            return;
        }
        if (blank) {
            oss << '\n';
        } else if (folded) {
            oss << ' ' << text;
        } else {
            oss << '\n' << text;
        }
    };

    std::size_t cursor = index + 1;
    while (cursor < lines.size()) {
        const std::string& raw = lines[cursor];
        if (trim(raw).empty()) {
            append_line("", true);
            ++cursor;
            continue;
        }
        const int current_indent = count_indent(raw);
        if (current_indent < expected_indent) {
            break;
        }
        std::string content = trim(raw.substr(expected_indent));
        append_line(content, false);
        ++cursor;
    }
    index = cursor - 1;
    return oss.str();
}

std::vector<Token> tokenize(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw WorkloadLoaderError(make_error("workload loader error", "failed to open file: " + path.string()));
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    std::vector<Token> tokens;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string raw = trim_right(lines[i]);
        if (raw.empty()) {
            continue;
        }
        std::string trimmed_line = trim(raw);
        if (trimmed_line.empty()) {
            continue;
        }
        if (!trimmed_line.empty() && trimmed_line.front() == '#') {
            continue;
        }

        const int indent = count_indent(raw);
        std::string_view sv = raw;
        sv.remove_prefix(static_cast<std::size_t>(indent));
        std::string content = trim(sv);

        if (content.rfind("- ", 0) == 0) {
            std::string rest = trim(content.substr(2));
            Token token{Token::Kind::SequenceEntry, indent, "", "", false, false};
            if (rest.empty()) {
                tokens.push_back(std::move(token));
                continue;
            }
            auto colon_pos = rest.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = trim(rest.substr(0, colon_pos));
                std::string value_part = trim(rest.substr(colon_pos + 1));
                bool has_block = value_part == ">" || value_part == "|";
                if (has_block) {
                    std::string block_value = parse_block_scalar(lines, i, indent, value_part == ">");
                    token.inline_map = true;
                    token.key = strip_quotes(key);
                    token.value = block_value;
                    token.has_value = true;
                } else if (!value_part.empty()) {
                    token.inline_map = true;
                    token.key = strip_quotes(key);
                    token.value = strip_quotes(value_part);
                    token.has_value = true;
                } else {
                    token.inline_map = true;
                    token.key = strip_quotes(key);
                }
            } else {
                token.value = strip_quotes(rest);
                token.has_value = true;
            }
            tokens.push_back(std::move(token));
        } else {
            auto colon_pos = content.find(':');
            if (colon_pos == std::string::npos) {
                throw WorkloadLoaderError(make_error("YAML parse error", "expected ':' in map entry"));
            }
            std::string key = strip_quotes(trim(content.substr(0, colon_pos)));
            std::string value_part = trim(content.substr(colon_pos + 1));
            Token token{Token::Kind::MapEntry, indent, key, "", false, false};
            bool has_block = value_part == ">" || value_part == "|";
            if (has_block) {
                std::string block_value = parse_block_scalar(lines, i, indent, value_part == ">");
                token.value = block_value;
                token.has_value = true;
            } else if (!value_part.empty()) {
                token.value = strip_quotes(value_part);
                token.has_value = true;
            }
            tokens.push_back(std::move(token));
        }
    }
    return tokens;
}

class MiniYamlParser {
  public:
    explicit MiniYamlParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    YamlNode parse_document() {
        index_ = 0;
        return parse_map(0);
    }

  private:
    const Token& peek() const { return tokens_.at(index_); }

    bool at_end() const { return index_ >= tokens_.size(); }

    YamlNode parse_map(int indent) {
        YamlNode node = YamlNode::make_map();
        while (!at_end()) {
            const Token& token = peek();
            if (token.indent < indent) {
                break;
            }
            if (token.indent > indent || token.kind != Token::Kind::MapEntry) {
                break;
            }
            ++index_;
            if (token.has_value) {
                node.map.emplace(token.key, YamlNode::make_scalar(token.value));
            } else {
                node.map.emplace(token.key, parse_value(indent + 2));
            }
        }
        return node;
    }

    YamlNode parse_sequence(int indent) {
        YamlNode node = YamlNode::make_sequence();
        while (!at_end()) {
            const Token& token = peek();
            if (token.indent < indent) {
                break;
            }
            if (token.indent > indent || token.kind != Token::Kind::SequenceEntry) {
                break;
            }
            ++index_;

            if (token.inline_map) {
                YamlNode entry = YamlNode::make_map();
                if (token.has_value) {
                    entry.map.emplace(token.key, YamlNode::make_scalar(token.value));
                } else {
                    entry.map.emplace(token.key, parse_value(indent + 2));
                }
                if (!at_end()) {
                    const Token& next = peek();
                    if (next.indent == indent + 2 && next.kind == Token::Kind::MapEntry) {
                        YamlNode nested = parse_map(indent + 2);
                        entry.map.insert(nested.map.begin(), nested.map.end());
                    }
                }
                node.sequence.push_back(std::move(entry));
            } else if (token.has_value) {
                node.sequence.push_back(YamlNode::make_scalar(token.value));
            } else {
                node.sequence.push_back(parse_value(indent + 2));
            }
        }
        return node;
    }

    YamlNode parse_value(int indent) {
        if (at_end()) {
            return YamlNode::make_scalar("");
        }
        const Token& token = peek();
        if (token.indent < indent) {
            return YamlNode::make_scalar("");
        }
        if (token.kind == Token::Kind::SequenceEntry && token.indent >= indent) {
            return parse_sequence(token.indent);
        }
        if (token.kind == Token::Kind::MapEntry && token.indent >= indent) {
            return parse_map(token.indent);
        }
        if (token.has_value) {
            ++index_;
            return YamlNode::make_scalar(token.value);
        }
        throw WorkloadLoaderError(make_error("YAML parse error", "unexpected token indentation"));
    }

    std::vector<Token> tokens_;
    std::size_t index_{0};
};

const YamlNode& expect_map(const YamlNode& node, const std::string& field) {
    if (node.kind != YamlNode::Kind::Map) {
        throw WorkloadLoaderError(make_error("YAML type error", field + " must be a map"));
    }
    return node;
}

const YamlNode& expect_sequence(const YamlNode& node, const std::string& field) {
    if (node.kind != YamlNode::Kind::Sequence) {
        throw WorkloadLoaderError(make_error("YAML type error", field + " must be a sequence"));
    }
    return node;
}

const YamlNode& get_required(const YamlNode& node, const std::string& key, const std::string& context) {
    auto it = node.map.find(key);
    if (it == node.map.end()) {
        throw WorkloadLoaderError(make_error("missing field", context + "." + key));
    }
    return it->second;
}

std::optional<YamlNode> get_optional(const YamlNode& node, const std::string& key) {
    auto it = node.map.find(key);
    if (it == node.map.end()) {
        return std::nullopt;
    }
    return it->second;
}

double parse_double(const std::string& value, const std::string& field) {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw WorkloadLoaderError(make_error("invalid numeric value", field + "='" + value + "'"));
    }
}

std::uint64_t parse_uint64(const std::string& value, const std::string& field) {
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (const std::exception&) {
        throw WorkloadLoaderError(make_error("invalid integer value", field + "='" + value + "'"));
    }
}

std::string parse_string(const YamlNode& node, const std::string& field) {
    if (node.kind != YamlNode::Kind::Scalar) {
        throw WorkloadLoaderError(make_error("expected string", field));
    }
    return node.scalar;
}

StageResourceDemand parse_demand(const YamlNode& node, const std::string& context) {
    const auto& map = expect_map(node, context);
    std::string resource = parse_string(get_required(map, "resource", context), context + ".resource");
    double units = parse_double(parse_string(get_required(map, "units", context), context + ".units"),
                                context + ".units");
    ResourceClass resource_class;
    if (resource == "host_cpu") {
        resource_class = ResourceClass::kHostCpu;
    } else if (resource == "host_dram") {
        resource_class = ResourceClass::kHostDram;
    } else if (resource == "host_link") {
        resource_class = ResourceClass::kHostLink;
    } else if (resource == "nic_cpu") {
        resource_class = ResourceClass::kNicCpu;
    } else if (resource == "nic_dram") {
        resource_class = ResourceClass::kNicDram;
    } else if (resource == "nic_link") {
        resource_class = ResourceClass::kNicLink;
    } else {
        throw WorkloadLoaderError(make_error("unknown resource class", resource));
    }
    return StageResourceDemand{resource_class, units};
}

ServiceTimeProfileRef parse_service_profile(const YamlNode& node, const std::string& context) {
    const auto& map = expect_map(node, context);
    ServiceTimeProfileRef ref;
    ref.key = parse_string(get_required(map, "key", context), context + ".key");
    std::string domain = parse_string(get_required(map, "domain", context), context + ".domain");
    if (domain == "host") {
        ref.domain = ServiceTimeDomain::kHost;
    } else if (domain == "nic") {
        ref.domain = ServiceTimeDomain::kNic;
    } else {
        throw WorkloadLoaderError(make_error("unknown service domain", domain));
    }
    if (auto mode_node = get_optional(map, "mode")) {
        std::string mode = parse_string(*mode_node, context + ".mode");
        if (mode == "deterministic") {
            ref.mode = ServiceTimeMode::kDeterministic;
        } else if (mode == "stochastic") {
            ref.mode = ServiceTimeMode::kStochastic;
        } else {
            throw WorkloadLoaderError(make_error("unknown service mode", mode));
        }
    } else {
        ref.mode = ServiceTimeMode::kDeterministic;
    }
    return ref;
}

StageSpec parse_stage(const YamlNode& node, const std::string& context) {
    const auto& map = expect_map(node, context);
    StageSpec stage;
    if (auto deterministic_node = get_optional(map, "deterministic_service_time")) {
        stage.deterministic_service_time =
            parse_double(parse_string(*deterministic_node, context + ".deterministic_service_time"),
                         context + ".deterministic_service_time");
    }
    if (auto profile_node = get_optional(map, "service_profile")) {
        if (profile_node->kind != YamlNode::Kind::Map) {
            throw WorkloadLoaderError(make_error("service_profile must be a map",
                                                context + ".service_profile (was " +
                                                    node_kind_to_string(profile_node->kind) + ")"));
        }
        stage.service_profile = parse_service_profile(*profile_node, context + ".service_profile");
    }
    if (!stage.deterministic_service_time && !stage.service_profile) {
        throw WorkloadLoaderError(make_error("stage must define deterministic_service_time or service_profile",
                                             context));
    }
    const auto& demands_node = expect_sequence(get_required(map, "demands", context), context + ".demands");
    for (std::size_t i = 0; i < demands_node.sequence.size(); ++i) {
        stage.demands.push_back(parse_demand(demands_node.sequence[i],
                                             context + ".demands[" + std::to_string(i) + "]"));
    }
    return stage;
}

TaskSpec parse_task(const YamlNode& node, const std::string& context) {
    const auto& map = expect_map(node, context);
    TaskSpec task;
    task.id = parse_uint64(parse_string(get_required(map, "id", context), context + ".id"), context + ".id");
    task.arrival_time =
        parse_double(parse_string(get_required(map, "arrival_time", context), context + ".arrival_time"),
                     context + ".arrival_time");
    const auto& stages_node = expect_sequence(get_required(map, "stages", context), context + ".stages");
    for (std::size_t i = 0; i < stages_node.sequence.size(); ++i) {
        task.stages.push_back(parse_stage(stages_node.sequence[i],
                                          context + ".stages[" + std::to_string(i) + "]"));
    }
    if (task.stages.empty()) {
        throw WorkloadLoaderError(make_error("task must contain at least one stage", context));
    }
    return task;
}

} // namespace

LoadedWorkload load_workload_from_file(const std::filesystem::path& path) {
    auto tokens = tokenize(path);
    if (tokens.empty()) {
        throw WorkloadLoaderError(make_error("workload loader error", "file is empty: " + path.string()));
    }
    MiniYamlParser parser(std::move(tokens));
    YamlNode root = parser.parse_document();
    if (root.kind != YamlNode::Kind::Map) {
        throw WorkloadLoaderError(make_error("workload loader error", "root must be a map"));
    }

    LoadedWorkload workload;
    workload.schema_version = parse_string(get_required(root, "schema_version", "root"), "root.schema_version");
    workload.workload_name = parse_string(get_required(root, "workload_name", "root"), "root.workload_name");
    workload.description = parse_string(get_required(root, "description", "root"), "root.description");

    const auto& tasks_node = expect_sequence(get_required(root, "tasks", "root"), "root.tasks");
    for (std::size_t i = 0; i < tasks_node.sequence.size(); ++i) {
        workload.spec.tasks.push_back(parse_task(tasks_node.sequence[i],
                                                 "root.tasks[" + std::to_string(i) + "]"));
    }
    if (workload.spec.tasks.empty()) {
        throw WorkloadLoaderError(make_error("workload must contain at least one task", "root.tasks"));
    }
    return workload;
}

} // namespace nicloadoff
