#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace java {

struct Install {
    std::filesystem::path exe;
    int major = 0;
    std::string version;
    bool managed = false;  // installed by Voxual
};

void setManagedRoot(const std::filesystem::path& root);
std::filesystem::path managedRoot();

// Blocking: scans common install locations + PATH. Thread-safe.
void scan();
std::vector<Install> all();
bool scanning();

// explicitPath wins if non-empty. major == 0 -> newest installed. Otherwise exact match, then next higher.
std::optional<Install> pick(int major, const std::string& explicitPath = "");
bool has(int major);

// Downloads an Eclipse Temurin JRE into the managed folder.
bool installManaged(int major, const std::function<void(float, const std::string&)>& progress,
                    const std::atomic<bool>* cancel, std::string& err);

}  // namespace java
