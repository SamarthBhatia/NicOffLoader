#include "nicloadoff/profile.hh"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>

namespace nicloadoff::config {
namespace {

[[nodiscard]] std::string make_error_message(const std::string& context, const std::string& detail) {
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

[[nodiscard]] std::string remove_numeric_separators(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    return value;
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

struct Token {
    enum class Kind { MapEntry, SequenceEntry };

    Kind kind;
    int indent{0};
    std::string key;          // map key or inline key for sequence map entry
    std::string value;        // inline scalar value or sequence scalar
    bool has_value{false};    // true if value is provided inline
    bool inline_map{false};   // true for sequence entries with key/value inline
};

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
        throw ProfileLoaderError(make_error_message("YAML parse error", "indentation must be multiples of two spaces"));
    }
    return indent;
}

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
        throw ProfileLoaderError(make_error_message("Profile loader error", "failed to open file: " + path.string()));
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
                    token.value.clear();
                    token.has_value = false;
                }
            } else {
                token.value = strip_quotes(rest);
                token.has_value = true;
            }
            tokens.push_back(std::move(token));
            continue;
        }

        auto colon_pos = content.find(':');
        if (colon_pos == std::string::npos) {
            throw ProfileLoaderError(make_error_message("YAML parse error", "expected ':' in line: " + content));
        }

        std::string key = trim(content.substr(0, colon_pos));
        std::string value_part = trim(content.substr(colon_pos + 1));

        Token token{Token::Kind::MapEntry, indent, strip_quotes(key), "", false, false};

        if (value_part == ">" || value_part == "|") {
            std::string block_value = parse_block_scalar(lines, i, indent, value_part == ">");
            token.value = block_value;
            token.has_value = true;
        } else if (!value_part.empty()) {
            token.value = strip_quotes(value_part);
            token.has_value = true;
        } else {
            token.value.clear();
            token.has_value = false;
        }

        tokens.push_back(std::move(token));
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
        if (token.kind == Token::Kind::SequenceEntry && token.indent == indent) {
            return parse_sequence(indent);
        }
        if (token.kind == Token::Kind::MapEntry && token.indent == indent) {
            return parse_map(indent);
        }
        throw ProfileLoaderError(make_error_message("YAML parse error", "unexpected token indentation"));
    }

    std::vector<Token> tokens_;
    std::size_t index_{0};
};

const YamlNode& expect_map(const YamlNode& node, const std::string& path) {
    if (node.kind != YamlNode::Kind::Map) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be a mapping"));
    }
    return node;
}

const YamlNode& expect_sequence(const YamlNode& node, const std::string& path) {
    if (node.kind != YamlNode::Kind::Sequence) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be a sequence"));
    }
    return node;
}

const YamlNode& expect_scalar_node(const YamlNode& node, const std::string& path) {
    if (node.kind != YamlNode::Kind::Scalar) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be a scalar"));
    }
    return node;
}

const YamlNode& get_required(const YamlNode& map, const std::string& key, const std::string& path) {
    auto it = map.map.find(key);
    if (it == map.map.end()) {
        throw ProfileLoaderError(make_error_message("Profile loader error", "missing required field: " + path + "." + key));
    }
    return it->second;
}

std::optional<const YamlNode*> get_optional(const YamlNode& map, const std::string& key) {
    auto it = map.map.find(key);
    if (it == map.map.end()) {
        return std::nullopt;
    }
    return &it->second;
}

double parse_double(const YamlNode& node, const std::string& path) {
    const auto& scalar = expect_scalar_node(node, path);
    std::string value = remove_numeric_separators(scalar.scalar);
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be a floating-point number"));
    }
}

std::uint32_t parse_uint32(const YamlNode& node, const std::string& path) {
    const auto& scalar = expect_scalar_node(node, path);
    std::string value = remove_numeric_separators(scalar.scalar);
    try {
        long long parsed = std::stoll(value);
        if (parsed < 0 || parsed > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::out_of_range("out of range");
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (const std::exception&) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be an unsigned integer"));
    }
}

std::size_t parse_size(const YamlNode& node, const std::string& path) {
    const auto& scalar = expect_scalar_node(node, path);
    std::string value = remove_numeric_separators(scalar.scalar);
    try {
        long long parsed = std::stoll(value);
        if (parsed < 0) {
            throw std::out_of_range("negative size_t");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw ProfileLoaderError(make_error_message("Profile loader error", path + " must be a non-negative integer"));
    }
}

std::string parse_string(const YamlNode& node, const std::string& path) {
    return expect_scalar_node(node, path).scalar;
}

CpuSpec parse_cpu(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    CpuSpec spec{};
    spec.model = parse_string(get_required(map, "model", path), path + ".model");
    spec.sockets = parse_uint32(get_required(map, "sockets", path), path + ".sockets");
    spec.cores_per_socket = parse_uint32(get_required(map, "cores_per_socket", path), path + ".cores_per_socket");
    spec.cores_total = parse_uint32(get_required(map, "cores_total", path), path + ".cores_total");
    spec.clock_ghz = parse_double(get_required(map, "clock_ghz", path), path + ".clock_ghz");
    spec.base_service_scale = parse_double(get_required(map, "base_service_scale", path), path + ".base_service_scale");
    return spec;
}

DramSpec parse_dram(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    DramSpec spec{};
    spec.type = parse_string(get_required(map, "type", path), path + ".type");
    spec.capacity_gb = parse_uint32(get_required(map, "capacity_gb", path), path + ".capacity_gb");
    spec.bandwidth_gbps = parse_double(get_required(map, "bandwidth_gbps", path), path + ".bandwidth_gbps");
    spec.latency_ns = parse_double(get_required(map, "latency_ns", path), path + ".latency_ns");
    return spec;
}

NicDramSpec parse_nic_dram(const YamlNode& node, const std::string& path) {
    NicDramSpec spec{};
    DramSpec base = parse_dram(node, path);
    spec.type = base.type;
    spec.capacity_gb = base.capacity_gb;
    spec.bandwidth_gbps = base.bandwidth_gbps;
    spec.latency_ns = base.latency_ns;

    const auto& map = expect_map(node, path);
    spec.contention_variance_pct =
        parse_uint32(get_required(map, "contention_variance_pct", path), path + ".contention_variance_pct");
    return spec;
}

LinkMtu parse_mtu(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    LinkMtu mtu{};
    mtu.default_mtu = parse_uint32(get_required(map, "default", path), path + ".default");
    if (auto opt = get_optional(map, "jumbo")) {
        mtu.jumbo = parse_uint32(**opt, path + ".jumbo");
    }
    return mtu;
}

LinkSpec parse_link(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    LinkSpec link{};
    link.type = parse_string(get_required(map, "type", path), path + ".type");
    link.peak_bandwidth_gbps = parse_double(get_required(map, "peak_bandwidth_gbps", path), path + ".peak_bandwidth_gbps");
    link.effective_bandwidth_gbps =
        parse_double(get_required(map, "effective_bandwidth_gbps", path), path + ".effective_bandwidth_gbps");
    link.latency_us = parse_double(get_required(map, "latency_us", path), path + ".latency_us");
    link.mtu_bytes = parse_mtu(get_required(map, "mtu_bytes", path), path + ".mtu_bytes");
    return link;
}

QueueSpec parse_queue(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    QueueSpec queue{};
    queue.cpu_capacity = parse_uint32(get_required(map, "cpu_capacity", path), path + ".cpu_capacity");
    queue.dram_capacity_gb = parse_uint32(get_required(map, "dram_capacity_gb", path), path + ".dram_capacity_gb");
    return queue;
}

LinkQueueSpec parse_link_queue(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    LinkQueueSpec queue{};
    queue.max_inflight_bytes =
        parse_size(get_required(map, "max_inflight_bytes", path), path + ".max_inflight_bytes");
    return queue;
}

ServiceTimeOverride parse_service_override(const YamlNode& node, const std::string& path) {
    const auto& map = expect_map(node, path);
    ServiceTimeOverride override{};
    override.host_mean_us = parse_double(get_required(map, "host_mean_us", path), path + ".host_mean_us");
    override.nic_mean_us = parse_double(get_required(map, "nic_mean_us", path), path + ".nic_mean_us");
    return override;
}

ProfileReferences parse_references(const YamlNode& node, const std::string& path) {
    ProfileReferences refs;
    const auto& seq = expect_sequence(node, path);
    for (std::size_t i = 0; i < seq.sequence.size(); ++i) {
        const auto& entry_node = seq.sequence[i];
        const std::string entry_path = path + "[" + std::to_string(i) + "]";
        const auto& map = expect_map(entry_node, entry_path);
        ProfileReferences::Entry entry{};
        if (auto it = map.map.find("title"); it != map.map.end()) {
            entry.title = parse_string(it->second, entry_path + ".title");
        } else {
            throw ProfileLoaderError(make_error_message("Profile loader error", entry_path + " missing 'title'"));
        }
        if (auto it = map.map.find("publisher"); it != map.map.end()) {
            entry.publisher = parse_string(it->second, entry_path + ".publisher");
        }
        if (auto it = map.map.find("authors"); it != map.map.end()) {
            entry.authors = parse_string(it->second, entry_path + ".authors");
        }
        if (auto it = map.map.find("venue"); it != map.map.end()) {
            entry.venue = parse_string(it->second, entry_path + ".venue");
        }
        if (auto it = map.map.find("year"); it != map.map.end()) {
            entry.year = parse_uint32(it->second, entry_path + ".year");
        }
        if (auto it = map.map.find("url"); it != map.map.end()) {
            entry.url = parse_string(it->second, entry_path + ".url");
        }
        refs.entries.push_back(std::move(entry));
    }
    return refs;
}

std::vector<std::string> parse_notes(const YamlNode& node, const std::string& path) {
    std::vector<std::string> notes;
    const auto& seq = expect_sequence(node, path);
    for (std::size_t i = 0; i < seq.sequence.size(); ++i) {
        notes.push_back(parse_string(seq.sequence[i], path + "[" + std::to_string(i) + "]"));
    }
    return notes;
}

std::unordered_map<std::string, ServiceTimeOverride> parse_service_overrides(const YamlNode& node,
                                                                             const std::string& path) {
    const auto& map = expect_map(node, path);
    std::unordered_map<std::string, ServiceTimeOverride> overrides;
    for (const auto& [key, value] : map.map) {
        overrides.emplace(key, parse_service_override(value, path + "." + key));
    }
    return overrides;
}

} // namespace

Profile load_profile_from_file(const std::filesystem::path& path) {
    auto tokens = tokenize(path);
    MiniYamlParser parser(std::move(tokens));
    YamlNode document = parser.parse_document();

    const auto& root = expect_map(document, "root");

    Profile profile{};
    profile.schema_version = parse_string(get_required(root, "schema_version", "root"), "schema_version");
    profile.profile_name = parse_string(get_required(root, "profile_name", "root"), "profile_name");
    profile.description = parse_string(get_required(root, "description", "root"), "description");
    profile.last_verified = parse_string(get_required(root, "last_verified", "root"), "last_verified");

    profile.references = parse_references(get_required(root, "references", "root"), "references");

    const auto& host_node = expect_map(get_required(root, "host", "root"), "host");
    profile.host_cpu = parse_cpu(get_required(host_node, "cpu", "host"), "host.cpu");
    profile.host_dram = parse_dram(get_required(host_node, "dram", "host"), "host.dram");

    const auto& nic_node = expect_map(get_required(root, "nic", "root"), "nic");
    profile.nic_cpu = parse_cpu(get_required(nic_node, "cpu", "nic"), "nic.cpu");
    profile.nic_dram = parse_nic_dram(get_required(nic_node, "dram", "nic"), "nic.dram");

    const auto& links_node = expect_map(get_required(root, "links", "root"), "links");
    profile.host_nic_link = parse_link(get_required(links_node, "host_nic", "links"), "links.host_nic");
    profile.nic_network_link = parse_link(get_required(links_node, "nic_network", "links"), "links.nic_network");

    const auto& queues_node = expect_map(get_required(root, "queues", "root"), "queues");
    profile.host_queue = parse_queue(get_required(queues_node, "host", "queues"), "queues.host");
    profile.nic_queue = parse_queue(get_required(queues_node, "nic", "queues"), "queues.nic");
    profile.host_nic_queue = parse_link_queue(get_required(queues_node, "host_nic_link", "queues"), "queues.host_nic_link");
    profile.nic_network_queue =
        parse_link_queue(get_required(queues_node, "nic_network_link", "queues"), "queues.nic_network_link");
    profile.host_nic_link.max_inflight_bytes = profile.host_nic_queue.max_inflight_bytes;
    profile.nic_network_link.max_inflight_bytes = profile.nic_network_queue.max_inflight_bytes;

    profile.service_time_overrides =
        parse_service_overrides(get_required(root, "service_time_overrides", "root"), "service_time_overrides");

    if (auto opt = get_optional(root, "notes")) {
        profile.notes = parse_notes(**opt, "notes");
    }

    return profile;
}

} // namespace nicloadoff::config
