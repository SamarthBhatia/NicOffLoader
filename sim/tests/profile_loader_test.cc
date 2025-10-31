#include "nicloadoff/profile.hh"

#include <cassert>
#include <filesystem>
#include <iostream>

using nicloadoff::config::Profile;
using nicloadoff::config::ProfileLoaderError;
using nicloadoff::config::load_profile_from_file;

namespace {

std::filesystem::path repo_relative(const std::filesystem::path& from, const std::filesystem::path& relative) {
    auto base = std::filesystem::path(from).parent_path();
    return std::filesystem::weakly_canonical(base / relative);
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path profile_path = repo_relative(__FILE__, "../../profiles/bf2_default.yaml");
    Profile profile = load_profile_from_file(profile_path);

    assert(profile.profile_name == "bf2_default");
    assert(profile.host_cpu.cores_total == 32);
    assert(profile.nic_cpu.cores_total == 8);
    assert(profile.host_nic_link.mtu_bytes.default_mtu == 1500);
    assert(profile.host_nic_queue.max_inflight_bytes == 64'000'000);
    assert(profile.service_time_overrides.count("kv_lookup") == 1);
    assert(profile.notes.size() == 2);
    assert(!profile.references.entries.empty());

    bool threw = false;
    try {
        load_profile_from_file(profile_path.parent_path() / "missing_file.yaml");
    } catch (const ProfileLoaderError&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "profile_loader_test: expected missing file to raise ProfileLoaderError\n";
        return 1;
    }

    std::cout << "profile_loader_test passed\n";
    return 0;
}
